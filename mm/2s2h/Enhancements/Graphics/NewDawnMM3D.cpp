#include "NewDawnMM3D.h"

#include <fast/Fast3dWindow.h>
#include <fast/newdawn_mm3d.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "global.h"
}

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

struct SceneEntry {
    int id;
    const char* name;
};
#define MM3D_SCENE(id, name) { id, name },
const SceneEntry sAllowedScenes[] = {
#include "MM3DAllowlist.inc"
};
#undef MM3D_SCENE

struct OwnedTexture {
    std::vector<uint8_t> pixels;
    NewDawnMM3DTexture view{};
};

struct OwnedMesh {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    std::vector<NewDawnMM3DDraw> draws;
    std::vector<NewDawnMM3DTexture> textures;
    NewDawnMM3DMesh view{};
};

struct OwnedScene {
    fs::path root;
    Json manifest;
    std::map<std::string, std::unique_ptr<OwnedTexture>> textures;
    std::map<std::string, std::unique_ptr<OwnedMesh>> meshes;
    std::map<std::pair<int, int>, std::vector<const NewDawnMM3DMesh*>> rooms;
    size_t vertexBytes = 0, indexBytes = 0, textureBytes = 0;
};

// Never evict CPU objects while an interpolated display list may retain pointers
// into them. The renderer independently uploads/caches their immutable buffers.
std::map<std::string, std::unique_ptr<OwnedScene>> sScenes;
std::set<std::string> sWarnings;
thread_local NewDawnMM3DRoomParams* sActive[2] = {};

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

uint64_t StableId(const std::string& path) {
    uint64_t value = UINT64_C(14695981039346656037);
    for (unsigned char c : path) {
        value = (value ^ c) * UINT64_C(1099511628211);
    }
    return value;
}

fs::path AssetPath(const fs::path& root, const std::string& relative) {
    fs::path rel(relative);
    Require(!rel.empty() && !rel.is_absolute(), "asset path must be relative to MM3D root");
    const fs::path result = fs::weakly_canonical(root / rel);
    const fs::path inside = result.lexically_relative(root);
    Require(!inside.empty() && *inside.begin() != "..", "asset path escapes MM3D root");
    return result;
}

std::vector<uint8_t> ReadBytes(const fs::path& path, size_t maximum = 256U * 1024U * 1024U) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Require(input.is_open(), "MM3D asset is missing");
    const auto end = input.tellg();
    Require(end >= 0 && static_cast<uint64_t>(end) <= maximum, "MM3D asset has invalid size");
    std::vector<uint8_t> data(static_cast<size_t>(end));
    input.seekg(0);
    Require(data.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(data.data()), data.size())),
            "MM3D asset read failed");
    return data;
}

uint32_t ReadLE32(const std::vector<uint8_t>& data, size_t offset) {
    Require(offset <= data.size() && data.size() - offset >= 4, "truncated MM3D numeric field");
    return uint32_t(data[offset]) | uint32_t(data[offset + 1]) << 8 | uint32_t(data[offset + 2]) << 16 |
           uint32_t(data[offset + 3]) << 24;
}

float ReadLEFloat(const std::vector<uint8_t>& data, size_t offset) {
    uint32_t bits = ReadLE32(data, offset);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    Require(std::isfinite(value), "nonfinite MM3D vertex attribute");
    return value;
}

float Number(const Json& value) {
    float result = value.get<float>();
    Require(std::isfinite(result), "nonfinite MM3D metadata value");
    return result;
}

void FloatArray(const Json& source, float* output, size_t count, float scale = 1.0f) {
    Require(source.is_array() && source.size() == count, "MM3D metadata array length mismatch");
    for (size_t i = 0; i < count; ++i) {
        output[i] = Number(source.at(i)) * scale;
        Require(std::isfinite(output[i]), "MM3D metadata value overflow");
    }
}

void BlendArray(const Json& source, uint32_t* output) {
    Require(source.is_array() && source.size() == 3, "invalid MM3D blend state");
    for (int i = 0; i < 3; ++i) {
        output[i] = source.at(i).get<uint32_t>();
    }
    auto factor = [](uint32_t v) { return v <= 1 || (v >= 0x300 && v <= 0x308) || (v >= 0x8001 && v <= 0x8004); };
    Require(factor(output[0]) && factor(output[1]) &&
                (output[2] == 0x8006 || output[2] == 0x800A || output[2] == 0x800B),
            "unsupported MM3D GL blend enum");
}

