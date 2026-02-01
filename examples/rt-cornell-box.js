/**
 * Ray Tracing Cornell Box Example
 *
 * Demonstrates hardware ray tracing using the mystralRT API.
 * Renders a classic Cornell box scene with primary rays.
 *
 * Requirements:
 * - GPU with Vulkan RT support (NVIDIA RTX, AMD RDNA2+)
 * - MystralNative built with MYSTRAL_USE_RAYTRACING=ON
 *
 * Usage:
 *   ./mystral run examples/rt-cornell-box.js
 */

const width = 800;
const height = 600;

// Check RT support
console.log('Ray Tracing Backend:', mystralRT.getBackend());
console.log('Ray Tracing Supported:', mystralRT.isSupported());

if (!mystralRT.isSupported()) {
    console.log('Hardware ray tracing not available.');
    console.log('This example requires a GPU with Vulkan RT support.');
    // Exit gracefully
    if (typeof process !== 'undefined') {
        process.exit(0);
    }
}

// Cornell box geometry
// Box dimensions: 548.8 x 548.8 x 559.2 (scaled down for simplicity)
// Origin at center of floor

// Floor (white)
const floorVertices = new Float32Array([
    -1.0, 0.0, -1.0,
     1.0, 0.0, -1.0,
     1.0, 0.0,  1.0,
    -1.0, 0.0,  1.0,
]);
const floorIndices = new Uint32Array([0, 1, 2, 0, 2, 3]);

// Ceiling (white)
const ceilingVertices = new Float32Array([
    -1.0, 2.0, -1.0,
    -1.0, 2.0,  1.0,
     1.0, 2.0,  1.0,
     1.0, 2.0, -1.0,
]);
const ceilingIndices = new Uint32Array([0, 1, 2, 0, 2, 3]);

// Back wall (white)
const backWallVertices = new Float32Array([
    -1.0, 0.0, -1.0,
    -1.0, 2.0, -1.0,
     1.0, 2.0, -1.0,
     1.0, 0.0, -1.0,
]);
const backWallIndices = new Uint32Array([0, 1, 2, 0, 2, 3]);

// Left wall (red)
const leftWallVertices = new Float32Array([
    -1.0, 0.0, -1.0,
    -1.0, 0.0,  1.0,
    -1.0, 2.0,  1.0,
    -1.0, 2.0, -1.0,
]);
const leftWallIndices = new Uint32Array([0, 1, 2, 0, 2, 3]);

// Right wall (green)
const rightWallVertices = new Float32Array([
     1.0, 0.0, -1.0,
     1.0, 2.0, -1.0,
     1.0, 2.0,  1.0,
     1.0, 0.0,  1.0,
]);
const rightWallIndices = new Uint32Array([0, 1, 2, 0, 2, 3]);

// Tall box (rotated)
const tallBoxVertices = new Float32Array([
    // Front face
    -0.3, 0.0, 0.1,
     0.1, 0.0, 0.3,
     0.1, 1.2, 0.3,
    -0.3, 1.2, 0.1,
    // Back face
    -0.1, 0.0, -0.3,
     0.3, 0.0, -0.1,
     0.3, 1.2, -0.1,
    -0.1, 1.2, -0.3,
    // Left face
    -0.3, 0.0, 0.1,
    -0.3, 1.2, 0.1,
    -0.1, 1.2, -0.3,
    -0.1, 0.0, -0.3,
    // Right face
     0.1, 0.0, 0.3,
     0.3, 0.0, -0.1,
     0.3, 1.2, -0.1,
     0.1, 1.2, 0.3,
    // Top face
    -0.3, 1.2, 0.1,
     0.1, 1.2, 0.3,
     0.3, 1.2, -0.1,
    -0.1, 1.2, -0.3,
]);
const tallBoxIndices = new Uint32Array([
    0, 1, 2, 0, 2, 3,       // Front
    4, 5, 6, 4, 6, 7,       // Back
    8, 9, 10, 8, 10, 11,    // Left
    12, 13, 14, 12, 14, 15, // Right
    16, 17, 18, 16, 18, 19, // Top
]);

