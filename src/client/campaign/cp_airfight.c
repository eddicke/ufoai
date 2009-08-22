/**
 * @file cp_airfight.c
 * @brief Airfight related stuff.
 * @todo Somehow i need to know which alien race was in the ufo we shoot down
 * I need this info for spawning the crash site
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../client.h"
#include "cp_campaign.h"
#include "cp_mapfightequip.h"
#include "cp_map.h"
#include "cp_ufo.h"
#include "cp_missions.h"

/**
 * @brief Run bullets on geoscape.
 * @param[in] projectile Pointer to the projectile corresponding to this bullet.
 * @param[in] ortogonalVector vector perpendicular to the movement of the projectile.
 * @param[in] movement how much each bullet should move toward its target.
 */
#if 0
static void AIRFIGHT_RunBullets (aircraftProjectile_t *projectile, vec3_t ortogonalVector, float movement)
{
	int i;
	vec3_t startPoint, finalPoint;

	assert(projectile->bullets);

	for (i = 0; i < BULLETS_PER_SHOT; i++) {
		PolarToVec(projectile->bulletPos[i], startPoint);
		RotatePointAroundVector(finalPoint, ortogonalVector, startPoint, movement);
		VecToPolar(finalPoint, projectile->bulletPos[i]);
	}
}
#endif

/**
 * @brief Remove a projectile from ccs.projectiles
 * @param[in] projectile The projectile to remove
 * @sa AIRFIGHT_AddProjectile
 */
static qboolean AIRFIGHT_RemoveProjectile (aircraftProjectile_t *projectile)
{
	REMOVE_ELEM_ADJUST_IDX(ccs.projectiles, projectile->idx, ccs.numProjectiles);
	return qtrue;
}

/**
 * @brief Add a projectile in ccs.projectiles
 * @param[in] attackingBase the attacking base in ccs.bases[]. NULL is the attacker is an aircraft.
 * @param[in] attacker Pointer to the attacking aircraft
 * @param[in] target Pointer to the target aircraft
 * @param[in] weaponSlot Pointer to the weapon slot that fires the projectile.
 * @note we already checked in AIRFIGHT_ChooseWeapon that the weapon has still ammo
 * @sa AIRFIGHT_RemoveProjectile
 * @sa AII_ReloadWeapon for the aircraft item reload code
 */
static qboolean AIRFIGHT_AddProjectile (const base_t* attackingBase, const installation_t* attackingInstallation, aircraft_t *attacker, aircraft_t *target, aircraftSlot_t *weaponSlot)
{
	aircraftProjectile_t *projectile;

	if (ccs.numProjectiles >= MAX_PROJECTILESONGEOSCAPE) {
		Com_DPrintf(DEBUG_CLIENT, "Too many projectiles on map\n");
		return qfalse;
	}

	projectile = &ccs.projectiles[ccs.numProjectiles];

	if (!weaponSlot->ammo) {
		Com_Printf("AIRFIGHT_AddProjectile: Error - no ammo assigned\n");
		return qfalse;
	}

	assert(weaponSlot->item);

	projectile->idx = (ptrdiff_t)(projectile - ccs.projectiles);
	projectile->aircraftItem = weaponSlot->ammo;
	if (attackingBase) {
		projectile->attackingAircraft = NULL;
		VectorSet(projectile->pos[0], attackingBase->pos[0], attackingBase->pos[1], 0);
		VectorSet(projectile->attackerPos, attackingBase->pos[0], attackingBase->pos[1], 0);
	} else if (attackingInstallation) {
		projectile->attackingAircraft = NULL;
		VectorSet(projectile->pos[0], attackingInstallation->pos[0], attackingInstallation->pos[1], 0);
		VectorSet(projectile->attackerPos, attackingInstallation->pos[0], attackingInstallation->pos[1], 0);
	} else {
		assert(attacker);
		projectile->attackingAircraft = attacker;
		VectorSet(projectile->pos[0], attacker->pos[0], attacker->pos[1], 0);
		/* attacker may move, use attackingAircraft->pos */
		VectorSet(projectile->attackerPos, 0, 0, 0);
	}

	projectile->numProjectiles++;

	assert(target);
	projectile->aimedAircraft = target;
	VectorSet(projectile->idleTarget, 0, 0, 0);

	projectile->time = 0;
	projectile->angle = 0.0f;

	if (weaponSlot->item->craftitem.bullets)
		projectile->bullets = qtrue;
	else
		projectile->bullets = qfalse;

	if (weaponSlot->item->craftitem.laser)
		projectile->laser = qtrue;
	else
		projectile->laser = qfalse;

	if (weaponSlot->ammoLeft != AMMO_STATUS_UNLIMITED)
		weaponSlot->ammoLeft--;

	ccs.numProjectiles++;

	return qtrue;
}