NewDawnMM3DMaterial ReadMaterial(const Json& source, size_t textureCount) {
    NewDawnMM3DMaterial mat{};
    Require(source.at("bindings").size() == 3 && source.at("coordinators").size() == 3,
            "invalid MM3D material texture bindings");
    for (int i = 0; i < 3; ++i) {
        const Json& binding = source.at("bindings").at(i);
        const Json& coordinator = source.at("coordinators").at(i);
        auto& out = mat.bindings[i];
        out.texture = binding.at("texture").get<int>();
        Require(out.texture >= -1 && (out.texture < 0 || size_t(out.texture) < textureCount),
                "MM3D material texture reference out of range");
        out.wrapS = binding.at("wrap_s").get<uint32_t>();
        out.wrapT = binding.at("wrap_t").get<uint32_t>();
        out.minFilter = binding.at("min_filter").get<uint32_t>();
        out.magFilter = binding.at("mag_filter").get<uint32_t>();
        out.source = coordinator.at("source").get<int>();
        out.mapping = coordinator.at("mapping").get<int>();
        Require(out.source >= 0 && out.source <= 2 && out.mapping >= 0 && out.mapping <= 4,
                "unsupported MM3D texture coordinate source");
        FloatArray(coordinator.at("transform"), out.transform, 5);
        if (out.texture >= 0) {
            auto wrap = [](uint32_t v) { return v == 0x2900 || v == 0x2901 || v == 0x812F || v == 0x8370; };
            Require(wrap(out.wrapS) && wrap(out.wrapT), "unsupported MM3D texture wrap enum");
            Require(out.minFilter == 0x2600 || out.minFilter == 0x2601 ||
                        (out.minFilter >= 0x2700 && out.minFilter <= 0x2703),
                    "unsupported MM3D texture filter enum");
            Require(out.magFilter == 0x2600 || out.magFilter == 0x2601, "unsupported MM3D mag filter enum");
        }
    }
    const std::string cull = source.at("cull").get<std::string>();
    Require(cull == "none" || cull == "back" || cull == "front", "invalid MM3D cull mode");
    mat.cull = cull == "back" ? 1 : (cull == "front" ? 2 : 0);
    mat.blend = source.at("blend_mode").get<int>() != 0;
    mat.alphaTest = source.at("alpha_test").get<bool>();
    mat.depthTest = source.at("depth_test").get<bool>();
    mat.depthWrite = source.at("depth_write").get<bool>();
    mat.fog = source.at("fog").get<bool>();
    mat.fragmentLighting = source.at("fragment_lighting").get<bool>();
    mat.vertexLighting = source.at("vertex_lighting").get<bool>();
    mat.alphaFunction = source.at("alpha_function").get<uint32_t>();
    mat.depthFunction = source.at("depth_function").get<uint32_t>();
    Require(mat.alphaFunction >= 0x200 && mat.alphaFunction <= 0x207 && mat.depthFunction >= 0x200 &&
                mat.depthFunction <= 0x207,
            "unsupported MM3D depth/alpha compare enum");
    mat.alphaReference = Number(source.at("alpha_reference"));
    Require(mat.alphaReference >= 0 && mat.alphaReference <= 1, "invalid MM3D alpha reference");
    BlendArray(source.at("blend_rgb"), mat.blendRgb);
    BlendArray(source.at("blend_alpha"), mat.blendAlpha);
    FloatArray(source.at("blend_constant"), mat.blendConstant, 4);
    FloatArray(source.at("diffuse"), mat.diffuse, 4, 1.0f / 255.0f);
    FloatArray(source.at("ambient"), mat.ambient, 4, 1.0f / 255.0f);
    FloatArray(source.at("emission"), mat.emission, 4, 1.0f / 255.0f);
    Require(source.at("constants").size() == 6, "invalid MM3D constant colours");
    for (int i = 0; i < 6; ++i) {
        FloatArray(source.at("constants").at(i), mat.constants[i], 4, 1.0f / 255.0f);
    }
    const Json& combiners = source.at("combiners");
    Require(combiners.is_array() && combiners.size() <= 6, "too many MM3D combiners");
    mat.combinerCount = combiners.size();
    for (int i = 0; i < mat.combinerCount; ++i) {
        Require(combiners.at(i).size() == 20, "invalid MM3D combiner length");
        for (int j = 0; j < 20; ++j) {
            mat.combiners[i][j] = combiners.at(i).at(j).get<int>();
        }
    }
    // Exported mirror-decal projector stages are tex1 * constant.a(0) + previous.
    // Fold only this proved identity. Never invent projector coordinates when a
    // material actually needs them; keep that scene native instead.
    for (int binding = 0; binding < 3; ++binding) {
        auto& b = mat.bindings[binding];
        if (b.texture < 0 || (b.mapping != 2 && b.mapping != 4)) continue;
        const int textureSource = 0x84C0 + binding;
        for (int i = 0; i < mat.combinerCount; ++i) {
            int* c = mat.combiners[i];
            bool rgbReference = c[6] == textureSource || c[7] == textureSource || c[8] == textureSource;
            bool alphaReference = c[12] == textureSource || c[13] == textureSource || c[14] == textureSource;
            if (!rgbReference && !alphaReference) continue;
            Require(!alphaReference && c[0] == 0x6401 && c[6] == textureSource && c[7] == 0x8576 &&
                    c[8] == 0x8578 && c[9] == 0x300 && c[10] == 0x302 && c[11] == 0x300 &&
                    c[18] >= 0 && c[18] < 6 && mat.constants[c[18]][3] == 0.0f,
                    "active MM3D projector/cubemap needs native camera data; retaining N64 scene");
            c[0] = 0x1E01; c[6] = 0x8578; c[9] = 0x300;
        }
        b.texture = -1; b.mapping = 0;
    }
    return mat;
}

