//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "iconsole.h"
#include "IGameUIFuncs.h"
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include "tier1/utlstring.h"
#include "vgui_controls/ConsoleDialog.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CClientConsoleDialog : public vgui::CConsoleDialog
{
	DECLARE_CLASS_SIMPLE( CClientConsoleDialog, vgui::CConsoleDialog );

public:
	CClientConsoleDialog()
		: BaseClass( NULL, "GameConsole", false ),
		m_bPausedGame( false )
	{
		AddActionSignalTarget( this );

		int wide, tall;
		vgui::surface()->GetScreenSize( wide, tall );
		int offset = vgui::scheme()->GetProportionalScaledValue( 16 );

		SetBounds(
			wide / 2 - offset * 4,
			offset,
			wide / 2 + offset * 3,
			tall - offset * 8 );
	}

	virtual void OnCommandSubmitted( const char *pCommand )
	{
		if ( !Q_stricmp( pCommand, "clear" ) )
		{
			Clear();
			return;
		}
		if ( !Q_stricmp( pCommand, "condump" ) )
		{
			DumpConsoleTextToFile();
			return;
		}

		engine->ClientCmd_Unrestricted( pCommand );
	}

	virtual void OnKeyCodeTyped( vgui::KeyCode code )
	{
		BaseClass::OnKeyCodeTyped( code );

		if ( m_pConsolePanel->TextEntryHasFocus() && code >= KEY_F1 && code <= KEY_F12 )
		{
			const char *binding = gameuifuncs->GetBindingForButtonCode( code );
			if ( binding && binding[0] )
			{
				engine->ClientCmd_Unrestricted( binding );
			}
		}
	}

	MESSAGE_FUNC( OnClosedByHittingTilde, "ClosedByHittingTilde" )
	{
		Close();
	}

	virtual void Activate()
	{
		if ( !m_bPausedGame && engine->IsInGame() && engine->GetMaxClients() == 1 &&
			!engine->IsLevelMainMenuBackground() && !engine->IsPaused() )
		{
			engine->ClientCmd_Unrestricted( "setpause nomsg" );
			m_bPausedGame = true;
		}

		BaseClass::Activate();
	}

protected:
	virtual void OnClose()
	{
		if ( m_bPausedGame )
		{
			engine->ClientCmd_Unrestricted( "unpause nomsg" );
			m_bPausedGame = false;
		}

		BaseClass::OnClose();
	}

private:
	bool m_bPausedGame;
};

class CConsole : public IConsole, public IConsoleDisplayFunc
{
private:
	enum MessageType_t
	{
		MESSAGE_PRINT,
		MESSAGE_DPRINT,
		MESSAGE_COLOR_PRINT,
	};

	struct Message_t
	{
		MessageType_t type;
		Color color;
		CUtlString text;
	};

	CClientConsoleDialog *conPanel;
	CUtlVector< Message_t > messages;
	bool listening;

	void BufferMessage( MessageType_t type, const Color &color, const char *text )
	{
		Message_t &message = messages[ messages.AddToTail() ];
		message.type = type;
		message.color = color;
		message.text = text ? text : "";
	}

public:
	CConsole( void )
	{
		conPanel = NULL;
		listening = false;
	}

	void StartListening( void )
	{
		if ( !listening )
		{
			g_pCVar->InstallConsoleDisplayFunc( this );
			listening = true;
		}
	}

	virtual void Print( const char *text )
	{
		BufferMessage( MESSAGE_PRINT, Color(), text );
	}

	virtual void DPrint( const char *text )
	{
		BufferMessage( MESSAGE_DPRINT, Color(), text );
	}

	virtual void ColorPrint( const Color &color, const char *text )
	{
		BufferMessage( MESSAGE_COLOR_PRINT, color, text );
	}
	
	void Create( vgui::VPANEL )
	{
		if ( listening )
		{
			g_pCVar->RemoveConsoleDisplayFunc( this );
			listening = false;
		}

		if ( !conPanel )
		{
			conPanel = vgui::SETUP_PANEL( new CClientConsoleDialog() );
		}

		for ( int i = 0; i < messages.Count(); ++i )
		{
			const Message_t &message = messages[i];
			switch ( message.type )
			{
			case MESSAGE_DPRINT:
				conPanel->DPrint( message.text.String() );
				break;

			case MESSAGE_COLOR_PRINT:
				conPanel->ColorPrint( message.color, message.text.String() );
				break;

			default:
				conPanel->Print( message.text.String() );
				break;
			}
		}
		messages.Purge();
	}

	void Destroy( void )
	{
		if ( listening )
		{
			g_pCVar->RemoveConsoleDisplayFunc( this );
			listening = false;
		}

		if ( conPanel )
		{
			conPanel->SetParent( (vgui::Panel *)NULL );
			delete conPanel;
			conPanel = NULL;
		}

		messages.Purge();
	}

	bool IsVisible( void )
	{
		return conPanel && conPanel->IsVisible();
	}

	void Toggle( void )
	{
		if ( !conPanel )
			return;

		if ( conPanel->IsVisible() )
		{
			conPanel->Close();
		}
		else
		{
			vgui::surface()->RestrictPaintToSinglePanel( NULL );
			conPanel->Activate();
		}
	}
};

static CConsole g_Console;
IConsole *console = ( IConsole * )&g_Console;

CON_COMMAND( toggleconsole2, "Show or hide the client console." )
{
	g_Console.Toggle();
}
