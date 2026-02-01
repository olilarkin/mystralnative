/**
 * MystralNative Metal Ray Tracing Backend Implementation
 *
 * This file implements hardware-accelerated ray tracing using Metal's
 * ray tracing API introduced in Metal 3 for Apple Silicon.
 *
 * Architecture Overview:
 * - MetalRTBackend manages Metal device and ray tracing resources
 * - Geometry is uploaded to shared Metal buffers and referenced by BLAS
 * - BLAS (MTLPrimitiveAccelerationStructure) contains geometry in object space
 * - TLAS (MTLInstanceAccelerationStructure) contains positioned instances
 * - Compute pipeline with intersection functions traces rays
 *
 * Reference: https://developer.apple.com/documentation/metal/ray_tracing
 * Reference: Metal Performance Shaders Ray Tracing documentation
 */

#import <Metal/Metal.h>
#import <simd/simd.h>
#include "metal_rt.h"
#include <iostream>
#include <cstring>

namespace mystral {
namespace rt {

// ============================================================================
// Camera Uniform Structure (matches shader)
// ============================================================================

struct CameraUniforms {
    simd_float4x4 viewInverse;
    simd_float4x4 projInverse;
};

// ============================================================================
// Embedded Metal Shader Source
// ============================================================================

// The ray tracing compute shader is embedded as a string for simplicity.
// In production, this could be loaded from a .metallib or compiled at runtime.
static const char* kMetalRTShaderSource = R"(
#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

// Camera uniforms - inverse view and projection matrices
struct CameraUniforms {
    float4x4 viewInverse;
    float4x4 projInverse;
};

// Primary ray tracing kernel
[[kernel]]
void raytracingKernel(
    uint2 tid [[thread_position_in_grid]],
    constant uint2& outputSize [[buffer(0)]],
    device float4* output [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    instance_acceleration_structure tlas [[buffer(3)]]
) {
    // Skip if outside output dimensions
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) {
        return;
    }

    // Calculate normalized device coordinates [-1, 1]
    float2 pixelCenter = float2(tid) + 0.5;
    float2 uv = pixelCenter / float2(outputSize) * 2.0 - 1.0;
    uv.y = -uv.y;  // Flip Y for Metal's coordinate system

    // Generate ray from camera
    float4 origin = camera.viewInverse * float4(0, 0, 0, 1);
    float4 target = camera.projInverse * float4(uv, 1, 1);
    float4 direction = camera.viewInverse * float4(normalize(target.xyz), 0);

    // Create ray
    ray r;
    r.origin = origin.xyz;
    r.direction = direction.xyz;
    r.min_distance = 0.001;
    r.max_distance = 10000.0;

    // Create intersector
    intersector<triangle_data, instancing> inter;
    inter.accept_any_intersection(false);  // Find closest hit

    // Trace ray against acceleration structure
    intersection_result<triangle_data, instancing> result = inter.intersect(r, tlas);

    float3 color;
    if (result.type == intersection_type::triangle) {
        // Hit - visualize barycentric coordinates as color
        float2 bary = result.triangle_barycentric_coord;
        color = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
    } else {
        // Miss - draw sky gradient
        float t = 0.5 * (r.direction.y + 1.0);
        color = mix(float3(1.0, 1.0, 1.0), float3(0.5, 0.7, 1.0), t);
    }

    // Write output
    uint idx = tid.y * outputSize.x + tid.x;
    output[idx] = float4(color, 1.0);
}
)";

// ============================================================================
// MetalRTBackend Implementation
// ============================================================================

MetalRTBackend::MetalRTBackend() = default;

MetalRTBackend::~MetalRTBackend() {
    if (!initialized_) return;

    @autoreleasepool {
        // Wait for GPU to finish
        if (commandQueue_) {
            id<MTLCommandBuffer> cmdBuf = [(id<MTLCommandQueue>)commandQueue_ commandBuffer];
            [cmdBuf commit];
            [cmdBuf waitUntilCompleted];
        }

        // Clean up all tracked resources
        for (auto& [id, tlas] : tlases_) {
            if (tlas) {
                tlas->accelerationStructure = nil;
                destroyBuffer(tlas->instanceBuffer);
            }
        }
        tlases_.clear();

        for (auto& [id, blas] : blases_) {
            if (blas) {
                blas->accelerationStructure = nil;
            }
        }
        blases_.clear();

        for (auto& [id, geom] : geometries_) {
            if (geom) {
                destroyBuffer(geom->vertexBuffer);
                destroyBuffer(geom->indexBuffer);
            }
        }
        geometries_.clear();

        // Clean up output resources
        outputTexture_ = nil;
        destroyBuffer(cameraBuffer_);

        // Clean up pipeline and library
        rtPipeline_ = nil;
        shaderLibrary_ = nil;
        commandQueue_ = nil;
        device_ = nil;
    }

    std::cout << "[MetalRT] Backend cleaned up" << std::endl;
}

bool MetalRTBackend::initialize() {
    if (initialized_) return rtSupported_;

    @autoreleasepool {
        std::cout << "[MetalRT] Initializing Metal ray tracing backend..." << std::endl;

        // Step 1: Get Metal device
        if (!initDevice()) {
            std::cerr << "[MetalRT] Failed to get Metal device" << std::endl;
            return false;
        }

        // Step 2: Check ray tracing support
        if (!checkRayTracingSupport()) {
            std::cerr << "[MetalRT] Ray tracing not supported on this device" << std::endl;
            return false;
        }

        // Step 3: Create command queue
        if (!createCommandQueue()) {
            std::cerr << "[MetalRT] Failed to create command queue" << std::endl;
            return false;
        }

        // Step 4: Load shaders
        if (!loadShaders()) {
            std::cerr << "[MetalRT] Failed to load shaders" << std::endl;
            return false;
        }

        // Step 5: Create compute pipeline
        if (!createComputePipeline()) {
            std::cerr << "[MetalRT] Failed to create compute pipeline" << std::endl;
            return false;
        }

        // Step 6: Create camera uniform buffer
        if (!createBuffer(sizeof(CameraUniforms), cameraBuffer_, true)) {
            std::cerr << "[MetalRT] Failed to create camera buffer" << std::endl;
            return false;
        }

        initialized_ = true;
        rtSupported_ = true;

        std::cout << "[MetalRT] Metal ray tracing backend initialized successfully" << std::endl;
        return true;
    }
}

// ============================================================================
// Capability Queries
// ============================================================================

bool MetalRTBackend::isSupported() {
    return initialized_ && rtSupported_;
}

RTBackendType MetalRTBackend::getBackendType() {
    return RTBackendType::Metal;
}

const char* MetalRTBackend::getBackend() {
    return "metal";
}

// ============================================================================
// Device Initialization
// ============================================================================

bool MetalRTBackend::initDevice() {
    @autoreleasepool {
        // Get the default Metal device (system's GPU)
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "[MetalRT] No Metal device available" << std::endl;
            return false;
        }

        device_ = device;
        std::cout << "[MetalRT] Using device: " << [[device name] UTF8String] << std::endl;
        return true;
    }
}

