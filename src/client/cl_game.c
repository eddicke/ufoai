/**
 * @file cl_game.c
 * @brief Shared game type code
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

#include "client.h"
#include "cl_game.h"
#include "cl_team.h"
#include "cl_menu.h"
#include "menu/m_nodes.h"	/**< menuInventory */
#include "cl_le.h"

static invList_t invList[MAX_INVLIST];

typedef struct gameTypeList_s {
	const char *name;
	const char *menu;
	int gametype;
	void (*init)(void);
	void (*shutdown)(void);
	/** soldier spawn functions may differ between the different gametypes */
	qboolean (*spawn)(void);
	/** each gametype can handle the current team in a different way */
	int (*getteam)(void);
	/** some gametypes only support special maps */
	const mapDef_t* (*mapinfo)(int step);
	/** some gametypes require extra data in the results parsing (like e.g. campaign mode) */
	void (*results)(struct dbuffer *msg, int, int*, int*, int[][MAX_TEAMS], int[][MAX_TEAMS]);
	/** check whether the given item is usable in the current game mode */
	qboolean (*itemIsUseable)(const objDef_t *od);
	/** returns the equipment definition the game mode is using */
	equipDef_t * (*getequipdef)(void);
	/** update character display values for game type dependent stuff */
	void (*charactercvars)(const character_t *chr);
	/** checks whether the given team is known in the particular gamemode */
	qboolean (*teamisknown)(const teamDef_t *teamDef);
	/** called on errors */
	void (*drop)(void);
} gameTypeList_t;

static const gameTypeList_t gameTypeList[] = {
	{"Multiplayer mode", "multiplayer", GAME_MULTIPLAYER, GAME_MP_InitStartup, GAME_MP_Shutdown, GAME_MP_Spawn, GAME_MP_GetTeam, GAME_MP_MapInfo, GAME_MP_Results, NULL, GAME_MP_GetEquipmentDefinition, NULL, NULL, NULL},
	{"Campaign mode", "campaigns", GAME_CAMPAIGN, GAME_CP_InitStartup, GAME_CP_Shutdown, GAME_CP_Spawn, GAME_CP_GetTeam, GAME_CP_MapInfo, GAME_CP_Results, GAME_CP_ItemIsUseable, GAME_CP_GetEquipmentDefinition, GAME_CP_CharacterCvars, GAME_CP_TeamIsKnown, GAME_CP_Drop},
	{"Skirmish mode", "skirmish", GAME_SKIRMISH, GAME_SK_InitStartup, GAME_SK_Shutdown, GAME_SK_Spawn, GAME_SK_GetTeam, GAME_SK_MapInfo, GAME_SK_Results, NULL, NULL, NULL, NULL, NULL},

	{NULL, NULL, 0, NULL, NULL}
};

static const gameTypeList_t *GAME_GetCurrentType (void)
{
	const gameTypeList_t *list = gameTypeList;

	while (list->name) {
		if (list->gametype == cls.gametype)
			return list;
		list++;
	}

	return NULL;
}

void GAME_RestartMode (int gametype)
{
	GAME_SetMode(GAME_NONE);
	GAME_SetMode(gametype);
}

void GAME_SetMode (int gametype)
{
	const gameTypeList_t *list = gameTypeList;

	if (gametype < 0 || gametype > GAME_MAX) {
		Com_Printf("Invalid gametype %i given\n", gametype);
		return;
	}

	if (cls.gametype == gametype)
		return;

	list = GAME_GetCurrentType();
	if (list) {
		Com_Printf("Shutdown gametype '%s'\n", list->name);
		list->shutdown();

		/* option menu are the same everywhere when shutting down a game type */
		MN_InitStack("main", "", qtrue, qtrue);
	}

	cls.gametype = gametype;

	list = GAME_GetCurrentType();
	if (list) {
		Com_Printf("Change gametype to '%s'\n", list->name);
		memset(&invList, 0, sizeof(invList));
		INVSH_InitInventory(invList, qfalse); /* inventory structure switched/initialized */
		list->init();
	}

	CL_Disconnect();
}

/**
 * @brief Prints the map info for the server creation dialogue
 * @todo Skip special map that start with a '.' (e.g. .baseattack)
 */
