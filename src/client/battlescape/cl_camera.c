/**
 * @file cl_camera.c
 */

/*
All original material Copyright (C) 2002-2009 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/client/cl_input.c
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

#include "../client.h"
#include "cl_camera.h"
#include "cl_view.h"
#include "../input/cl_input.h"

static qboolean cameraRoute = qfalse;
static vec3_t routeFrom, routeDelta;
static float routeDist;

#define MIN_CAMROT_SPEED	50
#define MIN_CAMROT_ACCEL	50
#define MAX_CAMROT_SPEED	1000
#define MAX_CAMROT_ACCEL	1000
#define MIN_CAMMOVE_SPEED	150
#define MIN_CAMMOVE_ACCEL	150
#define MAX_CAMMOVE_SPEED	3000
#define MAX_CAMMOVE_ACCEL	3000
#define LEVEL_MIN			0.05
#define LEVEL_SPEED			3.0
#define ZOOM_SPEED			2.0

static cvar_t *cl_camrotspeed;
static cvar_t *cl_cammovespeed;
static cvar_t *cl_cammoveaccel;
static cvar_t *cl_campitchmin;
static cvar_t *cl_campitchmax;
cvar_t *cl_camzoommax;
cvar_t *cl_camzoomquant;
cvar_t *cl_camzoommin;

/**
 * @brief forces the camera to stay within the horizontal bounds of the
 * map plus some border
 */
static inline void CL_ClampCamToMap (const float border)
{
	if (cl.cam.origin[0] < mapMin[0] - border)
		cl.cam.origin[0] = mapMin[0] - border;
	else if (cl.cam.origin[0] > mapMax[0] + border)
		cl.cam.origin[0] = mapMax[0] + border;

	if (cl.cam.origin[1] < mapMin[1] - border)
		cl.cam.origin[1] = mapMin[1] - border;
	else if (cl.cam.origin[1] > mapMax[1] + border)
		cl.cam.origin[1] = mapMax[1] + border;
}