OwnedTexture& LoadTexture(OwnedScene& scene, const Json& source) {
    Require(!source.at("cubemap").get<bool>(), "room cubemaps are not supported");
    const Json& faces = source.at("faces");
    Require(faces.is_array() && faces.size() == 1 && !faces.at(0).empty(), "missing MM3D texture payload");
    const Json& level = faces.at(0).at(0);
    const std::string relative = level.at("path").get<std::string>();
    auto existing = scene.textures.find(relative);
    if (existing != scene.textures.end()) {
        return *existing->second;
    }
    auto texture = std::make_unique<OwnedTexture>();
    texture->view.width = level.at("width").get<uint32_t>();
    texture->view.height = level.at("height").get<uint32_t>();
    Require(texture->view.width > 0 && texture->view.width <= 8192 && texture->view.height > 0 &&
                texture->view.height <= 8192,
            "invalid MM3D texture dimensions");
    const fs::path path = AssetPath(scene.root, relative);
    texture->pixels = ReadBytes(path);
    Require(texture->pixels.size() == size_t(texture->view.width) * texture->view.height * 4,
            "MM3D RGBA payload size mismatch");
    texture->view.id = StableId(path.string());
    texture->view.rgba = texture->pixels.data();
    scene.textureBytes += texture->pixels.size();
    return *scene.textures.emplace(relative, std::move(texture)).first->second;
}

