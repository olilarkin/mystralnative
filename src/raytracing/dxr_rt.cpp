/**
 * MystralNative DirectX Raytracing (DXR) Backend Implementation
 *
 * This file implements hardware-accelerated ray tracing using DirectX 12's
 * DXR (DirectX Raytracing) API on Windows.
 *
 * Architecture Overview:
 * - DXRBackend manages D3D12 device, command queue, and RT resources
 * - Geometry is uploaded to GPU buffers and referenced by BLAS
 * - BLAS contains geometry in object space (can be reused/instanced)
 * - TLAS contains positioned instances of BLASes
 * - RT state object defines ray generation, miss, and closest hit shaders
 * - Shader Binding Table (SBT) maps shader records to shader programs
 *
 * Reference: Microsoft DXR specification
 * Reference: NVIDIA DXR tutorial series
 */

#ifdef _WIN32

#include "dxr_rt.h"
#include "shaders/rt_shaders_dxil.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <array>
#include <string>

// DXC compiler for runtime shader compilation
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

// D3D12 libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace mystral {
namespace rt {

// ============================================================================
// Helper Macros
// ============================================================================

#define ALIGN_UP(size, alignment) (((size) + (alignment) - 1) & ~((alignment) - 1))

// Shader record size must be aligned to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32)
static const uint32_t SHADER_RECORD_ALIGNMENT = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

// ============================================================================
// DXRBackend Implementation
// ============================================================================

DXRBackend::DXRBackend() = default;

DXRBackend::~DXRBackend() {
    if (!initialized_) return;

    // Wait for GPU to finish
    waitForGPU();

    // Clean up all tracked resources
    tlases_.clear();
    blases_.clear();
    geometries_.clear();

    // Clean up output resources
    outputTexture_.Reset();
    destroyBuffer(readbackBuffer_);
    destroyBuffer(cameraBuffer_);
    destroyBuffer(sbtBuffer_);

    // Clean up pipeline
    rtStateObjectProps_.Reset();
    rtStateObject_.Reset();
    globalRootSignature_.Reset();

    // Clean up descriptor heaps
    srvUavHeap_.Reset();

    // Clean up fence
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    fence_.Reset();

    // Clean up command objects
    commandList_.Reset();
    commandAllocator_.Reset();
    commandQueue_.Reset();

    // Clean up device and factory
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();

    std::cout << "[DXR] Backend cleaned up" << std::endl;
}

bool DXRBackend::initialize() {
    if (initialized_) return rtSupported_;

    std::cout << "[DXR] Initializing DirectX Raytracing backend..." << std::endl;

    // Step 1: Create D3D12 device with DXR support
    if (!createDevice()) {
        std::cerr << "[DXR] Failed to create D3D12 device" << std::endl;
        return false;
    }

    // Step 2: Create command queue
    if (!createCommandQueue()) {
        std::cerr << "[DXR] Failed to create command queue" << std::endl;
        return false;
    }

    // Step 3: Create command allocator and list
    if (!createCommandAllocatorAndList()) {
        std::cerr << "[DXR] Failed to create command allocator/list" << std::endl;
        return false;
    }

    // Step 4: Create fence for synchronization
    if (!createFence()) {
        std::cerr << "[DXR] Failed to create fence" << std::endl;
        return false;
    }

    // Step 5: Create descriptor heaps
    if (!createDescriptorHeaps()) {
        std::cerr << "[DXR] Failed to create descriptor heaps" << std::endl;
        return false;
    }

    // Step 6: Create RT pipeline state object
    if (!createRTPipeline()) {
        std::cerr << "[DXR] Failed to create RT pipeline" << std::endl;
        return false;
    }

    // Step 7: Create Shader Binding Table
    if (!createShaderBindingTable()) {
        std::cerr << "[DXR] Failed to create SBT" << std::endl;
        return false;
    }

    // Create camera uniform buffer (128 bytes for two 4x4 matrices)
    if (!createUploadBuffer(2 * 16 * sizeof(float), cameraBuffer_)) {
        std::cerr << "[DXR] Failed to create camera buffer" << std::endl;
        return false;
    }

    initialized_ = true;
    rtSupported_ = true;

    std::cout << "[DXR] DirectX Raytracing backend initialized successfully" << std::endl;
    return true;
}

// ============================================================================
// Capability Queries
// ============================================================================

bool DXRBackend::isSupported() {
    return initialized_ && rtSupported_;
}

RTBackendType DXRBackend::getBackendType() {
    return RTBackendType::DXR;
}

const char* DXRBackend::getBackend() {
    return "dxr";
}

// ============================================================================
// Device Creation
// ============================================================================

bool DXRBackend::createDevice() {
    HRESULT hr;

    // Enable debug layer in debug builds
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            std::cout << "[DXR] D3D12 debug layer enabled" << std::endl;
        }
    }
