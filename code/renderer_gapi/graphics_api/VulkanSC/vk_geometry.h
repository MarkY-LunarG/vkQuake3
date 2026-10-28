#ifndef VK_GEOMETRY_H_
#define VK_GEOMETRY_H_

#include "vk_gapi_types.h"

void vk_createVertexBuffer(void);
void vk_createIndexBuffer(void);
VkBuffer vk_getIndexBuffer(void);
void vk_resetGeometryBuffer(void);
void vk_destroy_shading_data(void);

#endif
