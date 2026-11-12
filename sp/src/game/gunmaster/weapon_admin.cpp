#include "cbase.h"
#include "ai_basenpc.h"
#include "ammodef.h"
#include "baseviewmodel.h"
#include "in_buttons.h"
#include "npcevent.h"
#include "player.h"
#include "recipientfilter.h"
#include "soundent.h"
#include "weapon_parse.h"
#include "explode.h"
#include "hl2/basehlcombatweapon_shared.h"
#include "hl2/grenade_frag.h"
#include "hl2/hl2_shareddefs.h"
#include "hl2/prop_combine_ball.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "tier0/memdbgon.h"

extern ISoundEmitterSystemBase *soundemitterbase;
acttable_t *GetSMG1Acttable();
int GetSMG1ActtableCount();
bool FireAdminCrossbowBolt( CBasePlayer *player, const Vector &origin, const Vector &direction, float speed, float damage );

namespace
{
// This order defines the reload cycle and the one-based InitialMode setting.
enum AdminMode { EXPLOSION, AIRBOAT, COMBINE_BALL, CROSSBOW, GRENADE, MODE_COUNT };
enum AdminSound { SINGLE_SHOT, EXPLOSION_SOUND, AIRBOAT_SOUND, BALL_SOUND, BOUNCE_SOUND, BOLT_SOUND, GRENADE_SOUND };

const char *const s_ModeNames[] = { "Explosion", "Airboat Gun", "Combine Ball", "Crossbow Bolt", "Armed Grenade" };
COMPILE_TIME_ASSERT( ARRAYSIZE( s_ModeNames ) == MODE_COUNT );

bool HasEntitySlots( int count )
{
	return engine->GetEntityCount() + count < MAX_EDICTS &&
		gEntList.NumberOfEntities() + count < NUM_ENT_ENTRIES;
}

struct AdminSoundDefinition
{
	const char *name;          // Weapon_Admin event emitted by this weapon; an existing definition wins.
	const char *source;        // Optional stock event to copy, including channel, waves, pitch, and sound level.
	const char *wave;          // Fallback WAV used only when the stock event is absent or unspecified.
	const char *alternateWave; // Optional second fallback WAV, chosen randomly instead of the first.
	float volume;             // Stock volume multiplier, or absolute fallback volume when no stock event is copied.
};

// Rows follow AdminSound order. Existing Weapon_Admin soundscript definitions take precedence.
// Otherwise, copy the stock event if found; only then fall back to the WAV candidates.
// Single uses 1.0f to preserve Weapon_SMG1.Single volume, including its random volume range.
// Change that multiplier below to scale it. If Weapon_Admin.Single is already defined in a
// soundscript, edit that definition's volume instead; this table does not override it.
const AdminSoundDefinition s_Sounds[] =
{
	// Primary fire, also used by NPCs; 1.0f copies SMG1 volume unchanged.
	{ "Weapon_Admin.Single", "Weapon_SMG1.Single", ")weapons/smg1/smg1_fire1.wav", NULL, 1.0f },
	// Explosion mode; env_explosion itself is silent.
	{ "Weapon_Admin.Explosion", NULL, ")weapons/explode3.wav", NULL, 0.15f },
	// Airboat mode; randomly select one of the two fallback WAVs.
	{ "Weapon_Admin.Airboat", NULL, ")weapons/airboat/airboat_gun_lastshot1.wav", ")weapons/airboat/airboat_gun_lastshot2.wav", 0.35f },
	// Combine Ball mode launch; played together with CombineBall2.
	{ "Weapon_Admin.CombineBall1", NULL, ")weapons/ar2/ar2_altfire.wav", NULL, 0.35f },
	// Combine Ball mode bounce accent; played at launch together with CombineBall1.
	{ "Weapon_Admin.CombineBall2", NULL, ")weapons/physcannon/energy_bounce1.wav", NULL, 0.35f },
	// Crossbow mode; copy the stock crossbow event when available.
	{ "Weapon_Admin.Crossbow", "Weapon_Crossbow.Single", ")weapons/crossbow/fire1.wav", NULL, 1.0f },
	// Grenade mode; ticking cue when the armed grenade is thrown.
	{ "Weapon_Admin.Grenade", NULL, ")weapons/grenade/tick1.wav", NULL, 0.15f }
};

void RegisterAdminSound( const AdminSoundDefinition &sound )
{
	if ( soundemitterbase->IsValidIndex( soundemitterbase->GetSoundIndex( sound.name ) ) )
	{
		if ( developer.GetInt() )
			DevMsg( "[weapon_admin] sound %s: existing definition\n", sound.name );
		return;
	}

	CSoundParametersInternal parameters;
	const int source = sound.source ? soundemitterbase->GetSoundIndex( sound.source ) : soundemitterbase->InvalidIndex();
	const char *script = "scripts/game_sounds_weapons.txt";
	if ( soundemitterbase->IsValidIndex( source ) )
	{
		script = soundemitterbase->GetSourceFileForSound( source );
		parameters.CopyFrom( *soundemitterbase->InternalGetParametersForSound( source ) );
		const volume_interval_t &volume = parameters.GetVolume();
		parameters.SetVolume( volume.start * sound.volume, volume.range * sound.volume );
	}
	else
	{
		parameters.SetChannel( CHAN_WEAPON );
		parameters.SetVolume( sound.volume );
		parameters.SetPitch( PITCH_NORM );
		parameters.SetSoundLevel( SNDLVL_GUNFIRE );
		SoundFile wave;
		wave.symbol = soundemitterbase->AddWaveName( sound.wave );
		parameters.AddSoundName( wave );
		if ( sound.alternateWave )
		{
			wave.symbol = soundemitterbase->AddWaveName( sound.alternateWave );
			parameters.AddSoundName( wave );
		}
	}

	const bool added = soundemitterbase->AddSound( sound.name, script, parameters );
	if ( developer.GetInt() )
		DevMsg( "[weapon_admin] sound %s: %s\n", sound.name, added ? "C++ default" : "registration failed" );
}
}