bool MetalRTBackend::checkRayTracingSupport() {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;

        // Check if the device supports ray tracing
        // Ray tracing requires Apple Silicon (M1+) and macOS 13+/iOS 16+
        if (![device supportsRaytracing]) {
            std::cerr << "[MetalRT] Device does not support ray tracing" << std::endl;
            std::cerr << "[MetalRT] Ray tracing requires Apple Silicon (M1/M2/M3) and macOS 13+" << std::endl;
            return false;
        }

        // Check for required GPU family support
        // Apple7 (A14, M1) or later is required for full ray tracing
        if (@available(macOS 13.0, iOS 16.0, *)) {
            if (![device supportsFamily:MTLGPUFamilyApple7]) {
                std::cerr << "[MetalRT] Device GPU family does not support ray tracing" << std::endl;
                std::cerr << "[MetalRT] Apple7 GPU family (M1/A14 or later) required" << std::endl;
                return false;
            }
        } else {
            std::cerr << "[MetalRT] macOS 13+ or iOS 16+ required for ray tracing" << std::endl;
            return false;
        }

        std::cout << "[MetalRT] Ray tracing support confirmed" << std::endl;
        return true;
    }
}

bool MetalRTBackend::createCommandQueue() {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            std::cerr << "[MetalRT] Failed to create command queue" << std::endl;
            return false;
        }
        commandQueue_ = queue;
        return true;
    }
}

