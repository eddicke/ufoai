/**
 * @file win_ref_glw.c
 * @brief This file contains ALL Win32 specific stuff having to do with the OpenGL refresh.
 * @note When a port is being made the following functions must be implemented by the port:
 ** Rimp_EndFrame
 ** Rimp_Init
 ** Rimp_Shutdown
 ** Rimp_SwitchFullscreen
 ** Rimp_SetGamma
 */

/*
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

#include <assert.h>
#include "../../renderer/r_local.h"
#include "win_ref_glw.h"
#include "win_local.h"

qboolean Rimp_InitGL(void);
void WG_RestoreGamma(void);

glwstate_t glw_state;

cvar_t *vid_xpos;			/* X coordinate of window position */
cvar_t *vid_ypos;			/* Y coordinate of window position */

/**
 * @brief
 */
static qboolean VerifyDriver (void)
{
	char buffer[1024];

	Q_strncpyz(buffer, (const char*)qglGetString(GL_RENDERER), sizeof(buffer));
	Q_strlwr(buffer);
	if (Q_strcmp(buffer, "gdi generic") == 0)
		return qfalse;
	return qtrue;
}

/* stencilbuffer shadows */
qboolean have_stencil = qfalse;

static unsigned short s_oldHardwareGamma[3][256];

/**
 * @brief Determines if the underlying hardware supports the Win32 gamma correction API.
 * @sa WG_RestoreGamma
 */
void WG_CheckHardwareGamma (void)
{
	HDC hDC;

	r_state.hwgamma = qfalse;

	hDC = GetDC(GetDesktopWindow());
	r_state.hwgamma = GetDeviceGammaRamp(hDC, s_oldHardwareGamma);
	ReleaseDC(GetDesktopWindow(), hDC);

	if (r_state.hwgamma) {
		/* do a sanity check on the gamma values */
		if ((HIBYTE(s_oldHardwareGamma[0][255]) <= HIBYTE(s_oldHardwareGamma[0][0])) ||
				(HIBYTE(s_oldHardwareGamma[1][255]) <= HIBYTE(s_oldHardwareGamma[1][0])) ||
				(HIBYTE(s_oldHardwareGamma[2][255]) <= HIBYTE(s_oldHardwareGamma[2][0]))) {
			r_state.hwgamma = qfalse;
			ri.Con_Printf(PRINT_ALL, "WARNING: device has broken gamma support, generated gamma.dat\n");
		}

		/* make sure that we didn't have a prior crash in the game, and if so we need to */
		/* restore the gamma values to at least a linear value */
		if ((HIBYTE(s_oldHardwareGamma[0][181]) == 255)) {
			int g;

			ri.Con_Printf(PRINT_ALL, "WARNING: suspicious gamma tables, using linear ramp for restoration\n");

			for (g = 0; g < 255; g++) {
				s_oldHardwareGamma[0][g] = g << 8;
				s_oldHardwareGamma[1][g] = g << 8;
				s_oldHardwareGamma[2][g] = g << 8;
			}
		}
	} else {
		ri.Con_Printf(PRINT_ALL, "...your card does not support win32 gamma correction api\n");
	}
}

/**
 * @brief
 */