void PrecacheAdminWeaponSounds()
{
	for ( int i = 0; i < ARRAYSIZE( s_Sounds ); ++i )
	{
		RegisterAdminSound( s_Sounds[i] );
		CBaseEntity::PrecacheScriptSound( s_Sounds[i].name );
	}
}

class CWeaponAdmin : public CBaseHLCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponAdmin, CBaseHLCombatWeapon );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CWeaponAdmin();
	void Spawn() OVERRIDE;
	void Precache() OVERRIDE;
	void PrimaryAttack() OVERRIDE;
	void SecondaryAttack() OVERRIDE;
	void WeaponSound( WeaponSound_t sound, float time ) OVERRIDE;
	void ItemPostFrame() OVERRIDE;
	void UpdateOnRemove() OVERRIDE;
	bool Holster( CBaseCombatWeapon *switchingTo ) OVERRIDE;
	void Drop( const Vector &velocity ) OVERRIDE;
	int CapabilitiesGet() OVERRIDE { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int GetMinBurst() OVERRIDE { return 2; }
	int GetMaxBurst() OVERRIDE { return 5; }
	float GetFireRate() OVERRIDE { return 0.075f; }
	acttable_t *ActivityList() OVERRIDE { return GetSMG1Acttable(); }
	int ActivityListCount() OVERRIDE { return GetSMG1ActtableCount(); }
	void Operator_HandleAnimEvent( animevent_t *event, CBaseCombatCharacter *operatorCharacter ) OVERRIDE;

private:
	float Setting( const char *key, float fallback, float minimum, float maximum ) const;
	void PlayAdminSound( AdminSound sound );
	void FireAdminBullets( CBaseCombatCharacter *owner, const Vector &direction, bool airboat );
	Vector ProjectileOrigin( CBasePlayer *player, const Vector &eye, float radius );
	void FinishAttack( CBasePlayer *player, float delay, const char *recoilKey );
	bool FireExplosion( CBasePlayer *player, const Vector &origin, const Vector &direction );
	void ShowModeHint();
	void CancelModeHint();

	int m_mode;
	EHANDLE m_explosion;
	EHANDLE m_hint;
	EHANDLE m_hintPlayer;
	float m_nextDiagnostic;
};

IMPLEMENT_SERVERCLASS_ST( CWeaponAdmin, DT_WeaponAdmin )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_admin, CWeaponAdmin );
PRECACHE_WEAPON_REGISTER( weapon_admin );

