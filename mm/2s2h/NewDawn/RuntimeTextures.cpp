// See RuntimeTextures.h. Driver around TextureSynth: decodes the player's N64 textures, runs the synthesis on all cores,
// writes the cache archive and mounts it. Mirrors RuntimeMaterials.cpp (cache, signature, staging, boot popup).
#include "RuntimeTextures.h"
#include "TextureSynth.h"
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
constexpr const char* kVersion = "1.0.0-cpp";
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
};

struct Data {
    json recipes, textures;
    std::string libraryManifest;
    uint64_t hash = 0;
};

bool LoadData(Data& data, std::string& error) {
    data.hash = 14695981039346656037ULL;
    auto load = [&](const char* name, json& dest) {
        auto file = Archives()->LoadFile(std::string(kDataDir) + name);
        if (!file || !file->Buffer) {
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
    if (!load("recipes.json", data.recipes) || !load("textures.json", data.textures)) {
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

void Flatten(const json& src, NewDawnSynth::Params& out) { // arrays of two numbers (tone, trange, rough) become key0/key1
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it->is_number()) {
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

void BuildJobs(const Data& data, std::vector<Job>& jobs) {
    const json& classes = data.recipes.value("classes", json::object());
    const json& tiers = data.recipes.value("tiers", json::object());
    const int hero = std::clamp(CVarGetInteger("gNewDawn.Textures.HeroSize", tiers.value("hero", 1024)), 128, 2048);
    const int rest = std::clamp(CVarGetInteger("gNewDawn.Textures.RestSize", tiers.value("rest", 512)), 64, hero);
    for (auto it = data.textures["textures"].begin(); it != data.textures["textures"].end(); ++it) {
        const json& e = it.value();
        Job j;
        j.resource = it.key();
        j.palette = e.value("palette", "");
        j.seed = e.value("seed", 1);
        j.tier = e.value("tier", "hero") == "hero" ? hero : rest;
        std::string gen = e.value("gen", "");
        bool stoch = e.value("stoch", false);
        json p = json::object();
        const std::string cls = e.value("class", "");
        if (classes.contains(cls)) {
            const json& c = classes[cls];
            gen = gen.empty() ? c.value("gen", "") : gen;
            stoch = e.contains("stoch") ? stoch : c.value("stoch", false);
            if (c.contains("p")) {
                p.update(c["p"]);
            }
        }
        if (e.contains("p")) {
            p.update(e["p"]);
        }
        if (gen.empty()) {
            continue;
        }
        j.gen = gen;
        j.stoch = stoch;
        j.role = e.value("role", "base");
        Flatten(p, j.params);
        j.face = j.params.Str("face", j.params.Str("set", "").c_str());
        jobs.push_back(std::move(j));
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
    std::atomic<uint32_t> done{ 0 }, missing{ 0 }, cutouts{ 0 }, unusable{ 0 }, fallbackPalette{ 0 };
    std::mutex mutex;
    std::vector<json> entries;
};

void RunJob(const Job& job, NewDawnSynth::Library& lib, NewDawn::ZipWriter& zip, Outcome& out) {
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
    // long side = tier size, original aspect (all N64 sizes are powers of two)
    const double k = (double)job.tier / std::max(o.w, o.h);
    const int W = std::max(64, (int)std::lround(o.w * k) & ~1), H = std::max(64, (int)std::lround(o.h * k) & ~1);
    NewDawnSynth::Mat mat;
    if (!NewDawnSynth::Generate(job.gen, lib, pal, job.params, W, H, job.seed, mat)) {
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
        NewDawnSynth::Library lib((LibraryDir()).generic_string());
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
                                { "fallback_palette", out.fallbackPalette.load() }, { "archive_bytes", bytes },
                                { "seconds", seconds } });
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