bool MetalRTBackend::loadShaders() {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;

        // Compile shader from embedded source
        NSError* error = nil;
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        options.languageVersion = MTLLanguageVersion3_0;

        NSString* source = [NSString stringWithUTF8String:kMetalRTShaderSource];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:options error:&error];

        if (!library) {
            std::cerr << "[MetalRT] Failed to compile shaders: "
                      << [[error localizedDescription] UTF8String] << std::endl;
            return false;
        }

        shaderLibrary_ = library;
        std::cout << "[MetalRT] Shaders compiled successfully" << std::endl;
        return true;
    }
}

bool MetalRTBackend::createComputePipeline() {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;
        id<MTLLibrary> library = (id<MTLLibrary>)shaderLibrary_;

        // Get the ray tracing kernel function
        id<MTLFunction> kernelFunc = [library newFunctionWithName:@"raytracingKernel"];
        if (!kernelFunc) {
            std::cerr << "[MetalRT] Failed to find raytracingKernel function" << std::endl;
            return false;
        }

        // Create compute pipeline state
        NSError* error = nil;
        MTLComputePipelineDescriptor* pipelineDesc = [[MTLComputePipelineDescriptor alloc] init];
        pipelineDesc.computeFunction = kernelFunc;
        pipelineDesc.threadGroupSizeIsMultipleOfThreadExecutionWidth = YES;

        // Configure linked functions for ray tracing (optional - for custom intersection)
        // For basic triangle intersection, Metal's built-in intersector is sufficient

        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithDescriptor:pipelineDesc
                                                                                     options:MTLPipelineOptionNone
                                                                                  reflection:nil
                                                                                       error:&error];
        if (!pipeline) {
            std::cerr << "[MetalRT] Failed to create compute pipeline: "
                      << [[error localizedDescription] UTF8String] << std::endl;
            return false;
        }

        rtPipeline_ = pipeline;
        std::cout << "[MetalRT] Compute pipeline created" << std::endl;
        return true;
    }
}

// ============================================================================
// Buffer Management
// ============================================================================

bool MetalRTBackend::createBuffer(size_t size, MetalBuffer& buffer, bool shared) {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;

        MTLResourceOptions options = shared
            ? MTLResourceStorageModeShared
            : MTLResourceStorageModePrivate;

        id<MTLBuffer> mtlBuffer = [device newBufferWithLength:size options:options];
        if (!mtlBuffer) {
            return false;
        }

        buffer.buffer = mtlBuffer;
        buffer.size = size;
        buffer.contents = shared ? [mtlBuffer contents] : nullptr;
        return true;
    }
}

void MetalRTBackend::destroyBuffer(MetalBuffer& buffer) {
    buffer.buffer = nil;
    buffer.size = 0;
    buffer.contents = nullptr;
}

// ============================================================================
// Acceleration Structure Building
// ============================================================================

bool MetalRTBackend::buildAccelerationStructure(id descriptor, id& outStructure) {
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (id<MTLCommandQueue>)commandQueue_;

        // Get sizes for acceleration structure
        MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:descriptor];

        // Create acceleration structure
        id<MTLAccelerationStructure> accelerationStructure =
            [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        if (!accelerationStructure) {
            std::cerr << "[MetalRT] Failed to create acceleration structure" << std::endl;
            return false;
        }

        // Create scratch buffer for building
        id<MTLBuffer> scratchBuffer = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                          options:MTLResourceStorageModePrivate];
        if (!scratchBuffer) {
            std::cerr << "[MetalRT] Failed to create scratch buffer" << std::endl;
            return false;
        }

        // Build acceleration structure
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLAccelerationStructureCommandEncoder> encoder =
            [commandBuffer accelerationStructureCommandEncoder];

        [encoder buildAccelerationStructure:accelerationStructure
                                 descriptor:descriptor
                              scratchBuffer:scratchBuffer
                        scratchBufferOffset:0];

        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if ([commandBuffer status] != MTLCommandBufferStatusCompleted) {
            std::cerr << "[MetalRT] Acceleration structure build failed" << std::endl;
            return false;
        }

        outStructure = accelerationStructure;
        return true;
    }
}