BEGIN_DATADESC( CWeaponAdmin )
	DEFINE_FIELD( m_mode, FIELD_INTEGER ),
	DEFINE_FIELD( m_explosion, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hint, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hintPlayer, FIELD_EHANDLE ),
	DEFINE_THINKFUNC( ShowModeHint ),
END_DATADESC()

CWeaponAdmin::CWeaponAdmin() : m_mode( EXPLOSION ), m_nextDiagnostic( 0.0f )
{
	m_fMinRange1 = 0;
	m_fMaxRange1 = 1400;
}

// Keys are relative to WeaponData/AdminAttack in scripts/weapon_admin.txt.
// Each call supplies the C++ default and the accepted minimum and maximum.
float CWeaponAdmin::Setting( const char *key, float fallback, float minimum, float maximum ) const
{
	return GetWpnData().GetAdminSetting( key, fallback, minimum, maximum );
}

void CWeaponAdmin::Spawn()
{
	BaseClass::Spawn();
	m_mode = static_cast<int>( Setting( "InitialMode", 1, 1, MODE_COUNT ) ) - 1;
}

void CWeaponAdmin::Precache()
{
	BaseClass::Precache();
	PrecacheSound( "UI/buttonclick.wav" );
	UTIL_PrecacheOther( "env_explosion" );
	UTIL_PrecacheOther( "crossbow_bolt" );
	UTIL_PrecacheOther( "prop_combine_ball" );
	UTIL_PrecacheOther( "npc_grenade_frag" );
}

void CWeaponAdmin::WeaponSound( WeaponSound_t sound, float time )
{
	if ( sound == SINGLE || sound == SINGLE_NPC )
		PlayAdminSound( SINGLE_SHOT );
}

void CWeaponAdmin::PlayAdminSound( AdminSound index )
{
	EmitSound( s_Sounds[index].name );
}

void CWeaponAdmin::FireAdminBullets( CBaseCombatCharacter *owner, const Vector &direction, bool airboat )
{
	const float spread = Setting( airboat ? "Airboat/Spread" : "Primary/Spread", 0.015f, 0, 1 );
	const int shots = static_cast<int>( Setting( airboat ? "Airboat/Shots" : "Primary/Shots", airboat ? 7 : 25, 1, 100 ) );
	const int ammo = GetAmmoDef()->Index( airboat ? "AirboatGun" : "Pistol" );
	FireBulletsInfo_t info( shots, owner->Weapon_ShootPosition(), direction, Vector( spread, spread, 0 ), MAX_TRACE_LENGTH, ammo );
	info.m_flDamage = Setting( airboat ? "Airboat/Damage" : "Primary/Damage", airboat ? 50 : 1000, 1, 100000 );
	info.m_flDamageForceScale = Setting( airboat ? "Airboat/ForceScale" : "Primary/ForceScale", 1, 0, 10000 );
	info.m_iTracerFreq = 1;
	// Keep the eye trace for damage, but attach the visible tracer to the gun.
	CBasePlayer *player = ToBasePlayer( owner );
	CBaseViewModel *viewModel = player ? player->GetViewModel() : NULL;
	info.m_pTracerWeapon = this;
	info.m_iTracerAttachment = viewModel ? viewModel->LookupAttachment( "muzzle" ) : LookupAttachment( "muzzle" );
	// Reuse one AI hearing slot per shooter instead of allocating one per pellet.
	info.m_nFlags |= FIRE_BULLETS_NO_IMPACT_EFFECTS | FIRE_BULLETS_TEMPORARY_DANGER_SOUND;
	if ( airboat )
		info.m_nFlags |= FIRE_BULLETS_AIRBOAT_TRACER;
	else
		info.m_nFlags |= FIRE_BULLETS_FIRST_SHOT_ACCURATE | FIRE_BULLETS_ADMIN_PRIMARY;
	owner->FireBullets( info );

	if ( !owner->IsPlayer() )
		return;

	const float cone = cosf( DEG2RAD( 2.2f ) );
	CBaseEntity *missile = NULL;
	while ( ( missile = gEntList.FindEntityByClassname( missile, "*_missile" ) ) != NULL )
	{
		Vector offset = missile->WorldSpaceCenter() - owner->GetAbsOrigin();
		if ( VectorNormalize( offset ) <= 8000 && DotProduct( offset, direction ) >= cone )
			missile->TakeDamage( CTakeDamageInfo( this, owner, 9999, DMG_MISSILEDEFENSE ) );
	}
}

