// See RuntimeTextures.h. Driver around TextureSynth: decodes the player's N64 textures, runs the synthesis on all cores,
// writes the cache archive and mounts it. Mirrors RuntimeMaterials.cpp (cache, signature, staging, boot popup).
#include "RuntimeTextures.h"
#include "TextureSynth.h"
#include "UniqueArt.h"
#include "Mm3dCharacters.h"
#include "ZipWriter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <libultraship/classes.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>

#include <fast/interpreter.h>
#include "2s2h/BenPort.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace NewDawnTextures {
namespace {

// Bump when the synthesis or the encoding changes; the data files and the library manifest are hashed into the signature.
constexpr const char* kVersion = "1.2.0-cpp";
constexpr const char* kDataDir = "newdawn_textures/"; // inside 2ship.o2r (mm/assets/custom/newdawn_textures)
constexpr const char* kArchiveName = "newdawn-textures.o2r";
constexpr uint32_t kTextureType = 0x4F544558; // "OTEX"

std::mutex gMutex;
State gState = State::None;
std::atomic<uint32_t> gDone{ 0 }, gTotal{ 0 };
std::atomic<bool> gRunning{ false };
bool gMounted = false, gPendingRestart = false, gStagedOk = false, gOn = false;
std::string gMessage, gArchivePath;
uint32_t gCount = 0, gSkipped = 0;
double gSeconds = 0;

void SetState(State s, const std::string& message = "") {
    std::lock_guard<std::mutex> lock(gMutex);
    gState = s;
    gMessage = message;
}

fs::path Root() {
    return fs::path(Ship::Context::GetPathRelativeToAppDirectory("cache/newdawn-textures", appShortName));
}
fs::path Live() {
    return Root() / "live";
}
fs::path Staging() {
    return Root() / "staging";
}
std::shared_ptr<Ship::ArchiveManager> Archives() {
    return Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
}
std::string SpanPath() {
    return (fs::path(CVarGetString("gNewDawn.Assets.Path", "assets/newdawn")) / "mm3d" / "span_v4_4x.bin").string();
}
fs::path LibraryDir() {
    return fs::path(CVarGetString("gNewDawn.Assets.Path", "assets/newdawn")) / "materials";
}

uint64_t Fnv1a(const void* data, size_t n, uint64_t h = 14695981039346656037ULL) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}
std::string Hex16(uint64_t v) {
    char b[17];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)v);
    return b;
}

void Put32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
uint32_t Get32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

std::vector<uint8_t> EncodeTexture(uint32_t kind, uint32_t w, uint32_t h, uint32_t flags, float hs, float vs,
                                   const uint8_t* px, size_t n) {
    std::vector<uint8_t> out(92 + n, 0);
    out[1] = 1; // custom resource
    Put32(&out[4], kTextureType);
    Put32(&out[8], 1);
    Put32(&out[12], 0xDEADBEEF);
    Put32(&out[16], 0xDEADBEEF);
    uint32_t hb, vb;
    std::memcpy(&hb, &hs, 4);
    std::memcpy(&vb, &vs, 4);
    Put32(&out[64], kind);
    Put32(&out[68], w);
    Put32(&out[72], h);
    Put32(&out[76], flags);
    Put32(&out[80], hb);
    Put32(&out[84], vb);
    Put32(&out[88], (uint32_t)n);
    std::memcpy(&out[92], px, n);
    return out;
}

// ------------------------------------------------------------------ the player's N64 originals
struct Original {
    uint32_t type = 0, w = 0, h = 0;
    std::vector<uint8_t> rgba; // decoded, palette applied
    bool cutout = false;       // real transparency: not replaceable by an opaque material
};

// Raw N64 texture resource (header 64 B, then V0 {type,w,h,size} or V1 {type,w,h,flags,hscale,vscale,size}).
bool LoadRaw(const std::string& path, uint32_t& type, uint32_t& w, uint32_t& h, std::vector<uint8_t>& data) {
    auto file = Archives()->LoadFile(path);
    if (!file || !file->Buffer || file->Buffer->size() < 80) {
        return false;
    }
    const auto* b = reinterpret_cast<const uint8_t*>(file->Buffer->data());
    const size_t n = file->Buffer->size();
    if (b[0] != 0 || Get32(b + 4) != kTextureType) { // little endian only
        return false;
    }
    const uint32_t version = Get32(b + 8);
    type = Get32(b + 64);
    w = Get32(b + 68);
    h = Get32(b + 72);
    size_t off = 80;
    if (version == 1) {
        off = 92;
    } else if (version != 0) {
        return false;
    }
    if (off > n || w == 0 || h == 0 || w > 4096 || h > 4096) {
        return false;
    }
    data.assign(b + off, b + n);
    return true;
}

inline uint8_t Scale5(uint32_t v) {
    return (uint8_t)((v * 255) / 31);
}
void Rgba16(const uint8_t* p, uint8_t* o) {
    const uint16_t c = (p[0] << 8) | p[1];
    o[0] = Scale5(c >> 11);
    o[1] = Scale5((c >> 6) & 31);
    o[2] = Scale5((c >> 1) & 31);
    o[3] = (c & 1) ? 255 : 0;
}