OwnedMesh& LoadMesh(OwnedScene& scene, const std::string& relative) {
    auto existing = scene.meshes.find(relative);
    if (existing != scene.meshes.end()) {
        return *existing->second;
    }
    const fs::path path = AssetPath(scene.root, relative);
    const auto bytes = ReadBytes(path);
    Require(bytes.size() >= 24 && std::memcmp(bytes.data(), "NDMM3D01", 8) == 0, "invalid MM3D mesh magic");
    const uint32_t jsonSize = ReadLE32(bytes, 8), nv = ReadLE32(bytes, 12), ni = ReadLE32(bytes, 16),
                   stride = ReadLE32(bytes, 20);
    Require(jsonSize <= 32U * 1024U * 1024U && nv <= 2000000 && ni <= 12000000 && stride == 48 && ni % 3 == 0,
            "invalid MM3D mesh header");
    const uint64_t vertexStart = uint64_t(24) + jsonSize;
    const uint64_t indexStart = vertexStart + uint64_t(nv) * stride;
    Require(indexStart + uint64_t(ni) * 4 == bytes.size(), "MM3D mesh payload size mismatch");
    const Json meta = Json::parse(bytes.begin() + 24, bytes.begin() + vertexStart);
    auto mesh = std::make_unique<OwnedMesh>();
    mesh->vertices.resize(size_t(nv) * 16, 0.0f);
    float bounds[2][3] = {};
    if (nv) {
        Require(meta.at("bounds").size() == 2, "missing MM3D bounds");
        FloatArray(meta.at("bounds").at(0), bounds[0], 3);
        FloatArray(meta.at("bounds").at(1), bounds[1], 3);
        for (int j = 0; j < 3; ++j) {
            Require(bounds[0][j] <= bounds[1][j], "inverted MM3D bounds");
        }
    }
    for (uint32_t i = 0; i < nv; ++i) {
        for (int j = 0; j < 12; ++j) {
            const float value = ReadLEFloat(bytes, vertexStart + size_t(i) * stride + j * 4);
            if (j < 3) {
                Require(std::abs(value) <= 1000000.0f && value >= bounds[0][j] - 0.01f && value <= bounds[1][j] + 0.01f,
                        "MM3D position outside bounds");
            }
            mesh->vertices[size_t(i) * 16 + j] = value;
        }
    }
    mesh->indices.reserve(ni);
    for (uint32_t i = 0; i < ni; ++i) {
        uint32_t index = ReadLE32(bytes, indexStart + size_t(i) * 4);
        Require(index < nv, "MM3D vertex index out of range");
        mesh->indices.push_back(index);
    }
    const Json& textureMeta = meta.at("textures");
    Require(textureMeta.is_array() && textureMeta.size() <= 4096, "too many MM3D textures");
    mesh->textures.reserve(textureMeta.size());
    for (const Json& item : textureMeta) {
        mesh->textures.push_back(LoadTexture(scene, item).view);
    }
    std::vector<NewDawnMM3DMaterial> materials;
    for (const Json& item : meta.at("materials")) {
        materials.push_back(ReadMaterial(item, mesh->textures.size()));
    }
    Require(meta.at("draws").is_array() && meta.at("draws").size() <= 65536, "too many MM3D draws");
    for (const Json& item : meta.at("draws")) {
        NewDawnMM3DDraw draw{};
        draw.firstIndex = item.at("first_index").get<uint32_t>();
        draw.indexCount = item.at("index_count").get<uint32_t>();
        const uint32_t material = item.at("material").get<uint32_t>();
        Require(draw.firstIndex <= ni && draw.indexCount <= ni - draw.firstIndex && draw.indexCount % 3 == 0 &&
                    material < materials.size(),
                "invalid MM3D draw range/material");
        draw.material = materials[material];
        const uint32_t vertexFirst = item.at("vertex_first").get<uint32_t>();
        const uint32_t vertexCount = item.at("vertex_count").get<uint32_t>();
        Require(vertexFirst <= nv && vertexCount <= nv - vertexFirst, "invalid MM3D draw vertex span");
        for (auto uv = item.at("extra_uvs").begin(); uv != item.at("extra_uvs").end(); ++uv) {
            Require(uv.key() == "1" || uv.key() == "2", "invalid MM3D extra UV stream");
            Require(uv.value().is_array() && uv.value().size() == vertexCount, "MM3D extra UV count mismatch");
            const int component = uv.key() == "1" ? 12 : 14;
            for (uint32_t i = 0; i < vertexCount; ++i) {
                FloatArray(uv.value().at(i), &mesh->vertices[size_t(vertexFirst + i) * 16 + component], 2);
            }
        }
        mesh->draws.push_back(draw);
    }
    mesh->view.id = StableId(path.string());
    mesh->view.vertexCount = nv;
    mesh->view.indexCount = ni;
    mesh->view.drawCount = mesh->draws.size();
    mesh->view.textureCount = mesh->textures.size();
    mesh->view.vertices = mesh->vertices.data();
    mesh->view.indices = mesh->indices.data();
    mesh->view.draws = mesh->draws.data();
    mesh->view.textures = mesh->textures.data();
    scene.vertexBytes += mesh->vertices.size() * sizeof(float);
    scene.indexBytes += mesh->indices.size() * sizeof(uint32_t);
    return *scene.meshes.emplace(relative, std::move(mesh)).first->second;
}

const Json& SelectSetup(const Json& setups, int layer) {
    Require(setups.is_array() && !setups.empty(), "missing MM3D setup headers");
    const Json* main = nullptr;
    for (const Json& setup : setups) {
        if (setup.at("header").get<int>() == 0) {
            main = &setup;
            break;
        }
    }
    Require(main != nullptr, "missing MM3D main setup");
    if (layer > 0) {
        for (const Json& reference : main->at("setup_refs")) {
            if (reference.at("slot").get<int>() == layer - 1 && !reference.at("header").is_null()) {
                int header = reference.at("header").get<int>();
                for (const Json& setup : setups) {
                    if (setup.at("header").get<int>() == header) {
                        return setup;
                    }
                }
                throw std::runtime_error("MM3D setup references missing header");
            }
        }
    }
    return *main;
}

