/**
 * MystralNative Metal Ray Tracing Backend
 *
 * Implements hardware-accelerated ray tracing using Metal's ray tracing API
 * introduced in Metal 3 (macOS 13+, iOS 16+) on Apple Silicon (M1/M2/M3).
 *
 * Reference: https://developer.apple.com/documentation/metal/metal_sample_code_library/accelerating_ray_tracing_using_metal
 * Reference: https://developer.apple.com/documentation/metal/ray_tracing
 *
 * Build Requirements:
 * - macOS 13+ (Ventura) or iOS 16+
 * - Apple Silicon (M1, M2, M3, or later)
 * - Xcode 14+ with Metal framework
 *
 * Note: Intel Macs do NOT support Metal ray tracing.
 */

#pragma once

#include "rt_common.h"
#include <vector>
#include <unordered_map>
#include <memory>

// Forward declarations for Objective-C types (avoid importing Metal headers in header)
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLLibrary;
@protocol MTLComputePipelineState;
@protocol MTLBuffer;
@protocol MTLAccelerationStructure;
@protocol MTLTexture;
#else
typedef void* id;
#endif

namespace mystral {
namespace rt {

// Forward declarations
struct MetalGeometry;
struct MetalBLAS;
struct MetalTLAS;

/**
 * Metal buffer wrapper.
 * Used for vertex/index buffers and acceleration structures.
 */
struct MetalBuffer {
    id buffer = nullptr;        // id<MTLBuffer>
    size_t size = 0;
    void* contents = nullptr;   // Mapped pointer (for shared/managed storage)
};

/**
 * Geometry data stored in Metal buffers.
 */
struct MetalGeometry {
    MetalBuffer vertexBuffer;
    MetalBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    size_t vertexStride = 12;   // Default: vec3 position
};

/**
 * Bottom-Level Acceleration Structure.
 * Contains geometry in object space.
 */
struct MetalBLAS {
    id accelerationStructure = nullptr;  // id<MTLAccelerationStructure>
    std::vector<uint32_t> geometryIds;   // Associated geometry IDs
};

/**
 * Top-Level Acceleration Structure.
 * Contains positioned instances of BLASes.
 */
struct MetalTLAS {
    id accelerationStructure = nullptr;  // id<MTLAccelerationStructure>
    MetalBuffer instanceBuffer;          // MTLAccelerationStructureInstanceDescriptor array
    uint32_t instanceCount = 0;
};

/**
 * Metal Ray Tracing Backend.
 *
 * Implements the IRTBackend interface using Metal's ray tracing API.
 * Uses compute pipelines with ray intersection functions.
 */
class MetalRTBackend : public IRTBackend {
public:
    MetalRTBackend();
    ~MetalRTBackend() override;

    /**
     * Initialize Metal device and ray tracing resources.
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

    bool initDevice();
    bool checkRayTracingSupport();
    bool createCommandQueue();
    bool loadShaders();
    bool createComputePipeline();

    // ========================================================================
    // Buffer Management
    // ========================================================================

    bool createBuffer(size_t size, MetalBuffer& buffer, bool shared = true);
    void destroyBuffer(MetalBuffer& buffer);

    // ========================================================================
    // Acceleration Structure Helpers
    // ========================================================================

    bool buildAccelerationStructure(id descriptor, id& outStructure);

    // ========================================================================
    // Output Image Management
    // ========================================================================

    bool ensureOutputTexture(uint32_t width, uint32_t height);

    // ========================================================================
    // Metal Core Objects
    // ========================================================================

    id device_ = nullptr;           // id<MTLDevice>
    id commandQueue_ = nullptr;     // id<MTLCommandQueue>
    id shaderLibrary_ = nullptr;    // id<MTLLibrary>
    id rtPipeline_ = nullptr;       // id<MTLComputePipelineState>

    // ========================================================================
    // Output Resources
    // ========================================================================

    id outputTexture_ = nullptr;    // id<MTLTexture>
    MetalBuffer cameraBuffer_;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    // ========================================================================
    // Resource Tracking
    // ========================================================================

    std::unordered_map<uint32_t, std::unique_ptr<MetalGeometry>> geometries_;
    std::unordered_map<uint32_t, std::unique_ptr<MetalBLAS>> blases_;
    std::unordered_map<uint32_t, std::unique_ptr<MetalTLAS>> tlases_;
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