bool Decode(const std::string& resource, const std::string& palette, Original& out, bool& usedFallbackPalette) {
    std::vector<uint8_t> d;
    if (!LoadRaw(resource, out.type, out.w, out.h, d)) {
        return false;
    }
    const size_t n = (size_t)out.w * out.h;
    out.rgba.assign(n * 4, 255);
    std::vector<uint8_t> pal; // RGBA8 entries
    if (out.type == 3 || out.type == 4) {
        uint32_t pt, pw, ph;
        std::vector<uint8_t> pd;
        if (!palette.empty() && LoadRaw(palette, pt, pw, ph, pd) && pt == 2) {
            pal.resize(pd.size() / 2 * 4);
            for (size_t i = 0; i < pd.size() / 2; ++i) {
                Rgba16(&pd[i * 2], &pal[i * 4]);
            }
        } else { // no palette known: a grey ramp keeps the structure (the colour then comes from the combiner)
            usedFallbackPalette = true;
            const size_t entries = out.type == 3 ? 16 : 256;
            pal.resize(entries * 4);
            for (size_t i = 0; i < entries; ++i) {
                const uint8_t v = (uint8_t)(i * 255 / (entries - 1));
                pal[i * 4] = pal[i * 4 + 1] = pal[i * 4 + 2] = v;
                pal[i * 4 + 3] = 255;
            }
        }
    }
    auto need = [&](size_t bytes) { return d.size() >= bytes; };
    switch (out.type) {
        case 1:
            if (!need(n * 4)) return false;
            std::memcpy(out.rgba.data(), d.data(), n * 4);
            break;
        case 2:
            if (!need(n * 2)) return false;
            for (size_t i = 0; i < n; ++i) Rgba16(&d[i * 2], &out.rgba[i * 4]);
            break;
        case 3:
            if (!need((n + 1) / 2)) return false;
            for (size_t i = 0; i < n; ++i) {
                const uint8_t idx = (d[i / 2] >> (4 - (i % 2) * 4)) & 15;
                if ((size_t)idx * 4 + 3 < pal.size()) std::memcpy(&out.rgba[i * 4], &pal[idx * 4], 4);
            }
            break;
        case 4:
            if (!need(n)) return false;
            for (size_t i = 0; i < n; ++i) {
                if ((size_t)d[i] * 4 + 3 < pal.size()) std::memcpy(&out.rgba[i * 4], &pal[d[i] * 4], 4);
            }
            break;
        case 5: // I4: intensity (alpha equals intensity in the engine; the replacement is opaque)
            if (!need((n + 1) / 2)) return false;
            for (size_t i = 0; i < n; ++i) {
                const uint8_t v = ((d[i / 2] >> (4 - (i % 2) * 4)) & 15) * 17;
                out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = v;
            }
            break;
        case 6: // I8
            if (!need(n)) return false;
            for (size_t i = 0; i < n; ++i) out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = d[i];
            break;
        case 7: // IA4
            if (!need((n + 1) / 2)) return false;
            for (size_t i = 0; i < n; ++i) {
                const uint8_t part = (d[i / 2] >> (4 - (i % 2) * 4)) & 15;
                out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = (part >> 1) * 36;
                out.rgba[i * 4 + 3] = (part & 1) ? 255 : 0;
            }
            break;
        case 8: // IA8
            if (!need(n)) return false;
            for (size_t i = 0; i < n; ++i) {
                out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = (d[i] >> 4) * 17;
                out.rgba[i * 4 + 3] = (d[i] & 15) * 17;
            }
            break;
        case 9: // IA16
            if (!need(n * 2)) return false;
            for (size_t i = 0; i < n; ++i) {
                out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = d[i * 2];
                out.rgba[i * 4 + 3] = d[i * 2 + 1];
            }
            break;
        default:
            return false;
    }
    if (out.type != 5 && out.type != 6) {
        for (size_t i = 0; i < n; ++i) {
            if (out.rgba[i * 4 + 3] < 255) {
                out.cutout = true;
                break;
            }
        }
    }
    return true;
}

double BytesPerTexel(uint32_t type) { // native N64 storage, what the pack's byte scale is relative to
    switch (type) {
        case 1: return 4;
        case 2: return 2;
        case 3: case 5: case 7: return .5;
        case 4: case 6: case 8: return 1;
        case 9: return 2;
    }
    return 1;
}

// ------------------------------------------------------------------ recipes
struct Job {
    std::string resource, palette, gen;
    NewDawnSynth::Params params;
    uint32_t seed = 1;
    int tier = 1024;
    std::string face;
    bool stoch = false;
    std::string role = "base";
    std::string region;  // scene region priority list ("interiors,clock_town") for manifest selectors
    std::string measure; // "layout" (brick, ashlar), "planks": count the rows and columns on the original's pixels
    bool hasRows = false, hasCols = false;
    bool unique = false;              // painted art: upscaled, not replaced (UniqueArt.h)
    std::string mm3dScene, mm3dTex;   // confident 3DS counterpart from mm3d_match.json
};

struct Data {
    json recipes, textures, classification, scale, layers, mm3d; // classification, scale, layers, mm3d: optional
    std::string libraryManifest;
    uint64_t hash = 0;
};

