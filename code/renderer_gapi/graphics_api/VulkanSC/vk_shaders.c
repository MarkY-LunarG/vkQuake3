#include "vk_instance.h"
#include "vk_shaders.h"

// The SPIR-V of the shaders, as C arrays (shaders/*.c).
extern unsigned char single_texture_vert_spv[];
extern int single_texture_vert_spv_size;
extern unsigned char single_texture_clipping_plane_vert_spv[];
extern int single_texture_clipping_plane_vert_spv_size;
extern unsigned char single_texture_frag_spv[];
extern int single_texture_frag_spv_size;
extern unsigned char multi_texture_vert_spv[];
extern int multi_texture_vert_spv_size;
extern unsigned char multi_texture_clipping_plane_vert_spv[];
extern int multi_texture_clipping_plane_vert_spv_size;
extern unsigned char multi_texture_frag_spv[];
extern int multi_texture_frag_spv_size;


void vk_getShaderCode(const gapi_shaderType_t shader_type, const VkBool32 isClippingPlane,
		vk_shaderCode_t* vs, vk_shaderCode_t* fs)
{
	const qboolean multiTexture = (shader_type == GAPI_SHADER_MULTI_TEXTURE_ADD) ||
		(shader_type == GAPI_SHADER_MULTI_TEXTURE_MUL);

	if (multiTexture)
	{
		fs->code = multi_texture_frag_spv;
		fs->size = multi_texture_frag_spv_size;
		fs->fileName = "multi_texture_frag.spv";
	}
	else
	{
		fs->code = single_texture_frag_spv;
		fs->size = single_texture_frag_spv_size;
		fs->fileName = "single_texture_frag.spv";
	}

	// Without shaderClipDistance, the fragment shader discards with the clip plane
	if (isClippingPlane && vk.features.shaderClipDistance)
	{
		vs->code = multiTexture ? multi_texture_clipping_plane_vert_spv : single_texture_clipping_plane_vert_spv;
		vs->size = multiTexture ? multi_texture_clipping_plane_vert_spv_size : single_texture_clipping_plane_vert_spv_size;
		vs->fileName = multiTexture ? "multi_texture_clipping_plane_vert.spv" : "single_texture_clipping_plane_vert.spv";
	}
	else
	{
		vs->code = multiTexture ? multi_texture_vert_spv : single_texture_vert_spv;
		vs->size = multiTexture ? multi_texture_vert_spv_size : single_texture_vert_spv_size;
		vs->fileName = multiTexture ? "multi_texture_vert.spv" : "single_texture_vert.spv";
	}
}