// ============================================================================
// Geometry Management
// ============================================================================

RTGeometryHandle MetalRTBackend::createGeometry(const RTGeometryDesc& desc) {
    if (!initialized_) {
        std::cerr << "[MetalRT] createGeometry: Backend not initialized" << std::endl;
        return RTGeometryHandle{};
    }

    @autoreleasepool {
        auto geometry = std::make_unique<MetalGeometry>();

        // Create vertex buffer
        size_t vertexSize = desc.vertexCount * desc.vertexStride;
        if (!createBuffer(vertexSize, geometry->vertexBuffer, true)) {
            std::cerr << "[MetalRT] Failed to create vertex buffer" << std::endl;
            return RTGeometryHandle{};
        }

        // Copy vertex data
        memcpy(geometry->vertexBuffer.contents, desc.vertices, vertexSize);
        geometry->vertexCount = static_cast<uint32_t>(desc.vertexCount);
        geometry->vertexStride = desc.vertexStride;

        // Create index buffer if provided
        if (desc.indices && desc.indexCount > 0) {
            size_t indexSize = desc.indexCount * sizeof(uint32_t);
            if (!createBuffer(indexSize, geometry->indexBuffer, true)) {
                std::cerr << "[MetalRT] Failed to create index buffer" << std::endl;
                destroyBuffer(geometry->vertexBuffer);
                return RTGeometryHandle{};
            }
            memcpy(geometry->indexBuffer.contents, desc.indices, indexSize);
            geometry->indexCount = static_cast<uint32_t>(desc.indexCount);
        }

        // Store geometry and return handle
        uint32_t id = nextGeometryId_++;
        geometries_[id] = std::move(geometry);

        RTGeometryHandle handle;
        handle._id = id;
        handle._handle = geometries_[id].get();
        return handle;
    }
}

void MetalRTBackend::destroyGeometry(RTGeometryHandle geometry) {
    if (geometry._id == 0) return;

    auto it = geometries_.find(geometry._id);
    if (it != geometries_.end()) {
        @autoreleasepool {
            destroyBuffer(it->second->vertexBuffer);
            destroyBuffer(it->second->indexBuffer);
        }
        geometries_.erase(it);
    }
}

// ============================================================================
// BLAS Management
// ============================================================================