bool LoadData(Data& data, std::string& error) {
    data.hash = 14695981039346656037ULL;
    auto load = [&](const char* name, json& dest, bool optional = false) {
        auto file = Archives()->LoadFile(std::string(kDataDir) + name);
        if (!file || !file->Buffer) {
            if (optional) {
                dest = json::object();
                return true;
            }
            error = std::string("missing ") + kDataDir + name + " in 2ship.o2r (regenerate 2ship.o2r)";
            return false;
        }
        data.hash = Fnv1a(file->Buffer->data(), file->Buffer->size(), data.hash);
        dest = json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, false);
        if (dest.is_discarded()) {
            error = std::string("cannot parse ") + name;
            return false;
        }
        return true;
    };
    if (!load("recipes.json", data.recipes) || !load("textures.json", data.textures) ||
        !load("classification.json", data.classification, true) || !load("scale.json", data.scale, true) ||
        !load("layers.json", data.layers, true) || !load("mm3d_match.json", data.mm3d, true)) {
        return false;
    }
    std::ifstream in(LibraryDir() / "manifest.json", std::ios::binary);
    if (!in) {
        error = "the base material library is missing (" + (LibraryDir() / "manifest.json").string() + ")";
        return false;
    }
    data.libraryManifest.assign(std::istreambuf_iterator<char>(in), {});
    data.hash = Fnv1a(data.libraryManifest.data(), data.libraryManifest.size(), data.hash);
    return true;
}

void Flatten(const json& src, NewDawnSynth::Params& out, uint32_t seed) { // arrays of two numbers (tone, trange, rough) become key0/key1
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it->is_array() && !it->empty() && (*it)[0].is_string()) { // a list of material ids: one per texture, by seed
            out.str[it.key()] = (*it)[seed % it->size()].get<std::string>();
        } else if (it->is_number()) {
            out.num[it.key()] = it->get<double>();
        } else if (it->is_boolean()) {
            out.num[it.key()] = it->get<bool>() ? 1 : 0;
        } else if (it->is_string()) {
            out.str[it.key()] = it->get<std::string>();
        } else if (it->is_array() && it->size() == 2 && (*it)[0].is_number()) {
            out.num[it.key() + "0"] = (*it)[0].get<double>();
            out.num[it.key() + "1"] = (*it)[1].get<double>();
        }
    }
}

uint32_t NameSeed(const std::string& s) {
    return (uint32_t)(Fnv1a(s.data(), s.size()) & 0x7fffffffu);
}

bool InScope(const json& scopes, const std::string& resource) {
    for (const auto& sc : scopes) {
        if (resource.rfind(sc.get<std::string>(), 0) == 0) {
            return true;
        }
    }
    return false;
}