#ifdef DEBUG
/**
 * @brief List all projectiles on map to console.
 * @note called with debug_listprojectile
 */
static void AIRFIGHT_ProjectileList_f (void)
{
	int i;

	for (i = 0; i < ccs.numProjectiles; i++) {
		Com_Printf("%i. (idx: %i)\n", i, ccs.projectiles[i].idx);
		Com_Printf("... type '%s'\n", ccs.projectiles[i].aircraftItem->id);
		if (ccs.projectiles[i].attackingAircraft)
			Com_Printf("... shooting aircraft '%s'\n", ccs.projectiles[i].attackingAircraft->id);
		else
			Com_Printf("... base is shooting, or shooting aircraft is destroyed\n");
		if (ccs.projectiles[i].aimedAircraft)
			Com_Printf("... aiming aircraft '%s'\n", ccs.projectiles[i].aimedAircraft->id);
		else
			Com_Printf("... aiming iddle target at (%.02f, %.02f)\n",
				ccs.projectiles[i].idleTarget[0], ccs.projectiles[i].idleTarget[1]);
	}
}
#endif

/**
 * @brief Change destination of projectile to an idle point of the map, close to its former target.
 * @param[in] projectile The projectile to update
 * @param[in] returnToBase
 */
static void AIRFIGHT_MissTarget (aircraftProjectile_t *projectile, qboolean returnToBase)
{
	vec3_t newTarget;
	float distance;
	float offset;

	assert(projectile);

	if (projectile->aimedAircraft) {
		VectorCopy(projectile->aimedAircraft->pos, newTarget);
		projectile->aimedAircraft = NULL;
	} else {
		VectorCopy(projectile->idleTarget, newTarget);
	}

	/* get the distance between the projectile and target */
	distance = MAP_GetDistance(projectile->pos[0], newTarget);

	/* Work out how much the projectile should miss the target by.  We dont want it too close
	 * or too far from the original target.
	 * * 1/3 distance between target and projectile * random (range -0.5 to 0.5)
	 * * Then make sure the value is at least greater than 0.1 or less than -0.1 so that
	 *   the projectile doesn't land too close to the target. */
	offset = (distance / 3) * (frand() - 0.5f);

	if (abs(offset) < 0.1f)
		offset = 0.1f;

	newTarget[0] = newTarget[0] + offset;
	newTarget[1] = newTarget[1] + offset;

	VectorCopy(newTarget, projectile->idleTarget);

	if (returnToBase && projectile->attackingAircraft) {
		if (projectile->attackingAircraft->homebase) {
			assert(projectile->attackingAircraft->ufotype == UFO_MAX);
			AIR_AircraftReturnToBase(projectile->attackingAircraft);
		}
	}
}

/**
 * @brief Check if the selected weapon can shoot.
 * @param[in] slot Pointer to the weapon slot to shoot with.
 * @param[in] distance distance between the weapon and the target.
 * @return 0 AIRFIGHT_WEAPON_CAN_SHOOT if the weapon can shoot,
 * -1 AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT if it can't shoot atm,
 * -2 AIRFIGHT_WEAPON_CAN_NEVER_SHOOT if it will never be able to shoot.
 */
int AIRFIGHT_CheckWeapon (const aircraftSlot_t *slot, float distance)
{
	assert(slot);

	/* check if there is a functional weapon in this slot */
	if (!slot->item || slot->installationTime != 0)
		return AIRFIGHT_WEAPON_CAN_NEVER_SHOOT;

	/* check if there is still ammo in this weapon */
	if (!slot->ammo || (slot->ammoLeft <= 0 && slot->ammoLeft != AMMO_STATUS_UNLIMITED))
		return AIRFIGHT_WEAPON_CAN_NEVER_SHOOT;

	/* check if the target is within range of this weapon */
	if (distance > slot->ammo->craftitem.stats[AIR_STATS_WRANGE])
		return AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT;

	/* check if weapon is reloaded */
	if (slot->delayNextShot > 0)
		return AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT;

	return AIRFIGHT_WEAPON_CAN_SHOOT;
}

