/**
 * @file e_event_actordie.c
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

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

#include "../../../../client.h"
#include "../../../cl_actor.h"
#include "../../../cl_hud.h"
#include "../../../../renderer/r_mesh_anim.h"
#include "e_event_actordie.h"

/**
 * @brief Kills an actor (all that is needed is the local entity state set to STATE_DEAD).
 * @note Also changes the animation to a random death sequence and appends the dead animation
 * @param[in] msg The netchannel message
 * @param[in] self Pointer to the event structure that is currently executed
 */
void CL_ActorDie (const eventRegister_t *self, struct dbuffer *msg)
{
	le_t *le, *attackerLE;
	int entnum, attackerEntnum, state;

	NET_ReadFormat(msg, self->formatString, &entnum, &attackerEntnum, &state);

	/* get les */
	le = LE_Get(entnum);
	attackerLE = LE_Get(attackerEntnum);

	if (!le)
		LE_NotFoundError(entnum);

	if (!LE_IsActor(le))
		Com_Error(ERR_DROP, "CL_ActorDie: Can't kill, LE is not an actor (type: %i)", le->type);

	if (LE_IsDead(le))
		Com_Error(ERR_DROP, "CL_ActorDie: Can't kill, actor already dead");

	LE_Lock(le);
	/* count spotted aliens */
	if (le->team != cls.team && le->team != TEAM_CIVILIAN)
		cl.numAliensSpotted--;

	/* set relevant vars */
	FLOOR(le) = NULL;

	le->state = state;

	/* play animation */
	LE_SetThink(le, NULL);
	if (!le->invis)
		R_AnimChange(&le->as, le->model1, va("death%i", LE_GetAnimationIndexForDeath(le)));
	R_AnimAppend(&le->as, le->model1, va("dead%i", LE_GetAnimationIndexForDeath(le)));

	/* Print some info about the death or stun. */
	if (le->team == cls.team) {
		const character_t *chr = CL_ActorGetChr(le);
		if (chr) {
			char tmpbuf[128];
			if (LE_IsStunned(le)) {
				Com_sprintf(tmpbuf, lengthof(tmpbuf), _("%s was stunned\n"), chr->name);
			} else {
				Com_sprintf(tmpbuf, lengthof(tmpbuf), _("%s was killed\n"), chr->name);
			}
			HUD_DisplayMessage(tmpbuf);
		}
	} else {
		switch (le->team) {
		case (TEAM_CIVILIAN):
			if (LE_IsStunned(le))
				HUD_DisplayMessage(_("A civilian was stunned.\n"));
			else
				HUD_DisplayMessage(_("A civilian was killed.\n"));
			break;
		case (TEAM_ALIEN):
			if (LE_IsStunned(le))
				HUD_DisplayMessage(_("An alien was stunned.\n"));
			else
				HUD_DisplayMessage(_("An alien was killed.\n"));
			break;
		case (TEAM_PHALANX):
			if (LE_IsStunned(le))
				HUD_DisplayMessage(_("A soldier was stunned.\n"));
			else
				HUD_DisplayMessage(_("A soldier was killed.\n"));
			break;
		default:
			if (LE_IsStunned(le))
				HUD_DisplayMessage(va(_("A member of team %i was stunned.\n"), le->team));
			else
				HUD_DisplayMessage(va(_("A member of team %i was killed.\n"), le->team));
			break;
		}
	}

	CL_ActorPlaySound(le, SND_DEATH);

	VectorCopy(player_dead_maxs, le->maxs);
	CL_ActorRemoveFromTeamList(le);

	/* update pathing as we maybe can walk onto the dead actor now */
	CL_ActorConditionalMoveCalc(selActor);
	LE_Unlock(le);
}