// Short box
const shortBoxVertices = new Float32Array([
    // Front face
     0.2, 0.0, 0.5,
     0.6, 0.0, 0.3,
     0.6, 0.6, 0.3,
     0.2, 0.6, 0.5,
    // Back face
     0.4, 0.0, 0.1,
     0.8, 0.0, -0.1,
     0.8, 0.6, -0.1,
     0.4, 0.6, 0.1,
    // Left face
     0.2, 0.0, 0.5,
     0.2, 0.6, 0.5,
     0.4, 0.6, 0.1,
     0.4, 0.0, 0.1,
    // Right face
     0.6, 0.0, 0.3,
     0.8, 0.0, -0.1,
     0.8, 0.6, -0.1,
     0.6, 0.6, 0.3,
    // Top face
     0.2, 0.6, 0.5,
     0.6, 0.6, 0.3,
     0.8, 0.6, -0.1,
     0.4, 0.6, 0.1,
]);
const shortBoxIndices = new Uint32Array([
    0, 1, 2, 0, 2, 3,       // Front
    4, 5, 6, 4, 6, 7,       // Back
    8, 9, 10, 8, 10, 11,    // Left
    12, 13, 14, 12, 14, 15, // Right
    16, 17, 18, 16, 18, 19, // Top
]);

console.log('Creating geometry...');

// Create geometry handles
const floorGeom = mystralRT.createGeometry({
    vertices: floorVertices,
    indices: floorIndices,
    vertexStride: 12,
});

const ceilingGeom = mystralRT.createGeometry({
    vertices: ceilingVertices,
    indices: ceilingIndices,
    vertexStride: 12,
});

const backWallGeom = mystralRT.createGeometry({
    vertices: backWallVertices,
    indices: backWallIndices,
    vertexStride: 12,
});

const leftWallGeom = mystralRT.createGeometry({
    vertices: leftWallVertices,
    indices: leftWallIndices,
    vertexStride: 12,
});

const rightWallGeom = mystralRT.createGeometry({
    vertices: rightWallVertices,
    indices: rightWallIndices,
    vertexStride: 12,
});

const tallBoxGeom = mystralRT.createGeometry({
    vertices: tallBoxVertices,
    indices: tallBoxIndices,
    vertexStride: 12,
});

const shortBoxGeom = mystralRT.createGeometry({
    vertices: shortBoxVertices,
    indices: shortBoxIndices,
    vertexStride: 12,
});

console.log('Building BLAS...');

// Build BLASes (one per geometry for simplicity)
const floorBLAS = mystralRT.createBLAS([floorGeom]);
const ceilingBLAS = mystralRT.createBLAS([ceilingGeom]);
const backWallBLAS = mystralRT.createBLAS([backWallGeom]);
const leftWallBLAS = mystralRT.createBLAS([leftWallGeom]);
const rightWallBLAS = mystralRT.createBLAS([rightWallGeom]);
const tallBoxBLAS = mystralRT.createBLAS([tallBoxGeom]);
const shortBoxBLAS = mystralRT.createBLAS([shortBoxGeom]);

// Identity matrix (column-major)
const identity = new Float32Array([
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
]);

console.log('Building TLAS...');

// Build TLAS with all instances at identity transform
const tlas = mystralRT.createTLAS([
    { blas: floorBLAS, transform: identity, instanceId: 0 },
    { blas: ceilingBLAS, transform: identity, instanceId: 1 },
    { blas: backWallBLAS, transform: identity, instanceId: 2 },
    { blas: leftWallBLAS, transform: identity, instanceId: 3 },
    { blas: rightWallBLAS, transform: identity, instanceId: 4 },
    { blas: tallBoxBLAS, transform: identity, instanceId: 5 },
    { blas: shortBoxBLAS, transform: identity, instanceId: 6 },
]);

