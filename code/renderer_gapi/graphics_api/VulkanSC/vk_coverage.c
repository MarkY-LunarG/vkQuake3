#include "tr_local.h"
#include "vk_instance.h"
#include "vk_cmd.h"
#include "vk_texture.h"
#include "vk_coverage.h"

// The frames that the timestamp results buffer has room for
#define COVERAGE_TIMING_SLOTS   2

static cvar_t* r_apiCoverage;
static qboolean s_heavy = qfalse;

// Debug aid: QVKSC_HEAVY_ONLY=<feature> turns on only one feature of heavy:
// timing, event, upload, bind2, blit, or depthclear.
static const char* s_heavyOnly = NULL;

qboolean vk_coverageFeature(const char* feature)
{
	// The upload feature is off, unless QVKSC_HEAVY_ONLY selects it: the
	// Premium-GFXR Vulkan SC replay of a submit that waits for a timeline value
	// from vkSignalSemaphore hangs the GPU (2026-10-28).
	if (!Q_stricmp(feature, "upload") && (s_heavyOnly == NULL || Q_stricmp(s_heavyOnly, "upload")))
		return qfalse;
	return s_heavy && (s_heavyOnly == NULL || !Q_stricmp(s_heavyOnly, feature));
}

static VkQueryPool s_queryPool = VK_NULL_HANDLE;
static VkBuffer s_resultBuffer = VK_NULL_HANDLE;
static VkDeviceMemory s_resultMemory = VK_NULL_HANDLE;
static VkEvent s_frameEvent = VK_NULL_HANDLE;
static VkSemaphore s_uploadTimeline = VK_NULL_HANDLE;
static uint64_t s_uploadValue = 0;
static VkImage s_screenshotImage = VK_NULL_HANDLE;
static VkDeviceMemory s_screenshotMemory = VK_NULL_HANDLE;
static uint32_t s_frameNumber = 0;
static float s_timestampPeriod = 0.0f;

// The layout of the results buffer: for each slot, the frame number, then
// the two timestamps (VK_QUERY_RESULT_64_BIT).
typedef struct {
	uint64_t frameNumber;
	uint64_t timestamps[2];
} timingSlot_t;


void vk_coverageReadCvar(void)
{
	r_apiCoverage = ri.Cvar_Get("r_apiCoverage", "default", CVAR_LATCH | CVAR_TEMP);
	s_heavy = !Q_stricmp(r_apiCoverage->string, "heavy");
	s_heavyOnly = getenv("QVKSC_HEAVY_ONLY");
	if (s_heavyOnly != NULL && s_heavyOnly[0] == '\0')
		s_heavyOnly = NULL;
	if (s_heavy && s_heavyOnly != NULL)
		ri.Printf(PRINT_ALL, " Vulkan SC: QVKSC_HEAVY_ONLY %s\n", s_heavyOnly);
	if (s_heavy)
		ri.Printf(PRINT_ALL, " Vulkan SC: r_apiCoverage heavy\n");
	else if (Q_stricmp(r_apiCoverage->string, "default"))
		ri.Printf(PRINT_WARNING, "Vulkan SC: r_apiCoverage \"%s\" is not \"default\" or \"heavy\"; using \"default\"\n",
			r_apiCoverage->string);
}


qboolean vk_coverageHeavy(void)
{
	return s_heavy;
}


void vk_bindBufferMemory(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
	if (!vk_coverageFeature("bind2"))
	{
		VK_CHECK(qvkBindBufferMemory(vk.device, buffer, memory, offset));
		return;
	}
	VkBindBufferMemoryInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
	info.buffer = buffer;
	info.memory = memory;
	info.memoryOffset = offset;
	VK_CHECK(qvkBindBufferMemory2(vk.device, 1, &info));
}


void vk_bindImageMemory(VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
	if (!vk_coverageFeature("bind2"))
	{
		VK_CHECK(qvkBindImageMemory(vk.device, image, memory, offset));
		return;
	}
	VkBindImageMemoryInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
	info.image = image;
	info.memory = memory;
	info.memoryOffset = offset;
	VK_CHECK(qvkBindImageMemory2(vk.device, 1, &info));
}


