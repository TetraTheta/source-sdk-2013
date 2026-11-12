#include "cbase.h"
#include "baseviewmodel.h"
#include "entityoutput.h"
#include "igamesystem.h"
#include "gameinterface.h"
#include "KeyValues.h"
#include "filesystem.h"
#include "props_shared.h"
#include "recipientfilter.h"
#include "tier1/utlstring.h"

#include "tier0/memdbgon.h"

namespace
{
const char *const s_Arguments[] = { "all", "ammo", "debris", "decals", "gibs", "powerups", "ragdolls", "small", "weapons" };
const char *const s_Labels[] = { "Ammo", "Debris", "Gibs", "Powerups", "Ragdolls", "Small objects", "Weapons" };
const char *const s_Categories[] = { "ammo", "debris", "gibs", "powerups", "ragdolls", "small", "weapons" };
enum { AMMO, DEBRIS, GIBS, POWERUPS, RAGDOLLS, SMALL, WEAPONS, CATEGORY_COUNT };

bool MatchesModel( const char *model, const char *pattern )
{
  const char *star = NULL;
  const char *retry = NULL;
  while ( *model )
  {
    if ( *pattern == '*' )
    {
      star = pattern++;
      retry = model;
    }
    else if ( *pattern == *model )
    {
      ++pattern;
      ++model;
    }
    else if ( star )
    {
      pattern = star + 1;
      model = ++retry;
    }
    else
      return false;
  }
  while ( *pattern == '*' ) ++pattern;
  return !*pattern;
}

class CCleanConfig : public CAutoGameSystem
{
public:
  CCleanConfig() : CAutoGameSystem( "CleanConfig" ), m_loaded( false ) {}
  void LevelInitPreEntity() OVERRIDE
  {
    CBaseEntity::PrecacheSound( "UI/buttonclick.wav" );
    if ( m_loaded ) return;
    m_loaded = true;
    KeyValues *config = new KeyValues( "Clean" );
    if ( config->LoadFromFile( filesystem, "scripts/sc_clean.txt", "GAME" ) )
    {
      KeyValues *models = config->FindKey( "Model" );
      for ( KeyValues *entry = models ? models->GetFirstSubKey() : NULL; entry; entry = entry->GetNextKey() )
      {
        if ( Q_stricmp( entry->GetName(), "model" ) || !entry->GetString()[0] ) continue;
        CUtlString pattern( entry->GetString() );
        V_FixSlashes( pattern.Get(), '/' );
        Q_strlower( pattern.Get() );
        m_models.AddToTail( pattern );
      }
      if ( !m_models.Count() ) Warning( "[sc_clean] No model patterns in scripts/sc_clean.txt; small-object cleanup disabled.\n" );
    }
    else
      Warning( "[sc_clean] Cannot read scripts/sc_clean.txt; small-object cleanup disabled until restart.\n" );
    config->deleteThis();
  }
  bool IsSmall( CBaseEntity *entity ) const
  {
    CUtlString model( STRING( entity->GetModelName() ) );
    V_FixSlashes( model.Get(), '/' );
    Q_strlower( model.Get() );
    FOR_EACH_VEC( m_models, i )
      if ( MatchesModel( model.String(), m_models[i].String() ) ) return true;
    return false;
  }
private:
  bool m_loaded;
  CUtlVector<CUtlString> m_models;
} s_Config;

bool IsProtected( CBaseEntity *entity )
{
  for ( CBaseEntity *parent = entity; parent; parent = parent->GetMoveParent() )
  {
    if ( parent->IsWorld() || parent->IsPlayer() || dynamic_cast<CBaseViewModel *>( parent ) || parent->GetEntityName() != NULL_STRING ) return true;
    CBaseEntity *owner = parent->GetOwnerEntity();
    if ( owner && owner->IsPlayer() ) return true;
    for ( datamap_t *map = parent->GetDataDescMap(); map; map = map->baseMap )
    {
      for ( int i = 0; i < map->dataNumFields; ++i )
      {
        typedescription_t &field = map->dataDesc[i];
        if ( field.fieldType == FIELD_CUSTOM && ( field.flags & FTYPEDESC_OUTPUT ) )
        {
          CBaseEntityOutput *output = reinterpret_cast<CBaseEntityOutput *>( reinterpret_cast<char *>( parent ) + field.fieldOffset[TD_OFFSET_NORMAL] );
          if ( output->NumberOfElements() ) return true;
        }
      }
    }
  }
  for ( CBaseEntity *child = entity->FirstMoveChild(); child; child = child->NextMovePeer() )
    if ( IsProtected( child ) ) return true;
  return false;
}

bool IsAmmo( CBaseEntity *entity )
{
  const char *const classes[] = {
    "item_ammo_pistol", "item_ammo_pistol_large", "item_ammo_smg1", "item_ammo_smg1_large",
    "item_ammo_ar2", "item_ammo_ar2_large", "item_ammo_357", "item_ammo_357_large",
    "item_ammo_crossbow", "item_ammo_smg1_grenade", "item_ammo_ar2_altfire", "item_box_buckshot",
    "item_rpg_round", "item_ml_grenade", "item_flare_round", "item_box_flare_rounds",
    "item_box_srounds", "item_large_box_srounds", "item_box_mrounds", "item_large_box_mrounds",
    "item_box_lrounds", "item_large_box_lrounds", "item_ar2_grenade", "item_box_sniper_rounds",
    "item_css_ammo_45acp", "item_css_ammo_45acp_large", "item_css_ammo_357sig", "item_css_ammo_357sig_large",
    "item_css_ammo_556mm", "item_css_ammo_556mm_large", "item_css_ammo_762mm", "item_css_ammo_762mm_large"
  };
  for ( int i = 0; i < ARRAYSIZE( classes ); ++i )
    if ( entity->ClassMatches( classes[i] ) ) return true;
  return false;
}

bool IsMapWeapon( CBaseEntity *entity )
{
  if ( entity->m_iHammerID != 0 ) return true;
  // Serial numbers distinguish an original map weapon from a reused edict slot.
  FOR_EACH_LL( g_MapEntityRefs, i )
  {
    const CMapEntityRef &reference = g_MapEntityRefs[i];
    if ( reference.m_iEdict == entity->entindex() && entity->edict() &&
         reference.m_iSerialNumber == entity->edict()->m_NetworkSerialNumber ) return true;
  }
  return false;
}

bool MatchesCategory( CBaseEntity *entity, int category )
{
  switch ( category )
  {
  case AMMO: return IsAmmo( entity );
  case DEBRIS: return entity->ClassMatches( "prop_physics" ) && entity->HasSpawnFlags( SF_PHYSPROP_IS_GIB );
  case GIBS: return entity->ClassMatches( "gib" );
  case POWERUPS:
    return entity->ClassMatches( "item_battery" ) || entity->ClassMatches( "item_healthkit" ) || entity->ClassMatches( "item_healthvial" ) ||
           entity->ClassMatches( "item_battery_custom" ) || entity->ClassMatches( "item_healthkit_custom" );
  case RAGDOLLS: return entity->ClassMatches( "prop_ragdoll" );
  case SMALL: return entity->ClassMatches( "prop_physics" ) && s_Config.IsSmall( entity );
  case WEAPONS: return entity->IsBaseCombatWeapon() && !entity->GetOwnerEntity() && !IsMapWeapon( entity );
  }
  return false;
}

int CleanCategory( int category )
{
  int count = 0;
  for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
  {
    if ( entity->IsMarkedForDeletion() || !entity->VPhysicsGetObject() || IsProtected( entity ) ) continue;
    if ( !MatchesCategory( entity, category ) ) continue;
    UTIL_Remove( entity );
    ++count;
  }
  return count;
}
}