// Hand-tuned entries (textures.json) first, then every other replaceable texture of the classification
// (class and confidence from classification.json, UV area from scale.json for the tier, roles from layers.json).
void BuildJobs(const Data& data, std::vector<Job>& jobs) {
    const json& classes = data.recipes.value("classes", json::object());
    const json& tiers = data.recipes.value("tiers", json::object());
    const int hero = std::clamp(CVarGetInteger("gNewDawn.Textures.HeroSize", tiers.value("hero", 1024)), 128, 2048);
    const int rest = std::clamp(CVarGetInteger("gNewDawn.Textures.RestSize", tiers.value("rest", 512)), 64, hero);
    const size_t heroCount = (size_t)std::max(0, CVarGetInteger("gNewDawn.Textures.HeroCount", tiers.value("hero_count", 450)));

    std::unordered_map<std::string, std::string> roles; // detail only if it is a detail in every pair it is part of
    if (data.layers.contains("pairs")) {
        std::unordered_map<std::string, bool> isBase;
        for (const auto& pr : data.layers["pairs"]) {
            const auto& ro = pr["roles"];
            const std::string rs[2] = { ro[0].get<std::string>(), ro[1].get<std::string>() };
            const std::string names[2] = { pr["a"].get<std::string>(), pr["b"].get<std::string>() };
            for (int i = 0; i < 2; ++i) {
                if (rs[i] == "detail") {
                    roles.emplace(names[i], "detail");
                } else if (rs[i] == "base" || rs[i] == "alt") {
                    isBase[names[i]] = true;
                }
            }
        }
        for (const auto& [n, _] : isBase) {
            roles.erase(n);
        }
    }

    const json sceneRegions = data.recipes.value("scene_regions", json::object());
    auto regionOf = [&](const std::string& resource) -> std::string { // scenes/nonmq/<SCENE>/...
        if (resource.rfind("scenes/", 0) != 0) return "";
        const size_t a = resource.find('/', 7);
        const size_t b = a == std::string::npos ? a : resource.find('/', a + 1);
        if (b == std::string::npos) return "";
        const std::string scene = resource.substr(a + 1, b - a - 1);
        return sceneRegions.contains(scene) ? sceneRegions[scene].get<std::string>() : "";
    };
    auto addJob = [&](const std::string& resource, const json& entry, const std::string& cls, const std::string& pal, int tier) {
        if (!classes.contains(cls)) {
            return;
        }
        Job j;
        j.resource = resource;
        j.palette = pal;
        j.seed = entry.value("seed", NameSeed(resource));
        j.tier = tier;
        j.region = regionOf(resource);
        const json& c = classes[cls];
        j.gen = entry.value("gen", c.value("gen", ""));
        j.stoch = entry.contains("stoch") ? entry.value("stoch", false) : c.value("stoch", false);
        j.measure = c.value("measure", "");
        auto r = roles.find(resource);
        j.role = entry.value("role", r != roles.end() ? r->second : "base");
        json p = json::object();
        if (c.contains("p")) {
            p.update(c["p"]);
        }
        if (entry.contains("p")) {
            p.update(entry["p"]);
        }
        // physical scale: repeat the CC0 set so one of its tiles covers about world_m metres of the surface
        if (c.contains("world_m") && !(p.contains("k")) && data.scale.contains("textures") && data.scale["textures"].contains(resource) &&
            data.scale["textures"][resource].contains("tile_m")) {
            const auto& tm = data.scale["textures"][resource]["tile_m"];
            const double span = std::max(tm[0].get<double>(), tm[1].get<double>());
            p["k"] = std::clamp((int)std::lround(span / c["world_m"].get<double>()), 1, data.recipes.value("max_repeat", 8));
        }
        j.hasRows = p.contains("rows") && entry.contains("p") && entry["p"].contains("rows");
        j.hasCols = p.contains("cols") && entry.contains("p") && entry["p"].contains("cols");
        if (j.gen.empty()) {
            return;
        }
        Flatten(p, j.params, j.seed);
        j.face = j.params.Str("face", j.params.Str("set", "").c_str());
        jobs.push_back(std::move(j));
    };

    std::unordered_set<std::string> explicitSet;
    for (auto it = data.textures["textures"].begin(); it != data.textures["textures"].end(); ++it) {
        const json& e = it.value();
        explicitSet.insert(it.key());
        addJob(it.key(), e, e.value("class", ""), e.value("palette", ""), e.value("tier", "hero") == "hero" ? hero : rest);
    }

    if (data.classification.contains("textures") && data.recipes.contains("class_map")) {
        const json& cmap = data.recipes["class_map"];
        const json& scopes = data.recipes.value("scopes", json::array());
        const double minConf = data.recipes.value("min_conf", 0.6);
        struct Cand {
            std::string resource, cls, pal;
            double area;
        };
        std::vector<Cand> cands;
        for (auto it = data.classification["textures"].begin(); it != data.classification["textures"].end(); ++it) {
            const json& e = it.value();
            if (explicitSet.count(it.key()) || e.value("conf", 0.0) < minConf || !cmap.contains(e.value("class", "")) ||
                !InScope(scopes, it.key())) {
                continue;
            }
            double area = 0;
            if (data.scale.contains("textures") && data.scale["textures"].contains(it.key())) {
                area = data.scale["textures"][it.key()].value("area", 0.0);
            }
            cands.push_back({ it.key(), cmap[e["class"].get<std::string>()].get<std::string>(), e.value("pal", ""), area });
        }
        // the tier follows the surface area the texture covers in the game: the biggest get the large size
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.area > b.area; });
        for (size_t i = 0; i < cands.size(); ++i) {
            addJob(cands[i].resource, json::object(), cands[i].cls, cands[i].pal, i < heroCount ? hero : rest);
        }
    }
    // Unique art (ND-016): everything scene-side the classification did not call a replaceable material, in the chosen scenes.
    std::string uniqueScenes = CVarGetString("gNewDawn.Textures.UniqueScenes", "");
    if (uniqueScenes.empty() && data.recipes.contains("unique")) {
        for (const auto& sc : data.recipes["unique"].value("scenes", json::array())) {
            uniqueScenes += (uniqueScenes.empty() ? "" : ",") + sc.get<std::string>();
        }
    }
    const bool allScenes = uniqueScenes == "all";
    if (CVarGetInteger("gNewDawn.Textures.Unique", 1) != 0 && data.classification.contains("textures") && !uniqueScenes.empty()) {
        const double minConf = data.recipes.value("min_conf", 0.6);
        const json& cmap = data.recipes.value("class_map", json::object());
        std::unordered_set<std::string> alreadyJob;
        for (const auto& j : jobs) {
            alreadyJob.insert(j.resource);
        }
        for (auto it = data.classification["textures"].begin(); it != data.classification["textures"].end(); ++it) {
            const json& e = it.value();
            const std::string cls = e.value("class", "");
            if (alreadyJob.count(it.key()) || cls == "skip" || cls == "water" || cls == "foliage" ||
                (cmap.contains(cls) && e.value("conf", 0.0) >= minConf) || e.contains("pal_alt") ||
                (it.key().rfind("scenes/", 0) != 0 && it.key().rfind("misc/scene_texture", 0) != 0)) {
                continue;
            }
            if (!allScenes) {
                bool in = false;
                for (size_t at = 0; at <= uniqueScenes.size() && !in;) {
                    const size_t comma = uniqueScenes.find(',', at);
                    const std::string name = uniqueScenes.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
                    in = !name.empty() && it.key().find("/" + name + "/") != std::string::npos;
                    at = comma == std::string::npos ? uniqueScenes.size() + 1 : comma + 1;
                }
                if (!in) {
                    continue;
                }
            }
            Job j;
            j.resource = it.key();
            j.palette = e.value("pal", "");
            j.seed = NameSeed(it.key());
            j.tier = rest;
            j.region = regionOf(it.key());
            j.unique = true;
            j.gen = "unique";
            if (data.mm3d.contains("textures") && data.mm3d["textures"].contains(it.key())) {
                const json& m = data.mm3d["textures"][it.key()];
                if (!m.value("weak", false)) {
                    j.mm3dScene = m.value("scene", "");
                    j.mm3dTex = m.value("mm3d", "");
                }
            }
            jobs.push_back(std::move(j));
        }
    }
    // group by size, then by the material they read, so the library surfaces in memory are reused back to back
    std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        return a.tier != b.tier ? a.tier > b.tier : a.face < b.face;
    });
}