/**
 * @brief Choose the weapon an attacking aircraft will use to fire on a target.
 * @param[in] slot Pointer to the first weapon slot of attacking base or aircraft.
 * @param[in] maxSlot maximum number of weapon slots in attacking base or aircraft.
 * @param[in] pos position of attacking base or aircraft.
 * @param[in] targetPos Pointer to the aimed aircraft.
 * @return indice of the slot to use (in array weapons[]),
 * -1 AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT no weapon to use atm,
 * -2 AIRFIGHT_WEAPON_CAN_NEVER_SHOOT if no weapon to use at all.
 * @sa AIRFIGHT_CheckWeapon
 */
int AIRFIGHT_ChooseWeapon (const aircraftSlot_t const *slot, int maxSlot, const vec3_t pos, const vec3_t targetPos)
{
	int slotIdx = AIRFIGHT_WEAPON_CAN_NEVER_SHOOT;
	int i, weaponStatus;
	float distance0 = 99999.9f;
	const float distance = MAP_GetDistance(pos, targetPos);

	/* We choose the usable weapon with the smallest range */
	for (i = 0; i < maxSlot; i++) {
		assert(slot);
		weaponStatus = AIRFIGHT_CheckWeapon(slot + i, distance);

		/* set slotIdx to AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT if needed */
		/* this will only happen if weapon_state is AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT
		 * and no weapon has been found that can shoot. */
		if (weaponStatus > slotIdx)
			slotIdx = AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT;

		/* select this weapon if this is the one with the shortest range */
		if (weaponStatus >= AIRFIGHT_WEAPON_CAN_SHOOT && distance < distance0) {
			slotIdx = i;
			distance0 = distance;
		}
	}
	return slotIdx;
}

/**
 * @brief Calculate the probability to hit the enemy.
 * @param[in] shooter Pointer to the attacking aircraft (may be NULL if a base fires the projectile).
 * @param[in] target Pointer to the aimed aircraft (may be NULL if a target is a base).
 * @param[in] slot Slot containing the weapon firing.
 * @return Probability to hit the target (0 when you don't have a chance, 1 (or more) when you're sure to hit).
 * @note that modifiers due to electronics, weapons, and shield are already taken into account in AII_UpdateAircraftStats
 * @sa AII_UpdateAircraftStats
 * @sa AIRFIGHT_ExecuteActions
 * @sa AIRFIGHT_ChooseWeapon
 * @pre slotIdx must have a weapon installed, with ammo available (see AIRFIGHT_ChooseWeapon)
 * @todo This probability should also depend on the pilot skills, when they will be implemented.
 */
static float AIRFIGHT_ProbabilityToHit (const aircraft_t *shooter, const aircraft_t *target, const aircraftSlot_t *slot)
{
	float probability = 0.0f;

	if (!slot->item) {
		Com_Printf("AIRFIGHT_ProbabilityToHit: no weapon assigned to attacking aircraft\n");
		return probability;
	}

	if (!slot->ammo) {
		Com_Printf("AIRFIGHT_ProbabilityToHit: no ammo in weapon of attacking aircraft\n");
		return probability;
	}

	/* Take Base probability from the ammo of the attacking aircraft */
	probability = slot->ammo->craftitem.stats[AIR_STATS_ACCURACY];
	Com_DPrintf(DEBUG_CLIENT, "AIRFIGHT_ProbabilityToHit: Base probablity: %f\n", probability);

	/* Modify this probability by items of the attacking aircraft (stats is in percent) */
	if (shooter)
		probability *= shooter->stats[AIR_STATS_ACCURACY] / 100.0f;

	/* Modify this probability by items of the aimed aircraft (stats is in percent) */
	if (target)
		probability /= target->stats[AIR_STATS_ECM] / 100.0f;

	Com_DPrintf(DEBUG_CLIENT, "AIRFIGHT_ProbabilityToHit: Probability to hit: %f\n", probability);
	return probability;
}

