# MystralNative - Claude Code Instructions

This document contains important instructions for AI assistants working on the MystralNative codebase.

## Critical Rules

### DO NOT Use Native GLTF/GLB Loaders

**NEVER** use or reference the C++ native GLTF/GLB loading code in this repository. The files exist but are deprecated and will cause compile errors if included:

- `src/gltf/gltf_loader.cpp` - DEPRECATED
- `src/utils/cgltf_impl.cpp` - DEPRECATED
- `include/mystral/gltf/gltf_loader.h` - DEPRECATED
- `third_party/cgltf/` - DEPRECATED

**Why?** MystralNative should use the **JavaScript GLBLoader/GLTFLoader** from the Mystral web engine (same code that runs in browsers). This ensures:

1. **Feature parity** with the web engine
2. **Consistent behavior** across platforms (web, desktop, mobile)
3. **Extension support** like Draco mesh compression (via WASM)
4. **Easier maintenance** - one codebase for all platforms

### How to Load GLTF/GLB Files

In your JavaScript/TypeScript code running on MystralNative, use:

```javascript
// For GLB files (binary GLTF)
import { loadGLBModel } from 'mystral';
const model = await loadGLBModel(device, './path/to/model.glb');

// For GLTF files (JSON + external resources)
import { loadGLTFModel } from 'mystral';
const model = await loadGLTFModel(device, './path/to/model.gltf');
```

These loaders are implemented in:
- `src/loaders/GLBLoader.ts` (main Mystral engine repo)
- `src/loaders/GLTFLoader.ts` (main Mystral engine repo, uses loaders.gl)

## Known Limitations

### Draco Compression Not Supported (Yet)

The JS `GLBLoader` does not currently support Draco-compressed meshes (`KHR_draco_mesh_compression`). When loading Draco-compressed files:
- Meshes will have 0 vertices
- Accessors will be missing `bufferView` fields

**Workaround**: Use uncompressed GLTF/GLB files. The Sponza model, for example, has both compressed and uncompressed versions.

**Task**: "Support Draco compression" is on the roadmap.

**Note on loaders.gl**: We intentionally avoid using loaders.gl for GLTF loading because:
1. It's a large dependency that causes bundle bloat
2. It uses DOM methods like `Image()` which aren't available in the native runtime

The preferred approach is to implement Draco WASM decoding directly in our own GLBLoader.

## Build Configuration

The project uses CMake with several options:

```bash
# Recommended: V8 + Dawn for full shader compatibility
cmake -B build \
  -DMYSTRAL_USE_V8=ON \
  -DMYSTRAL_USE_DAWN=ON \
  -DMYSTRAL_USE_QUICKJS=OFF \
  -DMYSTRAL_USE_WGPU=OFF

cmake --build build --parallel
```

See `README.md` for full build options.

## Repository Sync

This code syncs to the public repository `mystralengine/mystralnative`. See the main repo's `CLAUDE.md` for sync instructions.