// ------------------------------------------------------------------ signature, stamp, mount
bool IsOurs(const std::shared_ptr<Ship::Archive>& a) {
    const auto root = Root().lexically_normal().generic_string();
    return fs::path(a->GetPath()).lexically_normal().generic_string().rfind(root, 0) == 0;
}

std::string Signature(const Data& data) {
    uint64_t h = Fnv1a(kVersion, std::strlen(kVersion));
    h = Fnv1a(&data.hash, sizeof(data.hash), h);
    const int sizes[2] = { CVarGetInteger("gNewDawn.Textures.HeroSize", 0), CVarGetInteger("gNewDawn.Textures.RestSize", 0) };
    h = Fnv1a(sizes, sizeof(sizes), h);
    {   // unique art: which scenes, whether the 3DS dump is there (and where), and the upscaler's weights
        const std::string u = std::string(CVarGetString("gNewDawn.Textures.UniqueScenes", "")) + "|" +
                              std::to_string(CVarGetInteger("gNewDawn.Textures.Unique", 1)) + "|" +
                              (NewDawnMm3d::DumpValid(NewDawnMm3d::DumpPath()) ? NewDawnMm3d::DumpPath() : std::string("-"));
        h = Fnv1a(u.data(), u.size(), h);
        NewDawnMm3d::SpanUpscaler span;
        const uint64_t fp = span.Load(SpanPath()) ? span.Fingerprint() : 0;
        h = Fnv1a(&fp, sizeof(fp), h);
    }
    const auto archives = Archives()->GetArchives(); // keep the snapshot alive for the whole loop
    for (const auto& a : *archives) { // the ROM archive: a new extraction regenerates
        const fs::path p(a->GetPath());
        if (p.filename() != "mm.o2r") {
            continue;
        }
        std::error_code ec;
        h = Fnv1a(p.filename().generic_string().data(), p.filename().generic_string().size(), h);
        if (fs::is_regular_file(p, ec)) {
            const uint64_t size = fs::file_size(p, ec);
            const int64_t mtime = fs::last_write_time(p, ec).time_since_epoch().count();
            h = Fnv1a(&size, sizeof(size), h);
            h = Fnv1a(&mtime, sizeof(mtime), h);
        }
    }
    return Hex16(h);
}

json ReadStamp(const fs::path& dir) {
    std::ifstream in(dir / "stamp.json");
    if (!in) {
        return json();
    }
    auto stamp = json::parse(in, nullptr, false);
    return stamp.is_discarded() ? json() : stamp;
}
void WriteStamp(const fs::path& dir, const json& stamp) {
    std::ofstream(dir / "stamp.json", std::ios::trunc) << stamp.dump(1) << "\n";
}

bool Promote() {
    std::error_code ec;
    fs::remove_all(Live(), ec);
    fs::rename(Staging(), Live(), ec);
    if (ec) {
        SPDLOG_ERROR("NewDawn textures: cannot move {} to {}: {}", Staging().string(), Live().string(), ec.message());
        return false;
    }
    return true;
}

void Mount(const json& stamp) {
    const fs::path p = Live() / kArchiveName;
    auto archive = Archives()->AddArchive(p.generic_string());
    std::lock_guard<std::mutex> lock(gMutex);
    if (archive) {
        gArchivePath = archive->GetPath();
        Ship::Context::GetRawInstance()->GetResourceManager()->SetRemasterArchive(gArchivePath);
        gMounted = true;
        gCount = stamp.value("textures", 0);
        gSkipped = stamp.value("skipped", 0);
        gSeconds = stamp.value("seconds", 0.0);
        gState = State::Ready;
    } else {
        gState = State::Failed;
        gMessage = "cannot mount " + p.string();
    }
}

// ------------------------------------------------------------------ the generator
struct Outcome {
    std::atomic<uint32_t> done{ 0 }, missing{ 0 }, cutouts{ 0 }, unusable{ 0 }, fallbackPalette{ 0 }, uniqueN64{ 0 }, unique3ds{ 0 }, uniqueSkipped{ 0 };
    std::mutex mutex;
    std::vector<json> entries;
};

struct UniqueContext {
    NewDawnMm3d::SpanUpscaler span;
    bool spanOk = false;
    std::unique_ptr<NewDawnUnique::Mm3dScenes> scenes;
};
UniqueContext* gUnique = nullptr;

