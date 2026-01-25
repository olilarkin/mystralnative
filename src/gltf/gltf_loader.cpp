/**
 * GLTF/GLB Loader Implementation
 *
 * Uses cgltf for parsing GLTF 2.0 files
 */

#include "mystral/gltf/gltf_loader.h"
#include "cgltf.h"
#include <iostream>
#include <fstream>
#include <cstring>

// Android logging
#if defined(__ANDROID__)
#include <android/log.h>
#define GLTF_LOG_TAG "MystralGLTF"
#define GLTF_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, GLTF_LOG_TAG, __VA_ARGS__)
#define GLTF_LOGI(...) __android_log_print(ANDROID_LOG_INFO, GLTF_LOG_TAG, __VA_ARGS__)
#define GLTF_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, GLTF_LOG_TAG, __VA_ARGS__)
#else
#define GLTF_LOGD(...) do { printf("[GLTF DEBUG] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#define GLTF_LOGI(...) do { printf("[GLTF INFO] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#define GLTF_LOGE(...) do { printf("[GLTF ERROR] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#endif

namespace mystral {
namespace gltf {

// Helper to read accessor data as floats
static void readAccessorFloats(const cgltf_accessor* accessor, std::vector<float>& out, int& componentCount) {
    if (!accessor) return;

    componentCount = (int)cgltf_num_components(accessor->type);
    size_t count = accessor->count * componentCount;
    out.resize(count);

    for (size_t i = 0; i < accessor->count; i++) {
        cgltf_accessor_read_float(accessor, i, &out[i * componentCount], componentCount);
    }
}

// Helper to read accessor data as indices
static void readAccessorIndices(const cgltf_accessor* accessor, std::vector<uint32_t>& out) {
    if (!accessor) return;

    out.resize(accessor->count);
    for (size_t i = 0; i < accessor->count; i++) {
        out[i] = (uint32_t)cgltf_accessor_read_index(accessor, i);
    }
}

// Convert cgltf texture to TextureInfo
static TextureInfo convertTexture(const cgltf_texture_view* view, const cgltf_data* data) {
    TextureInfo info;
    if (view && view->texture) {
        if (view->texture->image) {
            info.imageIndex = (int)(view->texture->image - data->images);
        }
        if (view->texture->sampler) {
            info.samplerIndex = (int)(view->texture->sampler - data->samplers);
        }
    }
    return info;
}

std::unique_ptr<GLTFData> loadGLTF(const std::string& path) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        std::cerr << "[GLTF] Failed to parse file: " << path << " (error " << result << ")" << std::endl;
        return nullptr;
    }