#endif

    // Create DXGI factory
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create DXGI factory" << std::endl;
        return false;
    }

    // Find adapter with DXR support
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Check if adapter supports D3D12
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                               IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            continue;
        }

        // Check for ray tracing support
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        hr = device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
        if (FAILED(hr) || options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            std::wcout << L"[DXR] Device " << desc.Description << L" does not support DXR" << std::endl;
            device_.Reset();
            continue;
        }

        rtTier_ = options5.RaytracingTier;
        adapter_ = adapter;

        std::wcout << L"[DXR] Selected device: " << desc.Description << std::endl;
        std::cout << "[DXR] Raytracing Tier: " << (rtTier_ == D3D12_RAYTRACING_TIER_1_0 ? "1.0" : "1.1") << std::endl;

        return true;
    }

    std::cerr << "[DXR] No DXR-capable GPU found" << std::endl;
    return false;
}

// ============================================================================
// Command Queue
// ============================================================================

bool DXRBackend::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    if (FAILED(hr)) {
        return false;
    }

    return true;
}

// ============================================================================
// Command Allocator and List
// ============================================================================

bool DXRBackend::createCommandAllocatorAndList() {
    HRESULT hr;

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&commandAllocator_));
    if (FAILED(hr)) {
        return false;
    }

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    commandAllocator_.Get(), nullptr,
                                    IID_PPV_ARGS(&commandList_));
    if (FAILED(hr)) {
        return false;
    }

    // Command lists start in recording state; close it for now
    commandList_->Close();

    return true;
}

// ============================================================================
// Fence
// ============================================================================

bool DXRBackend::createFence() {
    HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        return false;
    }

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        return false;
    }

    fenceValue_ = 1;
    return true;
}

// ============================================================================
// Descriptor Heaps
// ============================================================================

bool DXRBackend::createDescriptorHeaps() {
    // Create SRV/UAV heap for ray tracing resources
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 16;  // TLAS SRV, output UAV, camera CBV, etc.
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvUavHeap_));
    if (FAILED(hr)) {
        return false;
    }

    srvUavDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

// ============================================================================
// RT Pipeline Creation
// ============================================================================