bool HasCommand(const Json& setup, int opcode) {
    return std::any_of(setup.at("commands").begin(), setup.at("commands").end(),
                       [opcode](const Json& cmd) { return cmd.at("opcode").get<int>() == opcode; });
}

const std::vector<const NewDawnMM3DMesh*>& RoomMeshes(OwnedScene& scene, int room, int layer) {
    const auto key = std::make_pair(room, layer);
    auto existing = scene.rooms.find(key);
    if (existing != scene.rooms.end()) {
        return existing->second;
    }
    std::vector<const NewDawnMM3DMesh*> meshes;
    const Json& selectedScene = SelectSetup(scene.manifest.at("setups"), layer);
    const Json& sceneRooms = HasCommand(selectedScene, 4) ? selectedScene.at("rooms")
                                                          : SelectSetup(scene.manifest.at("setups"), 0).at("rooms");
    std::string roomSource;
    for (const Json& ref : sceneRooms) {
        const std::string filename = fs::path(ref.get<std::string>()).filename().string();
        const std::string suffix = "_" + std::to_string(room) + "_info.zsi";
        if (filename.size() > suffix.size() &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
            roomSource = filename;
            break;
        }
    }
    if (!roomSource.empty()) {
        for (const Json& candidate : scene.manifest.at("rooms")) {
            if (candidate.at("source").get<std::string>() != roomSource) {
                continue;
            }
            const Json& selected = SelectSetup(candidate.at("setups"), layer);
            const Json& chosen = HasCommand(selected, 0x0A) ? selected : SelectSetup(candidate.at("setups"), 0);
            for (const Json& mesh : chosen.at("meshes")) {
                const auto found = scene.meshes.find(mesh.at("path").get<std::string>());
                Require(found != scene.meshes.end(), "MM3D room references unloaded mesh");
                if (found->second->view.indexCount) {
                    meshes.push_back(&found->second->view);
                }
            }
            break;
        }
    }
    return scene.rooms.emplace(key, std::move(meshes)).first->second;
}

