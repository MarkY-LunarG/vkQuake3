#ifndef VK_SHADERS_H_
#define VK_SHADERS_H_

#include "VKimpl.h"



#include "tr_state_bits.h"

enum Vk_Shader_Type {
    ST_SINGLE_TEXTURE,
	ST_MULTI_TEXURE_MUL,
	ST_MULTI_TEXURE_ADD
};

void vk_loadShaderModules(void);
void vk_specifyShaderModule(const enum Vk_Shader_Type shader_type, const VkBool32 isClippingPlane, VkShaderModule* vs, VkShaderModule* fs);
void vk_destroyShaderModules(void);

#endif
