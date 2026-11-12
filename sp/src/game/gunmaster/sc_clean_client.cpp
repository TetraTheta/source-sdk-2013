#include "cbase.h"
#include "c_baseanimating.h"
#include "cliententitylist.h"
#include "hud_macros.h"
#include "igamesystem.h"
#include "usermessages.h"
#include "engine/ivmodelrender.h"
#include "tier0/memdbgon.h"

static void __MsgFunc_SCClean( bf_read &message )
{
  const int flags = message.ReadByte();
  if ( flags & 2 )
  {
    // World decals are engine-owned; model decals need an explicit renderer clear.
    engine->ClientCmd_Unrestricted( "r_cleardecals\n" );
    modelrender->RemoveAllDecalsFromAllModels();
  }
  if ( !( flags & 1 ) ) return;
  for ( C_BaseEntity *entity = ClientEntityList().FirstBaseEntity(); entity; entity = ClientEntityList().NextBaseEntity( entity ) )
  {
    C_ClientRagdoll *ragdoll = dynamic_cast<C_ClientRagdoll *>( entity );
    if ( ragdoll ) ragdoll->ReleaseRagdoll();
  }
}

class CCleanClient : public CAutoGameSystem
{
public:
  CCleanClient() : CAutoGameSystem( "CleanClient" ) {}
  bool Init() OVERRIDE
  {
    HOOK_MESSAGE( SCClean );
    return true;
  }
} s_CleanClient;