void vk_coverageCreate(void)
{
	if (!s_heavy)
		return;

	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties(vk.physical_device, &props);
	s_timestampPeriod = props.limits.timestampPeriod;

	// Timestamps: two for each slot
	VkQueryPoolCreateInfo pool_info;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	pool_info.queryCount = 2 * COVERAGE_TIMING_SLOTS;
	VK_CHECK(qvkCreateQueryPool(vk.device, &pool_info, NULL, &s_queryPool));
	qvkResetQueryPool(vk.device, s_queryPool, 0, 2 * COVERAGE_TIMING_SLOTS);

	// The results buffer, which the host reads
	VkBufferCreateInfo buffer_info;
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = COVERAGE_TIMING_SLOTS * sizeof(timingSlot_t);
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VK_CHECK(qvkCreateBuffer(vk.device, &buffer_info, NULL, &s_resultBuffer));
	VkMemoryRequirements requirements;
	qvkGetBufferMemoryRequirements(vk.device, s_resultBuffer, &requirements);
	VkMemoryAllocateInfo alloc_info;
	memset(&alloc_info, 0, sizeof(alloc_info));
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &s_resultMemory));
	vk_bindBufferMemory(s_resultBuffer, s_resultMemory, 0);

	// The frame event starts set, as if a frame before the first one ended
	VkEventCreateInfo event_info;
	memset(&event_info, 0, sizeof(event_info));
	event_info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
	VK_CHECK(qvkCreateEvent(vk.device, &event_info, NULL, &s_frameEvent));
	VK_CHECK(qvkResetEvent(vk.device, s_frameEvent));
	VK_CHECK(qvkSetEvent(vk.device, s_frameEvent));

	// The upload timeline
	VkSemaphoreTypeCreateInfo timeline_info;
	memset(&timeline_info, 0, sizeof(timeline_info));
	timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	VkSemaphoreCreateInfo semaphore_info;
	memset(&semaphore_info, 0, sizeof(semaphore_info));
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphore_info.pNext = &timeline_info;
	VK_CHECK(qvkCreateSemaphore(vk.device, &semaphore_info, NULL, &s_uploadTimeline));
	s_uploadValue = 0;

	// The RGBA image for screenshots, with the size of the swapchain images
	VkImageCreateInfo image_info;
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent.width = vk.swapchain_extent.width;
	image_info.extent.height = vk.swapchain_extent.height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(qvkCreateImage(vk.device, &image_info, NULL, &s_screenshotImage));
	qvkGetImageMemoryRequirements(vk.device, s_screenshotImage, &requirements);
	alloc_info.allocationSize = requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &s_screenshotMemory));
	vk_bindImageMemory(s_screenshotImage, s_screenshotMemory, 0);

	s_frameNumber = 0;
}


void vk_coverageDestroy(void)
{
	// Vulkan SC has no vkDestroyQueryPool and no vkFreeMemory: they stay
	// until the device is destroyed.
	if (s_frameEvent != VK_NULL_HANDLE)
		qvkDestroyEvent(vk.device, s_frameEvent, NULL);
	if (s_uploadTimeline != VK_NULL_HANDLE)
		qvkDestroySemaphore(vk.device, s_uploadTimeline, NULL);
	if (s_resultBuffer != VK_NULL_HANDLE)
		qvkDestroyBuffer(vk.device, s_resultBuffer, NULL);
	if (s_screenshotImage != VK_NULL_HANDLE)
		qvkDestroyImage(vk.device, s_screenshotImage, NULL);
	s_queryPool = VK_NULL_HANDLE;
	s_resultBuffer = VK_NULL_HANDLE;
	s_resultMemory = VK_NULL_HANDLE;
	s_frameEvent = VK_NULL_HANDLE;
	s_uploadTimeline = VK_NULL_HANDLE;
	s_screenshotImage = VK_NULL_HANDLE;
	s_screenshotMemory = VK_NULL_HANDLE;
}