    // Load buffers (external files)
    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        std::cerr << "[GLTF] Failed to load buffers for: " << path << std::endl;
        cgltf_free(data);
        return nullptr;
    }

    // Validate
    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        std::cerr << "[GLTF] Validation failed for: " << path << std::endl;
        // Continue anyway, some files may have minor issues
    }

    std::cout << "[GLTF] Loaded: " << path << std::endl;
    std::cout << "[GLTF]   Meshes: " << data->meshes_count << std::endl;
    std::cout << "[GLTF]   Materials: " << data->materials_count << std::endl;
    std::cout << "[GLTF]   Images: " << data->images_count << std::endl;
    std::cout << "[GLTF]   Nodes: " << data->nodes_count << std::endl;

    auto gltfData = std::make_unique<GLTFData>();

    // Extract meshes
    for (size_t mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh& mesh = data->meshes[mi];
        MeshData meshData;
        meshData.name = mesh.name ? mesh.name : "";

        for (size_t pi = 0; pi < mesh.primitives_count; pi++) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            PrimitiveData primData;

            // Read attributes
            for (size_t ai = 0; ai < prim.attributes_count; ai++) {
                const cgltf_attribute& attr = prim.attributes[ai];

                if (attr.type == cgltf_attribute_type_position) {
                    readAccessorFloats(attr.data, primData.positions.data, primData.positions.componentCount);
                    primData.positions.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_normal) {
                    readAccessorFloats(attr.data, primData.normals.data, primData.normals.componentCount);
                    primData.normals.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                    readAccessorFloats(attr.data, primData.texcoords.data, primData.texcoords.componentCount);
                    primData.texcoords.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_tangent) {
                    readAccessorFloats(attr.data, primData.tangents.data, primData.tangents.componentCount);
                    primData.tangents.count = attr.data->count;
                }
            }

            // Read indices
            if (prim.indices) {
                readAccessorIndices(prim.indices, primData.indices);
            }

            // Material reference
            if (prim.material) {
                primData.materialIndex = (int)(prim.material - data->materials);
            }

            meshData.primitives.push_back(std::move(primData));
        }

        gltfData->meshes.push_back(std::move(meshData));
    }

    // Extract materials
    for (size_t mi = 0; mi < data->materials_count; mi++) {
        const cgltf_material& mat = data->materials[mi];
        MaterialData matData;
        matData.name = mat.name ? mat.name : "";

        // PBR metallic-roughness
        if (mat.has_pbr_metallic_roughness) {
            const auto& pbr = mat.pbr_metallic_roughness;
            memcpy(matData.baseColorFactor, pbr.base_color_factor, sizeof(float) * 4);
            matData.metallicFactor = pbr.metallic_factor;
            matData.roughnessFactor = pbr.roughness_factor;
            matData.baseColorTexture = convertTexture(&pbr.base_color_texture, data);
            matData.metallicRoughnessTexture = convertTexture(&pbr.metallic_roughness_texture, data);
        }

        // Normal map
        matData.normalTexture = convertTexture(&mat.normal_texture, data);
        matData.normalScale = mat.normal_texture.scale;

        // Occlusion
        matData.occlusionTexture = convertTexture(&mat.occlusion_texture, data);
        matData.occlusionStrength = mat.occlusion_texture.scale;

        // Emissive
        memcpy(matData.emissiveFactor, mat.emissive_factor, sizeof(float) * 3);
        matData.emissiveTexture = convertTexture(&mat.emissive_texture, data);

        // Alpha mode
        if (mat.alpha_mode == cgltf_alpha_mode_mask) {
            matData.alphaMode = MaterialData::AlphaMode::Mask;
        } else if (mat.alpha_mode == cgltf_alpha_mode_blend) {
            matData.alphaMode = MaterialData::AlphaMode::Blend;
        }
        matData.alphaCutoff = mat.alpha_cutoff;
        matData.doubleSided = mat.double_sided;

        gltfData->materials.push_back(std::move(matData));
    }

    // Extract images
    for (size_t ii = 0; ii < data->images_count; ii++) {
        const cgltf_image& img = data->images[ii];
        ImageData imgData;
        imgData.name = img.name ? img.name : "";
        imgData.uri = img.uri ? img.uri : "";
        imgData.mimeType = img.mime_type ? img.mime_type : "";

        // Check for embedded data
        if (img.buffer_view) {
            imgData.bufferView = (int)(img.buffer_view - data->buffer_views);
            const uint8_t* bufData = (const uint8_t*)img.buffer_view->buffer->data + img.buffer_view->offset;
            imgData.data.assign(bufData, bufData + img.buffer_view->size);
        }

        gltfData->images.push_back(std::move(imgData));
    }

    // Extract nodes
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node& node = data->nodes[ni];
        NodeData nodeData;
        nodeData.name = node.name ? node.name : "";

        if (node.mesh) {
            nodeData.meshIndex = (int)(node.mesh - data->meshes);
        }

        // Transform
        if (node.has_matrix) {
            nodeData.hasMatrix = true;
            memcpy(nodeData.matrix, node.matrix, sizeof(float) * 16);
        } else {
            memcpy(nodeData.translation, node.translation, sizeof(float) * 3);
            memcpy(nodeData.rotation, node.rotation, sizeof(float) * 4);
            memcpy(nodeData.scale, node.scale, sizeof(float) * 3);
        }

        // Children
        for (size_t ci = 0; ci < node.children_count; ci++) {
            nodeData.children.push_back((int)(node.children[ci] - data->nodes));
        }

        gltfData->nodes.push_back(std::move(nodeData));
    }

    // Extract scenes
    for (size_t si = 0; si < data->scenes_count; si++) {
        const cgltf_scene& scene = data->scenes[si];
        SceneData sceneData;
        sceneData.name = scene.name ? scene.name : "";

        for (size_t ni = 0; ni < scene.nodes_count; ni++) {
            sceneData.nodes.push_back((int)(scene.nodes[ni] - data->nodes));
        }

        gltfData->scenes.push_back(std::move(sceneData));
    }

    // Default scene
    if (data->scene) {
        gltfData->defaultScene = (int)(data->scene - data->scenes);
    }

    cgltf_free(data);
    return gltfData;
}

