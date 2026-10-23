#ifndef SDL_WINDOW_H_
#define SDL_WINDOW_H_

#include "../qcommon/q_shared.h"

struct SDL_Window;

// Start the SDL video subsystem, create the window, and start the graphics
// API for it (gapi_init). On Linux, try the Wayland driver first, then X11.
// The SDL_VIDEODRIVER environment variable or the r_sdlDriver cvar selects
// one driver. Then start the input system for the window.
void R_InitWindowAndGraphicsAPI(void);
void R_DestroyWindow(void);
void R_MinimizeWindow(void);

#endif
