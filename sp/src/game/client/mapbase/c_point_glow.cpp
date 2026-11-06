//========= Mapbase - https://github.com/mapbase-source/source-sdk-2013 ============//
//
// Purpose: Mapbase off-shoot of tf_glow (created using SDK code only)
//
//===========================================================================//

#include "cbase.h"
#include "glow_outline_effect.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class C_PointGlow : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_PointGlow, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	~C_PointGlow();

	enum
	{
		GLOW_VIS_ALWAYS,
		GLOW_VIS_NOT_WHEN_VISIBLE,
		GLOW_VIS_ONLY_WHEN_VISIBLE,
	};

	void OnDataChanged( DataUpdateType_t type );
	void ClientThink();

	CGlowObject			*GetGlowObject( void ){ return m_pGlowEffect; }
	void				UpdateGlowEffect( void );
	void				DestroyGlowEffect( void );

	EHANDLE m_hGlowTarget;
	color32 m_GlowColor;

	bool				m_bGlowDisabled;
	bool				m_bGravityGunFreeze;
	CGlowObject			*m_pGlowEffect;
};

IMPLEMENT_CLIENTCLASS_DT( C_PointGlow, DT_PointGlow, CPointGlow )
	RecvPropEHandle( RECVINFO( m_hGlowTarget ) ),
	RecvPropInt( RECVINFO( m_GlowColor ), 0, RecvProxy_IntToColor32 ),
	RecvPropBool( RECVINFO( m_bGlowDisabled ) ),
	RecvPropBool( RECVINFO( m_bGravityGunFreeze ) ),
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_PointGlow::~C_PointGlow()
{
	DestroyGlowEffect();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_PointGlow::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	UpdateGlowEffect();
	SetNextClientThink( m_bGravityGunFreeze ? CLIENT_THINK_ALWAYS : CLIENT_THINK_NEVER );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_PointGlow::ClientThink()
{
	static ConVarRef traceLength( "physcannon_tracelength" );
	const bool bInRange = m_hGlowTarget && m_hGlowTarget->GetAbsOrigin().DistToSqr( MainViewOrigin() ) <= Square( traceLength.GetFloat() );
	if ( ( m_pGlowEffect != NULL ) != ( !m_bGlowDisabled && bInRange ) )
		UpdateGlowEffect();
}

void C_PointGlow::UpdateGlowEffect( void )
{
	// destroy the existing effect
	if ( m_pGlowEffect )
	{
		DestroyGlowEffect();
	}

	// create a new effect
	static ConVarRef traceLength( "physcannon_tracelength" );
	if ( !m_bGlowDisabled && ( !m_bGravityGunFreeze || ( m_hGlowTarget && m_hGlowTarget->GetAbsOrigin().DistToSqr( MainViewOrigin() ) <= Square( traceLength.GetFloat() ) ) ) )
	{
		Vector4D vecColor( m_GlowColor.r, m_GlowColor.g, m_GlowColor.b, m_GlowColor.a );
		for (int i = 0; i < 4; i++)
		{
			if (vecColor[i] == 0.0f)
				continue;

			vecColor[i] /= 255.0f;
		}

		m_pGlowEffect = new CGlowObject( m_hGlowTarget, vecColor.AsVector3D(), vecColor.w, true );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_PointGlow::DestroyGlowEffect( void )
{
	if ( m_pGlowEffect )
	{
		delete m_pGlowEffect;
		m_pGlowEffect = NULL;
	}
}
