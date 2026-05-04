module;

#include <vulkan/vulkan.h>
#include <cstdint>

// Capture the macro-style constants before #undef. Anything used as an
// integer in caller code (handle compares, version literals, queue
// family sentinels) needs a captured constexpr alternate; the
// preprocessor would otherwise textually substitute the export-side
// declaration LHS too.
namespace _wv_vk {
inline constexpr std::uint32_t k_VK_TRUE                       = VK_TRUE;
inline constexpr std::uint32_t k_VK_FALSE                      = VK_FALSE;
inline constexpr std::uint32_t k_VK_API_VERSION_1_3            = VK_API_VERSION_1_3;
inline constexpr std::uint32_t k_VK_QUEUE_FAMILY_IGNORED       = VK_QUEUE_FAMILY_IGNORED;
inline constexpr std::uint32_t k_VK_QUEUE_FAMILY_FOREIGN_EXT   = VK_QUEUE_FAMILY_FOREIGN_EXT;
inline constexpr std::uint64_t k_VK_WHOLE_SIZE                 = VK_WHOLE_SIZE;
}

#undef VK_TRUE
#undef VK_FALSE
#undef VK_API_VERSION_1_3
#undef VK_QUEUE_FAMILY_IGNORED
#undef VK_QUEUE_FAMILY_FOREIGN_EXT
#undef VK_WHOLE_SIZE

// Extension-name string macros — capture as constexpr char arrays so
// they survive the macro/identifier collision pattern. We keep the
// originals' linkage by referring through a helper namespace.
namespace _wv_vk_ext {
inline constexpr const char* k_VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME      = VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME     = VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME               = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME  = VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME            = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
inline constexpr const char* k_VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME          = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME        = VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
inline constexpr const char* k_VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME             = VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;
}

#undef VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
#undef VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME
#undef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
#undef VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME
#undef VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME
#undef VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME

// VK_NULL_HANDLE is `#define VK_NULL_HANDLE 0` in the spec, used both
// against dispatchable handles (pointer types) and non-dispatchable
// handles (uint64_t aliases). Plain `int` / `nullptr_t` doesn't compare
// cleanly with both, so wrap in a tag struct with conversion operators.
namespace _wv_vk {
struct NullHandle {
    constexpr operator std::uint64_t() const noexcept { return 0; }
    template <class T>
    constexpr operator T*() const noexcept { return nullptr; }
};
}
#undef VK_NULL_HANDLE

export module vulkan;

