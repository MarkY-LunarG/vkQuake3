#ifndef VK_PIPELINE_H_
#define VK_PIPELINE_H_

void vk_createPipelineLayout(void);
void vk_destroyPipelineLayout(void);

#include "graphics_api/gapi.h"
// The name of the pipeline of a pipeline description, in the pipeline cache.
void vk_pipelineName(const gapi_pipelineDesc_t* def, char* name, int size);

// The Vulkan SC pipeline cache: load it before the device is created, and
// create its pipelines after the render pass and the pipeline layout.
qboolean vk_loadPipelineCache(void);
const void* vk_pipelineCacheData(size_t* pSize, uint32_t* pNumPipelines);
void vk_createCachePipelines(void);
void vk_destroyCachePipelines(void);

#endif