qboolean VID_CreateWindow (int width, int height, qboolean fullscreen)
{
	WNDCLASS		wc;
	HWND			desktop;
	RECT			r, desktopRect;
	int				stylebits;
	int				x, y, w, h;
	int				exstyle;

	/* Register the frame class */
	wc.style         = 0;
	wc.lpfnWndProc   = glw_state.wndproc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = glw_state.hInstance;
	wc.hIcon         = 0;
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = (void *)COLOR_GRAYTEXT;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = GAME_TITLE;

	if (!RegisterClass(&wc)) {
		char    *msg;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msg, 0, NULL);
		ri.Sys_Error(ERR_FATAL, "Couldn't register window class.%s\r\n\r\nPlease make sure you have installed the latest drivers for your video card.", msg);
	}

	if (fullscreen) {
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP|WS_VISIBLE;
	} else {
		exstyle = 0;
		stylebits = WINDOW_STYLE;
	}

	r.left = 0;
	r.top = 0;
	r.right = width;
	r.bottom = height;

	AdjustWindowRect(&r, stylebits, FALSE);

	w = r.right - r.left;
	h = r.bottom - r.top;

	if (fullscreen)
		x = y = 0;
	else {
		vid_xpos = ri.Cvar_Get("vid_xpos", "0", 0, NULL);
		vid_ypos = ri.Cvar_Get("vid_ypos", "0", 0, NULL);
		x = vid_xpos->integer;
		y = vid_ypos->integer;
	}

	glw_state.hWnd = CreateWindowEx(
		exstyle,
		GAME_TITLE,
		GAME_TITLE_LONG,
		stylebits,
		x, y, w, h,
		NULL,
		NULL,
		glw_state.hInstance,
		NULL);

	if (!glw_state.hWnd)
		ri.Sys_Error(ERR_FATAL, "Couldn't create window");

	ShowWindow(glw_state.hWnd, SW_SHOW );
	UpdateWindow(glw_state.hWnd );

	/* init all the gl stuff for the window */
	if (!Rimp_InitGL()) {
		ri.Con_Printf(PRINT_ALL, "VID_CreateWindow() - Rimp_InitGL failed\n");
		return qfalse;
	}

	SetForegroundWindow(glw_state.hWnd);
	SetFocus(glw_state.hWnd);

	desktop = GetDesktopWindow();
	GetWindowRect(desktop, &desktopRect);

	ri.Con_Printf(PRINT_ALL, "Desktop resolution: %i:%i\n",
		(int)(desktopRect.right - desktopRect.left),
		(int)(desktopRect.bottom - desktopRect.top));

	/* let the sound and input subsystems know about the new window */
	ri.Vid_NewWindow(width, height);

	return qtrue;
}


/**
 * @brief
 */
rserr_t Rimp_SetMode (unsigned *pwidth, unsigned *pheight, int mode, qboolean fullscreen)
{
	int width, height;
	const char *win_fs[] = {"W", "FS"};

	ri.Con_Printf(PRINT_ALL, "Initializing OpenGL display\n");

	ri.Con_Printf(PRINT_ALL, "...setting mode %d:", mode);

	if (!ri.Vid_GetModeInfo(&width, &height, mode)) {
		ri.Con_Printf(PRINT_ALL, " invalid mode\n");
		return rserr_invalid_mode;
	}

	ri.Con_Printf(PRINT_ALL, " %d %d %s\n", width, height, win_fs[fullscreen]);

	/* destroy the existing window */
	if (glw_state.hWnd)
		Rimp_Shutdown();

	/* do a CDS if needed */
	if (fullscreen) {
		DEVMODE dm;

		ri.Con_Printf(PRINT_ALL, "...attempting fullscreen\n");

		memset(&dm, 0, sizeof(dm));

		dm.dmSize = sizeof(dm);
		dm.dmPelsWidth  = width;
		dm.dmPelsHeight = height;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (r_displayrefresh->integer) {
			r_state.displayrefresh = r_displayrefresh->integer;
			dm.dmDisplayFrequency = r_displayrefresh->integer;
			dm.dmFields |= DM_DISPLAYFREQUENCY;
			ri.Con_Printf(PRINT_ALL, "...display frequency is %d hz\n", r_state.displayrefresh);
		} else {
			HDC hdc = GetDC(NULL);
			int displayref = GetDeviceCaps(hdc, VREFRESH);
			dm.dmDisplayFrequency = displayref;
			dm.dmFields |= DM_DISPLAYFREQUENCY;
			ri.Con_Printf(PRINT_ALL, "...using desktop frequency: %d hz\n", displayref);
		}

		if (r_bitdepth->integer) {
			dm.dmBitsPerPel = r_bitdepth->integer;
			dm.dmFields |= DM_BITSPERPEL;
			ri.Con_Printf(PRINT_ALL, "...using r_bitdepth of %d\n", r_bitdepth->integer);
		} else {
			HDC hdc = GetDC(NULL);
			int bitspixel = GetDeviceCaps(hdc, BITSPIXEL);

			ri.Con_Printf(PRINT_ALL, "...using desktop display depth of %d\n", bitspixel);
			ReleaseDC(0, hdc);
		}

		ri.Con_Printf(PRINT_ALL, "...calling CDS: ");
		if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) {
			*pwidth = width;
			*pheight = height;

			r_state.fullscreen = qtrue;

			ri.Con_Printf(PRINT_ALL, "ok\n");

			if (!VID_CreateWindow(width, height, qtrue))
				return rserr_invalid_mode;

			return rserr_ok;
		} else {
			*pwidth = width;
			*pheight = height;

			ri.Con_Printf(PRINT_ALL, "failed\n");
			ri.Con_Printf(PRINT_ALL, "...calling CDS assuming dual monitors:");

			dm.dmPelsWidth = width * 2;
			dm.dmPelsHeight = height;
			dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

			if (r_bitdepth->integer != 0) {
				dm.dmBitsPerPel = r_bitdepth->integer;
				dm.dmFields |= DM_BITSPERPEL;
			}

			/*
			** our first CDS failed, so maybe we're running on some weird dual monitor
			** system
			*/
			if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
				ri.Con_Printf(PRINT_ALL, " failed\n");
				ri.Con_Printf(PRINT_ALL, "...setting windowed mode\n");

				ChangeDisplaySettings(0, 0);

				*pwidth = width;
				*pheight = height;
				r_state.fullscreen = qfalse;
				if (!VID_CreateWindow(width, height, qfalse))
					return rserr_invalid_mode;
				return rserr_invalid_fullscreen;
			} else {
				ri.Con_Printf(PRINT_ALL, " ok\n");
				if (!VID_CreateWindow(width, height, qtrue))
					return rserr_invalid_mode;

				r_state.fullscreen = qtrue;
				return rserr_ok;
			}
		}
	} else {
		ri.Con_Printf(PRINT_ALL, "...setting windowed mode\n");

		ChangeDisplaySettings(0, 0);

		*pwidth = width;
		*pheight = height;
		r_state.fullscreen = qfalse;
		if (!VID_CreateWindow(width, height, qfalse))
			return rserr_invalid_mode;
	}

	return rserr_ok;
}

