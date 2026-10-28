#ifndef VK_SHADERS_H_
#define VK_SHADERS_H_

#include "VKimpl.h"



#include "tr_state_bits.h"
#include "graphics_api/gapi.h"

// The SPIR-V of one shader stage.
typedef struct {
	const unsigned char* code;
	int size;
	const char* fileName;	// for the pipeline JSON
} vk_shaderCode_t;

// Vulkan SC has no shader modules: a pipeline comes from the pipeline cache.
// The SPIR-V is only for the pipeline JSON that the pipeline cache compiler
// reads. Get the SPIR-V of the vertex and the fragment stage of a pipeline.
void vk_getShaderCode(const gapi_shaderType_t shader_type, const VkBool32 isClippingPlane,
		vk_shaderCode_t* vs, vk_shaderCode_t* fs);

#endif