/**
 * @brief Decide what an attacking aircraft can do.
 * @param[in] shooter The aircraft we attack with.
 * @param[in] target The ufo we are going to attack.
 * @todo Implement me and display an attack popup.
 */
void AIRFIGHT_ExecuteActions (aircraft_t* shooter, aircraft_t* target)
{
	int slotIdx;
	float probability;

	/* some asserts */
	assert(shooter);
	assert(target);

	/* Check if the attacking aircraft can shoot */
	slotIdx = AIRFIGHT_ChooseWeapon(shooter->weapons, shooter->maxWeapons, shooter->pos, target->pos);

	/* if weapon found that can shoot */
	if (slotIdx >= AIRFIGHT_WEAPON_CAN_SHOOT) {
		const objDef_t *ammo = shooter->weapons[slotIdx].ammo;

		/* shoot */
		if (AIRFIGHT_AddProjectile(NULL, NULL, shooter, target, shooter->weapons + slotIdx)) {
			shooter->weapons[slotIdx].delayNextShot = ammo->craftitem.weaponDelay;
			/* will we miss the target ? */
			probability = frand();
			if (probability > AIRFIGHT_ProbabilityToHit(shooter, target, shooter->weapons + slotIdx))
				AIRFIGHT_MissTarget(&ccs.projectiles[ccs.numProjectiles - 1], qfalse);

			if (shooter->type != AIRCRAFT_UFO) {
				/* Maybe UFO is going to shoot back ? */
				UFO_CheckShootBack(target, shooter);
			} else {
				/* an undetected UFO within radar range and firing should become detected */
				if (!shooter->detected && RADAR_CheckRadarSensored(shooter->pos)) {
					/* stop time and notify */
					MSO_CheckAddNewMessage(NT_UFO_ATTACKING,_("Notice"), va(_("A UFO is shooting at %s"), _(target->name)), qfalse, MSG_STANDARD, NULL);
					RADAR_AddDetectedUFOToEveryRadar(shooter);
					UFO_DetectNewUFO(shooter);
				}
			}
		}
	} else if (slotIdx == AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT) {
		/* no ammo to fire atm (too far or reloading), pursue target */
		if (shooter->type == AIRCRAFT_UFO) {
			/** @todo This should be calculated only when target destination changes, or when aircraft speed changes.
			 *  @sa AIR_GetDestination */
			UFO_SendPursuingAircraft(shooter, target);
		} else
			AIR_SendAircraftPursuingUFO(shooter, target);
	} else {
		/* no ammo left, or no weapon, proceed with mission */
		if (shooter->type == AIRCRAFT_UFO) {
			shooter->aircraftTarget = NULL;		/* reset target */
			CP_UFOProceedMission(shooter);
		} else {
			MS_AddNewMessage(_("Notice"), _("Our aircraft has no more ammo left - returning to home base now."), qfalse, MSG_STANDARD, NULL);
			AIR_AircraftReturnToBase(shooter);
		}
	}
}

/**
 * @brief Set all projectile aiming a given aircraft to an idle destination.
 * @param[in] aircraft Pointer to the aimed aircraft.
 * @note This function is called when @c aircraft is destroyed.
 * @sa AIRFIGHT_ActionsAfterAirfight
 */
static void AIRFIGHT_RemoveProjectileAimingAircraft (const aircraft_t * aircraft)
{
	aircraftProjectile_t *projectile;
	int idx = 0;

	if (!aircraft)
		return;

	for (projectile = ccs.projectiles; idx < ccs.numProjectiles; projectile++, idx++) {
		if (projectile->aimedAircraft == aircraft)
			AIRFIGHT_MissTarget(projectile, qtrue);
	}
}

/**
 * @brief Set all projectile attackingAircraft pointers to NULL
 * @param[in] aircraft Pointer to the destroyed aircraft.
 * @note This function is called when @c aircraft is destroyed.
 */
static void AIRFIGHT_UpdateProjectileForDestroyedAircraft (const aircraft_t * aircraft)
{
	aircraftProjectile_t *projectile;
	int idx;

	for (idx = 0, projectile = ccs.projectiles; idx < ccs.numProjectiles; projectile++, idx++) {
		const aircraft_t *attacker = projectile->attackingAircraft;

		if (attacker == aircraft)
			projectile->attackingAircraft = NULL;
	}
}