class CCleanResult : public CPointEntity
{
public:
  DECLARE_CLASS( CCleanResult, CPointEntity );
  DECLARE_DATADESC();
  void Replace( CBasePlayer *player, const char *message )
  {
    if ( !message[0] ) return;
    SetThink( NULL );
    SetNextThink( TICK_NEVER_THINK );
    if ( m_hint.Get() ) m_hint->AcceptInput( "HideHudHint", player, this, variant_t(), 0 );
    if ( !m_hint.Get() )
    {
      if ( engine->GetEntityCount() + 1 >= MAX_EDICTS || gEntList.NumberOfEntities() + 1 >= NUM_ENT_ENTRIES ) return;
      CBaseEntity *hint = CreateEntityByName( "env_hudhint" );
      if ( !hint || DispatchSpawn( hint ) < 0 ) return;
      m_hint = hint;
    }
    Q_strncpy( m_message, message, sizeof( m_message ) );
    m_showPending = true;
    SetThink( &CCleanResult::DisplayThink );
    SetNextThink( gpGlobals->curtime + 0.15f );
  }
  void DisplayThink()
  {
    CBasePlayer *player = ToBasePlayer( GetOwnerEntity() );
    if ( !player || !m_hint.Get() )
    {
      UTIL_Remove( this );
      return;
    }
    if ( m_showPending )
    {
      m_hint->KeyValue( "message", m_message );
      m_hint->AcceptInput( "ShowHudHint", player, this, variant_t(), 0 );
      CSingleUserRecipientFilter filter( player );
      EmitSound( filter, player->entindex(), "UI/buttonclick.wav" );
      m_showPending = false;
      SetNextThink( gpGlobals->curtime + 3.0f );
    }
    else
    {
      m_hint->AcceptInput( "HideHudHint", player, this, variant_t(), 0 );
      SetThink( NULL );
      SetNextThink( TICK_NEVER_THINK );
    }
  }
  void UpdateOnRemove() OVERRIDE
  {
    if ( m_hint.Get() ) UTIL_Remove( m_hint.Get() );
    BaseClass::UpdateOnRemove();
  }
private:
  EHANDLE m_hint;
  char m_message[512];
  bool m_showPending;
};
LINK_ENTITY_TO_CLASS( sc_clean_result, CCleanResult );
BEGIN_DATADESC( CCleanResult )
  DEFINE_FIELD( m_hint, FIELD_EHANDLE ),
  DEFINE_AUTO_ARRAY( m_message, FIELD_CHARACTER ),
  DEFINE_FIELD( m_showPending, FIELD_BOOLEAN ),
  DEFINE_THINKFUNC( DisplayThink ),
