/**
 * @file
 * @brief Misc utility functions for game module.
 */

/*
All original material Copyright (C) 2002-2011 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/game/g_utils.c
Copyright (C) 1997-2001 Id Software, Inc.

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

#pragma once

#include "g_local.h"

void G_FreeEdict(edict_t *e);
edict_t *G_GetEdictFromPos(const pos3_t pos, const entity_type_t type);
edict_t *G_GetEdictFromPosExcluding(const pos3_t pos, const int n, ...);
bool G_UseEdict(edict_t *ent, edict_t* activator);
const char* G_GetWeaponNameForFiredef(const fireDef_t *fd);
player_t* G_GetPlayerForTeam(int team);
void G_TakeDamage(edict_t *ent, int damage);
bool G_TestLineWithEnts(const vec3_t start, const vec3_t end);
bool G_TestLine(const vec3_t start, const vec3_t end);
trace_t G_Trace(const vec3_t start, const vec3_t end, const edict_t * passent, int contentmask);
const char* G_GetPlayerName(int pnum);
void G_PrintStats(const char *format, ...) __attribute__((format(__printf__, 1, 2)));
void G_PrintActorStats(const edict_t *victim, const edict_t *attacker, const fireDef_t *fd);
edict_t *G_FindRadius(edict_t *from, const vec3_t org, float rad, entity_type_t type = ET_NULL);
void G_GenerateEntList(const char *entList[MAX_EDICTS]);
void G_RecalcRouting(const char *model, const GridBox& box);
void G_CompleteRecalcRouting(void);
int G_TouchTriggers(edict_t *ent);
int G_TouchSolids(edict_t *ent, float extend);
void G_TouchEdicts(edict_t *ent, float extend);
uint32_t G_GetLevelFlagsFromPos(const pos3_t pos);