static void MN_MapInfo (int step)
{
	const char *mapname;
	const mapDef_t *md;
	const gameTypeList_t *list = gameTypeList;

	if (!csi.numMDs)
		return;

	cls.currentSelectedMap += step;

	if (cls.currentSelectedMap < 0)
		cls.currentSelectedMap = csi.numMDs - 1;

	cls.currentSelectedMap %= csi.numMDs;

	md = NULL;
	while (list->name) {
		if (list->gametype == cls.gametype) {
			md = list->mapinfo(step);
			break;
		}
		list++;
	}

	if (!md)
		return;

	mapname = md->map;
	/* skip random map char */
	if (mapname[0] == '+')
		mapname++;

	Cvar_Set("mn_svmapname", md->map);
	if (FS_CheckFile(va("pics/maps/shots/%s.jpg", mapname)) != -1)
		Cvar_Set("mn_mappic", va("maps/shots/%s", mapname));
	else
		Cvar_Set("mn_mappic", va("maps/shots/default"));

	if (FS_CheckFile(va("pics/maps/shots/%s_2.jpg", mapname)) != -1)
		Cvar_Set("mn_mappic2", va("maps/shots/%s_2", mapname));
	else
		Cvar_Set("mn_mappic2", va("maps/shots/default"));

	if (FS_CheckFile(va("pics/maps/shots/%s_3.jpg", mapname)) != -1)
		Cvar_Set("mn_mappic3", va("maps/shots/%s_3", mapname));
	else
		Cvar_Set("mn_mappic3", va("maps/shots/default"));
}

static void MN_GetMaps_f (void)
{
	MN_MapInfo(0);
}

static void MN_ChangeMap_f (void)
{
	if (!strcmp(Cmd_Argv(0), "mn_nextmap"))
		MN_MapInfo(1);
	else
		MN_MapInfo(-1);
}

static void MN_SelectMap_f (void)
{
	const char *mapname;
	int i;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <mapname>\n", Cmd_Argv(0));
		return;
	}

	if (!csi.numMDs)
		return;

	mapname = Cmd_Argv(1);

	for (i = 0; i < csi.numMDs; i++) {
		const mapDef_t *md = &csi.mds[i];
		if (strcmp(md->map, mapname))
			continue;
		cls.currentSelectedMap = i;
		MN_MapInfo(0);
		return;
	}

	for (i = 0; i < csi.numMDs; i++) {
		const mapDef_t *md = &csi.mds[i];
		if (strcmp(md->id, mapname))
			continue;
		cls.currentSelectedMap = i;
		MN_MapInfo(0);
		return;
	}

	Com_Printf("Could not find map %s\n", mapname);
}

/**
 * @brief Decides with game mode should be set - takes the menu as reference
 */
static void GAME_SetMode_f (void)
{
	const char *modeName;
	const gameTypeList_t *list = gameTypeList;

	if (Cmd_Argc() == 2)
		modeName = Cmd_Argv(1);
	else
		modeName = MN_GetActiveMenuName();

	if (modeName[0] == '\0')
		return;

	while (list->name) {
		if (!strcmp(list->menu, modeName)) {
			GAME_SetMode(list->gametype);
			return;
		}
		list++;
	}
	Com_Printf("GAME_SetMode_f: Mode '%s' not found\n", modeName);
}

qboolean GAME_ItemIsUseable (const objDef_t *od)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	if (list && list->itemIsUseable)
		return list->itemIsUseable(od);

	return qtrue;
}

/**
 * @brief After a mission was finished this function is called
 */
void GAME_HandleResults (struct dbuffer *msg, int winner, int *numSpawned, int *numAlive, int numKilled[][MAX_TEAMS], int numStunned[][MAX_TEAMS])
{
	const gameTypeList_t *list = GAME_GetCurrentType();
	if (list) {
		list->results(msg, winner, numSpawned, numAlive, numKilled, numStunned);
		INVSH_InvUnusedRevert(); /* inventory buffer switched back */
	}
}

/**
 * @brief Stores a team-list (chr-list) info to buffer (which might be a network buffer, too).
 * @sa G_ClientTeamInfo
 * @sa MP_SaveTeamMultiplayerInfo
 * @note Called in CL_Precache_f to send the team info to server
 */
static void GAME_SendCurrentTeamSpawningInfo (struct dbuffer * buf, chrList_t *team)
{
	int i, j;

	/* header */
	NET_WriteByte(buf, clc_teaminfo);
	NET_WriteByte(buf, team->num);

	Com_DPrintf(DEBUG_CLIENT, "GAME_SendCurrentTeamSpawningInfo: Upload information about %i soldiers to server\n", team->num);
	for (i = 0; i < team->num; i++) {
		character_t *chr = team->chr[i];
		assert(chr);
		assert(chr->fieldSize > 0);
		/* send the fieldSize ACTOR_SIZE_* */
		NET_WriteByte(buf, chr->fieldSize);

		/* unique character number */
		NET_WriteShort(buf, chr->ucn);

		/* name */
		NET_WriteString(buf, chr->name);

		/* model */
		NET_WriteString(buf, chr->path);
		NET_WriteString(buf, chr->body);
		NET_WriteString(buf, chr->head);
		NET_WriteByte(buf, chr->skin);

		NET_WriteShort(buf, chr->HP);
		NET_WriteShort(buf, chr->maxHP);
		if (chr->teamDef == NULL)
			Com_Error(ERR_DROP, "Character with no teamdef set");
		NET_WriteByte(buf, chr->teamDef->idx);
		NET_WriteByte(buf, chr->gender);
		NET_WriteByte(buf, chr->STUN);
		NET_WriteByte(buf, chr->morale);

		/** Scores @sa inv_shared.h:chrScoreGlobal_t */
		for (j = 0; j < SKILL_NUM_TYPES + 1; j++)
			NET_WriteLong(buf, chr->score.experience[j]);
		for (j = 0; j < SKILL_NUM_TYPES; j++)
			NET_WriteByte(buf, chr->score.skills[j]);
		for (j = 0; j < SKILL_NUM_TYPES + 1; j++)
			NET_WriteByte(buf, chr->score.initialSkills[j]);
		for (j = 0; j < KILLED_NUM_TYPES; j++)
			NET_WriteShort(buf, chr->score.kills[j]);
		for (j = 0; j < KILLED_NUM_TYPES; j++)
			NET_WriteShort(buf, chr->score.stuns[j]);
		NET_WriteShort(buf, chr->score.assignedMissions);

		/* Send user-defined (default) reaction-state. */
		NET_WriteShort(buf, chr->reservedTus.reserveReaction);

		/* inventory */
		CL_NetSendInventory(buf, &chr->inv);
	}
}