bool DXRBackend::createRTPipeline() {
    HRESULT hr;

    // Create DXC compiler
    ComPtr<IDxcUtils> dxcUtils;
    ComPtr<IDxcCompiler3> dxcCompiler;

    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create DXC utils" << std::endl;
        return false;
    }

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create DXC compiler" << std::endl;
        return false;
    }

    // Compile the combined shader library
    ComPtr<IDxcBlobEncoding> sourceBlob;
    hr = dxcUtils->CreateBlob(
        dxr_shaders::combinedLibraryHLSL,
        static_cast<UINT32>(strlen(dxr_shaders::combinedLibraryHLSL)),
        CP_UTF8,
        &sourceBlob
    );
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create source blob" << std::endl;
        return false;
    }

    // Compile arguments
    LPCWSTR compileArgs[] = {
        L"-T", L"lib_6_3",  // Library target for ray tracing shaders
        L"-Zi",             // Enable debug info
    };

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = CP_UTF8;

    ComPtr<IDxcResult> compileResult;
    hr = dxcCompiler->Compile(
        &sourceBuffer,
        compileArgs,
        _countof(compileArgs),
        nullptr,  // Include handler
        IID_PPV_ARGS(&compileResult)
    );

    // Check compilation status
    HRESULT compileStatus;
    compileResult->GetStatus(&compileStatus);
    if (FAILED(compileStatus)) {
        ComPtr<IDxcBlobUtf8> errors;
        compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            std::cerr << "[DXR] Shader compilation failed: " << errors->GetStringPointer() << std::endl;
        }
        return false;
    }

    ComPtr<IDxcBlob> shaderBlob;
    hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to get compiled shader" << std::endl;
        return false;
    }

    std::cout << "[DXR] Shader library compiled successfully (" << shaderBlob->GetBufferSize() << " bytes)" << std::endl;

    // Create global root signature
    // Layout:
    // - slot 0: SRV (TLAS)
    // - slot 1: UAV (output texture)
    // - slot 2: CBV (camera params)
    D3D12_DESCRIPTOR_RANGE1 ranges[3] = {};

    // TLAS SRV at t0
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    // Output UAV at u0
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;

    // Camera CBV at b0
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].RegisterSpace = 0;
    ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[2].OffsetInDescriptorsFromTableStart = 2;

    D3D12_ROOT_PARAMETER1 rootParams[1] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
    rootParams[0].DescriptorTable.pDescriptorRanges = ranges;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 1;
    rootSigDesc.Desc_1_1.pParameters = rootParams;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &rootSigBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "[DXR] Root signature serialization failed: "
                      << static_cast<const char*>(errorBlob->GetBufferPointer()) << std::endl;
        }
        return false;
    }

    hr = device_->CreateRootSignature(0, rootSigBlob->GetBufferPointer(),
                                      rootSigBlob->GetBufferSize(),
                                      IID_PPV_ARGS(&globalRootSignature_));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create root signature" << std::endl;
        return false;
    }

    // Create RT state object
    // We use CD3DX12_STATE_OBJECT_DESC-style construction manually
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(10);

    // 1. DXIL library subobject
    D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
    dxilLibDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
    dxilLibDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
    dxilLibDesc.NumExports = 0;  // Export all shaders

    D3D12_STATE_SUBOBJECT dxilLibSubobject = {};
    dxilLibSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    dxilLibSubobject.pDesc = &dxilLibDesc;
    subobjects.push_back(dxilLibSubobject);

    // 2. Hit group
    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.HitGroupExport = dxr_shaders::hitGroupName;
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = dxr_shaders::closestHitEntryPoint;
    hitGroupDesc.AnyHitShaderImport = nullptr;
    hitGroupDesc.IntersectionShaderImport = nullptr;

    D3D12_STATE_SUBOBJECT hitGroupSubobject = {};
    hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroupSubobject.pDesc = &hitGroupDesc;
    subobjects.push_back(hitGroupSubobject);

    // 3. Shader config (payload and attribute sizes)
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 3;  // RayPayload: float3 color
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;  // Barycentric coords

    D3D12_STATE_SUBOBJECT shaderConfigSubobject = {};
    shaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigSubobject.pDesc = &shaderConfig;
    subobjects.push_back(shaderConfigSubobject);

    // 4. Global root signature
    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};
    globalRootSigDesc.pGlobalRootSignature = globalRootSignature_.Get();

    D3D12_STATE_SUBOBJECT globalRootSigSubobject = {};
    globalRootSigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    globalRootSigSubobject.pDesc = &globalRootSigDesc;
    subobjects.push_back(globalRootSigSubobject);

    // 5. Pipeline config (max recursion depth)
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;  // Primary rays only

    D3D12_STATE_SUBOBJECT pipelineConfigSubobject = {};
    pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigSubobject.pDesc = &pipelineConfig;
    subobjects.push_back(pipelineConfigSubobject);

    // Create state object
    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
    stateObjectDesc.pSubobjects = subobjects.data();

    hr = device_->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&rtStateObject_));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to create RT state object" << std::endl;
        return false;
    }

    // Get state object properties for shader identifier retrieval
    hr = rtStateObject_.As(&rtStateObjectProps_);
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to query state object properties" << std::endl;
        return false;
    }

    std::cout << "[DXR] RT pipeline created successfully" << std::endl;
    return true;
}

