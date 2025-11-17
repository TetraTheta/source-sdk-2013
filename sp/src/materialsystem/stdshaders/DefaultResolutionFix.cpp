#include "tier1/strtools.h"
#include "icommandline.h"
#include "iregistry.h"

// Seed first-launch settings without replacing a saved resolution.
static class DefaultResolutionFix
{
public:
  DefaultResolutionFix()
  {
    const char *exeArg = CommandLine()->GetParm(0);
    if (!exeArg || V_stricmp(V_GetFileName(exeArg), "hl2.exe"))
      return;

    if (CommandLine()->FindParm("-safe"))
      return;

    bool widthOverridden = CommandLine()->FindParm("-width") || CommandLine()->FindParm("-w");
    bool heightOverridden = CommandLine()->FindParm("-height") || CommandLine()->FindParm("-h");
    if (widthOverridden || heightOverridden)
      return;

    // Preserve the -game path exactly: the engine uses it as the video settings key.
    const char *gamePath = CommandLine()->ParmValue("-game", "hl2");
    if (!gamePath[0])
      return;

    char settingsKey[MAX_PATH + 32];
    V_snprintf(settingsKey, sizeof(settingsKey), "Source\\%s\\Settings", gamePath);

    // Use an instance because DLL global constructors have no guaranteed order.
    IRegistry *settings = InstanceRegistry(settingsKey);
    if (!settings->ReadInt("ScreenWidth") && !settings->ReadInt("ScreenHeight"))
    {
      settings->WriteInt("ScreenWidth", 1920);
      settings->WriteInt("ScreenHeight", 1080);

      // Pass the first-launch defaults to the engine as well as persisting them.
      CommandLine()->AppendParm("-width", "1920");
      CommandLine()->AppendParm("-height", "1080");
    }
    ReleaseInstancedRegistry(settings);
  }
} s_DefaultResFix;