void CWeaponAdmin::FinishAttack( CBasePlayer *player, float delay, const char *recoilKey )
{
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + delay;
	const float recoil = Setting( recoilKey, 0.1f, 0, 20 );
	player->ViewPunch( QAngle( -recoil, random->RandomFloat( -recoil, recoil ), -recoil ) );
	player->DoMuzzleFlash();
	player->SetAnimation( PLAYER_ATTACK1 );
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
}

void CWeaponAdmin::PrimaryAttack()
{
	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( !player )
		return;

	FireAdminBullets( player, player->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT ), false );
	PlayAdminSound( SINGLE_SHOT );
	FinishAttack( player, Setting( "Primary/Delay", 0.05f, 0.05f, 10 ), "Primary/Recoil" );
}

void CWeaponAdmin::Operator_HandleAnimEvent( animevent_t *event, CBaseCombatCharacter *operatorCharacter )
{
	if ( event->event != EVENT_WEAPON_SMG1 )
	{
		BaseClass::Operator_HandleAnimEvent( event, operatorCharacter );
		return;
	}

	CAI_BaseNPC *npc = operatorCharacter->MyNPCPointer();
	if ( !npc )
		return;

	const Vector origin = npc->Weapon_ShootPosition();
	FireAdminBullets( npc, npc->GetActualShootTrajectory( origin ), false );
	PlayAdminSound( SINGLE_SHOT );
	CSoundEnt::InsertSound( SOUND_COMBAT | SOUND_CONTEXT_GUNFIRE, npc->GetAbsOrigin(), SOUNDENT_VOLUME_MACHINEGUN, 0.2f, npc, SOUNDENT_CHANNEL_WEAPON, npc->GetEnemy() );
	npc->DoMuzzleFlash();
	if ( developer.GetInt() >= 2 && gpGlobals->curtime >= m_nextDiagnostic )
	{
		m_nextDiagnostic = gpGlobals->curtime + 1.0f;
		DevMsg( "[weapon_admin] NPC %d fired at %.3f, regulator remaining=%d resting=%d cadence=%.3f\n",
			npc->entindex(), gpGlobals->curtime, npc->GetShotRegulator()->GetBurstShotsRemaining(),
			npc->GetShotRegulator()->IsInRestInterval(), GetFireRate() );
	}
}

bool CWeaponAdmin::FireExplosion( CBasePlayer *player, const Vector &origin, const Vector &direction )
{
	trace_t trace;
	UTIL_TraceLine( origin, origin + direction * MAX_TRACE_LENGTH, MASK_SHOT, player, COLLISION_GROUP_NONE, &trace );

	// Keep one explosion per weapon so sustained fire does not churn entity slots.
	CBaseEntity *explosion = m_explosion.Get();
	if ( !explosion )
	{
		if ( !HasEntitySlots( 1 ) )
			return false;

		explosion = CreateEntityByName( "env_explosion" );
		if ( !explosion )
			return false;

		// Match scaw_base's effect suppression while preserving blast damage.
		explosion->AddSpawnFlags( SF_ENVEXPLOSION_REPEATABLE | SF_ENVEXPLOSION_NOFIREBALL |
			SF_ENVEXPLOSION_NOSMOKE | SF_ENVEXPLOSION_NODECAL | SF_ENVEXPLOSION_NOSPARKS |
			SF_ENVEXPLOSION_NOSOUND | SF_ENVEXPLOSION_NOFIREBALLSMOKE | SF_ENVEXPLOSION_NOPARTICLES );
		char magnitude[16];
		Q_snprintf( magnitude, sizeof( magnitude ), "%d", static_cast<int>( Setting( "Explosion/Magnitude", 175, 1, 10000 ) ) );
		explosion->KeyValue( "iMagnitude", magnitude );
		explosion->SetAbsOrigin( trace.endpos );
		if ( DispatchSpawn( explosion ) < 0 )
			return false;
		m_explosion = explosion;
	}

	explosion->SetAbsOrigin( trace.endpos );
	explosion->SetOwnerEntity( player );
	explosion->AcceptInput( "Explode", player, this, variant_t(), 0 );
	return true;
}