void CL_CameraMove (void)
{
	float frac;
	vec3_t g_forward, g_right, g_up;
	vec3_t delta;
	int i;

	/* get relevant variables */
	const float rotspeed =
		(cl_camrotspeed->value > MIN_CAMROT_SPEED) ? ((cl_camrotspeed->value < MAX_CAMROT_SPEED) ? cl_camrotspeed->value : MAX_CAMROT_SPEED) : MIN_CAMROT_SPEED;
	const float movespeed =
		(cl_cammovespeed->value > MIN_CAMMOVE_SPEED) ?
		((cl_cammovespeed->value < MAX_CAMMOVE_SPEED) ? cl_cammovespeed->value / cl.cam.zoom : MAX_CAMMOVE_SPEED / cl.cam.zoom) : MIN_CAMMOVE_SPEED / cl.cam.zoom;
	const float moveaccel =
		(cl_cammoveaccel->value > MIN_CAMMOVE_ACCEL) ?
		((cl_cammoveaccel->value < MAX_CAMMOVE_ACCEL) ? cl_cammoveaccel->value / cl.cam.zoom : MAX_CAMMOVE_ACCEL / cl.cam.zoom) : MIN_CAMMOVE_ACCEL / cl.cam.zoom;

	if (cls.state != ca_active)
		return;

	if (!viddef.viewWidth || !viddef.viewHeight)
		return;

	/* calculate camera omega */
	/* stop acceleration */
	frac = cls.frametime * moveaccel * 2.5;

	for (i = 0; i < 2; i++) {
		if (fabs(cl.cam.omega[i]) > frac) {
			if (cl.cam.omega[i] > 0)
				cl.cam.omega[i] -= frac;
			else
				cl.cam.omega[i] += frac;
		} else
			cl.cam.omega[i] = 0;

		/* rotational acceleration */
		if (i == YAW)
			cl.cam.omega[i] += CL_GetKeyMouseState(STATE_ROT) * frac * 2;
		else
			cl.cam.omega[i] += CL_GetKeyMouseState(STATE_TILT) * frac * 2;

		if (cl.cam.omega[i] > rotspeed)
			cl.cam.omega[i] = rotspeed;
		if (-cl.cam.omega[i] > rotspeed)
			cl.cam.omega[i] = -rotspeed;
	}

	cl.cam.omega[ROLL] = 0;
	/* calculate new camera angles for this frame */
	VectorMA(cl.cam.angles, cls.frametime, cl.cam.omega, cl.cam.angles);

	if (cl.cam.angles[PITCH] > cl_campitchmax->value)
		cl.cam.angles[PITCH] = cl_campitchmax->value;
	if (cl.cam.angles[PITCH] < cl_campitchmin->value)
		cl.cam.angles[PITCH] = cl_campitchmin->value;

	AngleVectors(cl.cam.angles, cl.cam.axis[0], cl.cam.axis[1], cl.cam.axis[2]);

	/* camera route overrides user input */
	if (cameraRoute) {
		/* camera route */
		frac = cls.frametime * moveaccel * 2;
		if (VectorDist(cl.cam.origin, routeFrom) > routeDist - 200) {
			VectorMA(cl.cam.speed, -frac, routeDelta, cl.cam.speed);
			VectorNormalize2(cl.cam.speed, delta);
			if (DotProduct(delta, routeDelta) < 0.05)
				cameraRoute = qfalse;
		} else
			VectorMA(cl.cam.speed, frac, routeDelta, cl.cam.speed);
	} else {
		/* normal camera movement */
		/* calculate ground-based movement vectors */
		const float angle = cl.cam.angles[YAW] * torad;
		const float sy = sin(angle);
		const float cy = cos(angle);

		VectorSet(g_forward, cy, sy, 0.0);
		VectorSet(g_right, sy, -cy, 0.0);
		VectorSet(g_up, 0.0, 0.0, 1.0);

		/* calculate camera speed */
		/* stop acceleration */
		frac = cls.frametime * moveaccel;
		if (VectorLength(cl.cam.speed) > frac) {
			VectorNormalize2(cl.cam.speed, delta);
			VectorMA(cl.cam.speed, -frac, delta, cl.cam.speed);
		} else
			VectorClear(cl.cam.speed);

		/* acceleration */
		frac = cls.frametime * moveaccel * 3.5;
		VectorClear(delta);
		VectorScale(g_forward, CL_GetKeyMouseState(STATE_FORWARD), delta);
		VectorMA(delta, CL_GetKeyMouseState(STATE_RIGHT), g_right, delta);
		VectorNormalize(delta);
		VectorMA(cl.cam.speed, frac, delta, cl.cam.speed);

		/* lerp the level change */
		if (cl.cam.lerplevel < cl_worldlevel->value) {
			cl.cam.lerplevel += LEVEL_SPEED * (cl_worldlevel->value - cl.cam.lerplevel + LEVEL_MIN) * cls.frametime;
			if (cl.cam.lerplevel > cl_worldlevel->value)
				cl.cam.lerplevel = cl_worldlevel->value;
		} else if (cl.cam.lerplevel > cl_worldlevel->value) {
			cl.cam.lerplevel -= LEVEL_SPEED * (cl.cam.lerplevel - cl_worldlevel->value + LEVEL_MIN) * cls.frametime;
			if (cl.cam.lerplevel < cl_worldlevel->value)
				cl.cam.lerplevel = cl_worldlevel->value;
		}
	}

	/* clamp speed */
	frac = VectorLength(cl.cam.speed) / movespeed;
	if (frac > 1.0)
		VectorScale(cl.cam.speed, 1.0 / frac, cl.cam.speed);

	/* zoom change */
	frac = CL_GetKeyMouseState(STATE_ZOOM);
	if (frac > 0.1) {
		cl.cam.zoom *= 1.0 + cls.frametime * ZOOM_SPEED * frac;
		/* ensure zoom isn't greater than either MAX_ZOOM or cl_camzoommax */
		cl.cam.zoom = min(min(MAX_ZOOM, cl_camzoommax->value), cl.cam.zoom);
	} else if (frac < -0.1) {
		cl.cam.zoom /= 1.0 - cls.frametime * ZOOM_SPEED * frac;
		/* ensure zoom isn't less than either MIN_ZOOM or cl_camzoommin */
		cl.cam.zoom = max(max(MIN_ZOOM, cl_camzoommin->value), cl.cam.zoom);
	}
	V_CalcFovX();

	/* calc new camera reference and new camera real origin */
	VectorMA(cl.cam.origin, cls.frametime, cl.cam.speed, cl.cam.origin);
	cl.cam.origin[2] = 0.;
	if (cl_isometric->integer) {
		CL_ClampCamToMap(72.);
		VectorMA(cl.cam.origin, -CAMERA_START_DIST + cl.cam.lerplevel * CAMERA_LEVEL_HEIGHT, cl.cam.axis[0], cl.cam.camorg);
		cl.cam.camorg[2] += CAMERA_START_HEIGHT + cl.cam.lerplevel * CAMERA_LEVEL_HEIGHT;
	} else {
		CL_ClampCamToMap(min(144. * (cl.cam.zoom - cl_camzoommin->value - 0.4) / cl_camzoommax->value, 86));
		VectorMA(cl.cam.origin, -CAMERA_START_DIST / cl.cam.zoom , cl.cam.axis[0], cl.cam.camorg);
		cl.cam.camorg[2] += CAMERA_START_HEIGHT / cl.cam.zoom + cl.cam.lerplevel * CAMERA_LEVEL_HEIGHT;
	}
}

