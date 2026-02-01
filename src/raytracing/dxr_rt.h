/**
 * MystralNative DirectX Raytracing (DXR) Backend
 *
 * Implements hardware-accelerated ray tracing using DirectX 12's
 * DXR (DirectX Raytracing) API on Windows.
 *
 * Reference: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
 * Tutorial: https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-1
 *
 * Build Requirements:
 * - Windows 10 1809+ (October 2018 Update) or Windows 11
 * - GPU with DXR support (NVIDIA RTX, AMD RDNA2+, Intel Arc)
 * - DirectX 12 Ultimate for best performance
 */

#pragma once

#include "rt_common.h"

#ifdef _WIN32

// Prevent Windows.h from defining min/max macros
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>
#include <unordered_map>
#include <memory>

// Use Microsoft::WRL::ComPtr for COM object management
using Microsoft::WRL::ComPtr;

namespace mystral {
namespace rt {

// Forward declarations
struct DXRGeometry;
struct DXRBLAS;
struct DXRTLAS;
struct DXRBuffer;

/**
 * D3D12 buffer wrapper with resource and GPU virtual address.
 * Used for vertex/index buffers, acceleration structures, and SBT.
 */
struct DXRBuffer {
    ComPtr<ID3D12Resource> resource;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    size_t size = 0;
    void* mappedPtr = nullptr;
};

/**
 * Geometry data stored in D3D12 buffers.
 */
struct DXRGeometry {
    DXRBuffer vertexBuffer;
    DXRBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    size_t vertexStride = 12;  // Default: vec3 position
};

/**
 * Bottom-Level Acceleration Structure.
 * Contains geometry in object space.
 */
struct DXRBLAS {
    ComPtr<ID3D12Resource> accelerationStructure;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    std::vector<uint32_t> geometryIds;  // Associated geometry IDs
};

/**
 * Top-Level Acceleration Structure.
 * Contains positioned instances of BLASes.
 */
struct DXRTLAS {
    ComPtr<ID3D12Resource> accelerationStructure;
    DXRBuffer instanceBuffer;  // D3D12_RAYTRACING_INSTANCE_DESC array
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    uint32_t instanceCount = 0;
};

/**
 * DirectX Raytracing Backend.
 *
 * Implements the IRTBackend interface using DirectX 12 DXR.
 * Manages D3D12 device, command queues, and RT state objects.
 */
class DXRBackend : public IRTBackend {
public:
    DXRBackend();
    ~DXRBackend() override;

    /**
     * Initialize D3D12 device, command queue, and ray tracing state.
     * @return true if DXR is available and initialized
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

    bool createDevice();
    bool createCommandQueue();
    bool createCommandAllocatorAndList();
    bool createFence();
    bool createDescriptorHeaps();
    bool createRTPipeline();
    bool createShaderBindingTable();

    // ========================================================================
    // Buffer Management
    // ========================================================================

    bool createBuffer(size_t size, D3D12_HEAP_TYPE heapType,
                      D3D12_RESOURCE_STATES initialState,
                      D3D12_RESOURCE_FLAGS flags, DXRBuffer& buffer);
    bool createUploadBuffer(size_t size, DXRBuffer& buffer);
    bool createUAVBuffer(size_t size, D3D12_RESOURCE_STATES initialState,
                         D3D12_RESOURCE_FLAGS flags, DXRBuffer& buffer);
    void destroyBuffer(DXRBuffer& buffer);

    // ========================================================================
    // Command Buffer Helpers
    // ========================================================================

    void resetCommandList();
    void executeCommandList();
    void waitForGPU();

    // ========================================================================
    // Shader Helpers
    // ========================================================================

    ComPtr<ID3DBlob> compileShader(const char* source, const char* target,
                                   const char* entryPoint);

    // ========================================================================
    // D3D12 Core Objects
    // ========================================================================

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device5> device_;  // Device5 required for DXR
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<ID3D12CommandAllocator> commandAllocator_;
    ComPtr<ID3D12GraphicsCommandList4> commandList_;  // CommandList4 for DXR

    // ========================================================================
    // Synchronization
    // ========================================================================

    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;

    // ========================================================================
    // Descriptor Heaps
    // ========================================================================

    ComPtr<ID3D12DescriptorHeap> srvUavHeap_;  // SRV/UAV heap for RT resources
    uint32_t srvUavDescriptorSize_ = 0;

    // ========================================================================
    // RT Pipeline State
    // ========================================================================

    ComPtr<ID3D12StateObject> rtStateObject_;
    ComPtr<ID3D12StateObjectProperties> rtStateObjectProps_;
    ComPtr<ID3D12RootSignature> globalRootSignature_;

    // ========================================================================
    // Shader Binding Table
    // ========================================================================

    DXRBuffer sbtBuffer_;
    D3D12_GPU_VIRTUAL_ADDRESS raygenShaderRecord_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS missShaderRecord_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS hitGroupShaderRecord_ = 0;
    uint32_t shaderRecordSize_ = 0;

    // ========================================================================
    // Output Resources
    // ========================================================================

    ComPtr<ID3D12Resource> outputTexture_;
    DXRBuffer readbackBuffer_;
    uint32_t outputWidth_ = 0;
    uint32_t outputHeight_ = 0;

    // ========================================================================
    // Camera Uniform Buffer
    // ========================================================================

    DXRBuffer cameraBuffer_;

    // ========================================================================
    // Device Properties
    // ========================================================================

    D3D12_RAYTRACING_TIER rtTier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

    // ========================================================================
    // Resource Tracking
    // ========================================================================

    std::unordered_map<uint32_t, std::unique_ptr<DXRGeometry>> geometries_;
    std::unordered_map<uint32_t, std::unique_ptr<DXRBLAS>> blases_;
    std::unordered_map<uint32_t, std::unique_ptr<DXRTLAS>> tlases_;
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

#endif  // _WIN32