/**
 * @brief Actions to execute when a fight is done.
 * @param[in] shooter Pointer to the aircraft that fired the projectile.
 * @param[in] aircraft Pointer to the aircraft which was destroyed (alien or phalanx).
 * @param[in] phalanxWon qtrue if PHALANX won, qfalse if UFO won.
 * @note Some of these mission values are redone (and not reloaded) in CP_Load
 * @note shooter may be NULL
 * @sa UFO_DestroyAllUFOsOnGeoscape_f
 * @sa CP_Load
 * @sa CP_SpawnCrashSiteMission
 */
void AIRFIGHT_ActionsAfterAirfight (aircraft_t *shooter, aircraft_t* aircraft, qboolean phalanxWon)
{
	if (phalanxWon) {
		byte *color;

		assert(aircraft);

		/* change destination of other projectiles aiming aircraft */
		AIRFIGHT_RemoveProjectileAimingAircraft(aircraft);
		/* now update the projectile for the destroyed aircraft, too */
		AIRFIGHT_UpdateProjectileForDestroyedAircraft(aircraft);

		/* don't remove ufo from global array: the mission is not over yet
		 * UFO are removed from game only at the end of the mission
		 * (in case we need to know what item to collect e.g.) */

		/* get the color value of the map at the crash position */
		color = MAP_GetColor(aircraft->pos, MAPTYPE_TERRAIN);
		/* if this color value is not the value for water ...
		 * and we hit the probability to spawn a crashsite mission */
		if (!MapIsWater(color)) {
			CP_SpawnCrashSiteMission(aircraft);
		} else {
			Com_DPrintf(DEBUG_CLIENT, "AIRFIGHT_ActionsAfterAirfight: zone: %s (%i:%i:%i)\n", MAP_GetTerrainType(color), color[0], color[1], color[2]);
			MS_AddNewMessage(_("Interception"), _("UFO interception successful -- UFO lost to sea."), qfalse, MSG_STANDARD, NULL);
			CP_MissionIsOverByUFO(aircraft);
		}
	} else {
		/* change destination of other projectiles aiming aircraft */
		AIRFIGHT_RemoveProjectileAimingAircraft(aircraft);

		/* and now update the projectile pointers (there still might be some in the air
		 * of the current destroyed aircraft) - this is needed not send the aircraft
		 * back to base as soon as the projectiles will hit their target */
		AIRFIGHT_UpdateProjectileForDestroyedAircraft(aircraft);

		/* notify UFOs that a phalanx aircraft has been destroyed */
		UFO_NotifyPhalanxAircraftRemoved(aircraft);

		/* Destroy the aircraft and everything onboard - the aircraft pointer
		 * is no longer valid after this point */
		AIR_DestroyAircraft(aircraft);

		/* Make UFO proceed with its mission, if it has not been already destroyed */
		if (shooter)
			CP_UFOProceedMission(shooter);

		MS_AddNewMessage(_("Interception"), _("You've lost the battle"), qfalse, MSG_STANDARD, NULL);
	}
}

/**
 * @brief Check if some projectiles on geoscape reached their destination.
 * @note Destination is not necessarily an aircraft, in case the projectile missed its initial target.
 * @param[in] projectile Pointer to the projectile
 * @param[in] movement distance that the projectile will do up to next draw of geoscape
 * @sa AIRFIGHT_CampaignRunProjectiles
 */
static qboolean AIRFIGHT_ProjectileReachedTarget (const aircraftProjectile_t *projectile, float movement)
{
	float distance;

	if (!projectile->aimedAircraft)
		/* the target is idle, its position is in idleTarget*/
		distance = MAP_GetDistance(projectile->idleTarget, projectile->pos[0]);
	else {
		/* the target is moving, pointer to the other aircraft is aimedAircraft */
		distance = MAP_GetDistance(projectile->aimedAircraft->pos, projectile->pos[0]);
	}

	/* projectile reaches its target */
	if (distance < movement)
		return qtrue;

	assert(projectile->aircraftItem);

	/* check if the projectile went farther than it's range */
	distance = (float) projectile->time * projectile->aircraftItem->craftitem.weaponSpeed / (float)SECONDS_PER_HOUR;
	if (distance > projectile->aircraftItem->craftitem.stats[AIR_STATS_WRANGE])
		return qtrue;

	return qfalse;
}

