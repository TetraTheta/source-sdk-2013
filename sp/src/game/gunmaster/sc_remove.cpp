#include "cbase.h"
#include "baseviewmodel.h"

#include "tier0/memdbgon.h"

static bool IsProtectedEntity( CBaseEntity *pEntity )
{
  for ( CBaseEntity *pParent = pEntity; pParent; pParent = pParent->GetMoveParent() )
  {
    if ( pParent->IsWorld() || pParent->IsPlayer() || dynamic_cast<CBaseViewModel *>( pParent ) )
      return true;

    CBaseEntity *pOwner = pParent->GetOwnerEntity();
    if ( pOwner && pOwner->IsPlayer() )
      return true;
  }

  return false;
}

CON_COMMAND( sc_remove, "Safely remove the entity you are looking at." )
{
  CBasePlayer *pPlayer = UTIL_GetCommandClient();
  // Single-player console commands may arrive without a command client.
  if ( !pPlayer && !engine->IsDedicatedServer() && gpGlobals->maxClients == 1 )
    pPlayer = UTIL_GetLocalPlayer();

  if ( !pPlayer )
    return;

  Vector forward;
  pPlayer->EyeVectors( &forward );
  const Vector start = pPlayer->EyePosition();
  trace_t trace;
  UTIL_TraceLine( start, start + forward * MAX_TRACE_LENGTH, MASK_SOLID | CONTENTS_AUX | CONTENTS_DEBRIS, pPlayer, COLLISION_GROUP_NONE, &trace );

  CBaseEntity *pEntity = trace.m_pEnt;
  if ( !trace.DidHit() || !pEntity || pEntity->IsMarkedForDeletion() || IsProtectedEntity( pEntity ) )
    return;

  if ( pEntity->ClassMatches( "func_door" ) || pEntity->ClassMatches( "func_door_rotating" ) || pEntity->ClassMatches( "prop_door_rotating" ) )
  {
    const char *pName = STRING( pEntity->GetEntityName() );
    if ( pName[0] )
    {
      CBaseEntity *pPortal = NULL;
      while ( ( pPortal = gEntList.FindEntityByClassname( pPortal, "func_areaportal" ) ) != NULL )
      {
        if ( !pPortal->IsMarkedForDeletion() && !Q_stricmp( STRING( pPortal->m_target ), pName ) )
        {
          variant_t value;
          pPortal->AcceptInput( "Open", pPlayer, pPlayer, value, 0 );
        }
      }
    }
  }

  UTIL_Remove( pEntity );
}
