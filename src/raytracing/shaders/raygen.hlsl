/**
 * DXR Ray Generation Shader
 *
 * Generates primary rays from the camera through each pixel.
 * Traces rays against the TLAS and stores results to the output image.
 *
 * Reference: Microsoft DXR specification
 */

// Top-Level Acceleration Structure
RaytracingAccelerationStructure Scene : register(t0);

// Output texture (UAV)
RWTexture2D<float4> OutputTexture : register(u0);

// Camera uniform buffer
cbuffer CameraParams : register(b0) {
    float4x4 viewInverse;   // Camera position and orientation (row-major in HLSL)
    float4x4 projInverse;   // Inverse projection matrix
};

// Ray payload structure - color returned from hit/miss shaders
struct RayPayload {
    float3 color;
};

[shader("raygeneration")]
void RayGen() {
    // Get launch dimensions and current pixel
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    // Get normalized pixel coordinates [-1, 1]
    float2 pixelCenter = float2(launchIndex) + float2(0.5, 0.5);
    float2 uv = pixelCenter / float2(launchDim) * 2.0 - 1.0;

    // Compute ray origin (camera position in world space)
    // In HLSL with row-major matrices, we multiply vector on the left
    float4 origin = mul(float4(0.0, 0.0, 0.0, 1.0), viewInverse);

    // Compute ray direction using inverse projection and view matrices
    // Map screen coordinates to clip space, then to world space
    // Note: Y is flipped compared to Vulkan
    float4 target = mul(float4(uv.x, -uv.y, 1.0, 1.0), projInverse);
    target.xyz /= target.w;
    float4 direction = mul(float4(normalize(target.xyz), 0.0), viewInverse);

    // Set up ray description
    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;  // Avoid self-intersection
    ray.TMax = 10000.0;

    // Initialize payload
    RayPayload payload;
    payload.color = float3(0.0, 0.0, 0.0);

    // Trace ray through the scene
    // Parameters:
    // - Scene: acceleration structure
    // - RAY_FLAG_CULL_BACK_FACING_TRIANGLES: cull back faces
    // - 0xFF: visibility mask (hit all geometry)
    // - 0: ray contribution to hit group index
    // - 0: multiplier for geometry contribution to hit group index
    // - 0: miss shader index
    // - ray: ray description
    // - payload: ray payload
    TraceRay(
        Scene,
        RAY_FLAG_FORCE_OPAQUE,
        0xFF,
        0,      // RayContributionToHitGroupIndex
        0,      // MultiplierForGeometryContributionToHitGroupIndex
        0,      // MissShaderIndex
        ray,
        payload
    );

    // Write result to output texture
    OutputTexture[launchIndex] = float4(payload.color, 1.0);
}
