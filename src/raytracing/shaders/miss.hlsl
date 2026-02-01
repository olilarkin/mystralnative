/**
 * DXR Miss Shader
 *
 * Called when a ray doesn't hit any geometry.
 * Generates a simple sky gradient based on ray direction.
 *
 * Reference: Microsoft DXR specification
 */

// Ray payload structure - must match raygen.hlsl
struct RayPayload {
    float3 color;
};

[shader("miss")]
void Miss(inout RayPayload payload) {
    // Get world-space ray direction
    float3 dir = normalize(WorldRayDirection());

    // Simple sky gradient: blue at top, white at horizon
    // t = 0.0 at horizon, t = 1.0 at zenith
    float t = 0.5 * (dir.y + 1.0);

    // Lerp between white (horizon) and sky blue (zenith)
    float3 white = float3(1.0, 1.0, 1.0);
    float3 skyBlue = float3(0.5, 0.7, 1.0);

    payload.color = lerp(white, skyBlue, t);
}