void vk_coverageBeginFrame(void)
{
	if (!s_heavy)
		return;

	// The previous frame is finished: its event is set
	if (vk_coverageFeature("event") && qvkGetEventStatus(vk.device, s_frameEvent) != VK_EVENT_SET)
		ri.Printf(PRINT_WARNING, "Vulkan SC: the frame event of the previous frame is not set\n");

	// The slot of this frame was written two frames ago, which is finished.
	// Read its GPU time now and then, and reset its queries for this frame.
	const uint32_t slot = s_frameNumber % COVERAGE_TIMING_SLOTS;
	if (!vk_coverageFeature("timing"))
		return;
	if (s_frameNumber >= COVERAGE_TIMING_SLOTS && (s_frameNumber % 600) < COVERAGE_TIMING_SLOTS)
	{
		timingSlot_t* slots;
		VK_CHECK(qvkMapMemory(vk.device, s_resultMemory, 0, VK_WHOLE_SIZE, 0, (void**)&slots));
		const timingSlot_t result = slots[slot];
		qvkUnmapMemory(vk.device, s_resultMemory);
		const double ms = (double)(result.timestamps[1] - result.timestamps[0]) * s_timestampPeriod / 1000000.0;
		ri.Printf(PRINT_DEVELOPER, "Vulkan SC: frame %u took %.3f ms on the GPU\n", (uint32_t)result.frameNumber, ms);
	}
	qvkResetQueryPool(vk.device, s_queryPool, 2 * slot, 2);
}


void vk_coverageBeginCommands(VkCommandBuffer cmdBuf)
{
	if (!s_heavy)
		return;

	const uint32_t slot = s_frameNumber % COVERAGE_TIMING_SLOTS;
	if (vk_coverageFeature("event"))
		qvkCmdResetEvent2KHR(cmdBuf, s_frameEvent, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR);
	if (!vk_coverageFeature("timing"))
		return;

	const uint64_t frame_number = s_frameNumber;
	qvkCmdUpdateBuffer(cmdBuf, s_resultBuffer, slot * sizeof(timingSlot_t), sizeof(frame_number), &frame_number);
	qvkCmdWriteTimestamp2KHR(cmdBuf, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR, s_queryPool, 2 * slot);
}


void vk_coverageEndCommands(VkCommandBuffer cmdBuf)
{
	if (!s_heavy)
		return;

	const uint32_t slot = s_frameNumber % COVERAGE_TIMING_SLOTS;
	if (vk_coverageFeature("timing"))
	{
	qvkCmdWriteTimestamp2KHR(cmdBuf, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR, s_queryPool, 2 * slot + 1);
	qvkCmdCopyQueryPoolResults(cmdBuf, s_queryPool, 2 * slot, 2, s_resultBuffer,
		slot * sizeof(timingSlot_t) + offsetof(timingSlot_t, timestamps), sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	vk_cmdBufferBarrier(cmdBuf, s_resultBuffer,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_HOST_BIT_KHR, VK_ACCESS_2_HOST_READ_BIT_KHR);
	}

	// The frame event: set when all commands of the frame are done
	VkMemoryBarrier2KHR barrier;
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
	VkDependencyInfoKHR dependency;
	memset(&dependency, 0, sizeof(dependency));
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &barrier;
	if (vk_coverageFeature("event"))
		qvkCmdSetEvent2KHR(cmdBuf, s_frameEvent, &dependency);

	s_frameNumber++;
}


void vk_submitUpload(VkCommandBuffer cmdBuf)
{
	if (!vk_coverageFeature("upload"))
	{
		vk_submit(cmdBuf, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
		return;
	}

	// The host has written the staging buffer: signal the next value, which
	// the upload waits for.
	s_uploadValue++;
	VkSemaphoreSignalInfo signal_info;
	memset(&signal_info, 0, sizeof(signal_info));
	signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
	signal_info.semaphore = s_uploadTimeline;
	signal_info.value = s_uploadValue;
	VK_CHECK(qvkSignalSemaphore(vk.device, &signal_info));

	VkSemaphoreSubmitInfoKHR wait;
	memset(&wait, 0, sizeof(wait));
	wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR;
	wait.semaphore = s_uploadTimeline;
	wait.value = s_uploadValue;
	wait.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;

	VkCommandBufferSubmitInfoKHR command_buffer;
	memset(&command_buffer, 0, sizeof(command_buffer));
	command_buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
	command_buffer.commandBuffer = cmdBuf;

	VkSubmitInfo2KHR submit;
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR;
	submit.waitSemaphoreInfoCount = 1;
	submit.pWaitSemaphoreInfos = &wait;
	submit.commandBufferInfoCount = 1;
	submit.pCommandBufferInfos = &command_buffer;
	VK_CHECK(qvkQueueSubmit2KHR(vk.queue, 1, &submit, VK_NULL_HANDLE));
}


VkImage vk_coverageScreenshotImage(void)
{
	return s_screenshotImage;
}
