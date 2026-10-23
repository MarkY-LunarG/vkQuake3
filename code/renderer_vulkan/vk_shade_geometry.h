#ifndef VK_SHADE_GEOMETRY
#define VK_SHADE_GEOMETRY

#include "vk_instance.h"
#include "vk_gapi_types.h"

enum Vk_Depth_Range {
	DEPTH_RANGE_NORMAL, // [0..1]
	DEPTH_RANGE_ZERO, // [0..0]
	DEPTH_RANGE_ONE, // [1..1]
	DEPTH_RANGE_WEAPON // [0..0.3]
};


const float * getptr_modelview_matrix(void);

void set_modelview_matrix(const float mv[16]);

void vk_shade_geometry(const gapi_pipeline_t* pipeline, qboolean multitexture, enum Vk_Depth_Range depth_range, qboolean indexed);
void vk_UploadXYZI(float (*pXYZ)[4], uint32_t nVertex, uint32_t* pIdx, uint32_t nIndex);

void updateMVP(qboolean isPortal, qboolean is2D, const float mvMat4x4[16]);
void vk_resetGeometryBuffer(void);

void vk_createVertexBuffer(void);
void vk_createIndexBuffer(void);

VkBuffer vk_getIndexBuffer(void);
void vk_destroy_shading_data(void);

void updateCurDescriptor( const gapi_texture_t* tex, uint32_t tmu);

VkRect2D get_scissor_rect(void);


void vk_clearColorAttachments(const float* color);
void vk_clearDepthStencilAttachments(void);

#endif
