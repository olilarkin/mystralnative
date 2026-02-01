/**
 * DXR Shader Definitions for Runtime Compilation
 *
 * This file contains HLSL source code for the DXR ray tracing shaders.
 * The shaders are compiled at runtime using the DirectX Shader Compiler (dxc).
 *
 * For production use, these can be precompiled to DXIL using:
 *   dxc -T lib_6_3 -Fo raygen.dxil raygen.hlsl
 *   dxc -T lib_6_3 -Fo miss.dxil miss.hlsl
 *   dxc -T lib_6_3 -Fo closesthit.dxil closesthit.hlsl
 */

#pragma once

namespace mystral {
namespace rt {
namespace dxr_shaders {

// ============================================================================
// Embedded HLSL Source Code
// ============================================================================

// Ray generation shader - generates primary rays from camera
static const char* raygenHLSL = R"HLSL(
RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer CameraParams : register(b0) {
    float4x4 viewInverse;
    float4x4 projInverse;
};

struct RayPayload {
    float3 color;
};

[shader("raygeneration")]
void RayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    float2 pixelCenter = float2(launchIndex) + float2(0.5, 0.5);
    float2 uv = pixelCenter / float2(launchDim) * 2.0 - 1.0;

    float4 origin = mul(float4(0.0, 0.0, 0.0, 1.0), viewInverse);

    float4 target = mul(float4(uv.x, -uv.y, 1.0, 1.0), projInverse);
    target.xyz /= target.w;
    float4 direction = mul(float4(normalize(target.xyz), 0.0), viewInverse);

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float3(0.0, 0.0, 0.0);

    TraceRay(
        Scene,
        RAY_FLAG_FORCE_OPAQUE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload
    );

    OutputTexture[launchIndex] = float4(payload.color, 1.0);
}
)HLSL";

// Miss shader - sky gradient when ray misses geometry
static const char* missHLSL = R"HLSL(
struct RayPayload {
    float3 color;
};

[shader("miss")]
void Miss(inout RayPayload payload) {
    float3 dir = normalize(WorldRayDirection());
    float t = 0.5 * (dir.y + 1.0);
    float3 white = float3(1.0, 1.0, 1.0);
    float3 skyBlue = float3(0.5, 0.7, 1.0);
    payload.color = lerp(white, skyBlue, t);
}
)HLSL";

// Closest hit shader - barycentric coordinates as color
static const char* closestHitHLSL = R"HLSL(
struct RayPayload {
    float3 color;
};

struct BuiltInTriangleIntersectionAttributes {
    float2 barycentrics;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    float u = attribs.barycentrics.x;
    float v = attribs.barycentrics.y;
    float w = 1.0 - u - v;
    payload.color = float3(w, u, v);
}
)HLSL";

// Combined shader library for state object creation
// This single HLSL file contains all shaders and is compiled as a library
static const char* combinedLibraryHLSL = R"HLSL(
// ============================================================================
// DXR Ray Tracing Shader Library
// ============================================================================

// Descriptor bindings
RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer CameraParams : register(b0) {
    float4x4 viewInverse;
    float4x4 projInverse;
};

// Ray payload structure
struct RayPayload {
    float3 color;
};

// Triangle hit attributes
struct BuiltInTriangleIntersectionAttributes {
    float2 barycentrics;
};

// ============================================================================
// Ray Generation Shader
// ============================================================================

[shader("raygeneration")]
void RayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;

    float2 pixelCenter = float2(launchIndex) + float2(0.5, 0.5);
    float2 uv = pixelCenter / float2(launchDim) * 2.0 - 1.0;

    float4 origin = mul(float4(0.0, 0.0, 0.0, 1.0), viewInverse);

    float4 target = mul(float4(uv.x, -uv.y, 1.0, 1.0), projInverse);
    target.xyz /= target.w;
    float4 direction = mul(float4(normalize(target.xyz), 0.0), viewInverse);

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float3(0.0, 0.0, 0.0);

    TraceRay(
        Scene,
        RAY_FLAG_FORCE_OPAQUE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload
    );

    OutputTexture[launchIndex] = float4(payload.color, 1.0);
}

// ============================================================================
// Miss Shader
// ============================================================================

[shader("miss")]
void Miss(inout RayPayload payload) {
    float3 dir = normalize(WorldRayDirection());
    float t = 0.5 * (dir.y + 1.0);
    float3 white = float3(1.0, 1.0, 1.0);
    float3 skyBlue = float3(0.5, 0.7, 1.0);
    payload.color = lerp(white, skyBlue, t);
}

// ============================================================================
// Closest Hit Shader
// ============================================================================

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    float u = attribs.barycentrics.x;
    float v = attribs.barycentrics.y;
    float w = 1.0 - u - v;
    payload.color = float3(w, u, v);
}
)HLSL";

// Shader entry point names (must match HLSL function names)
static const wchar_t* raygenEntryPoint = L"RayGen";
static const wchar_t* missEntryPoint = L"Miss";
static const wchar_t* closestHitEntryPoint = L"ClosestHit";
static const wchar_t* hitGroupName = L"HitGroup";

// Shader compile target for DXR library
static const wchar_t* shaderLibraryTarget = L"lib_6_3";

}  // namespace dxr_shaders
}  // namespace rt
}  // namespace mystral