RTBLASHandle MetalRTBackend::createBLAS(RTGeometryHandle* geometries, size_t count) {
    if (!initialized_ || count == 0) {
        std::cerr << "[MetalRT] createBLAS: Invalid parameters" << std::endl;
        return RTBLASHandle{};
    }

    @autoreleasepool {
        auto blas = std::make_unique<MetalBLAS>();

        // Collect geometry descriptors
        NSMutableArray<MTLAccelerationStructureTriangleGeometryDescriptor*>* geometryDescs =
            [NSMutableArray arrayWithCapacity:count];

        for (size_t i = 0; i < count; i++) {
            auto* geom = static_cast<MetalGeometry*>(geometries[i]._handle);
            if (!geom) {
                std::cerr << "[MetalRT] Invalid geometry handle at index " << i << std::endl;
                return RTBLASHandle{};
            }

            MTLAccelerationStructureTriangleGeometryDescriptor* desc =
                [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];

            desc.vertexBuffer = (id<MTLBuffer>)geom->vertexBuffer.buffer;
            desc.vertexBufferOffset = 0;
            desc.vertexStride = geom->vertexStride;

            if (geom->indexCount > 0) {
                desc.indexBuffer = (id<MTLBuffer>)geom->indexBuffer.buffer;
                desc.indexBufferOffset = 0;
                desc.indexType = MTLIndexTypeUInt32;
                desc.triangleCount = geom->indexCount / 3;
            } else {
                desc.triangleCount = geom->vertexCount / 3;
            }

            desc.opaque = YES;  // Assume opaque geometry for performance

            [geometryDescs addObject:desc];
            blas->geometryIds.push_back(geometries[i]._id);
        }

        // Create BLAS descriptor
        MTLPrimitiveAccelerationStructureDescriptor* blasDesc =
            [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        blasDesc.geometryDescriptors = geometryDescs;

        // Build acceleration structure
        id accelerationStructure = nil;
        if (!buildAccelerationStructure(blasDesc, accelerationStructure)) {
            std::cerr << "[MetalRT] Failed to build BLAS" << std::endl;
            return RTBLASHandle{};
        }

        blas->accelerationStructure = accelerationStructure;

        // Store BLAS and return handle
        uint32_t id = nextBLASId_++;
        blases_[id] = std::move(blas);

        RTBLASHandle handle;
        handle._id = id;
        handle._handle = blases_[id].get();

        std::cout << "[MetalRT] Created BLAS with " << count << " geometries" << std::endl;
        return handle;
    }
}

void MetalRTBackend::destroyBLAS(RTBLASHandle blas) {
    if (blas._id == 0) return;

    auto it = blases_.find(blas._id);
    if (it != blases_.end()) {
        @autoreleasepool {
            it->second->accelerationStructure = nil;
        }
        blases_.erase(it);
    }
}

// ============================================================================
// TLAS Management
// ============================================================================

RTTLASHandle MetalRTBackend::createTLAS(const RTTLASInstance* instances, size_t count) {
    if (!initialized_ || count == 0) {
        std::cerr << "[MetalRT] createTLAS: Invalid parameters" << std::endl;
        return RTTLASHandle{};
    }

    @autoreleasepool {
        auto tlas = std::make_unique<MetalTLAS>();
        tlas->instanceCount = static_cast<uint32_t>(count);

        // Create instance buffer
        size_t instanceBufferSize = count * sizeof(MTLAccelerationStructureInstanceDescriptor);
        if (!createBuffer(instanceBufferSize, tlas->instanceBuffer, true)) {
            std::cerr << "[MetalRT] Failed to create instance buffer" << std::endl;
            return RTTLASHandle{};
        }

        // Fill instance descriptors
        MTLAccelerationStructureInstanceDescriptor* instanceDescs =
            static_cast<MTLAccelerationStructureInstanceDescriptor*>(tlas->instanceBuffer.contents);

        NSMutableArray<id<MTLAccelerationStructure>>* blasArray =
            [NSMutableArray arrayWithCapacity:count];

        for (size_t i = 0; i < count; i++) {
            const RTTLASInstance& inst = instances[i];
            auto* blas = static_cast<MetalBLAS*>(inst.blas._handle);
            if (!blas) {
                std::cerr << "[MetalRT] Invalid BLAS handle at instance " << i << std::endl;
                destroyBuffer(tlas->instanceBuffer);
                return RTTLASHandle{};
            }

            // Convert column-major 4x4 matrix to MTLPackedFloat4x3 (row-major 3x4)
            // Metal uses row-major 3x4 transform: each row is [m00, m01, m02, m03]
            MTLPackedFloat4x3 transform;
            for (int row = 0; row < 3; row++) {
                transform.columns[0][row] = inst.transform[row * 4 + 0];  // Column 0
                transform.columns[1][row] = inst.transform[row * 4 + 1];  // Column 1
                transform.columns[2][row] = inst.transform[row * 4 + 2];  // Column 2
                transform.columns[3][row] = inst.transform[row * 4 + 3];  // Column 3 (translation)
            }

            instanceDescs[i].transformationMatrix = transform;
            instanceDescs[i].options = MTLAccelerationStructureInstanceOptionOpaque;
            instanceDescs[i].mask = inst.mask;
            instanceDescs[i].intersectionFunctionTableOffset = 0;
            instanceDescs[i].accelerationStructureIndex = static_cast<uint32_t>(i);

            [blasArray addObject:(id<MTLAccelerationStructure>)blas->accelerationStructure];
        }

        // Create TLAS descriptor
        MTLInstanceAccelerationStructureDescriptor* tlasDesc =
            [MTLInstanceAccelerationStructureDescriptor descriptor];
        tlasDesc.instanceDescriptorBuffer = (id<MTLBuffer>)tlas->instanceBuffer.buffer;
        tlasDesc.instanceCount = static_cast<NSUInteger>(count);
        tlasDesc.instancedAccelerationStructures = blasArray;

        // Build acceleration structure
        id accelerationStructure = nil;
        if (!buildAccelerationStructure(tlasDesc, accelerationStructure)) {
            std::cerr << "[MetalRT] Failed to build TLAS" << std::endl;
            destroyBuffer(tlas->instanceBuffer);
            return RTTLASHandle{};
        }

        tlas->accelerationStructure = accelerationStructure;

        // Store TLAS and return handle
        uint32_t id = nextTLASId_++;
        tlases_[id] = std::move(tlas);

        RTTLASHandle handle;
        handle._id = id;
        handle._handle = tlases_[id].get();

        std::cout << "[MetalRT] Created TLAS with " << count << " instances" << std::endl;
        return handle;
    }
}

void MetalRTBackend::updateTLAS(RTTLASHandle tlas, const RTTLASInstance* instances, size_t count) {
    if (!initialized_ || tlas._id == 0) return;

    auto it = tlases_.find(tlas._id);
    if (it == tlases_.end() || !it->second) {
        std::cerr << "[MetalRT] updateTLAS: Invalid TLAS handle" << std::endl;
        return;
    }

    @autoreleasepool {
        MetalTLAS* tlasPtr = it->second.get();

        if (count != tlasPtr->instanceCount) {
            std::cerr << "[MetalRT] updateTLAS: Instance count mismatch" << std::endl;
            return;
        }

        // Update instance transforms
        MTLAccelerationStructureInstanceDescriptor* instanceDescs =
            static_cast<MTLAccelerationStructureInstanceDescriptor*>(tlasPtr->instanceBuffer.contents);

        for (size_t i = 0; i < count; i++) {
            const RTTLASInstance& inst = instances[i];

            // Convert transform matrix
            MTLPackedFloat4x3 transform;
            for (int row = 0; row < 3; row++) {
                transform.columns[0][row] = inst.transform[row * 4 + 0];
                transform.columns[1][row] = inst.transform[row * 4 + 1];
                transform.columns[2][row] = inst.transform[row * 4 + 2];
                transform.columns[3][row] = inst.transform[row * 4 + 3];
            }

            instanceDescs[i].transformationMatrix = transform;
            instanceDescs[i].mask = inst.mask;
        }

        // Refit the acceleration structure (faster than full rebuild for transform updates)
        id<MTLDevice> device = (id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (id<MTLCommandQueue>)commandQueue_;

        // Get refitting sizes
        MTLInstanceAccelerationStructureDescriptor* refitDesc =
            [MTLInstanceAccelerationStructureDescriptor descriptor];
        refitDesc.instanceDescriptorBuffer = (id<MTLBuffer>)tlasPtr->instanceBuffer.buffer;
        refitDesc.instanceCount = static_cast<NSUInteger>(count);

        // Collect BLAS references
        NSMutableArray<id<MTLAccelerationStructure>>* blasArray =
            [NSMutableArray arrayWithCapacity:count];
        for (size_t i = 0; i < count; i++) {
            auto* blas = static_cast<MetalBLAS*>(instances[i].blas._handle);
            [blasArray addObject:(id<MTLAccelerationStructure>)blas->accelerationStructure];
        }
        refitDesc.instancedAccelerationStructures = blasArray;

        MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:refitDesc];

        // Create scratch buffer for refitting
        id<MTLBuffer> scratchBuffer = [device newBufferWithLength:sizes.refitScratchBufferSize
                                                          options:MTLResourceStorageModePrivate];

        // Refit acceleration structure
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLAccelerationStructureCommandEncoder> encoder =
            [commandBuffer accelerationStructureCommandEncoder];

        [encoder refitAccelerationStructure:(id<MTLAccelerationStructure>)tlasPtr->accelerationStructure
                                 descriptor:refitDesc
                                destination:nil
                              scratchBuffer:scratchBuffer
                        scratchBufferOffset:0];

        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
    }
}

void MetalRTBackend::destroyTLAS(RTTLASHandle tlas) {
    if (tlas._id == 0) return;

    auto it = tlases_.find(tlas._id);
    if (it != tlases_.end()) {
        @autoreleasepool {
            destroyBuffer(it->second->instanceBuffer);
            it->second->accelerationStructure = nil;
        }
        tlases_.erase(it);
    }
}

// ============================================================================
// Output Texture Management
// ============================================================================

bool MetalRTBackend::ensureOutputTexture(uint32_t width, uint32_t height) {
    if (outputWidth_ == width && outputHeight_ == height && outputTexture_) {
        return true;
    }

    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;

        // Create new output texture
        MTLTextureDescriptor* texDesc = [MTLTextureDescriptor new];
        texDesc.textureType = MTLTextureType2D;
        texDesc.pixelFormat = MTLPixelFormatRGBA8Unorm;
        texDesc.width = width;
        texDesc.height = height;
        texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        texDesc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = [device newTextureWithDescriptor:texDesc];
        if (!texture) {
            std::cerr << "[MetalRT] Failed to create output texture" << std::endl;
            return false;
        }

        outputTexture_ = texture;
        outputWidth_ = width;
        outputHeight_ = height;
        return true;
    }
}

