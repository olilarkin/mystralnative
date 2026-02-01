/**
 * MystralNative Metal Ray Tracing Shaders
 *
 * This file contains the Metal Shading Language (MSL) ray tracing shaders
 * for hardware-accelerated ray tracing on Apple Silicon.
 *
 * The shader uses Metal's ray tracing API introduced in Metal 3:
 * - instance_acceleration_structure for scene hierarchy
 * - intersector<> for ray-triangle intersection
 * - intersection_result<> for hit information
 *
 * Reference: https://developer.apple.com/documentation/metal/ray_tracing
 * Build: xcrun -sdk macosx metal -c raytracing.metal -o raytracing.air
 *        xcrun -sdk macosx metallib raytracing.air -o raytracing.metallib
 */

#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace raytracing;

// ============================================================================
// Uniform Structures
// ============================================================================

/**
 * Camera uniforms for ray generation.
 * Contains inverse view and projection matrices for transforming
 * pixel coordinates to world-space ray directions.
 */
struct CameraUniforms {
    float4x4 viewInverse;   // Inverse view matrix (camera-to-world)
    float4x4 projInverse;   // Inverse projection matrix
};

/**
 * Hit information passed through ray tracing pipeline.
 * Extended for future use with material/lighting data.
 */
struct HitInfo {
    float3 worldPosition;
    float3 worldNormal;
    float2 barycentrics;
    uint instanceId;
    uint primitiveId;
};

// ============================================================================
// Ray Generation Kernel
// ============================================================================

/**
 * Primary ray tracing kernel.
 *
 * Generates primary rays from the camera through each pixel,
 * traces them against the acceleration structure, and writes
 * the result color to the output buffer.
 *
 * @param tid Thread position in grid (pixel coordinates)
 * @param outputSize Dimensions of the output image
 * @param output Buffer to write RGBA color values
 * @param camera Camera matrices for ray generation
 * @param tlas Top-level acceleration structure containing the scene
 */