void RunUnique(const Job& job, NewDawnSynth::Library& lib, NewDawn::ZipWriter& zip, Outcome& out) {
    if (!gUnique || !gUnique->spanOk) {
        ++out.uniqueSkipped;
        return;
    }
    Original o;
    bool fallback = false;
    if (!Decode(job.resource, job.palette, o, fallback) || o.type == 5 || o.type == 6 || o.w < 4 || o.h < 4 || fallback) {
        ++out.uniqueSkipped; // intensity textures double as alpha in the engine: the opaque replacement would change them
        return;
    }
    Mm3d::Image original;
    original.width = (int)o.w;
    original.height = (int)o.h;
    original.rgba = o.rgba;
    const Mm3d::Image* candidate = nullptr;
    bool from3ds = false;
    if (gUnique->scenes && !job.mm3dTex.empty()) {
        candidate = gUnique->scenes->Find(job.mm3dScene, job.mm3dTex);
        from3ds = candidate && NewDawnUnique::AcceptCandidate(original, *candidate);
    }
    NewDawnUnique::Options opt;
    opt.span = &gUnique->span;
    std::shared_ptr<const NewDawnSynth::Surface> grain;
    const std::string grainId = lib.Resolve("class:plaster", job.seed, job.region);
    if (!grainId.empty()) {
        grain = lib.Get(grainId, 512);
        opt.grain = grain.get();
    }
    if (from3ds) {
        SPDLOG_INFO("NewDawn unique art: {} <- 3DS texture {}/{} ({}x{})", job.resource, job.mm3dScene, job.mm3dTex, candidate->width, candidate->height);
    }
    Mm3d::Image result;
    if (!NewDawnUnique::UpscaleAndGrade(from3ds ? *candidate : original, original, from3ds, opt, result)) {
        ++out.uniqueSkipped;
        return;
    }
    const int W = result.width, H = result.height;
    const uint32_t kind = (o.type == 3 || o.type == 4) ? 1 : o.type;
    const float hs = (float)(W * 4.0 / (o.w * BytesPerTexel(o.type))), vs = (float)H / o.h;
    auto albedo = EncodeTexture(kind, W, H, 1u, hs, vs, result.rgba.data(), result.rgba.size());
    zip.Add("alt/" + job.resource, albedo.data(), albedo.size());
    (from3ds ? out.unique3ds : out.uniqueN64)++;
    ++out.done;
}

void RunJob(const Job& job, NewDawnSynth::Library& lib, NewDawn::ZipWriter& zip, Outcome& out) {
    if (job.unique) {
        RunUnique(job, lib, zip, out);
        return;
    }
    Original o;
    bool fallback = false;
    if (!Decode(job.resource, job.palette, o, fallback)) {
        ++out.missing;
        return;
    }
    if (o.cutout) {
        ++out.cutouts;
        return;
    }
    if (fallback) {
        ++out.fallbackPalette;
    }
    const NewDawnSynth::Palette pal = NewDawnSynth::Palette::FromRgba(o.rgba.data(), (size_t)o.w * o.h);
    NewDawnSynth::Params params = job.params;
    if (!job.measure.empty()) { // counts of courses and boards, measured on the player's own pixels (hand-set values win)
        std::vector<float> Y((size_t)o.w * o.h);
        for (size_t i = 0; i < Y.size(); ++i) {
            Y[i] = (o.rgba[i * 4] * .299f + o.rgba[i * 4 + 1] * .587f + o.rgba[i * 4 + 2] * .114f) / 255.f;
        }
        const NewDawnSynth::Layout L = NewDawnSynth::MeasureLayout(Y, (int)o.w, (int)o.h);
        if (job.measure == "layout") {
            if (!job.hasRows && L.rowScore >= .5f) params.num["rows"] = L.rows;
            if (!job.hasCols && L.colScore >= .25f) params.num["cols"] = L.cols;
        } else if (job.measure == "planks" && L.rowScore >= .2f && L.rows >= 2) {
            params.num["planks"] = L.rows; // horizontal boards: the rows of the original
        }
    }
    // long side = tier size, original aspect (all N64 sizes are powers of two)
    const double k = (double)job.tier / std::max(o.w, o.h);
    const int W = std::max(64, (int)std::lround(o.w * k) & ~1), H = std::max(64, (int)std::lround(o.h * k) & ~1);
    NewDawnSynth::Mat mat;
    if (!NewDawnSynth::Generate(job.gen, lib, pal, params, W, H, job.seed, job.region, mat)) {
        ++out.unusable;
        return;
    }
    const NewDawnSynth::Encoded e = NewDawnSynth::Encode(mat);
    // the pack's header layout: expanded RGBA8 under the native type (CI textures: RGBA32), byte scales relative to native
    const uint32_t kind = (o.type == 3 || o.type == 4) ? 1 : o.type;
    const float hs = (float)(W * 4.0 / (o.w * BytesPerTexel(o.type))), vs = (float)H / o.h;
    const std::string entry = "alt/" + job.resource;
    const uint32_t flags = 1u | (job.stoch ? 0x100u : 0u); // LOAD_AS_RAW | NEWDAWN_STOCH
    auto albedo = EncodeTexture(kind, W, H, flags, hs, vs, e.rgba.data(), e.rgba.size());
    zip.Add(entry, albedo.data(), albedo.size());
    auto n = EncodeTexture(9, e.mw, e.mh, 0, 1.f, 1.f, e.normal.data(), e.normal.size());
    zip.Add(entry + "_n", n.data(), n.size());
    auto r = EncodeTexture(9, e.mw, e.mh, 0, 1.f, 1.f, e.rough.data(), e.rough.size());
    zip.Add(entry + "_r", r.data(), r.size());
    json meta = { { "resource", job.resource },
                  { "label", job.gen },
                  { "role", job.role },
                  { "source_pixels_fnv1a64", Hex16(Fnv1a(e.rgba.data(), e.rgba.size())) },
                  { "source_size", { W, H } },
                  { "output_size", { e.mw, e.mh } },
                  { "source_type", kind },
                  { "source_flags", flags },
                  { "source_h_byte_scale", hs },
                  { "source_v_pixel_scale", vs } };
    std::lock_guard<std::mutex> lock(out.mutex);
    out.entries.push_back(std::move(meta));
    ++out.done;
}

