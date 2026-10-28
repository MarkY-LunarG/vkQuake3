#ifndef VK_PIPELINE_EXPORT_H_
#define VK_PIPELINE_EXPORT_H_

// The pipeline JSON export for the Vulkan SC pipeline cache compiler (PCC).
//
// When QVKSC_EXPORT_PIPELINES names a directory, the renderer runs without a
// pipeline cache. It records the create info of the render pass, the layouts,
// and each pipeline that the front end asks for, in place of creating the
// pipeline. At shutdown it writes into the directory:
//  - q3.pc.<identifier>.json: one Khronos pipeline JSON document per pipeline,
//  - the SPIR-V files that the documents name,
//  - q3_pipelines.txt: the names of all pipelines, also those of earlier runs
//    into the same directory.
// The PCC builds the pipeline cache from the documents and the SPIR-V.

// Only the Vulkan SC header: the C++ exporter includes this file too.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_sc.h>

#ifdef __cplusplus
extern "C" {
#endif

// The name of the pipeline cache documents and of the manifest.
#define VK_PIPELINE_EXPORT_NAME     "q3"
#define VK_PIPELINE_MANIFEST_FILE   "q3_pipelines.txt"
#define VK_PIPELINE_CACHE_FILE      "q3.vksc.bin"
// The size of each pipeline in the pipeline pool (VkPipelineOfflineCreateInfo).
#define VK_PIPELINE_POOL_ENTRY_SIZE (256 * 1024)

int vk_pipelineExportEnabled(void);

void vk_pipelineExportRenderPass2(const VkRenderPassCreateInfo2* info, VkRenderPass handle);
void vk_pipelineExportDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo* info, VkDescriptorSetLayout handle);
void vk_pipelineExportPipelineLayout(const VkPipelineLayoutCreateInfo* info, VkPipelineLayout handle);
void vk_pipelineExportShader(const char* fileName, const unsigned char* code, int size);
void vk_pipelineExportGraphicsPipeline(const char* name, const VkGraphicsPipelineCreateInfo* info,
		const char* vertexShaderFile, const char* fragmentShaderFile);

// Write the documents, the SPIR-V, and the manifest. Return 0 on failure.
int vk_pipelineExportWrite(void);

// The pipeline identifier of a pipeline name, as the documents have it.
void vk_pipelineIdentifier(const char* name, uint8_t identifier[VK_UUID_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
