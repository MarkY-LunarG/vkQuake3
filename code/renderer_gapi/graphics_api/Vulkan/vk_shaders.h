#ifndef VK_SHADERS_H_
#define VK_SHADERS_H_

#include "VKimpl.h"



#include "tr_state_bits.h"
#include "graphics_api/gapi.h"


void vk_loadShaderModules(void);
void vk_specifyShaderModule(const gapi_shaderType_t shader_type, const VkBool32 isClippingPlane, VkShaderModule* vs, VkShaderModule* fs);
void vk_destroyShaderModules(void);

#endif
