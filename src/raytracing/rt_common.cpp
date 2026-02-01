/**
 * MystralNative Ray Tracing Backend Factory
 *
 * Provides the factory function to create the appropriate RT backend
 * based on platform capabilities.
 *
 * Backend selection order:
 * 1. Vulkan RT (Linux, Windows with Vulkan SDK, macOS with MoltenVK)
 * 2. DXR (Windows with DirectX 12 - TODO)
 * 3. Metal RT (Apple Silicon with Metal 3 - TODO)
 * 4. Stub (fallback when no hardware RT available)
 */

#include "rt_common.h"
#include <iostream>

#ifdef MYSTRAL_HAS_VULKAN_RT
#include "vulkan_rt.h"
#endif

namespace mystral {
namespace rt {

// ============================================================================
// Stub Backend Implementation
// ============================================================================

/**
 * Stub RT backend for when no hardware RT is available.
 * All methods return error or no-op, isSupported() returns false.
 */
class StubRTBackend : public IRTBackend {
public:
    StubRTBackend() {
        // Silent construction - logging happens in isSupported() check
    }

    ~StubRTBackend() override = default;

    // ========================================================================
    // Capability Queries
    // ========================================================================

    bool isSupported() override {
        return false;
    }

    RTBackendType getBackendType() override {
        return RTBackendType::None;
    }

    const char* getBackend() override {
        return "none";
    }

    // ========================================================================
    // Geometry Management
    // ========================================================================

    RTGeometryHandle createGeometry(const RTGeometryDesc& desc) override {
        std::cerr << "[MystralRT] createGeometry: Hardware ray tracing not available" << std::endl;
        return RTGeometryHandle{};
    }

    void destroyGeometry(RTGeometryHandle geometry) override {
        // No-op for stub
    }

    // ========================================================================
    // Acceleration Structure Management
    // ========================================================================

    RTBLASHandle createBLAS(RTGeometryHandle* geometries, size_t count) override {
        std::cerr << "[MystralRT] createBLAS: Hardware ray tracing not available" << std::endl;
        return RTBLASHandle{};
    }

    void destroyBLAS(RTBLASHandle blas) override {
        // No-op for stub
    }

    RTTLASHandle createTLAS(const RTTLASInstance* instances, size_t count) override {
        std::cerr << "[MystralRT] createTLAS: Hardware ray tracing not available" << std::endl;
        return RTTLASHandle{};
    }

    void updateTLAS(RTTLASHandle tlas, const RTTLASInstance* instances, size_t count) override {
        std::cerr << "[MystralRT] updateTLAS: Hardware ray tracing not available" << std::endl;
    }

    void destroyTLAS(RTTLASHandle tlas) override {
        // No-op for stub
    }

    // ========================================================================
    // Ray Tracing Execution
    // ========================================================================

    void traceRays(const TraceRaysOptions& options) override {
        std::cerr << "[MystralRT] traceRays: Hardware ray tracing not available" << std::endl;
    }
};

// ============================================================================
// Factory Implementation
// ============================================================================

std::unique_ptr<IRTBackend> createRTBackend() {
    // Try platform-specific backends in order of preference

#ifdef MYSTRAL_HAS_VULKAN_RT
    // Try Vulkan RT backend (cross-platform)
    {
        auto vulkan = std::make_unique<VulkanRTBackend>();
        if (vulkan->initialize()) {
            std::cout << "[MystralRT] Using Vulkan RT backend" << std::endl;
            return vulkan;
        }
        std::cout << "[MystralRT] Vulkan RT initialization failed, trying fallback..." << std::endl;
    }
#endif

    // TODO: Add DXR backend for Windows
    // #ifdef MYSTRAL_HAS_DXR
    //     auto dxr = std::make_unique<DXRRTBackend>();
    //     if (dxr->initialize()) {
    //         return dxr;
    //     }
    // #endif

    // TODO: Add Metal RT backend for Apple Silicon
    // #ifdef MYSTRAL_HAS_METAL_RT
    //     auto metal = std::make_unique<MetalRTBackend>();
    //     if (metal->initialize()) {
    //         return metal;
    //     }
    // #endif

    // Fallback to stub backend
    std::cout << "[MystralRT] No hardware RT available, using stub backend" << std::endl;
    return std::make_unique<StubRTBackend>();
}

}  // namespace rt
}  // namespace mystral