/**
 * @brief This routine does all OS specific shutdown procedures for the OpenGL subsystem.
 * @note Under OpenGL this means NULLing out the current DC and
 * HGLRC, deleting the rendering context, and releasing the DC acquired
 * for the window.  The state structure is also nulled out.
 * @sa WG_RestoreGamma
 */
void Rimp_Shutdown (void)
{
	if (!qwglMakeCurrent)
		return;

	if (r_state.hwgamma)
		WG_RestoreGamma();

	if (qwglMakeCurrent && !qwglMakeCurrent(NULL, NULL))
		ri.Con_Printf(PRINT_ALL, "renderer::R_Shutdown() - wglMakeCurrent failed\n");
	if (glw_state.hGLRC) {
		if (qwglDeleteContext && !qwglDeleteContext( glw_state.hGLRC) )
			ri.Con_Printf(PRINT_ALL, "renderer::R_Shutdown() - wglDeleteContext failed\n");
		glw_state.hGLRC = NULL;
	}
	if (glw_state.hDC) {
		if (!ReleaseDC(glw_state.hWnd, glw_state.hDC))
			ri.Con_Printf(PRINT_ALL, "renderer::R_Shutdown() - ReleaseDC failed\n");
		glw_state.hDC = NULL;
	}
	if (glw_state.hWnd) {
		DestroyWindow(glw_state.hWnd);
		glw_state.hWnd = NULL;
	}

	if (glw_state.log_fp) {
		fclose(glw_state.log_fp);
		glw_state.log_fp = 0;
	}

	UnregisterClass(GAME_TITLE, glw_state.hInstance);

	if (r_state.fullscreen) {
		ChangeDisplaySettings(0, 0);
		r_state.fullscreen = qfalse;
	}
}


/**
 * @brief This routine is responsible for initializing the OS specific portions of OpenGL.
 * @note Under Win32 this means dealing with the pixelformats and doing the wgl interface stuff.
 */