void Generate(bool boot) {
    const auto start = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& why) {
        SPDLOG_ERROR("NewDawn textures: {}", why);
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gState = State::Failed;
            gMessage = why;
        }
        gRunning = false;
    };
    try {
        Data data;
        std::string error;
        if (!LoadData(data, error)) {
            return fail(error);
        }
        std::vector<Job> jobs;
        BuildJobs(data, jobs);
        const std::string signature = Signature(data);

        std::error_code ec;
        fs::remove_all(Staging(), ec);
        fs::create_directories(Staging(), ec);
        NewDawn::ZipWriter zip;
        if (!zip.Open(Staging() / kArchiveName)) {
            return fail("cannot write to " + Staging().string());
        }
        gTotal = (uint32_t)jobs.size();
        gDone = 0;
        // a hero texture holds about 120 MB of float fields while it is built: bound the threads by cores and by 8
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const unsigned threads = std::clamp(boot ? hw : std::max(1u, hw / 2), 1u, boot ? 8u : 4u);
        SPDLOG_INFO("NewDawn textures: {} textures on {} threads", jobs.size(), threads);
        std::vector<NewDawnSynth::Library::Entry> entries;
        const json manifest = json::parse(data.libraryManifest, nullptr, false);
        if (manifest.is_object() && manifest.contains("materials")) {
            for (const auto& m : manifest["materials"]) {
                NewDawnSynth::Library::Entry e;
                e.id = m.value("id", "");
                e.cls = m.value("class", "");
                if (m.contains("regions")) {
                    for (const auto& r : m["regions"]) e.regions.push_back(r.get<std::string>());
                } else {
                    e.regions.push_back(m.value("region", "any"));
                }
                for (const auto& r : m.value("roles", json::array())) {
                    e.roles.push_back(r.get<std::string>());
                }
                entries.push_back(std::move(e));
            }
        }
        NewDawnSynth::Library lib(LibraryDir().generic_string(), std::move(entries));
        UniqueContext uniqueCtx;
        const bool anyUnique = std::any_of(jobs.begin(), jobs.end(), [](const Job& j) { return j.unique; });
        if (anyUnique) {
            uniqueCtx.spanOk = uniqueCtx.span.Load(SpanPath());
            if (!uniqueCtx.spanOk) {
                SPDLOG_WARN("NewDawn textures: the SPAN weights ({}) are missing, unique art stays native", SpanPath());
            }
            const std::string romfs = NewDawnMm3d::DumpPath();
            if (NewDawnMm3d::DumpValid(romfs)) {
                uniqueCtx.scenes = std::make_unique<NewDawnUnique::Mm3dScenes>(romfs);
            }
        }
        gUnique = &uniqueCtx;
        Outcome out;
        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) {
            pool.emplace_back([&]() {
                for (size_t i; (i = next++) < jobs.size();) {
                    try {
                        RunJob(jobs[i], lib, zip, out);
                    } catch (const std::exception& e) {
                        SPDLOG_WARN("NewDawn textures: {}: {}", jobs[i].resource, e.what());
                        ++out.unusable;
                    }
                    ++gDone;
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
        gUnique = nullptr;
        for (const auto& m : lib.Missing()) {
            SPDLOG_WARN("NewDawn textures: base material '{}' is not in the library", m);
        }
        std::sort(out.entries.begin(), out.entries.end(),
                  [](const json& a, const json& b) { return a["resource"] < b["resource"]; });
        const std::string meta = json{ { "generator", "newdawn-textures" }, { "version", kVersion }, { "materials", out.entries } }.dump();
        zip.Add("newdawn-materials.json", reinterpret_cast<const uint8_t*>(meta.data()), meta.size());
        zip.Add("newdawn-textures.json", reinterpret_cast<const uint8_t*>("{}"), 2); // marker: not a source for other generators
        if (!zip.Close()) {
            return fail("could not finish the archive (disk full?)");
        }
        const uint32_t count = out.done.load();
        if (count == 0) {
            fs::remove(Staging() / kArchiveName, ec);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        uint64_t bytes = 0;
        if (count) {
            bytes = fs::file_size(Staging() / kArchiveName, ec);
        }
        const uint32_t skipped = out.missing + out.cutouts + out.unusable;
        WriteStamp(Staging(), { { "version", kVersion }, { "signature", signature }, { "state", count ? "ok" : "nosource" },
                                { "textures", count }, { "skipped", skipped }, { "missing_originals", out.missing.load() },
                                { "cutouts", out.cutouts.load() }, { "unusable", out.unusable.load() },
                                { "unique_n64", out.uniqueN64.load() }, { "unique_3ds", out.unique3ds.load() }, { "unique_skipped", out.uniqueSkipped.load() },
                                { "fallback_palette", out.fallbackPalette.load() }, { "archive_bytes", bytes },
                                { "seconds", seconds } });
        SPDLOG_INFO("NewDawn textures: unique art {} upscaled from the N64 original, {} from the 3DS texture, {} left native", out.uniqueN64.load(),
                    out.unique3ds.load(), out.uniqueSkipped.load());
        SPDLOG_INFO("NewDawn textures: {} textures ({} skipped: {} missing, {} with transparency, {} unusable; {} without a palette), "
                    "{:.1f} MB, {:.1f}s",
                    count, skipped, out.missing.load(), out.cutouts.load(), out.unusable.load(), out.fallbackPalette.load(), bytes / 1e6,
                    seconds);
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gStagedOk = true;
            gCount = count;
            gSkipped = skipped;
            gSeconds = seconds;
            gState = count ? State::Ready : State::NoSource;
            gPendingRestart = !boot && count > 0;
        }
    } catch (const std::exception& e) { return fail(e.what()); }
    gRunning = false;
}

} // namespace

bool BootNeedsGeneration() {
    if (CVarGetInteger("gNewDawn.Textures.Generate", 1) == 0) {
        SetState(State::Disabled);
        return false;
    }
    std::error_code ec;
    fs::create_directories(Root(), ec);
    Data data;
    std::string error;
    if (!LoadData(data, error)) {
        SetState(State::Failed, error);
        SPDLOG_ERROR("NewDawn textures: {}", error);
        return false;
    }
    const std::string signature = Signature(data);
    json staged = ReadStamp(Staging());
    if (staged.is_object()) {
        if (staged.value("signature", "") == signature && staged.value("version", "") == kVersion) {
            SPDLOG_INFO("NewDawn textures: using the textures regenerated last session");
            Promote();
        } else {
            fs::remove_all(Staging(), ec);
        }
    } else if (fs::exists(Staging())) {
        fs::remove_all(Staging(), ec);
    }
    json stamp = ReadStamp(Live());
    if (stamp.is_object() && stamp.value("signature", "") == signature && stamp.value("version", "") == kVersion) {
        const std::string state = stamp.value("state", "");
        if (state == "ok" && fs::exists(Live() / kArchiveName)) {
            Mount(stamp);
            SPDLOG_INFO("NewDawn textures: mounted the cache ({} textures)", gCount);
            return false;
        }
        if (state == "nosource") {
            SetState(State::NoSource);
            return false;
        }
    }
    return true;
}

void StartGeneration(bool boot) {
    if (gRunning.exchange(true)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gState = State::Running;
        gMessage.clear();
        gStagedOk = false;
        gPendingRestart = false;
    }
    gDone = 0;
    gTotal = 0;
    std::thread([boot]() { Generate(boot); }).detach();
}

bool IsRunning() {
    return gRunning;
}

void FinishBoot() {
    bool staged;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        staged = gStagedOk;
    }
    if (!staged) {
        return;
    }
    if (Promote()) {
        const json stamp = ReadStamp(Live());
        if (stamp.value("state", "") == "ok") {
            Mount(stamp);
        } else {
            SetState(State::NoSource);
        }
    }
}