export {

// ---- captured macros re-exported under the original name ----

inline constexpr std::uint32_t VK_TRUE                       = _wv_vk::k_VK_TRUE;
inline constexpr std::uint32_t VK_FALSE                      = _wv_vk::k_VK_FALSE;
inline constexpr std::uint32_t VK_API_VERSION_1_3            = _wv_vk::k_VK_API_VERSION_1_3;
inline constexpr std::uint32_t VK_QUEUE_FAMILY_IGNORED       = _wv_vk::k_VK_QUEUE_FAMILY_IGNORED;
inline constexpr std::uint32_t VK_QUEUE_FAMILY_FOREIGN_EXT   = _wv_vk::k_VK_QUEUE_FAMILY_FOREIGN_EXT;
inline constexpr std::uint64_t VK_WHOLE_SIZE                 = _wv_vk::k_VK_WHOLE_SIZE;

inline constexpr const char* VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME      = _wv_vk_ext::k_VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME     = _wv_vk_ext::k_VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME               = _wv_vk_ext::k_VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
inline constexpr const char* VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME  = _wv_vk_ext::k_VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
inline constexpr const char* VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME            = _wv_vk_ext::k_VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
inline constexpr const char* VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME = _wv_vk_ext::k_VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
inline constexpr const char* VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME          = _wv_vk_ext::k_VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
inline constexpr const char* VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME        = _wv_vk_ext::k_VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
inline constexpr const char* VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME             = _wv_vk_ext::k_VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME;

inline constexpr _wv_vk::NullHandle VK_NULL_HANDLE {};

// ---- core handles & dispatch-result types ----

using ::VkInstance;
using ::VkPhysicalDevice;
using ::VkDevice;
using ::VkQueue;
using ::VkCommandPool;
using ::VkCommandBuffer;
using ::VkBuffer;
using ::VkImage;
using ::VkImageView;
using ::VkDeviceMemory;
using ::VkSampler;
using ::VkSemaphore;
using ::VkFence;
using ::VkShaderModule;
using ::VkPipeline;
using ::VkPipelineLayout;
using ::VkDescriptorPool;
using ::VkDescriptorSet;
using ::VkDescriptorSetLayout;

using ::VkResult;
using ::VkFormat;
using ::VkImageLayout;
using ::VkImageType;
using ::VkImageViewType;
using ::VkImageTiling;
using ::VkImageUsageFlags;
using ::VkImageAspectFlags;
using ::VkSampleCountFlagBits;
using ::VkSharingMode;
using ::VkComponentSwizzle;
using ::VkFilter;
using ::VkSamplerAddressMode;
using ::VkSamplerMipmapMode;
using ::VkAccessFlags;
using ::VkPipelineStageFlags;
using ::VkBufferUsageFlags;
using ::VkMemoryPropertyFlags;
using ::VkDeviceSize;
using ::VkQueueFlags;
using ::VkQueueFlagBits;
using ::VkVideoCodecOperationFlagBitsKHR;
using ::VkExternalSemaphoreHandleTypeFlagBits;

// ---- struct types (createInfo / properties / barriers / etc.) ----

using ::VkApplicationInfo;
using ::VkInstanceCreateInfo;
using ::VkDeviceCreateInfo;
using ::VkDeviceQueueCreateInfo;
using ::VkBufferCreateInfo;
using ::VkImageCreateInfo;
using ::VkImageViewCreateInfo;
using ::VkSamplerCreateInfo;
using ::VkSemaphoreCreateInfo;
using ::VkFenceCreateInfo;
using ::VkCommandPoolCreateInfo;
using ::VkCommandBufferAllocateInfo;
using ::VkCommandBufferBeginInfo;
using ::VkShaderModuleCreateInfo;
using ::VkPipelineLayoutCreateInfo;
using ::VkPipelineShaderStageCreateInfo;
using ::VkComputePipelineCreateInfo;
using ::VkPushConstantRange;
using ::VkDescriptorPoolCreateInfo;
using ::VkDescriptorPoolSize;
using ::VkDescriptorSetAllocateInfo;
using ::VkDescriptorSetLayoutCreateInfo;
using ::VkDescriptorSetLayoutBinding;
using ::VkDescriptorImageInfo;
using ::VkWriteDescriptorSet;
using ::VkMemoryAllocateInfo;
using ::VkMemoryRequirements;
using ::VkBufferImageCopy;
using ::VkImageMemoryBarrier;
using ::VkSubmitInfo;
using ::VkTimelineSemaphoreSubmitInfo;
using ::VkExportSemaphoreCreateInfo;
using ::VkSemaphoreGetFdInfoKHR;
using ::VkExtensionProperties;
using ::VkQueueFamilyProperties;
using ::VkPhysicalDeviceFeatures2;
using ::VkPhysicalDeviceProperties2;
using ::VkPhysicalDeviceMemoryProperties;
using ::VkPhysicalDeviceIDProperties;
using ::VkPhysicalDeviceVulkan12Features;
using ::VkPhysicalDeviceVulkan13Features;
using ::VkPhysicalDeviceDrmPropertiesEXT;

// ---- enumerator constants ----

using ::VK_SUCCESS;
using ::VK_ERROR_OUT_OF_HOST_MEMORY;
using ::VK_ERROR_OUT_OF_DEVICE_MEMORY;
using ::VK_ERROR_INITIALIZATION_FAILED;
using ::VK_ERROR_DEVICE_LOST;
using ::VK_ERROR_LAYER_NOT_PRESENT;
using ::VK_ERROR_EXTENSION_NOT_PRESENT;
using ::VK_ERROR_FEATURE_NOT_PRESENT;
using ::VK_ERROR_INCOMPATIBLE_DRIVER;
using ::VK_ERROR_FORMAT_NOT_SUPPORTED;

using ::VK_FORMAT_R8_UNORM;
using ::VK_FORMAT_R8G8_UNORM;
using ::VK_FORMAT_R8G8B8A8_UNORM;
using ::VK_FORMAT_R16_UNORM;
using ::VK_FORMAT_R16G16_UNORM;
using ::VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

using ::VK_IMAGE_LAYOUT_UNDEFINED;
using ::VK_IMAGE_LAYOUT_GENERAL;
using ::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
using ::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

using ::VK_IMAGE_TYPE_2D;
using ::VK_IMAGE_VIEW_TYPE_2D;
using ::VK_IMAGE_TILING_OPTIMAL;
using ::VK_IMAGE_USAGE_SAMPLED_BIT;
using ::VK_IMAGE_USAGE_TRANSFER_DST_BIT;
using ::VK_IMAGE_ASPECT_COLOR_BIT;

using ::VK_SAMPLE_COUNT_1_BIT;
using ::VK_SHARING_MODE_EXCLUSIVE;
using ::VK_COMPONENT_SWIZZLE_IDENTITY;
using ::VK_FILTER_LINEAR;
using ::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
using ::VK_SAMPLER_MIPMAP_MODE_NEAREST;

using ::VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
using ::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
using ::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
using ::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

using ::VK_QUEUE_GRAPHICS_BIT;
using ::VK_QUEUE_COMPUTE_BIT;
using ::VK_QUEUE_TRANSFER_BIT;

using ::VK_ACCESS_SHADER_READ_BIT;
using ::VK_ACCESS_SHADER_WRITE_BIT;
using ::VK_ACCESS_TRANSFER_WRITE_BIT;

using ::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
using ::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
using ::VK_PIPELINE_STAGE_TRANSFER_BIT;
using ::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

using ::VK_PIPELINE_BIND_POINT_COMPUTE;
using ::VK_SHADER_STAGE_COMPUTE_BIT;

using ::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
using ::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

using ::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
using ::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
using ::VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

using ::VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

using ::VK_STRUCTURE_TYPE_APPLICATION_INFO;
using ::VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
using ::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
using ::VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
using ::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
using ::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
using ::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
using ::VK_STRUCTURE_TYPE_SUBMIT_INFO;
using ::VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
using ::VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
using ::VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
using ::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;

// ---- functions ----

using ::vkCreateInstance;
using ::vkDestroyInstance;
using ::vkEnumeratePhysicalDevices;
using ::vkGetPhysicalDeviceFeatures2;
using ::vkGetPhysicalDeviceProperties2;
using ::vkGetPhysicalDeviceMemoryProperties;
using ::vkGetPhysicalDeviceQueueFamilyProperties;
using ::vkEnumerateDeviceExtensionProperties;

using ::vkCreateDevice;
using ::vkDestroyDevice;
using ::vkDeviceWaitIdle;
using ::vkGetDeviceQueue;
using ::vkGetInstanceProcAddr;
using ::vkGetDeviceProcAddr;

using ::vkCreateCommandPool;
using ::vkDestroyCommandPool;
using ::vkAllocateCommandBuffers;
using ::vkBeginCommandBuffer;
using ::vkEndCommandBuffer;
using ::vkResetCommandBuffer;
using ::vkCmdPipelineBarrier;
using ::vkCmdBindDescriptorSets;
using ::vkCmdBindPipeline;
using ::vkCmdCopyBufferToImage;
using ::vkCmdDispatch;
using ::vkCmdPushConstants;

using ::vkCreateBuffer;
using ::vkDestroyBuffer;
using ::vkBindBufferMemory;
using ::vkGetBufferMemoryRequirements;
using ::vkCreateImage;
using ::vkDestroyImage;
using ::vkBindImageMemory;
using ::vkGetImageMemoryRequirements;
using ::vkCreateImageView;
using ::vkDestroyImageView;
using ::vkCreateSampler;
using ::vkDestroySampler;

using ::vkAllocateMemory;
using ::vkFreeMemory;
using ::vkMapMemory;
using ::vkUnmapMemory;

using ::vkCreateSemaphore;
using ::vkDestroySemaphore;
using ::vkCreateFence;
using ::vkDestroyFence;
using ::vkResetFences;
using ::vkWaitForFences;

using ::vkCreateShaderModule;
using ::vkDestroyShaderModule;
using ::vkCreatePipelineLayout;
using ::vkDestroyPipelineLayout;
using ::vkCreateComputePipelines;
using ::vkDestroyPipeline;

using ::vkCreateDescriptorPool;
using ::vkDestroyDescriptorPool;
using ::vkCreateDescriptorSetLayout;
using ::vkDestroyDescriptorSetLayout;
using ::vkAllocateDescriptorSets;
using ::vkUpdateDescriptorSets;

using ::vkQueueSubmit;

// ---- function-pointer typedefs (PFN_*) used for runtime extension lookup ----
using ::PFN_vkVoidFunction;
using ::PFN_vkGetSemaphoreFdKHR;
using ::PFN_vkGetPhysicalDeviceFeatures2;
using ::PFN_vkGetPhysicalDeviceProperties2;

}
