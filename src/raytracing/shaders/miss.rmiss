/**
 * Miss Shader
 *
 * Called when a ray doesn't hit any geometry.
 * Generates a simple sky gradient based on ray direction.
 *
 * Reference: VK_KHR_ray_tracing_pipeline specification
 */

#version 460
#extension GL_EXT_ray_tracing : require

// Ray payload from ray generation shader
layout(location = 0) rayPayloadInEXT vec3 payload;

void main() {
    // Get world-space ray direction
    vec3 dir = normalize(gl_WorldRayDirectionEXT);

    // Simple sky gradient: blue at top, white at horizon
    // t = 0.0 at horizon, t = 1.0 at zenith
    float t = 0.5 * (dir.y + 1.0);

    // Lerp between white (horizon) and sky blue (zenith)
    vec3 white = vec3(1.0, 1.0, 1.0);
    vec3 skyBlue = vec3(0.5, 0.7, 1.0);

    payload = mix(white, skyBlue, t);
}
