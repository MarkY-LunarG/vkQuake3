#ifndef VK_INIT_H_
#define VK_INIT_H_

#include "VKimpl.h"


void vk_shutdown(void);
VkBool32 isVKinitialied(void);
struct SDL_Window;
void vk_initialize(struct SDL_Window* window);


#endif