qboolean Rimp_Init (void* hinstance, void* wndproc)
{
#define OSR2_BUILD_NUMBER 1111

	OSVERSIONINFO	vinfo;

	vinfo.dwOSVersionInfoSize = sizeof(vinfo);

	glw_state.allowdisplaydepthchange = qfalse;

	if (GetVersionEx(&vinfo)) {
		if (vinfo.dwMajorVersion > 4)
			glw_state.allowdisplaydepthchange = qtrue;
		else if (vinfo.dwMajorVersion == 4) {
			if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
				glw_state.allowdisplaydepthchange = qtrue;
			else if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
				if (LOWORD(vinfo.dwBuildNumber) >= OSR2_BUILD_NUMBER)
					glw_state.allowdisplaydepthchange = qtrue;
		}
	} else {
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - GetVersionEx failed\n");
		return qfalse;
	}

	glw_state.hInstance = hinstance;
	glw_state.wndproc = wndproc;

	WG_CheckHardwareGamma();

	return qtrue;
}

/**
 * @brief
 * @sa VID_CreateWindow
 */
qboolean Rimp_InitGL (void)
{
	PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),	/* size of this pfd */
		1,								/* version number */
		PFD_DRAW_TO_WINDOW |			/* support window */
		PFD_SUPPORT_OPENGL |			/* support OpenGL */
		PFD_DOUBLEBUFFER,				/* double buffered */
		PFD_TYPE_RGBA,					/* RGBA type */
		32,								/* 32-bit color depth */
		0, 0, 0, 0, 0, 0,				/* color bits ignored */
		0,								/* no alpha buffer */
		0,								/* shift bit ignored */
		0,								/* no accumulation buffer */
		0, 0, 0, 0, 					/* accum bits ignored */
		24,								/* 24-bit z-buffer */
#if 1
		8,								/* 8 bit stencil buffer */
#else
		0,								/* no stencil buffer */
#endif
		0,								/* no auxiliary buffer */
		PFD_MAIN_PLANE,					/* main layer */
		0,								/* reserved */
		0, 0, 0							/* layer masks ignored */
	};
	int pixelformat;

	/* figure out if we're running on a minidriver or not */
	if (strstr(r_driver->string, "opengl32") != 0)
		glw_state.minidriver = qfalse;
	else
		glw_state.minidriver = qtrue;

	/* Get a DC for the specified window */
	if (glw_state.hDC != NULL)
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - non-NULL DC exists\n");

	if ((glw_state.hDC = GetDC(glw_state.hWnd)) == NULL) {
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - GetDC failed\n");
		return qfalse;
	}

	if (glw_state.minidriver) {
		if ((pixelformat = qwglChoosePixelFormat(glw_state.hDC, &pfd)) == 0) {
			ri.Con_Printf(PRINT_ALL, "Rimp_Init() - qwglChoosePixelFormat failed\n");
			return qfalse;
		}
		if (qwglSetPixelFormat(glw_state.hDC, pixelformat, &pfd) == FALSE) {
			ri.Con_Printf(PRINT_ALL, "Rimp_Init() - qwglSetPixelFormat failed\n");
			return qfalse;
		}
		qwglDescribePixelFormat(glw_state.hDC, pixelformat, sizeof(pfd), &pfd);
	} else {
		if ((pixelformat = ChoosePixelFormat(glw_state.hDC, &pfd)) == 0) {
			ri.Con_Printf(PRINT_ALL, "Rimp_Init() - ChoosePixelFormat failed\n");
			return qfalse;
		}
		if (SetPixelFormat(glw_state.hDC, pixelformat, &pfd) == FALSE) {
			ri.Con_Printf(PRINT_ALL, "Rimp_Init() - SetPixelFormat failed\n");
			return qfalse;
		}
		DescribePixelFormat(glw_state.hDC, pixelformat, sizeof(pfd), &pfd);
	}

	/* startup the OpenGL subsystem by creating a context and making it current */
	if ((glw_state.hGLRC = qwglCreateContext(glw_state.hDC)) == 0) {
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - qwglCreateContext failed\n");
		goto fail;
	}

	if (!qwglMakeCurrent(glw_state.hDC, glw_state.hGLRC)) {
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - qwglMakeCurrent failed\n");
		goto fail;
	}

	if (!VerifyDriver()) {
		ri.Con_Printf(PRINT_ALL, "Rimp_Init() - no hardware acceleration detected\n");
		goto fail;
	}

	/* print out PFD specifics */
	ri.Con_Printf(PRINT_ALL, "GL PFD: color(%d-bits) Z(%d-bit)\n", (int) pfd.cColorBits, (int) pfd.cDepthBits);

	return qtrue;

