#ifndef SDL_WINDOW_H_
#define SDL_WINDOW_H_

#include "../qcommon/q_shared.h"

struct SDL_Window;

// Create the window with the SDL window flags that the graphics API needs,
// then start the input system for it.
struct SDL_Window* R_CreateWindow(uint32_t windowFlags);
void R_DestroyWindow(void);
void R_MinimizeWindow(void);

#endif
