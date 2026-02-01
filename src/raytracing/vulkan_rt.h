/**
 * MystralNative Vulkan Ray Tracing Backend
 *
 * Implements hardware-accelerated ray tracing using Vulkan's
 * VK_KHR_ray_tracing_pipeline and VK_KHR_acceleration_structure extensions.
 *
 * Reference: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_KHR_ray_tracing_pipeline.html
 * Tutorial: https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/
 *
 * Build Requirements:
 * - Vulkan SDK with ray tracing headers
 * - GPU with VK_KHR_ray_tracing_pipeline support (NVIDIA RTX, AMD RDNA2+)
 * - Linux or Windows (macOS via MoltenVK is experimental)
 */

#pragma once

#include "rt_common.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>

namespace mystral {
namespace rt {

// Forward declarations
struct VulkanGeometry;
struct VulkanBLAS;
struct VulkanTLAS;
struct VulkanBuffer;

/**
 * Vulkan buffer wrapper with device memory.
 * Used for vertex/index buffers, acceleration structures, and SBT.
 */
struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    VkDeviceSize size = 0;
    void* mappedPtr = nullptr;
};

/**
 * Geometry data stored in Vulkan buffers.
 */
struct VulkanGeometry {
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    size_t vertexStride = 12;  // Default: vec3 position
};

/**
 * Bottom-Level Acceleration Structure.
 * Contains geometry in object space.
 */
struct VulkanBLAS {
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    VulkanBuffer buffer;
    VkDeviceAddress deviceAddress = 0;
    std::vector<uint32_t> geometryIds;  // Associated geometry IDs
};

/**
 * Top-Level Acceleration Structure.
 * Contains positioned instances of BLASes.
 */
struct VulkanTLAS {
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    VulkanBuffer buffer;
    VulkanBuffer instanceBuffer;  // VkAccelerationStructureInstanceKHR array
    VkDeviceAddress deviceAddress = 0;
    uint32_t instanceCount = 0;
};

/**
 * Vulkan Ray Tracing Backend.
 *
 * Implements the IRTBackend interface using Vulkan ray tracing extensions.
 * Manages Vulkan instance, device, command pools, and RT pipeline.
 */
class VulkanRTBackend : public IRTBackend {
public:
    VulkanRTBackend();
    ~VulkanRTBackend() override;

    /**
     * Initialize Vulkan instance, device, and ray tracing extensions.
     * @return true if hardware RT is available and initialized
     */
    bool initialize();

    // ========================================================================
    // IRTBackend Interface Implementation
    // ========================================================================

    bool isSupported() override;
    RTBackendType getBackendType() override;
    const char* getBackend() override;

    RTGeometryHandle createGeometry(const RTGeometryDesc& desc) override;
    void destroyGeometry(RTGeometryHandle geometry) override;

    RTBLASHandle createBLAS(RTGeometryHandle* geometries, size_t count) override;
    void destroyBLAS(RTBLASHandle blas) override;

    RTTLASHandle createTLAS(const RTTLASInstance* instances, size_t count) override;
    void updateTLAS(RTTLASHandle tlas, const RTTLASInstance* instances, size_t count) override;
    void destroyTLAS(RTTLASHandle tlas) override;

    void traceRays(const TraceRaysOptions& options) override;

private:
    // ========================================================================
    // Initialization Helpers
    // ========================================================================

    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createCommandPool();
    bool loadExtensionFunctions();
    bool createDescriptorPool();
    bool createRTPipeline();
    bool createShaderBindingTable();

    // ========================================================================
    // Buffer Management
    // ========================================================================

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VulkanBuffer& buffer);
    void destroyBuffer(VulkanBuffer& buffer);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // ========================================================================
    // Command Buffer Helpers
    // ========================================================================

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    // ========================================================================
    // Shader Compilation
    // ========================================================================

    std::vector<uint32_t> compileGLSLToSPIRV(const char* source, const char* shaderType);
    VkShaderModule createShaderModule(const std::vector<uint32_t>& spirvCode);

    // ========================================================================
    // Vulkan Core Objects
    // ========================================================================

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_ = 0;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;

    // ========================================================================
    // Shader Binding Table
    // ========================================================================

    VulkanBuffer sbtBuffer_;
    VkStridedDeviceAddressRegionKHR raygenRegion_ = {};
    VkStridedDeviceAddressRegionKHR missRegion_ = {};
    VkStridedDeviceAddressRegionKHR hitRegion_ = {};
    VkStridedDeviceAddressRegionKHR callableRegion_ = {};

    // ========================================================================
    // Output Resources
    // ========================================================================

    VkImage outputImage_ = VK_NULL_HANDLE;
    VkDeviceMemory outputImageMemory_ = VK_NULL_HANDLE;
    VkImageView outputImageView_ = VK_NULL_HANDLE;
    VulkanBuffer stagingBuffer_;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    // ========================================================================
    // Camera Uniform Buffer
    // ========================================================================

    VulkanBuffer cameraUBO_;

    // ========================================================================
    // Device Properties
    // ========================================================================

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties_ = {};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures_ = {};

    // ========================================================================
    // Extension Function Pointers
    // ========================================================================

    // Acceleration Structure functions
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR_ = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR_ = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR_ = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR_ = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR_ = nullptr;

    // Ray Tracing Pipeline functions
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR_ = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR_ = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR_ = nullptr;

    // Buffer Device Address
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR_ = nullptr;

    // ========================================================================
    // Resource Tracking
    // ========================================================================

    std::unordered_map<uint32_t, std::unique_ptr<VulkanGeometry>> geometries_;
    std::unordered_map<uint32_t, std::unique_ptr<VulkanBLAS>> blases_;
    std::unordered_map<uint32_t, std::unique_ptr<VulkanTLAS>> tlases_;
    uint32_t nextGeometryId_ = 1;
    uint32_t nextBLASId_ = 1;
    uint32_t nextTLASId_ = 1;

    // ========================================================================
    // State
    // ========================================================================

    bool initialized_ = false;
    bool rtSupported_ = false;
};

}  // namespace rt
}  // namespace mystral