/**
 * @brief Calculates the damage value for the airfight
 * @param[in] od The ammo object definition of the craft item
 * @sa AII_UpdateAircraftStats
 * @note ECM is handled in AIRFIGHT_ProbabilityToHit
 */
static int AIRFIGHT_GetDamage (const objDef_t *od, const aircraft_t* target)
{
	int damage;

	assert(od);

	/* already destroyed - do nothing */
	if (target->damage <= 0)
		return 0;

	/* base damage is given by the ammo */
	damage = od->craftitem.weaponDamage;

	/* reduce damages with shield target */
	damage -= target->stats[AIR_STATS_SHIELD];

	return damage;
}

/**
 * @brief Solve the result of one projectile hitting an aircraft.
 * @param[in] projectile Pointer to the projectile.
 * @note the target loose (base damage - shield of target) hit points
 */
static void AIRFIGHT_ProjectileHits (aircraftProjectile_t *projectile)
{
	aircraft_t *target;
	int damage = 0;

	assert(projectile);
	target = projectile->aimedAircraft;
	assert(target);

	/* if the aircraft is not on geoscape anymore, do nothing (returned to base) */
	if (AIR_IsAircraftInBase(target))
		return;

	damage = AIRFIGHT_GetDamage(projectile->aircraftItem, target);

	/* apply resulting damages - but only if damage > 0 - because the target might
	 * already be destroyed, and we don't want to execute the actions after airfight
	 * for every projectile */
	if (damage > 0) {
		assert(target->damage > 0);
		target->damage -= damage;
		if (target->damage <= 0)
			/* Target is destroyed */
			AIRFIGHT_ActionsAfterAirfight(projectile->attackingAircraft, target, target->type == AIRCRAFT_UFO);
	}
}

/**
 * @brief Get the next point in the object path based on movement converting the positions from
 * polar coordinates to vector for the calculation and back again to be returned.
 * @param[in] movement The distance that the object needs to move.
 * @param[in] originalPoint The point from which the object is moving.
 * @param[in] orthogonalVector The orthogonal vector.
 * @param[out] finalPoint The next point from the original point + movement in "angle" direction.
 */
static void AIRFIGHT_GetNextPointInPathFromVector (const float *movement, const vec2_t originalPoint, const vec3_t orthogonalVector, vec2_t finalPoint)
{
	vec3_t startPoint, finalVectorPoint;

	PolarToVec(originalPoint, startPoint);
	RotatePointAroundVector(finalVectorPoint, orthogonalVector, startPoint, *movement);
	VecToPolar(finalVectorPoint, finalPoint);
}

/**
 * @brief Get the next point in the object path based on movement.
 * @param[in] movement The distance that the object needs to move.
 * @param[in] originalPoint The point from which the object is moving.
 * @param[in] targetPoint The final point to which the object is moving.
 * @param[in] angle The direction that the object moving in.
 * @param[out] finalPoint The next point from the original point + movement in "angle" direction.
 * @param[out] orthogonalVector The orthogonal vector.
 */
static void AIRFIGHT_GetNextPointInPath (const float *movement, const vec2_t originalPoint, const vec2_t targetPoint, float *angle, vec2_t finalPoint, vec3_t orthogonalVector)
{
	*angle = MAP_AngleOfPath(originalPoint, targetPoint, NULL, orthogonalVector);
	AIRFIGHT_GetNextPointInPathFromVector(movement, originalPoint, orthogonalVector, finalPoint);
}


/**
 * @brief Update values of projectiles.
 * @param[in] dt Time elapsed since last call of this function.
 */
