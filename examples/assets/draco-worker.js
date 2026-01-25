/**
 * Draco Decoder Web Worker
 *
 * Decodes Draco-compressed mesh data using the Draco WASM decoder.
 * Communicates with the main thread via postMessage.
 *
 * Expected message format:
 * {
 *   requestId: number,
 *   compressedData: ArrayBuffer,
 *   attributeIds: { POSITION?: number, NORMAL?: number, TEX_COORD?: number }
 * }
 *
 * Response format:
 * {
 *   requestId: number,
 *   success: boolean,
 *   error?: string,
 *   positions?: Float32Array,
 *   normals?: Float32Array,
 *   uvs?: Float32Array,
 *   indices?: Uint32Array
 * }
 */

let dracoModule = null;
let dracoDecoder = null;
let initPromise = null;

/**
 * Initialize the Draco decoder
 * Loads the Draco decoder from local files (bundled with the engine)
 */
async function initDraco() {
    if (initPromise) return initPromise;

    initPromise = (async () => {
        try {
            // Use local files bundled with MystralNative
            // These are relative to where the GLB file is loaded from
            const dracoUrl = './examples/assets/draco_decoder.wasm';
            const jsUrl = './examples/assets/draco_decoder.js';

            console.log('[Draco Worker] Loading decoder from:', jsUrl);

            // Load the decoder JS
            const jsResponse = await fetch(jsUrl);
            if (!jsResponse.ok) {
                throw new Error(`Failed to fetch Draco decoder JS: ${jsResponse.status}`);
            }
            const jsCode = await jsResponse.text();

            // The Three.js Draco decoder exports DracoDecoderModule as a self-executing function
            // We need to eval it and get the module factory
            const DracoModule = eval('(' + jsCode + ')');

            // Load WASM binary
            const wasmResponse = await fetch(dracoUrl);
            if (!wasmResponse.ok) {
                throw new Error(`Failed to fetch Draco WASM: ${wasmResponse.status}`);
            }
            const wasmBinary = await wasmResponse.arrayBuffer();

            console.log('[Draco Worker] WASM loaded, size:', wasmBinary.byteLength);

            // Initialize Draco module with WASM
            dracoModule = await new Promise((resolve, reject) => {
                DracoModule({
                    wasmBinary: wasmBinary,
                    onModuleLoaded: (module) => {
                        console.log('[Draco Worker] Module loaded callback');
                        resolve(module);
                    }
                }).then(resolve).catch(reject);
            });

            dracoDecoder = new dracoModule.Decoder();
            console.log('[Draco Worker] Initialized successfully');

        } catch (e) {
            console.error('[Draco Worker] Failed to initialize:', e);
            throw e;
        }
    })();

    return initPromise;
}

/**
 * Extract a float attribute from decoded geometry
 */
function extractFloatAttribute(decoder, geometry, attributeType, numComponents) {
    const attribute = decoder.GetAttributeByUniqueId(geometry, attributeType);
    if (!attribute) {
        return null;
    }

    const numPoints = geometry.num_points();
    const numValues = numPoints * numComponents;
    const array = new dracoModule.DracoFloat32Array();

    decoder.GetAttributeFloatForAllPoints(geometry, attribute, array);

    const result = new Float32Array(numValues);
    for (let i = 0; i < numValues; i++) {
        result[i] = array.GetValue(i);
    }

    dracoModule.destroy(array);
    return result;
}

/**
 * Extract indices from decoded mesh
 */
function extractIndices(decoder, mesh) {
    const numFaces = mesh.num_faces();
    const numIndices = numFaces * 3;
    const indices = new Uint32Array(numIndices);

    const faceArray = new dracoModule.DracoInt32Array();

    for (let i = 0; i < numFaces; i++) {
        decoder.GetFaceFromMesh(mesh, i, faceArray);
        indices[i * 3] = faceArray.GetValue(0);
        indices[i * 3 + 1] = faceArray.GetValue(1);
        indices[i * 3 + 2] = faceArray.GetValue(2);
    }

    dracoModule.destroy(faceArray);
    return indices;
}

/**
 * Decode Draco compressed mesh data
 */
async function decodeMesh(compressedData, attributeIds) {
    await initDraco();

    const buffer = new dracoModule.DecoderBuffer();
    buffer.Init(new Int8Array(compressedData), compressedData.byteLength);

    const geometryType = dracoDecoder.GetEncodedGeometryType(buffer);

    let geometry;
    let status;

    if (geometryType === dracoModule.TRIANGULAR_MESH) {
        geometry = new dracoModule.Mesh();
        status = dracoDecoder.DecodeBufferToMesh(buffer, geometry);
    } else if (geometryType === dracoModule.POINT_CLOUD) {
        geometry = new dracoModule.PointCloud();
        status = dracoDecoder.DecodeBufferToPointCloud(buffer, geometry);
    } else {
        dracoModule.destroy(buffer);
        throw new Error('Unknown Draco geometry type');
    }

    if (!status.ok()) {
        dracoModule.destroy(geometry);
        dracoModule.destroy(buffer);
        throw new Error('Draco decode failed: ' + status.error_msg());
    }

    // Extract attributes using GLTF attribute IDs
    // GLTF attribute indices: POSITION=0, NORMAL=1, TEX_COORD=2, etc.
    const result = {
        positions: null,
        normals: null,
        uvs: null,
        indices: null
    };

    // Position (attribute 0 in GLTF, but may be different in Draco)
    const posId = attributeIds.POSITION !== undefined ? attributeIds.POSITION : 0;
    result.positions = extractFloatAttribute(dracoDecoder, geometry, posId, 3);

    // Normal (attribute 1 in GLTF)
    if (attributeIds.NORMAL !== undefined) {
        result.normals = extractFloatAttribute(dracoDecoder, geometry, attributeIds.NORMAL, 3);
    }

    // UV (attribute 2 in GLTF)
    if (attributeIds.TEXCOORD_0 !== undefined) {
        result.uvs = extractFloatAttribute(dracoDecoder, geometry, attributeIds.TEXCOORD_0, 2);
    }

    // Indices (only for meshes, not point clouds)
    if (geometryType === dracoModule.TRIANGULAR_MESH) {
        result.indices = extractIndices(dracoDecoder, geometry);
    }

    // Cleanup
    dracoModule.destroy(geometry);
    dracoModule.destroy(buffer);

    return result;
}

// Message handler
self.onmessage = async (e) => {
    const { requestId, compressedData, attributeIds } = e.data;

    try {
        const result = await decodeMesh(compressedData, attributeIds);

        // Build transfer list
        const transfers = [];
        if (result.positions) transfers.push(result.positions.buffer);
        if (result.normals) transfers.push(result.normals.buffer);
        if (result.uvs) transfers.push(result.uvs.buffer);
        if (result.indices) transfers.push(result.indices.buffer);

        postMessage({
            requestId,
            success: true,
            ...result
        }, transfers);

    } catch (error) {
        postMessage({
            requestId,
            success: false,
            error: error.message || 'Unknown error'
        });
    }
};

// Signal ready
console.log('[Draco Worker] Started');