Vector CWeaponAdmin::ProjectileOrigin( CBasePlayer *player, const Vector &eye, float radius )
{
	Vector muzzle;
	QAngle angles;
	CBaseViewModel *viewModel = player->GetViewModel();
	if ( viewModel && viewModel->GetAttachmentLocal( "muzzle", muzzle, angles ) )
	{
		// Viewmodel bob and sway run on the client. Place the local muzzle in the server's eye frame.
		matrix3x4_t eyeToWorld;
		AngleMatrix( player->EyeAngles(), eye, eyeToWorld );
		Vector localMuzzle = muzzle;
		VectorTransform( localMuzzle, eyeToWorld, muzzle );
	}
	else if ( !GetAttachment( "muzzle", muzzle, angles ) )
	{
		return eye;
	}

	// A viewmodel can extend into nearby geometry. Keep the projectile on the shooter's side.
	const Vector extent( radius, radius, radius );
	CTraceFilterSkipTwoEntities filter( player, this, COLLISION_GROUP_NONE );
	trace_t clearance;
	UTIL_TraceHull( eye, muzzle, -extent, extent, MASK_SOLID, &filter, &clearance );
	return clearance.startsolid ? eye : clearance.endpos;
}

void CWeaponAdmin::SecondaryAttack()
{
	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( !player )
		return;

	const Vector origin = player->Weapon_ShootPosition();
	const Vector direction = player->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	bool fired = true;
	float delay;
	AdminSound sound;

	switch ( m_mode )
	{
	case EXPLOSION:
		delay = Setting( "Explosion/Delay", 0.05f, 0.05f, 10 );
		sound = EXPLOSION_SOUND;
		fired = FireExplosion( player, origin, direction );
		break;
	case AIRBOAT:
		delay = Setting( "Airboat/Delay", 0.05f, 0.05f, 10 );
		sound = AIRBOAT_SOUND;
		FireAdminBullets( player, direction, true );
		break;
	case COMBINE_BALL:
	{
		delay = Setting( "CombineBall/Delay", 0.5f, 0.05f, 10 );
		sound = BALL_SOUND;
		// Use the AR2 lifetime path so a spawner can cancel it when it captures the ball.
		const float radius = Setting( "CombineBall/Radius", 2, 1, 64 );
		fired = HasEntitySlots( 2 ) && CreateCombineBall( ProjectileOrigin( player, origin, radius ), direction * Setting( "CombineBall/Speed", 3000, 1, 10000 ),
			radius, 150,
			Setting( "CombineBall/Lifetime", 5, 0.1f, 120 ), player ) != NULL;
		break;
	}
	case CROSSBOW:
		delay = Setting( "Crossbow/Delay", 0.25f, 0.05f, 10 );
		sound = BOLT_SOUND;
		fired = HasEntitySlots( 2 ) && FireAdminCrossbowBolt( player, ProjectileOrigin( player, origin, 1 ), direction, Setting( "Crossbow/Speed", 3750, 1, 10000 ),
			Setting( "Crossbow/Damage", -1, 1, 100000 ) );
		break;
	default:
		delay = Setting( "Grenade/Delay", 0.25f, 0.05f, 10 );
		sound = GRENADE_SOUND;
		fired = HasEntitySlots( 3 ) && Fraggrenade_Create( ProjectileOrigin( player, origin, 4 ), player->EyeAngles(),
			direction * Setting( "Grenade/Speed", 1000, 1, 10000 ), AngularImpulse( 0, 0, 0 ), player,
			Setting( "Grenade/Fuse", 3, 0.1f, 120 ), false ) != NULL;
		break;
	}

	if ( fired )
	{
		PlayAdminSound( sound );
		if ( m_mode == COMBINE_BALL )
			PlayAdminSound( BOUNCE_SOUND );
		FinishAttack( player, delay, "Secondary/Recoil" );
	}
	else
	{
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + delay;
	}

	if ( developer.GetInt() >= ( fired ? 2 : 1 ) && gpGlobals->curtime >= m_nextDiagnostic )
	{
		m_nextDiagnostic = gpGlobals->curtime + 1.0f;
		DevMsg( "[weapon_admin] %s: fired=%d delay=%.3f explosion=%d edicts=%d\n", s_ModeNames[m_mode], fired,
			delay, m_explosion.Get() ? m_explosion->entindex() : -1, engine->GetEntityCount() );
	}
}