// Camera setup
// Looking at the Cornell box from in front
const cameraPos = [0, 1, 3.5];
const cameraTarget = [0, 1, 0];
const cameraUp = [0, 1, 0];

// Build view matrix (simple look-at)
function lookAt(eye, target, up) {
    const zAxis = normalize(subtract(eye, target));
    const xAxis = normalize(cross(up, zAxis));
    const yAxis = cross(zAxis, xAxis);

    return new Float32Array([
        xAxis[0], yAxis[0], zAxis[0], 0,
        xAxis[1], yAxis[1], zAxis[1], 0,
        xAxis[2], yAxis[2], zAxis[2], 0,
        -dot(xAxis, eye), -dot(yAxis, eye), -dot(zAxis, eye), 1,
    ]);
}

function normalize(v) {
    const len = Math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    return [v[0]/len, v[1]/len, v[2]/len];
}

function subtract(a, b) {
    return [a[0]-b[0], a[1]-b[1], a[2]-b[2]];
}

function cross(a, b) {
    return [
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    ];
}

function dot(a, b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

function invertMatrix(m) {
    // Simplified 4x4 matrix inversion for affine matrices
    const inv = new Float32Array(16);

    inv[0] = m[0]; inv[1] = m[4]; inv[2] = m[8]; inv[3] = 0;
    inv[4] = m[1]; inv[5] = m[5]; inv[6] = m[9]; inv[7] = 0;
    inv[8] = m[2]; inv[9] = m[6]; inv[10] = m[10]; inv[11] = 0;
    inv[12] = -(m[0]*m[12] + m[1]*m[13] + m[2]*m[14]);
    inv[13] = -(m[4]*m[12] + m[5]*m[13] + m[6]*m[14]);
    inv[14] = -(m[8]*m[12] + m[9]*m[13] + m[10]*m[14]);
    inv[15] = 1;

    return inv;
}

function perspectiveProjectionInverse(fovY, aspect, near, far) {
    const tanHalfFovy = Math.tan(fovY / 2);

    return new Float32Array([
        tanHalfFovy * aspect, 0, 0, 0,
        0, tanHalfFovy, 0, 0,
        0, 0, 0, (near - far) / (2 * far * near),
        0, 0, -1, (near + far) / (2 * far * near),
    ]);
}

const viewMatrix = lookAt(cameraPos, cameraTarget, cameraUp);
const viewInverse = invertMatrix(viewMatrix);
const projInverse = perspectiveProjectionInverse(
    Math.PI / 3,  // 60 degree FOV
    width / height,
    0.01,
    100.0
);

// Combine into uniform buffer (viewInverse + projInverse = 2 * 16 floats)
const uniforms = new Float32Array(32);
uniforms.set(viewInverse, 0);
uniforms.set(projInverse, 16);

console.log('Tracing rays...');

// Create output texture (placeholder - in real usage this would be a WebGPU texture)
// For now, the RT backend handles the output internally
const outputTexture = null;  // TODO: Create actual WebGPU texture

// Trace rays
const startTime = performance.now();

mystralRT.traceRays({
    tlas: tlas,
    width: width,
    height: height,
    outputTexture: outputTexture,
    uniforms: uniforms,
});

const elapsed = performance.now() - startTime;
console.log(`Ray tracing complete in ${elapsed.toFixed(2)} ms`);
console.log(`Resolution: ${width}x${height}`);
console.log(`Rays per second: ${((width * height) / (elapsed / 1000) / 1e6).toFixed(2)} million`);

// Cleanup
console.log('Cleaning up...');

mystralRT.destroyTLAS(tlas);
mystralRT.destroyBLAS(floorBLAS);
mystralRT.destroyBLAS(ceilingBLAS);
mystralRT.destroyBLAS(backWallBLAS);
mystralRT.destroyBLAS(leftWallBLAS);
mystralRT.destroyBLAS(rightWallBLAS);
mystralRT.destroyBLAS(tallBoxBLAS);
mystralRT.destroyBLAS(shortBoxBLAS);

console.log('Done!');