// ============================================================================
// Shader Binding Table
// ============================================================================

bool DXRBackend::createShaderBindingTable() {
    // Shader identifier size is fixed at D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES (32)
    const uint32_t identifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    // Each shader record is: identifier + local root arguments (none in our case)
    // Aligned to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32 bytes)
    shaderRecordSize_ = ALIGN_UP(identifierSize, SHADER_RECORD_ALIGNMENT);

    // SBT layout: [raygen] | [miss] | [hit group]
    const uint32_t sbtSize = shaderRecordSize_ * 3;

    // Create upload buffer for SBT
    if (!createUploadBuffer(sbtSize, sbtBuffer_)) {
        std::cerr << "[DXR] Failed to create SBT buffer" << std::endl;
        return false;
    }

    // Map and fill SBT
    uint8_t* sbtData = nullptr;
    D3D12_RANGE readRange = {0, 0};  // We're not reading
    HRESULT hr = sbtBuffer_.resource->Map(0, &readRange, reinterpret_cast<void**>(&sbtData));
    if (FAILED(hr)) {
        std::cerr << "[DXR] Failed to map SBT buffer" << std::endl;
        return false;
    }

    // Get shader identifiers
    void* raygenId = rtStateObjectProps_->GetShaderIdentifier(dxr_shaders::raygenEntryPoint);
    void* missId = rtStateObjectProps_->GetShaderIdentifier(dxr_shaders::missEntryPoint);
    void* hitGroupId = rtStateObjectProps_->GetShaderIdentifier(dxr_shaders::hitGroupName);

    if (!raygenId || !missId || !hitGroupId) {
        std::cerr << "[DXR] Failed to get shader identifiers" << std::endl;
        sbtBuffer_.resource->Unmap(0, nullptr);
        return false;
    }

    // Copy shader identifiers to SBT
    memcpy(sbtData + 0 * shaderRecordSize_, raygenId, identifierSize);
    memcpy(sbtData + 1 * shaderRecordSize_, missId, identifierSize);
    memcpy(sbtData + 2 * shaderRecordSize_, hitGroupId, identifierSize);

    sbtBuffer_.resource->Unmap(0, nullptr);

    // Store GPU addresses for DispatchRays
    raygenShaderRecord_ = sbtBuffer_.gpuAddress + 0 * shaderRecordSize_;
    missShaderRecord_ = sbtBuffer_.gpuAddress + 1 * shaderRecordSize_;
    hitGroupShaderRecord_ = sbtBuffer_.gpuAddress + 2 * shaderRecordSize_;

    std::cout << "[DXR] Shader Binding Table created successfully" << std::endl;
    return true;
}

// ============================================================================
// Buffer Management
// ============================================================================

bool DXRBackend::createBuffer(size_t size, D3D12_HEAP_TYPE heapType,
                               D3D12_RESOURCE_STATES initialState,
                               D3D12_RESOURCE_FLAGS flags, DXRBuffer& buffer) {
    buffer.size = size;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = flags;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&buffer.resource)
    );

    if (FAILED(hr)) {
        return false;
    }

    buffer.gpuAddress = buffer.resource->GetGPUVirtualAddress();
    return true;
}

bool DXRBackend::createUploadBuffer(size_t size, DXRBuffer& buffer) {
    return createBuffer(size, D3D12_HEAP_TYPE_UPLOAD,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        D3D12_RESOURCE_FLAG_NONE, buffer);
}

bool DXRBackend::createUAVBuffer(size_t size, D3D12_RESOURCE_STATES initialState,
                                  D3D12_RESOURCE_FLAGS flags, DXRBuffer& buffer) {
    return createBuffer(size, D3D12_HEAP_TYPE_DEFAULT,
                        initialState, flags, buffer);
}

void DXRBackend::destroyBuffer(DXRBuffer& buffer) {
    buffer.resource.Reset();
    buffer.gpuAddress = 0;
    buffer.size = 0;
    buffer.mappedPtr = nullptr;
}

// ============================================================================
// Command Buffer Helpers
// ============================================================================

