/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/


// The SDL window of the renderer. The graphics API gives the window flags
// that it needs (gapi_sdlWindowFlags), and makes its surface from the window.

#include "sdl_window.h"
#include "graphics_api/gapi.h"
#include "ref_import.h"
#include "tr_cvar.h"
#include "icon_oa.h"
#include "glConfig.h"


#ifdef _WIN32
    #include "../SDL2/include/SDL.h"
#else

  #ifdef USE_LOCAL_HEADERS
    #include "../SDL2/include/SDL.h"
  #else
    #include <SDL2/SDL.h>
  #endif

#endif


static SDL_Window* window_sdl = NULL;


// TODO: multi display support
static cvar_t* r_displayIndex;


static void R_DetectAvailableModes(void)
{
	int i, j;
	char buf[ MAX_STRING_CHARS ] = { 0 };

	SDL_DisplayMode windowMode;
    
	// If a window exists, note its display index
	if( window_sdl != NULL )
	{
		r_displayIndex->integer = SDL_GetWindowDisplayIndex( window_sdl );
		if( r_displayIndex->integer < 0 )
		{
			ri.Printf(PRINT_ALL, "SDL_GetWindowDisplayIndex() failed: %s\n", SDL_GetError() );
            return;
		}
	}

	int numSDLModes = SDL_GetNumDisplayModes( r_displayIndex->integer );

	if( SDL_GetWindowDisplayMode( window_sdl, &windowMode ) < 0 || numSDLModes <= 0 )
	{
		ri.Printf(PRINT_ALL, "Couldn't get window display mode, no resolutions detected: %s\n", SDL_GetError() );
		return;
	}

	int numModes = 0;
	SDL_Rect* modes = SDL_calloc(numSDLModes, sizeof( SDL_Rect ));
	if ( !modes )
	{
        ////////////////////////////////////
		ri.Error(ERR_FATAL, "Out of memory" );
        ////////////////////////////////////
	}

	for( i = 0; i < numSDLModes; i++ )
	{
		SDL_DisplayMode mode;

		if( SDL_GetDisplayMode( r_displayIndex->integer, i, &mode ) < 0 )
			continue;

		if( !mode.w || !mode.h )
		{
			ri.Printf(PRINT_ALL,  " Display supports any resolution\n" );
			SDL_free( modes );
			return;
		}

		if( windowMode.format != mode.format )
			continue;

		// SDL can give the same resolution with different refresh rates.
		// Only list resolution once.
		for( j = 0; j < numModes; j++ )
		{
			if( (mode.w == modes[ j ].w) && (mode.h == modes[ j ].h) )
				break;
		}

		if( j != numModes )
			continue;

		modes[ numModes ].w = mode.w;
		modes[ numModes ].h = mode.h;
		numModes++;
	}

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			ri.Printf(PRINT_ALL,  " Skipping mode %ux%u, buffer too small\n", modes[ i ].w, modes[ i ].h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		ri.Printf(PRINT_ALL, " Available modes: '%s'\n", buf );
		ri.Cvar_Set( "r_availableModes", buf );
	}
	SDL_free( modes );
}


static int R_SetMode(int mode, qboolean fullscreen, uint32_t windowFlags)
{
	SDL_DisplayMode desktopMode;

	Uint32 flags = SDL_WINDOW_SHOWN | windowFlags;

	if ( r_allowResize->integer )
		flags |= SDL_WINDOW_RESIZABLE;


	ri.Printf(PRINT_ALL,  "\n...R_SetMode()...\n");

    SDL_GetNumVideoDisplays();

	int display_mode_count = SDL_GetNumDisplayModes(r_displayIndex->integer);
	if (display_mode_count < 1)
	{
		ri.Printf(PRINT_ALL, " SDL_GetNumDisplayModes failed: %s", SDL_GetError());
	}


    int tmp = SDL_GetDesktopDisplayMode(r_displayIndex->integer, &desktopMode);
	if( (tmp == 0) && (desktopMode.h > 0) )
    {
    	Uint32 f = desktopMode.format;
        ri.Printf(PRINT_ALL, " bpp %i\t%s\t%i x %i, refresh_rate: %dHz\n", SDL_BITSPERPIXEL(f), SDL_GetPixelFormatName(f), desktopMode.w, desktopMode.h, desktopMode.refresh_rate);
    }
    else if (SDL_GetDisplayMode(r_displayIndex->integer, 0, &desktopMode) != 0)
	{
    	//mode = 0: use the first display mode SDL return;
        ri.Printf(PRINT_ALL," SDL_GetDisplayMode failed: %s\n", SDL_GetError());
        mode = 3;
        desktopMode.w = 640;
        desktopMode.h = 480;
        desktopMode.refresh_rate = 60;
        fullscreen = 0;
	}

    if(fullscreen)
    {
        // prevent crush the OS
        r_mode->integer = mode = -2;
        		
        flags |= SDL_WINDOW_FULLSCREEN;
		flags |= SDL_WINDOW_BORDERLESS;
    }

    R_SetWinMode( mode, desktopMode.w, desktopMode.h, desktopMode.refresh_rate );
    


    if( window_sdl != NULL )
	{
		// SDL_GetWindowPosition( window_sdl, &x, &y );
		SDL_DestroyWindow( window_sdl );
		window_sdl = NULL;
        ri.Printf(PRINT_ALL, "Existing window being destroyed\n");
	}

    int width = 640;
    int height = 480;

    R_GetWinResolution(&width, &height);

	window_sdl = SDL_CreateWindow( 
					CLIENT_WINDOW_TITLE, 
					SDL_WINDOWPOS_CENTERED,
					SDL_WINDOWPOS_CENTERED, 
					width, 
					height, 
					flags );


	if( window_sdl )
    {
        R_DetectAvailableModes();
        return 0;
    }

	ri.Printf(PRINT_WARNING, " Couldn't create a window: %s\n", SDL_GetError() );
    return -1;
}