// ============================================================================
// Ray Tracing Execution
// ============================================================================

void MetalRTBackend::traceRays(const TraceRaysOptions& options) {
    if (!initialized_) {
        std::cerr << "[MetalRT] traceRays: Backend not initialized" << std::endl;
        return;
    }

    auto it = tlases_.find(options.tlas._id);
    if (it == tlases_.end() || !it->second) {
        std::cerr << "[MetalRT] traceRays: Invalid TLAS handle" << std::endl;
        return;
    }

    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (id<MTLCommandQueue>)commandQueue_;
        id<MTLComputePipelineState> pipeline = (id<MTLComputePipelineState>)rtPipeline_;
        MetalTLAS* tlas = it->second.get();

        // Create output buffer (using buffer instead of texture for simplicity)
        size_t outputSize = options.width * options.height * sizeof(float) * 4;
        id<MTLBuffer> outputBuffer = [device newBufferWithLength:outputSize
                                                         options:MTLResourceStorageModeShared];
        if (!outputBuffer) {
            std::cerr << "[MetalRT] Failed to create output buffer" << std::endl;
            return;
        }

        // Update camera uniforms
        if (options.uniforms && options.uniformsSize >= sizeof(CameraUniforms)) {
            memcpy(cameraBuffer_.contents, options.uniforms, sizeof(CameraUniforms));
        } else {
            // Default identity matrices
            CameraUniforms defaultCamera;
            defaultCamera.viewInverse = matrix_identity_float4x4;
            defaultCamera.projInverse = matrix_identity_float4x4;
            memcpy(cameraBuffer_.contents, &defaultCamera, sizeof(CameraUniforms));
        }

        // Create output size buffer
        simd_uint2 outputSizeData = { options.width, options.height };

        // Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

        // Set pipeline
        [encoder setComputePipelineState:pipeline];

        // Set buffers
        [encoder setBytes:&outputSizeData length:sizeof(outputSizeData) atIndex:0];
        [encoder setBuffer:outputBuffer offset:0 atIndex:1];
        [encoder setBuffer:(id<MTLBuffer>)cameraBuffer_.buffer offset:0 atIndex:2];
        [encoder setAccelerationStructure:(id<MTLAccelerationStructure>)tlas->accelerationStructure
                            atBufferIndex:3];

        // Dispatch threads
        MTLSize threadGroupSize = MTLSizeMake(8, 8, 1);
        MTLSize threadGroups = MTLSizeMake(
            (options.width + threadGroupSize.width - 1) / threadGroupSize.width,
            (options.height + threadGroupSize.height - 1) / threadGroupSize.height,
            1
        );

        [encoder dispatchThreadgroups:threadGroups threadsPerThreadgroup:threadGroupSize];
        [encoder endEncoding];

        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Copy result to output texture if provided
        if (options.outputTexture) {
            // TODO: Implement WebGPU texture interop
            // For now, the output buffer contains the result that could be copied
            // to a WebGPU texture via staging buffer
            std::cout << "[MetalRT] Ray tracing completed (" << options.width << "x" << options.height << ")" << std::endl;
        }
    }
}

}  // namespace rt
}  // namespace mystral