void AIRFIGHT_CampaignRunProjectiles (int dt)
{
	int idx;

	/* ccs.numProjectiles is changed in AIRFIGHT_RemoveProjectile */
	for (idx = ccs.numProjectiles - 1; idx >= 0; idx--) {
		aircraftProjectile_t *projectile = &ccs.projectiles[idx];
		const float movement = (float) dt * projectile->aircraftItem->craftitem.weaponSpeed / (float)SECONDS_PER_HOUR;
		projectile->time += dt;
		projectile->hasMoved = qtrue;
		projectile->numInterpolationPoints = 0;

		/* Check if the projectile reached its destination (aircraft or idle point) */
		if (AIRFIGHT_ProjectileReachedTarget(projectile, movement)) {
			/* check if it got the ennemy */
			if (projectile->aimedAircraft)
				AIRFIGHT_ProjectileHits(projectile);

			/* remove the missile from ccs.projectiles[] */
			AIRFIGHT_RemoveProjectile(projectile);
		} else {
			float angle;
			vec3_t ortogonalVector, finalPoint, projectedPoint;

			/* missile is moving towards its target */
			if (projectile->aimedAircraft) {
				AIRFIGHT_GetNextPointInPath(&movement, projectile->pos[0], projectile->aimedAircraft->pos, &angle, finalPoint, ortogonalVector);
				AIRFIGHT_GetNextPointInPath(&movement, finalPoint, projectile->aimedAircraft->pos, &angle, projectedPoint, ortogonalVector);
			} else {
				AIRFIGHT_GetNextPointInPath(&movement, projectile->pos[0], projectile->idleTarget, &angle, finalPoint, ortogonalVector);
				AIRFIGHT_GetNextPointInPath(&movement, finalPoint, projectile->idleTarget, &angle, projectedPoint, ortogonalVector);
			}

			/* udpate angle of the projectile */
			projectile->angle = angle;
			VectorCopy(finalPoint, projectile->pos[0]);
			VectorCopy(projectedPoint, projectile->projectedPos[0]);
		}

	}
}

/**
 * @brief Check if one type of battery (missile or laser) can shoot now.
 * @param[in] base Pointer to the firing base.
 * @param[in] weapons The base weapon to check and fire.
 * @param[in] maxWeapons
 */
static void AIRFIGHT_BaseShoot (const base_t *base, baseWeapon_t *weapons, int maxWeapons)
{
	int i, test;
	float distance;

	for (i = 0; i < maxWeapons; i++) {
		/* if no target, can't shoot */
		if (!weapons[i].target)
			continue;

		/* If the weapon is not ready in base, can't shoot. */
		if (weapons[i].slot.installationTime > 0)
			continue;

		/* if weapon is reloading, can't shoot */
		if (weapons[i].slot.delayNextShot > 0)
			continue;

		/* check that the ufo is still visible */
		if (!UFO_IsUFOSeenOnGeoscape(weapons[i].target)) {
			weapons[i].target = NULL;
			continue;
		}

		/* Check if we can still fire on this target. */
		distance = MAP_GetDistance(base->pos, weapons[i].target->pos);
		test = AIRFIGHT_CheckWeapon(&weapons[i].slot + i, distance);
		/* weapon unable to shoot, reset target */
		if (test == AIRFIGHT_WEAPON_CAN_NEVER_SHOOT) {
			weapons[i].target = NULL;
			continue;
		}
		/* we can't shoot with this weapon atm, wait to see if UFO comes closer */
		else if (test == AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT)
			continue;
		/* target is too far, wait to see if UFO comes closer */
		else if (distance > weapons[i].slot.ammo->craftitem.stats[AIR_STATS_WRANGE])
			continue;

		/* shoot */
		if (AIRFIGHT_AddProjectile(base, NULL, NULL, weapons[i].target, &weapons[i].slot + i)) {
			weapons[i].slot.delayNextShot = weapons[i].slot.ammo->craftitem.weaponDelay;
			/* will we miss the target ? */
			if (frand() > AIRFIGHT_ProbabilityToHit(NULL, weapons[i].target, &weapons[i].slot + i))
				AIRFIGHT_MissTarget(&ccs.projectiles[ccs.numProjectiles - 1], qfalse);
		}
	}
}

/**
 * @brief Check if one type of battery (missile or laser) can shoot now.
 * @param[in] installation Pointer to the firing intallation.
 * @param[in] weapons The installation weapon to check and fire.
 * @param[in] maxWeapons
 */