END_DATADESC()

static int CleanCompletion( const char *partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH] )
{
  const char *argument = strchr( partial, ' ' );
  argument = argument ? argument + 1 : "";
  while ( *argument == ' ' ) ++argument;
  int count = 0;
  for ( int i = 0; i < ARRAYSIZE( s_Arguments ); ++i )
    if ( !Q_strnicmp( s_Arguments[i], argument, Q_strlen( argument ) ) )
      Q_snprintf( commands[count++], COMMAND_COMPLETION_ITEM_LENGTH, "sc_clean %s", s_Arguments[i] );
  return count;
}

CON_COMMAND_F_COMPLETION( sc_clean, "Remove unprotected objects from the current map.", FCVAR_NONE, CleanCompletion )
{
  CBasePlayer *player = UTIL_GetCommandClient();
  if ( !player && !engine->IsDedicatedServer() && gpGlobals->maxClients == 1 ) player = UTIL_GetLocalPlayer();
  if ( !player ) return;
  bool valid = false;
  for ( int i = 0; i < ARRAYSIZE( s_Arguments ); ++i )
    if ( args.ArgC() == 2 && !Q_stricmp( args[1], s_Arguments[i] ) ) valid = true;
  if ( !valid )
  {
    ClientPrint( player, HUD_PRINTCONSOLE, "Usage: sc_clean <all|ammo|debris|decals|gibs|powerups|ragdolls|small|weapons>\n" );
    return;
  }
  bool all = !Q_stricmp( args[1], "all" );
  CReliableBroadcastRecipientFilter filter;
  UserMessageBegin( filter, "SCClean" );
  WRITE_BYTE( ( ( all || !Q_stricmp( args[1], "ragdolls" ) ) ? 1 : 0 ) |
              ( ( all || !Q_stricmp( args[1], "decals" ) ) ? 2 : 0 ) );
  MessageEnd();

  CUtlString message;
  for ( int i = 0; i < CATEGORY_COUNT; ++i )
  {
    if ( !all && Q_stricmp( args[1], s_Categories[i] ) ) continue;
    int count = CleanCategory( i );
    if ( !count ) continue;
    if ( message.IsEmpty() ) message = "%$Clean Result:%";
    char line[64];
    Q_snprintf( line, sizeof( line ), "\n%d %s", count, s_Labels[i] );
    message += line;
  }
  CCleanResult *result = NULL;
  CBaseEntity *entity = NULL;
  while ( ( entity = gEntList.FindEntityByClassname( entity, "sc_clean_result" ) ) != NULL )
    if ( entity->GetOwnerEntity() == player ) { result = static_cast<CCleanResult *>( entity ); break; }
  if ( !result && !message.IsEmpty() && engine->GetEntityCount() + 2 < MAX_EDICTS && gEntList.NumberOfEntities() + 2 < NUM_ENT_ENTRIES )
  {
    result = static_cast<CCleanResult *>( CreateEntityByName( "sc_clean_result" ) );
    if ( result )
    {
      result->SetOwnerEntity( player );
      if ( DispatchSpawn( result ) < 0 ) result = NULL;
    }
  }
  if ( result ) result->Replace( player, message.String() );
}
