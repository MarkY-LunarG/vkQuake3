#ifndef VK_GAPI_TYPES_H_
#define VK_GAPI_TYPES_H_

#include "VKimpl.h"
#include "graphics_api/gapi.h"

// The Vulkan objects behind the graphics API handles.

struct gapi_texture_s {
	VkImage handle;
	// To use any VkImage, including those in the swap chain, in the render pipeline
	// we have to create a VkImageView object. An image view is quite literally a
	// view into image. It describe how to access the image and witch part of the
	// image to access, if it should be treated as a 2D texture depth texture without
	// any mipmapping levels.
	VkImageView view;
	// Descriptor set that contains single descriptor used to access the given image.
	// It is updated only once during image initialization.
	VkDescriptorSet descriptor_set;

	uint32_t width, height;
	uint32_t mipLevels;
	qboolean mipmap;
	qboolean repeat;		// GL_REPEAT, else GL_CLAMP
};

struct gapi_pipeline_s {
	VkPipeline pipeline;
	// The pipeline has dynamic depth bias (Q3 shader polygonOffset).
	qboolean polygonOffset;
	// Dynamic state (VK_EXT_extended_dynamic_state), set by gapi_draw
	VkCullModeFlags cullMode;
	VkFrontFace frontFace;
};

#endif