/**
 * @brief Interpolates the camera movement from the given start point to the given end point
 * @sa CL_CameraMove
 * @sa V_CenterView
 * @param[in] from The grid position to start the camera movement from
 * @param[in] target The grid position to move the camera to
 */
void CL_CameraRoute (const pos3_t from, const pos3_t target)
{
	if (!cl_centerview->integer)
		return;

	/* initialize the camera route variables */
	PosToVec(from, routeFrom);
	PosToVec(target, routeDelta);
	VectorSubtract(routeDelta, routeFrom, routeDelta);
	routeDelta[2] = 0;
	routeDist = VectorLength(routeDelta);
	VectorNormalize(routeDelta);

	/* center the camera on the route starting position */
	VectorCopy(routeFrom, cl.cam.origin);
	/* set the world level to the z axis value of the camera target
	 * the camera lerp will do a smooth translate from the old level
	 * to the new one */
	Cvar_SetValue("cl_worldlevel", target[2]);

	VectorClear(cl.cam.speed);
	cameraRoute = qtrue;
}

void CL_CameraInit (void)
{
	cl_camrotspeed = Cvar_Get("cl_camrotspeed", "250", CVAR_ARCHIVE, NULL);
	cl_cammovespeed = Cvar_Get("cl_cammovespeed", "750", CVAR_ARCHIVE, NULL);
	cl_cammoveaccel = Cvar_Get("cl_cammoveaccel", "1250", CVAR_ARCHIVE, NULL);
	cl_campitchmax = Cvar_Get("cl_campitchmax", "89", 0, "Max camera pitch - over 90 presents apparent mouse inversion");
	cl_campitchmin = Cvar_Get("cl_campitchmin", "35", 0, "Min camera pitch - under 35 presents difficulty positioning cursor");
	cl_camzoomquant = Cvar_Get("cl_camzoomquant", "0.16", CVAR_ARCHIVE, NULL);
	cl_camzoommin = Cvar_Get("cl_camzoommin", "0.7", 0, "Minimum zoom value for tactical missions");
	cl_camzoommax = Cvar_Get("cl_camzoommax", "3.4", 0, "Maximum zoom value for tactical missions");
	cl_centerview = Cvar_Get("cl_centerview", "1", CVAR_ARCHIVE, "Center the view when selecting a new soldier");
}