#ifndef SDL_HINT_VIDEODRIVER
#define SDL_HINT_VIDEODRIVER "SDL_VIDEODRIVER"
#endif

#define MAX_VIDEO_DRIVERS 4

/*
 * Make the list of SDL video drivers to try. A NULL entry is the default
 * driver of SDL. Return the number of drivers.
 */
static int R_GetVideoDrivers(const char* drivers[MAX_VIDEO_DRIVERS], qboolean* pSetHint)
{
	const cvar_t* r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ARCHIVE | CVAR_LATCH );
	const char* env = SDL_getenv( "SDL_VIDEODRIVER" );

	*pSetHint = qfalse;

	// The user selected a driver: use only that driver.
	if ( env && env[0] )
	{
		drivers[0] = env;
		return 1;
	}
	if ( r_sdlDriver->string[0] )
	{
		drivers[0] = r_sdlDriver->string;
		*pSetHint = qtrue;
		return 1;
	}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	// Wayland first. If the graphics API cannot start there, use X11.
	drivers[0] = "wayland";
	drivers[1] = "x11";
	*pSetHint = qtrue;
	return 2;
#else
	drivers[0] = NULL;
	return 1;
#endif
}


/*
 * Start the SDL video subsystem with a driver, and create the window.
 * Return qfalse if the driver or the window is not available.
 */