fail:
	if (glw_state.hGLRC) {
		qwglDeleteContext(glw_state.hGLRC);
		glw_state.hGLRC = NULL;
	}

	if (glw_state.hDC) {
		ReleaseDC(glw_state.hWnd, glw_state.hDC);
		glw_state.hDC = NULL;
	}
	return qfalse;
}

/**
 * @brief
 */
void Rimp_BeginFrame (void)
{
	if (r_bitdepth->modified) {
		if (r_bitdepth->integer && !glw_state.allowdisplaydepthchange) {
			ri.Cvar_SetValue("r_bitdepth", 0);
			ri.Con_Printf(PRINT_ALL, "r_bitdepth requires Win95 OSR2.x or WinNT 4.x\n");
		}
		r_bitdepth->modified = qfalse;
	}

	qglDrawBuffer(GL_BACK);
}

/**
 * @brief Responsible for doing a swapbuffers and possibly for other stuff
 * as yet to be determined.  Probably better not to make this a Rimp
 * function and instead do a call to Rimp_SwapBuffers.
 */
void Rimp_EndFrame (void)
{
#ifdef DEBUG
	int		err;
	err = qglGetError();
#ifdef PARANOID
	if (err != GL_NO_ERROR)
		ri.Con_Printf(PRINT_ALL, "Rimp_EndFrame: glGetError: %i\n", err);
#else
	assert(err == GL_NO_ERROR);
#endif /*PARANOID*/
#endif /*DEBUG*/

	if (Q_stricmp(r_drawbuffer->string, "GL_BACK") == 0) {
		if (!qwglSwapBuffers(glw_state.hDC))
			ri.Sys_Error(ERR_FATAL, "Rimp_EndFrame() - SwapBuffers() failed!\n");
	}
}

/**
 * @brief
 */
void Rimp_AppActivate (qboolean active)
{
	if (active) {
		SetForegroundWindow(glw_state.hWnd);
		ShowWindow(glw_state.hWnd, SW_RESTORE);
	} else {
		if (vid_fullscreen->integer)
			ShowWindow(glw_state.hWnd, SW_MINIMIZE);
	}
}

/**
 * @brief This routine should only be called if r_state.hwgamma is TRUE
 */
void Rimp_SetGamma (void)
{
	int o, i, ret;
	WORD gamma_ramp[3][256];

	if (r_state.hwgamma) {
		float v, i_f;

		for (o = 0; o < 3; o++) {
			for(i = 0; i < 256; i++) {
				i_f = (float)i/255.0f;
				v = pow(i_f, vid_gamma->value);

				if (v < 0.0f)
					v = 0.0f;
				else if (v > 1.0f)
					v = 1.0f;

				gamma_ramp[o][i] = (WORD)(v * 65535.0f + 0.5f);
			}
		}

		ret = SetDeviceGammaRamp(glw_state.hDC, gamma_ramp);
		if (!ret)
			ri.Con_Printf(PRINT_ALL, "...SetDeviceGammaRamp failed.\n");
	}
}

/**
 * @brief Restore the old gamme value when shutting down the renderer
 * @sa Rimp_Shutdown
 * @sa WG_CheckHardwareGamma
 */
void WG_RestoreGamma (void)
{
	HDC hDC;
	if (r_state.hwgamma) {
		hDC = GetDC(GetDesktopWindow());
		SetDeviceGammaRamp(hDC, s_oldHardwareGamma);
		ReleaseDC(GetDesktopWindow(), hDC);
	}
}
