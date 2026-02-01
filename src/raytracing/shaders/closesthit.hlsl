/**
 * DXR Closest Hit Shader
 *
 * Called when a ray hits the closest piece of geometry.
 * Outputs barycentric coordinates as color for debugging/visualization.
 *
 * Reference: Microsoft DXR specification
 */

// Ray payload structure - must match raygen.hlsl
struct RayPayload {
    float3 color;
};

// Built-in hit attributes for triangles
// DXR provides barycentric coordinates automatically
struct BuiltInTriangleIntersectionAttributes {
    float2 barycentrics;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    // Compute full barycentric coordinates
    // For a triangle with vertices A, B, C:
    // hit_point = A * (1-u-v) + B * u + C * v
    float u = attribs.barycentrics.x;
    float v = attribs.barycentrics.y;
    float w = 1.0 - u - v;

    // Output barycentric coordinates as RGB color
    // This creates a smooth gradient across the triangle surface
    // Red channel = weight of vertex A (1-u-v)
    // Green channel = weight of vertex B (u)
    // Blue channel = weight of vertex C (v)
    payload.color = float3(w, u, v);
}