[[kernel]]
void raytracingKernel(
    uint2 tid [[thread_position_in_grid]],
    constant uint2& outputSize [[buffer(0)]],
    device float4* output [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    instance_acceleration_structure tlas [[buffer(3)]]
) {
    // Early exit if thread is outside image bounds
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) {
        return;
    }

    // Calculate normalized device coordinates (NDC) in range [-1, 1]
    // Add 0.5 to sample pixel center rather than corner
    float2 pixelCenter = float2(tid) + 0.5;
    float2 ndc = pixelCenter / float2(outputSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y: Metal's texture origin is top-left

    // Generate camera ray using inverse matrices
    // Origin: camera position in world space (extracted from inverse view)
    float4 origin = camera.viewInverse * float4(0, 0, 0, 1);

    // Direction: transform NDC through inverse projection, then to world space
    float4 target = camera.projInverse * float4(ndc, 1, 1);
    float4 direction = camera.viewInverse * float4(normalize(target.xyz), 0);

    // Create ray for intersection testing
    ray r;
    r.origin = origin.xyz;
    r.direction = normalize(direction.xyz);
    r.min_distance = 0.001;   // Avoid self-intersection
    r.max_distance = 10000.0; // Far plane

    // Create intersector for triangle geometry with instancing
    // accept_any_intersection(false) = find closest hit
    intersector<triangle_data, instancing> inter;
    inter.accept_any_intersection(false);

    // Perform intersection against acceleration structure
    intersection_result<triangle_data, instancing> result = inter.intersect(r, tlas);

    // Compute output color based on hit/miss
    float3 color;

    if (result.type == intersection_type::triangle) {
        // Hit: visualize barycentric coordinates as RGB
        // This is useful for debugging geometry
        float2 bary = result.triangle_barycentric_coord;
        float w = 1.0 - bary.x - bary.y;  // Third barycentric coord
        color = float3(w, bary.x, bary.y);

        // Alternative: simple normal visualization
        // Uncomment to visualize interpolated normals
        // float3 normal = normalize(float3(bary, w));
        // color = normal * 0.5 + 0.5;
    } else {
        // Miss: procedural sky gradient
        // Gradient from white at horizon to blue at zenith
        float t = 0.5 * (r.direction.y + 1.0);
        float3 white = float3(1.0, 1.0, 1.0);
        float3 skyBlue = float3(0.5, 0.7, 1.0);
        color = mix(white, skyBlue, t);
    }

    // Write output color (RGBA)
    uint pixelIndex = tid.y * outputSize.x + tid.x;
    output[pixelIndex] = float4(color, 1.0);
}

// ============================================================================
// Shadow Ray Kernel (for future use)
// ============================================================================

/**
 * Shadow ray tracing kernel.
 *
 * Traces shadow rays from surface points toward light sources
 * to determine visibility (shadowed or lit).
 *
 * @param tid Thread position in grid
 * @param numRays Number of shadow rays to trace
 * @param shadowRays Input ray origins and directions
 * @param shadowResults Output visibility (0 = shadowed, 1 = lit)
 * @param tlas Scene acceleration structure
 */
[[kernel]]
void shadowRayKernel(
    uint tid [[thread_position_in_grid]],
    constant uint& numRays [[buffer(0)]],
    device packed_float3* rayOrigins [[buffer(1)]],
    device packed_float3* rayDirections [[buffer(2)]],
    device float* maxDistances [[buffer(3)]],
    device float* shadowResults [[buffer(4)]],
    instance_acceleration_structure tlas [[buffer(5)]]
) {
    if (tid >= numRays) {
        return;
    }

    // Create shadow ray
    ray shadowRay;
    shadowRay.origin = float3(rayOrigins[tid]);
    shadowRay.direction = normalize(float3(rayDirections[tid]));
    shadowRay.min_distance = 0.001;
    shadowRay.max_distance = maxDistances[tid];

    // Create intersector for any-hit (shadow test optimization)
    intersector<triangle_data, instancing> inter;
    inter.accept_any_intersection(true);  // Stop at first hit

    // Perform intersection
    intersection_result<triangle_data, instancing> result = inter.intersect(shadowRay, tlas);

    // Write visibility result
    shadowResults[tid] = (result.type == intersection_type::none) ? 1.0 : 0.0;
}

// ============================================================================
// Cornell Box Ray Generation (for testing)
// ============================================================================

/**
 * Cornell box ray tracing kernel with simple diffuse shading.
 *
 * This kernel is designed for testing the ray tracing pipeline
 * with the classic Cornell box scene.
 */
[[kernel]]
void cornellBoxKernel(
    uint2 tid [[thread_position_in_grid]],
    constant uint2& outputSize [[buffer(0)]],
    device float4* output [[buffer(1)]],
    constant CameraUniforms& camera [[buffer(2)]],
    instance_acceleration_structure tlas [[buffer(3)]]
) {
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) {
        return;
    }

    // Generate ray
    float2 pixelCenter = float2(tid) + 0.5;
    float2 ndc = pixelCenter / float2(outputSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    float4 origin = camera.viewInverse * float4(0, 0, 0, 1);
    float4 target = camera.projInverse * float4(ndc, 1, 1);
    float4 direction = camera.viewInverse * float4(normalize(target.xyz), 0);

    ray r;
    r.origin = origin.xyz;
    r.direction = normalize(direction.xyz);
    r.min_distance = 0.001;
    r.max_distance = 10000.0;

    intersector<triangle_data, instancing> inter;
    inter.accept_any_intersection(false);

    intersection_result<triangle_data, instancing> result = inter.intersect(r, tlas);

    float3 color;

    if (result.type == intersection_type::triangle) {
        // Get instance ID for material color
        uint instanceId = result.instance_id;

        // Cornell box colors based on instance ID
        // 0 = floor (white), 1 = ceiling (white), 2 = back wall (white)
        // 3 = left wall (red), 4 = right wall (green)
        // 5,6 = boxes (white)
        float3 albedo;
        switch (instanceId) {
            case 3: albedo = float3(0.65, 0.05, 0.05); break;  // Red
            case 4: albedo = float3(0.12, 0.45, 0.15); break;  // Green
            default: albedo = float3(0.73, 0.73, 0.73); break; // White
        }

        // Simple diffuse shading with light from above
        float2 bary = result.triangle_barycentric_coord;
        float w = 1.0 - bary.x - bary.y;

        // Approximate normal from barycentrics (for testing)
        // In production, normals would come from vertex attributes
        float3 normal = normalize(float3(bary.x - 0.5, w, bary.y - 0.5));

        // Light direction (from ceiling)
        float3 lightDir = normalize(float3(0, 1, 0));
        float NdotL = max(dot(normal, lightDir), 0.0);

        // Ambient + diffuse
        color = albedo * (0.3 + 0.7 * NdotL);
    } else {
        // Black background for Cornell box
        color = float3(0.0, 0.0, 0.0);
    }

    uint pixelIndex = tid.y * outputSize.x + tid.x;
    output[pixelIndex] = float4(color, 1.0);
}
