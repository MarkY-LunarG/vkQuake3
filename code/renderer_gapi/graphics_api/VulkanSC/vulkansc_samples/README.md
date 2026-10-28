# Pipeline JSON exporter from vulkansc-samples

These files come from `base/` of the LunarG `vulkansc-samples` repository
(<https://github.com/LunarG/vulkansc-samples>), commit `6ea064d`.
They are MIT licensed, as their headers say.
The copy is the one in Warzone 2100 (`lib/ivis_opengl/3rdparty/vulkansc_samples`), with its local
changes.

The Vulkan SC renderer needs every pipeline compiled offline by a pipeline cache compiler (PCC),
which reads one Khronos pipeline JSON document per pipeline.
In export mode (`QVKSC_EXPORT_PIPELINES`), the renderer records the create info of the render pass,
the descriptor set layout, the pipeline layout, and each pipeline, and these files write the
documents (`vk_pipeline_export.cpp`).

| File | Purpose |
|---|---|
| `VulkanSCPipelineExport.h`, `VulkanSCPipelineExport.cpp` | Records create infos by handle and writes one document per pipeline |
| `VulkanSCPipelineIdentifier.h` | The 16-byte pipeline identifier: two FNV-1a 64-bit hashes of the pipeline name |
| `VulkanSCEnumStrings.h` | Enum value names, generated from `vulkan_sc_core.h` by `scripts/gen_enum_strings.py` |
| `vulkansc_compat.h` | Local: includes `<vulkan/vulkan_sc.h>` |

## Local changes

1. `VulkanSCPipelineExport.h` and `VulkanSCEnumStrings.h` include `vulkansc_compat.h` instead of
   `<vulkan/vulkan_sc.h>`.
   The Warzone 2100 version of `vulkansc_compat.h` can also select the desktop header.
   This version always includes `<vulkan/vulkan_sc.h>`.
2. `pResolveAttachments` is written out, for both render pass create info forms.
   The original always wrote `"NULL"`.
3. Each document is named `<name>.pc.<identifier in hex>.json` instead of `<name>.pc.json`,
   `<name>.pc1.json`, and so on in record order.
   A pipeline then has the same file in every export run, and runs that reach different
   pipelines can write into one directory.
