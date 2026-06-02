#ifndef _G_MFQ3ENTS_H__
#define _G_MFQ3ENTS_H__



#include "g_entity.h"

struct Think_RadioComply : public GameEntity::EntityFunc_Think
{
	virtual void	execute();
};

struct Die_ExplosiveDie : public GameEntity::EntityFunc_Die
{
	virtual void	execute( GameEntity* inflictor, GameEntity* attacker, int damage, int mod );
};

struct Touch_RechargeTouch : public GameEntity::EntityFunc_Touch
{
	virtual void	execute( GameEntity* other, trace_t *trace );
};

struct Touch_RadioTower : public GameEntity::EntityFunc_Touch
{
	virtual void	execute( GameEntity* other, trace_t *trace );
};

struct Think_ExplodeVehicle : public GameEntity::EntityFunc_Think
{
	virtual void	execute();
};

// MFQ3 missions: ground-installation AI brain (scan / track / fire).
struct Think_GroundInstallation : public GameEntity::EntityFunc_Think
{
	virtual void	execute();
};

// MFQ3 missions: shared death handler for mission vehicles & installations
// (explosion, score award, radius damage, objective notification).
struct Die_MiscVehicle : public GameEntity::EntityFunc_Die
{
	virtual void	execute( GameEntity* inflictor, GameEntity* attacker, int damage, int mod );
};

// MFQ3 missions: no-op pain so non-lethal hits don't deref a NULL painFunc_.
struct Pain_MiscVehicle : public GameEntity::EntityFunc_Pain
{
	virtual void	execute( GameEntity* attacker, int damage );
};

#endif // _G_MFQ3ENTS_H__

