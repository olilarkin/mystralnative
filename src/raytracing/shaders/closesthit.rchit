/**
 * Closest Hit Shader
 *
 * Called when a ray hits the closest piece of geometry.
 * Outputs barycentric coordinates as color for debugging/visualization.
 *
 * Reference: VK_KHR_ray_tracing_pipeline specification
 */

#version 460
#extension GL_EXT_ray_tracing : require

// Ray payload to return color to ray generation shader
layout(location = 0) rayPayloadInEXT vec3 payload;

// Barycentric coordinates of the hit point within the triangle
// (u, v) are provided; w = 1 - u - v
hitAttributeEXT vec2 baryCoord;

void main() {
    // Compute full barycentric coordinates
    // For a triangle with vertices A, B, C:
    // hit_point = A * (1-u-v) + B * u + C * v
    float u = baryCoord.x;
    float v = baryCoord.y;
    float w = 1.0 - u - v;

    // Output barycentric coordinates as RGB color
    // This creates a smooth gradient across the triangle surface
    // Red channel = weight of vertex A (1-u-v)
    // Green channel = weight of vertex B (u)
    // Blue channel = weight of vertex C (v)
    payload = vec3(w, u, v);
}