OwnedScene* LoadScene(const std::string& rootName, const char* name) {
    const std::string key = rootName + "/" + name;
    auto found = sScenes.find(key);
    if (found != sScenes.end()) {
        return found->second.get();
    }
    const auto started = std::chrono::steady_clock::now();
    auto scene = std::make_unique<OwnedScene>();
    scene->root = fs::canonical(fs::path(rootName));
    const auto manifestBytes =
        ReadBytes(AssetPath(scene->root, std::string(name) + "/manifest.json"), 32U * 1024U * 1024U);
    scene->manifest = Json::parse(manifestBytes.begin(), manifestBytes.end());
    Require(scene->manifest.at("format") == "NDMM3D01" && scene->manifest.at("scene") == name,
            "MM3D scene manifest mismatch");
    Require(scene->manifest.at("rooms").is_array(), "invalid MM3D room list");
    for (const Json& room : scene->manifest.at("rooms")) {
        for (const Json& setup : room.at("setups")) {
            for (const Json& mesh : setup.at("meshes")) {
                LoadMesh(*scene, mesh.at("path").get<std::string>());
            }
        }
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    SPDLOG_INFO("NewDawnMM3D: loaded {} in {:.3f} ms CPU: {} meshes, {} vertex bytes, {} index bytes, {} texture bytes",
                name, elapsed, scene->meshes.size(), scene->vertexBytes, scene->indexBytes, scene->textureBytes);
    return sScenes.emplace(key, std::move(scene)).first->second.get();
}

void FillLighting(PlayState* play, NewDawnMM3DRoomParams& params) {
    for (int i = 0; i < 3; ++i) {
        params.ambient[i] = play->lightCtx.ambientColor[i] / 255.0f;
        params.fogColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    params.fogNear = play->lightCtx.fogNear;
    const LightDirectional* lights[] = { &play->envCtx.dirLight1.params.dir, &play->envCtx.dirLight2.params.dir };
    for (int i = 0; i < 2; ++i) {
        const auto& light = *lights[i];
        const float direction[] = { float(light.x), float(light.y), float(light.z) };
        const float length =
            std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        for (int c = 0; c < 3; ++c) {
            params.lightDirection[i][c] = length > 0.001f ? direction[c] / length : 0.0f;
            params.lightColor[i][c] = light.color[c] / 255.0f;
        }
    }
}
} // namespace

extern "C" bool NewDawnMM3D_BeginRoom(PlayState* play, Room* room, u32 flags) {
    sActive[0] = sActive[1] = nullptr;
    if (!CVarGetInteger("gNewDawn.MM3D.Rooms", 0) || !play || !room || !room->roomShape ||
        room->roomShape->base.type == ROOM_SHAPE_TYPE_IMAGE || room->roomShape->base.type == ROOM_SHAPE_TYPE_NONE ||
        Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() != Fast::WindowBackend::FAST3D_SDL_OPENGL) {
        return false;
    }
    const SceneEntry* entry = nullptr;
    for (const auto& candidate : sAllowedScenes) {
        if (candidate.id == play->sceneId) {
            entry = &candidate;
            break;
        }
    }
    const std::string root = CVarGetString("gNewDawn.MM3D.Path", "");
    if (!entry || root.empty()) {
        return false;
    }
    try {
        const std::string key = root + "/" + entry->name;
        if (sWarnings.count(key)) {
            return false;
        }
        OwnedScene* scene = nullptr;
        try {
            scene = LoadScene(root, entry->name);
        } catch (const std::exception& exception) {
            sWarnings.insert(key);
            SPDLOG_WARN("NewDawnMM3D: keeping N64 scene {}: {} (root {})", entry->name, exception.what(), root);
            return false;
        }
        const auto& meshes = RoomMeshes(*scene, room->num, gSaveContext.sceneLayer);
        if (meshes.empty()) {
            const std::string warning =
                key + "/room" + std::to_string(room->num) + "/layer" + std::to_string(gSaveContext.sceneLayer);
            if (sWarnings.insert(warning).second) {
                SPDLOG_WARN("NewDawnMM3D: keeping empty/missing N64 room {}:{} setup {}", entry->name, room->num,
                            gSaveContext.sceneLayer);
            }
            return false;
        }
        OPEN_DISPS(play->state.gfxCtx);
        for (int pass = 0; pass < 2; ++pass) {
            if ((flags & (pass ? ROOM_DRAW_XLU : ROOM_DRAW_OPA)) == 0) {
                continue;
            }
            auto* params =
                static_cast<NewDawnMM3DRoomParams*>(GRAPH_ALLOC(play->state.gfxCtx, sizeof(NewDawnMM3DRoomParams)));
            Require(params != nullptr, "MM3D frame parameter allocation failed");
            *params = {};
            params->scene = play->sceneId;
            params->room = room->num;
            params->pass = pass;
            params->meshCount = meshes.size();
            params->meshes = meshes.data();
            FillLighting(play, *params);
            sActive[pass] = params;
        }
        // Allocate/validate both parameter blocks before suppressing either
        // stream. A failure above must leave the native room fully drawable.
        for (int pass = 0; pass < 2; ++pass) {
            if (!sActive[pass]) {
                continue;
            }
            if (pass == 0) {
                gSPNewDawnMM3D(POLY_OPA_DISP++, 0, sActive[pass]);
            } else {
                gSPNewDawnMM3D(POLY_XLU_DISP++, 0, sActive[pass]);
            }
        }
        CLOSE_DISPS(play->state.gfxCtx);
        return true;
    } catch (const std::exception& exception) {
        const std::string key = root + "/" + entry->name;
        if (sWarnings.insert(key).second) {
            SPDLOG_WARN("NewDawnMM3D: keeping N64 scene {}: {}", entry->name, exception.what());
        }
        return false;
    }
}

extern "C" void NewDawnMM3D_EndRoom(PlayState* play, Room* room, u32 flags) {
    if (!play || !room) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    for (int pass = 0; pass < 2; ++pass) {
        if (sActive[pass] && (flags & (pass ? ROOM_DRAW_XLU : ROOM_DRAW_OPA))) {
            if (pass == 0) {
                gSPNewDawnMM3D(POLY_OPA_DISP++, 1, sActive[pass]);
            } else {
                gSPNewDawnMM3D(POLY_XLU_DISP++, 1, sActive[pass]);
            }
        }
        sActive[pass] = nullptr;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
