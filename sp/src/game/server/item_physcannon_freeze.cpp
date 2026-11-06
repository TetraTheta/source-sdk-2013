//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Handling for the suit batteries.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "hl2_player.h"
#include "basecombatweapon.h"
#include "gamerules.h"
#include "items.h"
#include "engine/IEngineSound.h"
#include "recipientfilter.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CItemPhyscannonFreeze : public CItem
{
public:
	DECLARE_CLASS(CItemPhyscannonFreeze, CItem);

	void Spawn(void)
	{
		Precache();
		SetModel("models/gravgun_crystal/gravgun_crystal.mdl");
		BaseClass::Spawn();
	}
	void Precache(void)
	{
		PrecacheModel("models/gravgun_crystal/gravgun_crystal.mdl");
		PrecacheScriptSound("SuitRecharge.Start");
	}
	bool MyTouch(CBasePlayer *pPlayer)
	{
		CHL2_Player *pHL2Player = dynamic_cast<CHL2_Player *>(pPlayer);
		if ( !pHL2Player || pHL2Player->m_bHasGGFreezeModule )
			return false;

		pHL2Player->m_bHasGGFreezeModule = true;
		pHL2Player->EmitSound( "SuitRecharge.Start" );

		CSingleUserRecipientFilter user( pHL2Player );
		user.MakeReliable();
		UserMessageBegin( user, "ItemPickup" );
		WRITE_STRING( "item_physcannon_freeze" );
		MessageEnd();

		UTIL_HudHintText( pHL2Player, "#RTSL_Hint_GGFreeze" );
		return true;
	}
};

LINK_ENTITY_TO_CLASS(item_physcannon_freeze, CItemPhyscannonFreeze);
PRECACHE_REGISTER(item_physcannon_freeze);
