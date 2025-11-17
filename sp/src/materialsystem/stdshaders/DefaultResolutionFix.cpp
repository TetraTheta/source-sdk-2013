#include "cdll_int.h"
#include "tier1/tier1.h"
#include "icommandline.h"
#include "iregistry.h"
#include "IGameUIFuncs.h"

// Purpose: Ensure we pick a default resolution on first launch that isn't horrible
static class DefaultResolutionFix
{
public:
	DefaultResolutionFix()
	{
		// I don't see how this should be possible, but just incase
		const char* exeArg = CommandLine()->GetParm(0);
		if (!exeArg)
			return;

		// only apply this in game
		const char* exeName = V_GetFileName(exeArg);
		if (V_stricmp(exeName, "hl2.exe"))
			return;
		
		CreateInterfaceFn vstdlibFactory = Sys_GetFactory("vstdlib.dll");
		if (!vstdlibFactory)
			return;

		CreateInterfaceFn engineFactory = Sys_GetFactory("engine.dll");
		if (!engineFactory)
			return;

		ConnectTier1Libraries(&vstdlibFactory, 1);

		IGameUIFuncs *gameUIFuncs = (IGameUIFuncs*)engineFactory(VENGINE_GAMEUIFUNCS_VERSION, nullptr);
		if (!g_pCVar || !gameUIFuncs)
			return;
		
		// initialise the registry
		char gamedir[MAX_PATH];
		V_snprintf(gamedir, sizeof(gamedir), "Source/%s", CommandLine()->ParmValue("-game", "hl2"));
		V_FixSlashes(gamedir);
		registry->Init(gamedir);
		
		ApplyFirstTimeSettings(gameUIFuncs);
	}
	
	// determine what to do with the window
	void ApplyFirstTimeSettings(IGameUIFuncs* gameUIFuncs)
	{
		// video safe mode, don't do anything
		if (CommandLine()->FindParm("-safe"))
			return;

		int desktopWidth, desktopHeight;
		gameUIFuncs->GetDesktopResolution(desktopWidth, desktopHeight);

		// check we aren't explictly setting a width and height
		bool widthOverridden = CommandLine()->FindParm("-width") || CommandLine()->FindParm("-w");
		bool heightOverridden = CommandLine()->FindParm("-height") || CommandLine()->FindParm("-h");

		if (!widthOverridden && !heightOverridden)
		{
			// if we don't have any settings, write them to the registry so we have more reasonable defaults
			if (!registry->ReadInt("ScreenWidth") && !registry->ReadInt("ScreenHeight"))
			{
				registry->WriteInt("ScreenWidth", desktopWidth);
				registry->WriteInt("ScreenHeight", desktopHeight);
			}
		}
	}
} s_DefaultResFix;