/**
 * @brief Called during startup of mission to send team info
 */
void GAME_SpawnSoldiers (void)
{
	const gameTypeList_t *list = GAME_GetCurrentType();
	if (list) {
		/* this callback is responsible to set up the cl.chrList */
		const qboolean spawnStatus = list->spawn();
		if (spawnStatus && cl.chrList.num > 0) {
			struct dbuffer *msg;

			/* send team info */
			msg = new_dbuffer();
			GAME_SendCurrentTeamSpawningInfo(msg, &cl.chrList);
			NET_WriteMsg(cls.netStream, msg);

			msg = new_dbuffer();
			NET_WriteByte(msg, clc_stringcmd);
			NET_WriteString(msg, va("spawn %i\n", cl.servercount));
			NET_WriteMsg(cls.netStream, msg);
		}
	}
}

int GAME_GetCurrentTeam (void)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	if (list)
		return list->getteam();

	Com_Error(ERR_FATAL, "GAME_GetCurrentTeam: Could not determine gamemode");
}

equipDef_t *GAME_GetEquipmentDefinition (void)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	if (list) {
		if (list->getequipdef == NULL)
			return NULL;
		return list->getequipdef();
	}

	Com_Error(ERR_FATAL, "GAME_GetEquipmentDefinition: Could not determine gamemode");
}

qboolean GAME_TeamIsKnown (const teamDef_t *teamDef)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	if (!teamDef)
		return qfalse;

	if (list) {
		if (list->teamisknown)
			return list->teamisknown(teamDef);
		return qtrue;
	}

	Com_Error(ERR_FATAL, "GAME_TeamIsKnown: Could not determine gamemode");
}

void GAME_CharacterCvars (const character_t *chr)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	if (list) {
		if (list->charactercvars)
			list->charactercvars(chr);
		return;
	}

	Com_Error(ERR_FATAL, "GAME_CharacterCvars: Could not determine gamemode");
}

/**
 * @brief Let the aliens win the match
 */
static void GAME_Abort_f (void)
{
	/* aborting means letting the aliens win */
	Cbuf_AddText(va("sv win %i\n", TEAM_ALIEN));
}

void GAME_Drop (void)
{
	const gameTypeList_t *list = GAME_GetCurrentType();

	LE_Cleanup();

	if (list) {
		if (!list->drop) {
			GAME_SetMode(GAME_NONE);
			MN_InitStack("main", NULL, qfalse, qtrue);
		} else {
			list->drop();
		}
		return;
	}

	Com_Error(ERR_FATAL, "GAME_Drop: Could not determine gamemode");
}

/**
 * @brief Quits the current running game by calling the @c shutdown callback
 */
static void GAME_Exit_f (void)
{
	GAME_SetMode(GAME_NONE);
}

void GAME_InitStartup (void)
{
	Cmd_AddCommand("game_setmode", GAME_SetMode_f, "Decides with game mode should be set - takes the menu as reference");
	Cmd_AddCommand("game_exit", GAME_Exit_f, "Abort the game and let the aliens/opponents win");
	Cmd_AddCommand("game_abort", GAME_Abort_f, "Abort the game and let the aliens/opponents win");
	Cmd_AddCommand("mn_getmaps", MN_GetMaps_f, "The initial map to show");
	Cmd_AddCommand("mn_nextmap", MN_ChangeMap_f, "Switch to the next valid map for the selected gametype");
	Cmd_AddCommand("mn_prevmap", MN_ChangeMap_f, "Switch to the previous valid map for the selected gametype");
	Cmd_AddCommand("mn_selectmap", MN_SelectMap_f, "Switch to the map given by the parameter - may be invalid for the current gametype");
}