Progress GetProgress() {
    Progress p;
    std::lock_guard<std::mutex> lock(gMutex);
    p.state = gState;
    p.done = gDone;
    p.total = gTotal;
    p.mounted = gMounted;
    p.pendingRestart = gPendingRestart;
    return p;
}

std::string StatusText() {
    const Progress p = GetProgress();
    std::lock_guard<std::mutex> lock(gMutex);
    char buf[256];
    switch (p.state) {
        case State::Running:
            snprintf(buf, sizeof(buf), "Generating remastered textures... %u%% (%u of %u)", p.total ? p.done * 100 / p.total : 0,
                     p.done, p.total);
            return buf;
        case State::Ready:
            if (p.pendingRestart) {
                snprintf(buf, sizeof(buf), "Regenerated %u textures in %.0fs. Restart the game to use them.", gCount, gSeconds);
            } else {
                snprintf(buf, sizeof(buf), "Generated: %u textures (%u skipped), built in %.0fs.", gCount, gSkipped, gSeconds);
            }
            return buf;
        case State::Disabled:
            return "Disabled (gNewDawn.Textures.Generate = 0).";
        case State::NoSource:
            return "Nothing to generate: no texture in the tables could be read from your game data.";
        case State::Failed:
            return "Generation failed: " + gMessage;
        default:
            return "Missing. Press Regenerate (or restart the game).";
    }
}

void Regenerate() {
    StartGeneration(false);
}

void ApplyToggle() {
    bool mounted;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        mounted = gMounted;
    }
    const bool want = mounted && CVarGetInteger("gNewDawn.Textures.Remastered", 0) != 0;
    if (want == gOn) {
        return;
    }
    gOn = want;
    Ship::Context::GetRawInstance()->GetResourceManager()->SetRemasterEnabled(want);
    gfx_texture_cache_clear();
}

uint32_t MountedCount() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gMounted ? gCount : 0;
}

} // namespace NewDawnTextures