static qboolean R_OpenWindow(const char* driver, qboolean setHint, uint32_t windowFlags)
{
	if ( setHint && driver )
	{
		SDL_SetHint( SDL_HINT_VIDEODRIVER, driver );
	}

	if ( SDL_InitSubSystem( SDL_INIT_VIDEO ) != 0 )
	{
		ri.Printf(PRINT_ALL, " SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
		return qfalse;
	}
	ri.Printf(PRINT_ALL, " SDL using driver \"%s\"\n", SDL_GetCurrentVideoDriver( ));

	if( 0 == R_SetMode(r_mode->integer, r_fullscreen->integer, windowFlags) )
	{
		return qtrue;
	}

	ri.Printf(PRINT_ALL, " Setting r_mode=%d, r_fullscreen=%d failed, falling back on r_mode=%d\n",
			r_mode->integer, r_fullscreen->integer, 3 );

	if( 0 == R_SetMode(3, qfalse, windowFlags) )
	{
		return qtrue;
	}

	ri.Printf(PRINT_WARNING, " Couldn't create a window: %s\n", SDL_GetError());
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	return qfalse;
}


/*
 * This routine is responsible for initializing the OS specific portions of the renderer
 */
void R_InitWindowAndGraphicsAPI(void)
{
	ri.Printf(PRINT_ALL, "\n...Creating window (using SDL2)...\n");

	// Print SDL2 Version ....
	SDL_version v;
	SDL_version *sdl_version = &v;
	SDL_GetVersion(&v);
	ri.Printf(PRINT_ALL, " Found SDL version %i.%i.%i\n",
		sdl_version->major,sdl_version->minor,sdl_version->patch);

	r_displayIndex = ri.Cvar_Get( "r_displayIndex", "0", CVAR_ARCHIVE | CVAR_LATCH );

	SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
			(void *)CLIENT_WINDOW_ICON.pixel_data,
			CLIENT_WINDOW_ICON.width,
			CLIENT_WINDOW_ICON.height,
			CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
			CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
			0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
			);

	if (icon == NULL)
	{
		ri.Printf(PRINT_ALL, " SDL_CreateRGBSurface Failed. \n" );
	}

	if (ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		ri.Cvar_Set( "r_fullscreen", "0" );
        ri.Cvar_Set( "r_mode", "3" );
		ri.Cvar_Set( "com_abnormalExit", "0" );
	}

	const char* drivers[MAX_VIDEO_DRIVERS];
	qboolean setHint;
	int numDrivers = R_GetVideoDrivers(drivers, &setHint);

	// If the client started the video subsystem, its driver cannot change.
	if (SDL_WasInit(SDL_INIT_VIDEO))
	{
		ri.Printf(PRINT_ALL, " Video is already initialized.\n");
		drivers[0] = SDL_GetCurrentVideoDriver();
		numDrivers = 1;
		setHint = qfalse;
	}

	char failed[MAX_STRING_CHARS] = "";
	int i;
	for (i = 0; i < numDrivers; i++)
	{
		const char* name = drivers[i] ? drivers[i] : "default";

		if ( !R_OpenWindow(drivers[i], setHint, gapi_sdlWindowFlags()) )
		{
			Q_strcat(failed, sizeof(failed), va(" %s (no window)", name));
			continue;
		}

		if ( gapi_init(window_sdl) )
		{
			break;
		}

		// The graphics API released all of its objects for this window.
		Q_strcat(failed, sizeof(failed), va(" %s (graphics API)", name));
		SDL_DestroyWindow( window_sdl );
		window_sdl = NULL;
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}

	if (i == numDrivers)
	{
		ri.Error(ERR_FATAL, "R_InitWindowAndGraphicsAPI() - no SDL video driver works:%s", failed);
	}

	ri.Printf(PRINT_ALL, " Video driver in use: %s\n", SDL_GetCurrentVideoDriver());
	ri.Cvar_Get( "r_sdlDriverInUse", "", CVAR_ROM );
	ri.Cvar_Set( "r_sdlDriverInUse", SDL_GetCurrentVideoDriver() );

	SDL_SetWindowIcon( window_sdl, icon );

	SDL_FreeSurface( icon );

	// This depends on SDL_INIT_VIDEO, hence having it here
	ri.IN_Init(window_sdl);
}


void R_DestroyWindow( void )
{
	ri.Printf(PRINT_ALL, " Destroy Window Subsystem.\n");

	ri.IN_Shutdown();
	SDL_QuitSubSystem( SDL_INIT_VIDEO );

    SDL_DestroyWindow( window_sdl );
    window_sdl = NULL;
}


/*
===============
Minimize the game so that user is back at the desktop
===============
*/
void R_MinimizeWindow( void )
{
    qboolean toggleWorked = qtrue;
    ri.Printf( PRINT_ALL, " Minimizing Window (SDL). \n");

	qboolean isWinFullscreen = ( SDL_GetWindowFlags( window_sdl ) & SDL_WINDOW_FULLSCREEN ) ? qtrue : qfalse;
    

    if( isWinFullscreen )
	{
		toggleWorked = (SDL_SetWindowFullscreen( window_sdl, 0 ) >= 0);
	}

    // SDL_WM_ToggleFullScreen didn't work, so do it the slow way
    if( toggleWorked )
    {
        // ri.IN_Shutdown( );
        SDL_MinimizeWindow( window_sdl );
        // SDL_HideWindow( window_sdl );
    }
    else
    {
        ri.Printf( PRINT_ALL, " SDL_SetWindowFullscreen didn't work, so do it the slow way \n");

        ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
    }
}

/*
	if( r_fullscreen->modified )
	{
		qboolean    needToToggle;
		qboolean    sdlToggled = qfalse;

		// Find out the current state
		int fullscreen = !!( SDL_GetWindowFlags( window_sdl ) & SDL_WINDOW_FULLSCREEN );

		if( r_fullscreen->integer && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
			ri.Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if( needToToggle )
		{
			sdlToggled = SDL_SetWindowFullscreen( window_sdl, r_fullscreen->integer ) >= 0;

			// SDL_WM_ToggleFullScreen didn't work, so do it the slow way
			if( !sdlToggled )
				ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");

			ri.IN_Restart( );
		}

		r_fullscreen->modified = qfalse;
	}
*/