void CWeaponAdmin::ItemPostFrame()
{
	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( !player )
		return;

	if ( WeaponShouldBeLowered() )
	{
		WeaponIdle();
		return;
	}

	if ( ( player->GetButtonPressed() & IN_RELOAD ) && m_flNextPrimaryAttack <= gpGlobals->curtime )
	{
		m_mode = ( m_mode + 1 ) % MODE_COUNT;
		CSingleUserRecipientFilter filter( player );
		// Mode switching is server-only, so the client cannot predict this click.
		filter.SetIgnorePredictionCull( true );
		EmitSound( filter, player->entindex(), "UI/buttonclick.wav" );
		if ( developer.GetInt() )
			DevMsg( "[weapon_admin] mode: %s\n", s_ModeNames[m_mode] );

		if ( !m_hint.Get() && HasEntitySlots( 1 ) )
		{
			m_hint = CreateEntityByName( "env_hudhint" );
			if ( m_hint.Get() && DispatchSpawn( m_hint.Get() ) < 0 )
				m_hint = NULL;
		}

		if ( !m_hint.Get() && developer.GetInt() )
			DevMsg( "[weapon_admin] could not create env_hudhint\n" );

		if ( m_hint.Get() )
		{
			// Give the HUD time to clear the previous hint before showing its replacement.
			m_hint->AcceptInput( "HideHudHint", player, this, variant_t(), 0 );
			m_hintPlayer = player;
			SetContextThink( &CWeaponAdmin::ShowModeHint, gpGlobals->curtime + 0.15f, "AdminModeHint" );
			if ( developer.GetInt() >= 2 )
				DevMsg( "[weapon_admin] HideHudHint at %.3f\n", gpGlobals->curtime );
		}
	}

	const int buttons = player->GetButtons();
	if ( ( buttons & IN_ATTACK2 ) && m_flNextSecondaryAttack <= gpGlobals->curtime )
		SecondaryAttack();
	else if ( ( buttons & IN_ATTACK ) && m_flNextPrimaryAttack <= gpGlobals->curtime )
		PrimaryAttack();
	else if ( !( buttons & ( IN_ATTACK | IN_ATTACK2 ) ) )
		WeaponIdle();
}

void CWeaponAdmin::ShowModeHint()
{
	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( player && player->IsAlive() && m_hintPlayer.Get() == player && player->GetActiveWeapon() == this && m_hint.Get() )
	{
		variant_t message;
		message.SetString( AllocPooledString( s_ModeNames[m_mode] ) );
		m_hint->AcceptInput( "SetHudHint", player, this, message, 0 );
		m_hint->AcceptInput( "ShowHudHint", player, this, variant_t(), 0 );
		if ( developer.GetInt() >= 2 )
			DevMsg( "[weapon_admin] ShowHudHint at %.3f\n", gpGlobals->curtime );
	}
	m_hintPlayer = NULL;
}

void CWeaponAdmin::CancelModeHint()
{
	SetContextThink( NULL, TICK_NEVER_THINK, "AdminModeHint" );
	m_hintPlayer = NULL;
}

bool CWeaponAdmin::Holster( CBaseCombatWeapon *switchingTo )
{
	if ( !BaseClass::Holster( switchingTo ) )
		return false;

	CancelModeHint();
	return true;
}

void CWeaponAdmin::Drop( const Vector &velocity )
{
	CancelModeHint();
	BaseClass::Drop( velocity );
}

void CWeaponAdmin::UpdateOnRemove()
{
	CancelModeHint();
	if ( m_hint.Get() )
		UTIL_Remove( m_hint.Get() );
	if ( m_explosion.Get() )
		UTIL_Remove( m_explosion.Get() );
	BaseClass::UpdateOnRemove();
}