void DXRBackend::resetCommandList() {
    commandAllocator_->Reset();
    commandList_->Reset(commandAllocator_.Get(), nullptr);
}

void DXRBackend::executeCommandList() {
    commandList_->Close();

    ID3D12CommandList* cmdLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, cmdLists);
}

void DXRBackend::waitForGPU() {
    const uint64_t fence = fenceValue_++;
    commandQueue_->Signal(fence_.Get(), fence);

    if (fence_->GetCompletedValue() < fence) {
        fence_->SetEventOnCompletion(fence, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

// ============================================================================
// Geometry Creation
// ============================================================================

RTGeometryHandle DXRBackend::createGeometry(const RTGeometryDesc& desc) {
    if (!initialized_) {
        std::cerr << "[DXR] createGeometry: Not initialized" << std::endl;
        return RTGeometryHandle{};
    }

    auto geometry = std::make_unique<DXRGeometry>();
    geometry->vertexCount = static_cast<uint32_t>(desc.vertexCount);
    geometry->indexCount = static_cast<uint32_t>(desc.indexCount);
    geometry->vertexStride = desc.vertexStride;

    // Create vertex buffer
    size_t vertexBufferSize = desc.vertexCount * desc.vertexStride;
    if (!createUploadBuffer(vertexBufferSize, geometry->vertexBuffer)) {
        std::cerr << "[DXR] createGeometry: Failed to create vertex buffer" << std::endl;
        return RTGeometryHandle{};
    }

    // Copy vertex data
    void* mappedData;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(geometry->vertexBuffer.resource->Map(0, &readRange, &mappedData))) {
        memcpy(mappedData, desc.vertices, vertexBufferSize);
        geometry->vertexBuffer.resource->Unmap(0, nullptr);
    }

    // Create index buffer if indexed
    if (desc.indices && desc.indexCount > 0) {
        size_t indexBufferSize = desc.indexCount * sizeof(uint32_t);
        if (!createUploadBuffer(indexBufferSize, geometry->indexBuffer)) {
            std::cerr << "[DXR] createGeometry: Failed to create index buffer" << std::endl;
            return RTGeometryHandle{};
        }

        if (SUCCEEDED(geometry->indexBuffer.resource->Map(0, &readRange, &mappedData))) {
            memcpy(mappedData, desc.indices, indexBufferSize);
            geometry->indexBuffer.resource->Unmap(0, nullptr);
        }
    }

    uint32_t id = nextGeometryId_++;
    geometries_[id] = std::move(geometry);

    RTGeometryHandle handle;
    handle._handle = geometries_[id].get();
    handle._id = id;
    return handle;
}

void DXRBackend::destroyGeometry(RTGeometryHandle geometry) {
    auto it = geometries_.find(geometry._id);
    if (it != geometries_.end()) {
        geometries_.erase(it);
    }
}

// ============================================================================
// BLAS Creation
// ============================================================================

RTBLASHandle DXRBackend::createBLAS(RTGeometryHandle* geometries, size_t count) {
    if (!initialized_ || count == 0) {
        std::cerr << "[DXR] createBLAS: Not initialized or empty" << std::endl;
        return RTBLASHandle{};
    }

    auto blas = std::make_unique<DXRBLAS>();

    // Build geometry descriptions
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs;
    geomDescs.reserve(count);

    for (size_t i = 0; i < count; i++) {
        DXRGeometry* geom = static_cast<DXRGeometry*>(geometries[i]._handle);
        if (!geom) continue;

        blas->geometryIds.push_back(geometries[i]._id);

        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
        geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        geomDesc.Triangles.VertexBuffer.StartAddress = geom->vertexBuffer.gpuAddress;
        geomDesc.Triangles.VertexBuffer.StrideInBytes = geom->vertexStride;
        geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geomDesc.Triangles.VertexCount = geom->vertexCount;

        if (geom->indexCount > 0) {
            geomDesc.Triangles.IndexBuffer = geom->indexBuffer.gpuAddress;
            geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            geomDesc.Triangles.IndexCount = geom->indexCount;
        }

        geomDescs.push_back(geomDesc);
    }

    // Get prebuild info
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = static_cast<UINT>(geomDescs.size());
    inputs.pGeometryDescs = geomDescs.data();
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    // Create scratch buffer
    DXRBuffer scratchBuffer;
    if (!createUAVBuffer(prebuildInfo.ScratchDataSizeInBytes,
                         D3D12_RESOURCE_STATE_COMMON,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         scratchBuffer)) {
        std::cerr << "[DXR] createBLAS: Failed to create scratch buffer" << std::endl;
        return RTBLASHandle{};
    }

    // Create BLAS buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        IID_PPV_ARGS(&blas->accelerationStructure)
    );

    if (FAILED(hr)) {
        std::cerr << "[DXR] createBLAS: Failed to create AS buffer" << std::endl;
        return RTBLASHandle{};
    }

    blas->gpuAddress = blas->accelerationStructure->GetGPUVirtualAddress();

    // Build BLAS
    resetCommandList();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = blas->gpuAddress;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.gpuAddress;

    commandList_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // Add barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = blas->accelerationStructure.Get();
    commandList_->ResourceBarrier(1, &barrier);

    executeCommandList();
    waitForGPU();

    // Clean up scratch buffer
    destroyBuffer(scratchBuffer);

    uint32_t id = nextBLASId_++;
    blases_[id] = std::move(blas);

    RTBLASHandle handle;
    handle._handle = blases_[id].get();
    handle._id = id;
    return handle;
}

void DXRBackend::destroyBLAS(RTBLASHandle blas) {
    auto it = blases_.find(blas._id);
    if (it != blases_.end()) {
        blases_.erase(it);
    }
}

// ============================================================================
// TLAS Creation
// ============================================================================

RTTLASHandle DXRBackend::createTLAS(const RTTLASInstance* instances, size_t count) {
    if (!initialized_ || count == 0) {
        std::cerr << "[DXR] createTLAS: Not initialized or empty" << std::endl;
        return RTTLASHandle{};
    }

    auto tlas = std::make_unique<DXRTLAS>();
    tlas->instanceCount = static_cast<uint32_t>(count);

    // Build D3D12_RAYTRACING_INSTANCE_DESC array
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(count);

    for (size_t i = 0; i < count; i++) {
        const RTTLASInstance& inst = instances[i];
        DXRBLAS* blasPtr = static_cast<DXRBLAS*>(inst.blas._handle);
        if (!blasPtr) {
            std::cerr << "[DXR] createTLAS: Invalid BLAS at instance " << i << std::endl;
            return RTTLASHandle{};
        }

        D3D12_RAYTRACING_INSTANCE_DESC& desc = instanceDescs[i];

        // Convert 4x4 column-major to 3x4 row-major transform
        // Input is column-major 4x4: [m0 m4 m8  m12]
        //                           [m1 m5 m9  m13]
        //                           [m2 m6 m10 m14]
        //                           [m3 m7 m11 m15]
        // Output is row-major 3x4: Transform[row][col]
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                desc.Transform[row][col] = inst.transform[col * 4 + row];
            }
        }

        desc.InstanceID = inst.instanceId & 0xFFFFFF;  // 24-bit
        desc.InstanceMask = inst.mask;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        desc.AccelerationStructure = blasPtr->gpuAddress;
    }

    // Create instance buffer
    size_t instanceBufferSize = count * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    if (!createUploadBuffer(instanceBufferSize, tlas->instanceBuffer)) {
        std::cerr << "[DXR] createTLAS: Failed to create instance buffer" << std::endl;
        return RTTLASHandle{};
    }

    // Copy instance data
    void* mappedData;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(tlas->instanceBuffer.resource->Map(0, &readRange, &mappedData))) {
        memcpy(mappedData, instanceDescs.data(), instanceBufferSize);
        tlas->instanceBuffer.resource->Unmap(0, nullptr);
    }

    // Get prebuild info
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = static_cast<UINT>(count);
    inputs.InstanceDescs = tlas->instanceBuffer.gpuAddress;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    // Create scratch buffer
    DXRBuffer scratchBuffer;
    if (!createUAVBuffer(prebuildInfo.ScratchDataSizeInBytes,
                         D3D12_RESOURCE_STATE_COMMON,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                         scratchBuffer)) {
        std::cerr << "[DXR] createTLAS: Failed to create scratch buffer" << std::endl;
        return RTTLASHandle{};
    }

    // Create TLAS buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        IID_PPV_ARGS(&tlas->accelerationStructure)
    );

    if (FAILED(hr)) {
        std::cerr << "[DXR] createTLAS: Failed to create AS buffer" << std::endl;
        return RTTLASHandle{};
    }

    tlas->gpuAddress = tlas->accelerationStructure->GetGPUVirtualAddress();

    // Build TLAS
    resetCommandList();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = tlas->gpuAddress;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.gpuAddress;

    commandList_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // Add barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlas->accelerationStructure.Get();
    commandList_->ResourceBarrier(1, &barrier);

    executeCommandList();
    waitForGPU();

    // Clean up scratch buffer
    destroyBuffer(scratchBuffer);

    uint32_t id = nextTLASId_++;
    tlases_[id] = std::move(tlas);

    RTTLASHandle handle;
    handle._handle = tlases_[id].get();
    handle._id = id;
    return handle;
}