static void AIRFIGHT_InstallationShoot (const installation_t *installation, baseWeapon_t *weapons, int maxWeapons)
{
	int i, test;
	float distance;

	for (i = 0; i < maxWeapons; i++) {
		/* if no target, can't shoot */
		if (!weapons[i].target)
			continue;

		/* If the weapon is not ready in base, can't shoot. */
		if (weapons[i].slot.installationTime > 0)
			continue;

		/* if weapon is reloading, can't shoot */
		if (weapons[i].slot.delayNextShot > 0)
			continue;

		/* check that the ufo is still visible */
		if (!UFO_IsUFOSeenOnGeoscape(weapons[i].target)) {
			weapons[i].target = NULL;
			continue;
		}

		/* Check if we can still fire on this target. */
		distance = MAP_GetDistance(installation->pos, weapons[i].target->pos);
		test = AIRFIGHT_CheckWeapon(&weapons[i].slot + i, distance);
		/* weapon unable to shoot, reset target */
		if (test == AIRFIGHT_WEAPON_CAN_NEVER_SHOOT) {
			weapons[i].target = NULL;
			continue;
		}
		/* we can't shoot with this weapon atm, wait to see if UFO comes closer */
		else if (test == AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT)
			continue;
		/* target is too far, wait to see if UFO comes closer */
		else if (distance > weapons[i].slot.ammo->craftitem.stats[AIR_STATS_WRANGE])
			continue;

		/* shoot */
		if (AIRFIGHT_AddProjectile(NULL, installation, NULL, weapons[i].target, &weapons[i].slot + i)) {
			weapons[i].slot.delayNextShot = weapons[i].slot.ammo->craftitem.weaponDelay;
			/* will we miss the target ? */
			if (frand() > AIRFIGHT_ProbabilityToHit(NULL, weapons[i].target, &weapons[i].slot + i))
				AIRFIGHT_MissTarget(&ccs.projectiles[ccs.numProjectiles - 1], qfalse);
		}
	}
}

/**
 * @brief Run base defences.
 * @param[in] dt Time elapsed since last call of this function.
 */
void AIRFIGHT_CampaignRunBaseDefense (int dt)
{
	int baseIdx;
	int installationIdx;

	for (baseIdx = 0; baseIdx < MAX_BASES; baseIdx++) {
		int idx;
		base_t *base = B_GetFoundedBaseByIDX(baseIdx);
		if (!base)
			continue;

		if (base->baseStatus == BASE_UNDER_ATTACK)
			continue;
		for (idx = 0; idx < base->numBatteries; idx++) {
			if (base->batteries[idx].slot.delayNextShot > 0)
				base->batteries[idx].slot.delayNextShot -= dt;
		}

		for (idx = 0; idx < base->numLasers; idx++) {
			if (base->lasers[idx].slot.delayNextShot > 0)
				base->lasers[idx].slot.delayNextShot -= dt;
		}

		if (AII_BaseCanShoot(base)) {
			if (B_GetBuildingStatus(base, B_DEFENCE_MISSILE))
				AIRFIGHT_BaseShoot(base, base->batteries, base->numBatteries);
			if (B_GetBuildingStatus(base, B_DEFENCE_LASER))
				AIRFIGHT_BaseShoot(base, base->lasers, base->numLasers);
		}
	}

	for (installationIdx = 0; installationIdx < MAX_INSTALLATIONS; installationIdx++) {
		int idx;
		installation_t *installation = INS_GetFoundedInstallationByIDX(installationIdx);
		if (!installation)
			continue;

		assert(installation);

		if (installation->installationTemplate->maxBatteries <= 0)
			continue;

		for (idx = 0; idx < installation->installationTemplate->maxBatteries; idx++) {
			if (installation->batteries[idx].slot.delayNextShot > 0)
				installation->batteries[idx].slot.delayNextShot -= dt;
		}

		if (AII_InstallationCanShoot(installation)) {
			AIRFIGHT_InstallationShoot(installation, installation->batteries, installation->installationTemplate->maxBatteries);
		}
	}
}

/**
 * @sa MN_InitStartup
 */
void AIRFIGHT_InitStartup (void)
{
#ifdef DEBUG
	Cmd_AddCommand("debug_listprojectile", AIRFIGHT_ProjectileList_f, "Print Projectiles information to game console");
#endif
}