std::unique_ptr<GLTFData> loadGLTFFromMemory(const uint8_t* buffer, size_t size, const std::string& basePath) {
    GLTF_LOGI("loadGLTFFromMemory: buffer=%p, size=%zu, basePath=%s", buffer, size, basePath.c_str());

    if (!buffer || size == 0) {
        GLTF_LOGE("Invalid buffer or size");
        return nullptr;
    }

    // Check GLB magic header
    if (size >= 4) {
        GLTF_LOGD("First 4 bytes: 0x%02x 0x%02x 0x%02x 0x%02x", buffer[0], buffer[1], buffer[2], buffer[3]);
        // GLB magic is "glTF" = 0x46546C67
        bool isGLB = (buffer[0] == 'g' && buffer[1] == 'l' && buffer[2] == 'T' && buffer[3] == 'F');
        GLTF_LOGI("File type: %s", isGLB ? "GLB binary" : "GLTF JSON");
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;

    GLTF_LOGI("Calling cgltf_parse...");
    cgltf_result result = cgltf_parse(&options, buffer, size, &data);
    GLTF_LOGI("cgltf_parse returned: %d", (int)result);

    if (result != cgltf_result_success) {
        GLTF_LOGE("Failed to parse from memory (error %d)", (int)result);
        std::cerr << "[GLTF] Failed to parse from memory (error " << result << ")" << std::endl;
        return nullptr;
    }

    GLTF_LOGI("Parse successful, data=%p", data);
    GLTF_LOGD("buffers_count=%zu, bin=%p, bin_size=%zu", data->buffers_count, data->bin, data->bin_size);

    // For GLB files parsed from memory, cgltf stores the binary chunk in data->bin
    // We need to call cgltf_load_buffers() to set up buffer data pointers
    // It will use data->bin for GLB files even if path is null
    if (data->buffers_count > 0 && data->buffers[0].data == nullptr) {
        GLTF_LOGI("Buffer data is null, calling cgltf_load_buffers...");
        // For GLB files loaded from memory, cgltf_load_buffers will use data->bin
        // Pass the basePath if available, or nullptr for GLB
        const char* loadPath = basePath.empty() ? nullptr : basePath.c_str();
        GLTF_LOGD("Loading buffers with path: %s", loadPath ? loadPath : "(null/GLB embedded)");
        result = cgltf_load_buffers(&options, data, loadPath);
        if (result != cgltf_result_success) {
            GLTF_LOGE("Failed to load buffers (error %d)", (int)result);
            std::cerr << "[GLTF] Failed to load buffers" << std::endl;
        } else {
            GLTF_LOGI("Buffers loaded successfully, buffer[0].data=%p", data->buffers[0].data);
        }
    } else if (data->buffers_count > 0) {
        GLTF_LOGD("Buffer data already set, data=%p", data->buffers[0].data);
    }

    // Continue with same extraction logic as loadGLTF
    // (In production, this would be refactored to share code)

    GLTF_LOGI("Loaded from memory successfully");
    GLTF_LOGI("  Meshes: %zu", data->meshes_count);
    GLTF_LOGI("  Materials: %zu", data->materials_count);
    GLTF_LOGI("  Images: %zu", data->images_count);

    std::cout << "[GLTF] Loaded from memory" << std::endl;
    std::cout << "[GLTF]   Meshes: " << data->meshes_count << std::endl;
    std::cout << "[GLTF]   Materials: " << data->materials_count << std::endl;
    std::cout << "[GLTF]   Images: " << data->images_count << std::endl;

    auto gltfData = std::make_unique<GLTFData>();
    GLTF_LOGD("Created GLTFData object");

    // Extract meshes (same as above)
    GLTF_LOGI("Extracting %zu meshes...", data->meshes_count);
    for (size_t mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh& mesh = data->meshes[mi];
        MeshData meshData;
        meshData.name = mesh.name ? mesh.name : "";

        for (size_t pi = 0; pi < mesh.primitives_count; pi++) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            PrimitiveData primData;

            for (size_t ai = 0; ai < prim.attributes_count; ai++) {
                const cgltf_attribute& attr = prim.attributes[ai];

                if (attr.type == cgltf_attribute_type_position) {
                    readAccessorFloats(attr.data, primData.positions.data, primData.positions.componentCount);
                    primData.positions.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_normal) {
                    readAccessorFloats(attr.data, primData.normals.data, primData.normals.componentCount);
                    primData.normals.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                    readAccessorFloats(attr.data, primData.texcoords.data, primData.texcoords.componentCount);
                    primData.texcoords.count = attr.data->count;
                } else if (attr.type == cgltf_attribute_type_tangent) {
                    readAccessorFloats(attr.data, primData.tangents.data, primData.tangents.componentCount);
                    primData.tangents.count = attr.data->count;
                }
            }

            if (prim.indices) {
                readAccessorIndices(prim.indices, primData.indices);
            }

            if (prim.material) {
                primData.materialIndex = (int)(prim.material - data->materials);
            }

            meshData.primitives.push_back(std::move(primData));
        }

        gltfData->meshes.push_back(std::move(meshData));
        GLTF_LOGD("Extracted mesh %zu: %s", mi, meshData.name.c_str());
    }
    GLTF_LOGI("Mesh extraction complete");

    // Extract materials
    GLTF_LOGI("Extracting %zu materials...", data->materials_count);
    for (size_t mi = 0; mi < data->materials_count; mi++) {
        const cgltf_material& mat = data->materials[mi];
        MaterialData matData;
        matData.name = mat.name ? mat.name : "";

        if (mat.has_pbr_metallic_roughness) {
            const auto& pbr = mat.pbr_metallic_roughness;
            memcpy(matData.baseColorFactor, pbr.base_color_factor, sizeof(float) * 4);
            matData.metallicFactor = pbr.metallic_factor;
            matData.roughnessFactor = pbr.roughness_factor;
            matData.baseColorTexture = convertTexture(&pbr.base_color_texture, data);
            matData.metallicRoughnessTexture = convertTexture(&pbr.metallic_roughness_texture, data);
        }

        matData.normalTexture = convertTexture(&mat.normal_texture, data);
        matData.normalScale = mat.normal_texture.scale;
        matData.occlusionTexture = convertTexture(&mat.occlusion_texture, data);
        matData.occlusionStrength = mat.occlusion_texture.scale;
        memcpy(matData.emissiveFactor, mat.emissive_factor, sizeof(float) * 3);
        matData.emissiveTexture = convertTexture(&mat.emissive_texture, data);

        if (mat.alpha_mode == cgltf_alpha_mode_mask) {
            matData.alphaMode = MaterialData::AlphaMode::Mask;
        } else if (mat.alpha_mode == cgltf_alpha_mode_blend) {
            matData.alphaMode = MaterialData::AlphaMode::Blend;
        }
        matData.alphaCutoff = mat.alpha_cutoff;
        matData.doubleSided = mat.double_sided;

        gltfData->materials.push_back(std::move(matData));
        GLTF_LOGD("Extracted material %zu: %s", mi, matData.name.c_str());
    }
    GLTF_LOGI("Material extraction complete");

    // Extract images
    GLTF_LOGI("Extracting %zu images...", data->images_count);
    for (size_t ii = 0; ii < data->images_count; ii++) {
        const cgltf_image& img = data->images[ii];
        ImageData imgData;
        imgData.name = img.name ? img.name : "";
        imgData.uri = img.uri ? img.uri : "";
        imgData.mimeType = img.mime_type ? img.mime_type : "";

        if (img.buffer_view) {
            imgData.bufferView = (int)(img.buffer_view - data->buffer_views);
            const uint8_t* bufData = (const uint8_t*)img.buffer_view->buffer->data + img.buffer_view->offset;
            imgData.data.assign(bufData, bufData + img.buffer_view->size);
        }

        gltfData->images.push_back(std::move(imgData));
        GLTF_LOGD("Extracted image %zu: %s", ii, imgData.name.c_str());
    }
    GLTF_LOGI("Image extraction complete");

    // Extract nodes
    GLTF_LOGI("Extracting %zu nodes...", data->nodes_count);
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node& node = data->nodes[ni];
        NodeData nodeData;
        nodeData.name = node.name ? node.name : "";

        if (node.mesh) {
            nodeData.meshIndex = (int)(node.mesh - data->meshes);
        }

        if (node.has_matrix) {
            nodeData.hasMatrix = true;
            memcpy(nodeData.matrix, node.matrix, sizeof(float) * 16);
        } else {
            memcpy(nodeData.translation, node.translation, sizeof(float) * 3);
            memcpy(nodeData.rotation, node.rotation, sizeof(float) * 4);
            memcpy(nodeData.scale, node.scale, sizeof(float) * 3);
        }

        for (size_t ci = 0; ci < node.children_count; ci++) {
            nodeData.children.push_back((int)(node.children[ci] - data->nodes));
        }

        gltfData->nodes.push_back(std::move(nodeData));
    }
    GLTF_LOGI("Node extraction complete");

    // Extract scenes
    GLTF_LOGI("Extracting %zu scenes...", data->scenes_count);
    for (size_t si = 0; si < data->scenes_count; si++) {
        const cgltf_scene& scene = data->scenes[si];
        SceneData sceneData;
        sceneData.name = scene.name ? scene.name : "";

        for (size_t ni = 0; ni < scene.nodes_count; ni++) {
            sceneData.nodes.push_back((int)(scene.nodes[ni] - data->nodes));
        }

        gltfData->scenes.push_back(std::move(sceneData));
    }
    GLTF_LOGI("Scene extraction complete");

    if (data->scene) {
        gltfData->defaultScene = (int)(data->scene - data->scenes);
    }

    GLTF_LOGI("Calling cgltf_free...");
    cgltf_free(data);
    GLTF_LOGI("cgltf_free complete, returning GLTFData");
    return gltfData;
}

} // namespace gltf
} // namespace mystral
