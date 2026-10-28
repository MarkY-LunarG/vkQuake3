// The pipeline JSON export: the C interface (vk_pipeline_export.h) to the
// pipeline exporter of vulkansc-samples (vulkansc_samples/).

#include "vk_pipeline_export.h"
#include "vulkansc_samples/VulkanSCPipelineExport.h"
#include "vulkansc_samples/VulkanSCPipelineIdentifier.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

struct ExportState
{
	vks::PipelineExporter exporter;
	std::string directory;
	std::set<std::string> pipelineNames;
	std::map<std::string, std::vector<unsigned char>> shaders;
};

ExportState* exportState()
{
	static ExportState* state = nullptr;
	static bool checked = false;
	if (!checked)
	{
		checked = true;
		const char* directory = getenv("QVKSC_EXPORT_PIPELINES");
		if (directory != nullptr && directory[0] != '\0')
		{
			state = new ExportState();
			state->directory = directory;
			state->exporter.setSampleName(VK_PIPELINE_EXPORT_NAME);
			state->exporter.setOutputDirectory(directory);
		}
	}
	return state;
}

} // namespace

extern "C" int vk_pipelineExportEnabled(void)
{
	return exportState() != nullptr;
}

extern "C" void vk_pipelineExportRenderPass2(const VkRenderPassCreateInfo2* info, VkRenderPass handle)
{
	if (ExportState* state = exportState())
	{
		state->exporter.recordRenderPass2(*info, handle);
	}
}

extern "C" void vk_pipelineExportDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo* info, VkDescriptorSetLayout handle)
{
	if (ExportState* state = exportState())
	{
		state->exporter.recordDescriptorSetLayout(*info, handle);
	}
}

extern "C" void vk_pipelineExportPipelineLayout(const VkPipelineLayoutCreateInfo* info, VkPipelineLayout handle)
{
	if (ExportState* state = exportState())
	{
		state->exporter.recordPipelineLayout(*info, handle);
	}
}

extern "C" void vk_pipelineExportShader(const char* fileName, const unsigned char* code, int size)
{
	if (ExportState* state = exportState())
	{
		state->shaders[fileName].assign(code, code + size);
	}
}

extern "C" void vk_pipelineExportGraphicsPipeline(const char* name, const VkGraphicsPipelineCreateInfo* info,
		const char* vertexShaderFile, const char* fragmentShaderFile)
{
	ExportState* state = exportState();
	if (state == nullptr || !state->pipelineNames.insert(name).second)
	{
		return;
	}
	state->exporter.recordGraphicsPipeline(name, *info, { vertexShaderFile, fragmentShaderFile });
}

extern "C" int vk_pipelineExportWrite(void)
{
	ExportState* state = exportState();
	if (state == nullptr)
	{
		return 1;
	}
	if (!state->exporter.write())
	{
		return 0;
	}

	for (const auto& shader : state->shaders)
	{
		std::ofstream file(state->directory + "/" + shader.first, std::ios::binary);
		file.write(reinterpret_cast<const char*>(shader.second.data()), static_cast<std::streamsize>(shader.second.size()));
		if (!file)
		{
			std::cerr << "Vulkan SC: cannot write " << shader.first << "\n";
			return 0;
		}
	}

	// The manifest has the pipelines of this run and of earlier runs into the directory
	const std::string manifestPath = state->directory + "/" + VK_PIPELINE_MANIFEST_FILE;
	std::set<std::string> names = state->pipelineNames;
	{
		std::ifstream file(manifestPath);
		std::string line;
		while (std::getline(file, line))
		{
			if (!line.empty())
			{
				names.insert(line);
			}
		}
	}
	std::ofstream file(manifestPath);
	for (const auto& name : names)
	{
		file << name << "\n";
	}
	return file ? 1 : 0;
}

extern "C" void vk_pipelineIdentifier(const char* name, uint8_t identifier[VK_UUID_SIZE])
{
	vks::computePipelineIdentifier(std::string(VK_PIPELINE_EXPORT_NAME) + "/" + name, identifier);
}