void DXRBackend::updateTLAS(RTTLASHandle tlas, const RTTLASInstance* instances, size_t count) {
    auto it = tlases_.find(tlas._id);
    if (it == tlases_.end()) {
        std::cerr << "[DXR] updateTLAS: Invalid TLAS" << std::endl;
        return;
    }

    DXRTLAS* tlasPtr = it->second.get();
    if (count != tlasPtr->instanceCount) {
        std::cerr << "[DXR] updateTLAS: Instance count mismatch" << std::endl;
        return;
    }

    // Update instance buffer with new transforms
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(count);

    for (size_t i = 0; i < count; i++) {
        const RTTLASInstance& inst = instances[i];
        DXRBLAS* blasPtr = static_cast<DXRBLAS*>(inst.blas._handle);
        if (!blasPtr) continue;

        D3D12_RAYTRACING_INSTANCE_DESC& desc = instanceDescs[i];

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                desc.Transform[row][col] = inst.transform[col * 4 + row];
            }
        }

        desc.InstanceID = inst.instanceId & 0xFFFFFF;
        desc.InstanceMask = inst.mask;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        desc.AccelerationStructure = blasPtr->gpuAddress;
    }

    // Copy to instance buffer
    void* mappedData;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(tlasPtr->instanceBuffer.resource->Map(0, &readRange, &mappedData))) {
        memcpy(mappedData, instanceDescs.data(), count * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
        tlasPtr->instanceBuffer.resource->Unmap(0, nullptr);
    }

    // Get prebuild info for update
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = static_cast<UINT>(count);
    inputs.InstanceDescs = tlasPtr->instanceBuffer.gpuAddress;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE |
                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    // Create scratch buffer for update
    DXRBuffer scratchBuffer;
    createUAVBuffer(prebuildInfo.UpdateScratchDataSizeInBytes,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    scratchBuffer);

    // Update TLAS
    resetCommandList();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.SourceAccelerationStructureData = tlasPtr->gpuAddress;
    buildDesc.DestAccelerationStructureData = tlasPtr->gpuAddress;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.gpuAddress;

    commandList_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlasPtr->accelerationStructure.Get();
    commandList_->ResourceBarrier(1, &barrier);

    executeCommandList();
    waitForGPU();

    destroyBuffer(scratchBuffer);
}

void DXRBackend::destroyTLAS(RTTLASHandle tlas) {
    auto it = tlases_.find(tlas._id);
    if (it != tlases_.end()) {
        tlases_.erase(it);
    }
}

// ============================================================================
// Ray Tracing Execution
// ============================================================================

void DXRBackend::traceRays(const TraceRaysOptions& options) {
    if (!initialized_) {
        std::cerr << "[DXR] traceRays: Not initialized" << std::endl;
        return;
    }

    DXRTLAS* tlasPtr = static_cast<DXRTLAS*>(options.tlas._handle);
    if (!tlasPtr) {
        std::cerr << "[DXR] traceRays: Invalid TLAS" << std::endl;
        return;
    }

    // Recreate output texture if size changed
    if (options.width != outputWidth_ || options.height != outputHeight_) {
        outputTexture_.Reset();
        destroyBuffer(readbackBuffer_);

        outputWidth_ = options.width;
        outputHeight_ = options.height;

        // Create output texture (UAV)
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = options.width;
        texDesc.Height = options.height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&outputTexture_)
        );

        // Create readback buffer
        size_t readbackSize = options.width * options.height * 4;
        D3D12_HEAP_PROPERTIES readbackHeapProps = {};
        readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC readbackDesc = {};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = readbackSize;
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        device_->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readbackBuffer_.resource)
        );
        readbackBuffer_.size = readbackSize;
    }

    // Update camera UBO if uniforms provided
    if (options.uniforms && options.uniformsSize > 0) {
        void* mappedData;
        D3D12_RANGE readRange = {0, 0};
        if (SUCCEEDED(cameraBuffer_.resource->Map(0, &readRange, &mappedData))) {
            memcpy(mappedData, options.uniforms, options.uniformsSize);
            cameraBuffer_.resource->Unmap(0, nullptr);
        }
    }

    // Update descriptor heap
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();

    // Descriptor 0: TLAS SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = tlasPtr->gpuAddress;
    device_->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);

    // Descriptor 1: Output UAV
    cpuHandle.ptr += srvUavDescriptorSize_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(outputTexture_.Get(), nullptr, &uavDesc, cpuHandle);

    // Descriptor 2: Camera CBV
    cpuHandle.ptr += srvUavDescriptorSize_;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = cameraBuffer_.gpuAddress;
    cbvDesc.SizeInBytes = static_cast<UINT>(ALIGN_UP(cameraBuffer_.size, 256));  // CBV size must be 256-aligned
    device_->CreateConstantBufferView(&cbvDesc, cpuHandle);

    // Record and dispatch rays
    resetCommandList();

    commandList_->SetComputeRootSignature(globalRootSignature_.Get());
    commandList_->SetPipelineState1(rtStateObject_.Get());

    ID3D12DescriptorHeap* heaps[] = {srvUavHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetComputeRootDescriptorTable(0, srvUavHeap_->GetGPUDescriptorHandleForHeapStart());

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

    // Ray generation shader
    dispatchDesc.RayGenerationShaderRecord.StartAddress = raygenShaderRecord_;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = shaderRecordSize_;

    // Miss shader
    dispatchDesc.MissShaderTable.StartAddress = missShaderRecord_;
    dispatchDesc.MissShaderTable.SizeInBytes = shaderRecordSize_;
    dispatchDesc.MissShaderTable.StrideInBytes = shaderRecordSize_;

    // Hit group
    dispatchDesc.HitGroupTable.StartAddress = hitGroupShaderRecord_;
    dispatchDesc.HitGroupTable.SizeInBytes = shaderRecordSize_;
    dispatchDesc.HitGroupTable.StrideInBytes = shaderRecordSize_;

    // Dispatch dimensions
    dispatchDesc.Width = options.width;
    dispatchDesc.Height = options.height;
    dispatchDesc.Depth = 1;

    commandList_->DispatchRays(&dispatchDesc);

    // Copy output to readback buffer for WebGPU texture interop
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outputTexture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList_->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = outputTexture_.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readbackBuffer_.resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint.Offset = 0;
    dstLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dstLoc.PlacedFootprint.Footprint.Width = options.width;
    dstLoc.PlacedFootprint.Footprint.Height = options.height;
    dstLoc.PlacedFootprint.Footprint.Depth = 1;
    dstLoc.PlacedFootprint.Footprint.RowPitch = ALIGN_UP(options.width * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

    commandList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition back to UAV for next frame
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList_->ResourceBarrier(1, &barrier);

    executeCommandList();
    waitForGPU();

    // TODO: Copy readback buffer data to WebGPU texture
    // This requires WebGPU/D3D12 interop which will be implemented in the next phase
    // For now, the data is in readbackBuffer_ and can be read
}

}  // namespace rt
}  // namespace mystral

#endif  // _WIN32
