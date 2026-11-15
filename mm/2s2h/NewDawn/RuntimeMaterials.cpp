// See RuntimeMaterials.h and docs/RUNTIME_MATERIALS.md. A C++ port of tools/newdawn/materials.py (the
// numpy path, resolution divisor 2) and tools/newdawn/recolor.py, run on the player's own HD textures.
#include "RuntimeMaterials.h"

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
#include <zlib.h>
#include <spdlog/spdlog.h>
#include <libultraship/classes.h>
#include <ship/resource/File.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>

#include "2s2h/BenPort.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace NewDawnMaterials {
namespace {

// Bump when the algorithms below change; the data files (recipes) are hashed into the signature themselves.
constexpr const char* kRecipeVersion = "2.0.0-cpp";
constexpr const char* kDataDir = "newdawn_materials/"; // inside 2ship.o2r (mm/assets/custom/newdawn_materials)
constexpr uint32_t kTextureType = 0x4F544558;          // "OTEX"

// ------------------------------------------------------------------ state
std::mutex gMutex;
State gState = State::None;
std::atomic<uint32_t> gDone{ 0 }, gTotal{ 0 };
std::atomic<bool> gRunning{ false };
bool gMounted = false, gPendingRestart = false, gStagedOk = false;
std::string gMessage;
uint32_t gMaterialCount = 0, gRecolorCount = 0;
double gSeconds = 0;

void SetState(State s, const std::string& message = "") {
    std::lock_guard<std::mutex> lock(gMutex);
    gState = s;
    gMessage = message;
}

fs::path Root() {
    return fs::path(Ship::Context::GetPathRelativeToAppDirectory("cache/newdawn-materials", appShortName));
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

// ------------------------------------------------------------------ small helpers
uint64_t Fnv1a(const void* data, size_t n, uint64_t h = 14695981039346656037ULL) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 1099511628211ULL;
    }
    return h;
}
uint64_t Fnv1a(const std::string& s, uint64_t h) {
    return Fnv1a(s.data(), s.size(), h);
}

std::string Hex16(uint64_t v) {
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

// numpy's rint: round half to even (the default FP rounding mode).
inline uint8_t RintByte(float v) {
    return (uint8_t)std::nearbyint(v);
}

// ------------------------------------------------------------------ TextureV1 (see tools/newdawn/README.md)
struct Tex {
    uint32_t kind, width, height, flags;
    float hScale, vScale;
    const uint8_t* pixels;
    size_t size;
};

uint32_t Rd32(const uint8_t* p, bool be) {
    return be ? (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24
              : (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// ResourceLoader's 64-byte header followed by the TextureV1 body, expanded RGBA8 only (the HD pack's layout).
bool DecodeAlbedo(const std::vector<char>& file, Tex& t) {
    const uint8_t* d = reinterpret_cast<const uint8_t*>(file.data());
    if (file.size() < 92 || d[0] > 1) {
        return false;
    }
    const bool be = d[0] == 1;
    if (Rd32(d + 4, be) != kTextureType || Rd32(d + 8, be) != 1) {
        return false;
    }
    t.kind = Rd32(d + 64, be);
    t.width = Rd32(d + 68, be);
    t.height = Rd32(d + 72, be);
    t.flags = Rd32(d + 76, be);
    uint32_t hs = Rd32(d + 80, be), vs = Rd32(d + 84, be);
    std::memcpy(&t.hScale, &hs, 4);
    std::memcpy(&t.vScale, &vs, 4);
    t.size = Rd32(d + 88, be);
    if (!(t.width > 0 && t.width <= 65535 && t.height > 0 && t.height <= 65535) || !std::isfinite(t.hScale) ||
        !std::isfinite(t.vScale) || !(t.hScale > 0) || !(t.vScale > 0) || file.size() != 92 + t.size) {
        return false;
    }
    t.pixels = d + 92;
    const bool expanded =
        (t.flags == 1 || t.flags == 2) && (t.kind == 1 || t.kind == 2 || (t.kind >= 5 && t.kind <= 9));
    const bool nativeRgba = t.flags == 0 && t.kind == 1;
    return (expanded || nativeRgba) && t.size == (size_t)t.width * t.height * 4;
}

void Put32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
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

// ------------------------------------------------------------------ zip writer (deflate in the caller's thread)
class ZipWriter {
  public:
    bool Open(const fs::path& path) {
        mOut.open(path, std::ios::binary | std::ios::trunc);
        return mOut.is_open();
    }

    // Thread safe. Entries are deflated outside the lock and appended whole.
    void Add(const std::string& name, const uint8_t* data, size_t n) {
        std::vector<uint8_t> packed;
        uint16_t method = 0;
        if (n > 0 && Deflate(data, n, packed) && packed.size() < n) {
            method = 8;
        }
        const uint8_t* body = method ? packed.data() : data;
        const size_t bodySize = method ? packed.size() : n;
        const uint32_t crc = (uint32_t)crc32(0, data, (uInt)n);

        std::lock_guard<std::mutex> lock(mMutex);
        if (mFailed || mPos + bodySize + name.size() + 30 > 0xFFFFFFF0ull || mEntries.size() >= 65534) {
            mFailed =
                true; // would need zip64; the maps are ~1 GB so this is a real error, not a limit to engineer around
            return;
        }
        uint8_t h[30] = {};
        Put32(h, 0x04034b50);
        h[4] = 20;
        h[8] = (uint8_t)method;
        h[11] = 0x00;
        h[12] = 0x21; // date 1980-01-01, time 0: reproducible
        Put32(h + 14, crc);
        Put32(h + 18, (uint32_t)bodySize);
        Put32(h + 22, (uint32_t)n);
        h[26] = (uint8_t)name.size();
        h[27] = (uint8_t)(name.size() >> 8);
        mOut.write(reinterpret_cast<const char*>(h), 30);
        mOut.write(name.data(), name.size());
        mOut.write(reinterpret_cast<const char*>(body), bodySize);
        mEntries.push_back({ name, crc, (uint32_t)bodySize, (uint32_t)n, (uint32_t)mPos, method });
        mPos += 30 + name.size() + bodySize;
    }

    bool Close() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mFailed || !mOut.is_open()) {
            return false;
        }
        const uint64_t cdStart = mPos;
        for (const auto& e : mEntries) {
            uint8_t h[46] = {};
            Put32(h, 0x02014b50);
            h[4] = 20;
            h[6] = 20;
            h[10] = (uint8_t)e.method;
            h[13] = 0x00;
            h[14] = 0x21;
            Put32(h + 16, e.crc);
            Put32(h + 20, e.csize);
            Put32(h + 24, e.usize);
            h[28] = (uint8_t)e.name.size();
            h[29] = (uint8_t)(e.name.size() >> 8);
            Put32(h + 42, e.offset);
            mOut.write(reinterpret_cast<const char*>(h), 46);
            mOut.write(e.name.data(), e.name.size());
            mPos += 46 + e.name.size();
        }
        uint8_t eocd[22] = {};
        Put32(eocd, 0x06054b50);
        eocd[8] = eocd[10] = (uint8_t)mEntries.size();
        eocd[9] = eocd[11] = (uint8_t)(mEntries.size() >> 8);
        Put32(eocd + 12, (uint32_t)(mPos - cdStart));
        Put32(eocd + 16, (uint32_t)cdStart);
        mOut.write(reinterpret_cast<const char*>(eocd), 22);
        mOut.close();
        return !mOut.fail();
    }

    size_t Count() const {
        return mEntries.size();
    }

  private:
    struct Entry {
        std::string name;
        uint32_t crc, csize, usize, offset;
        uint16_t method;
    };

    static bool Deflate(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
        z_stream s{};
        if (deflateInit2(&s, 3, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            return false;
        }
        out.resize(deflateBound(&s, (uLong)n));
        s.next_in = const_cast<Bytef*>(in);
        s.avail_in = (uInt)n;
        s.next_out = out.data();
        s.avail_out = (uInt)out.size();
        const int rc = deflate(&s, Z_FINISH);
        out.resize(s.total_out);
        deflateEnd(&s);
        return rc == Z_STREAM_END;
    }

    std::ofstream mOut;
    std::mutex mMutex;
    std::vector<Entry> mEntries;
    uint64_t mPos = 0;
    bool mFailed = false;
};

// ------------------------------------------------------------------ materials.py: normal / roughness / relief
struct MatRecipe {
    double heightScale, normalStrength, lo, hi, relief;
    bool invert;
};

// Separable periodic box filter, O(pixels). Radius is clamped per axis exactly like box_blur().
std::vector<float> BoxBlur(const std::vector<float>& in, int w, int h, int radius) {
    std::vector<float> a = in;
    int r = std::min(radius, (w - 1) / 2);
    if (r > 0) {
        std::vector<float> out(a.size());
        for (int y = 0; y < h; ++y) {
            const float* row = &a[(size_t)y * w];
            double s = 0;
            for (int k = -r; k <= r; ++k) {
                s += row[(k + w) % w];
            }
            for (int x = 0; x < w; ++x) {
                out[(size_t)y * w + x] = (float)(s / (2 * r + 1));
                s += (double)row[(x + r + 1) % w] - (double)row[(x - r + w) % w];
            }
        }
        a.swap(out);
    }
    r = std::min(radius, (h - 1) / 2);
    if (r > 0) {
        std::vector<float> out(a.size());
        std::vector<double> s(w, 0.0);
        for (int k = -r; k <= r; ++k) {
            const float* row = &a[(size_t)((k + h) % h) * w];
            for (int x = 0; x < w; ++x) {
                s[x] += row[x];
            }
        }
        for (int y = 0; y < h; ++y) {
            const float* add = &a[(size_t)((y + r + 1) % h) * w];
            const float* sub = &a[(size_t)((y - r + h) % h) * w];
            float* dst = &out[(size_t)y * w];
            for (int x = 0; x < w; ++x) {
                dst[x] = (float)(s[x] / (2 * r + 1));
                s[x] += (double)add[x] - (double)sub[x];
            }
        }
        a.swap(out);
    }
    return a;
}

// +X right, +Y up (image rows down), +Z out; Sobel with wrap; height scale in UV units.
std::vector<uint8_t> NormalMap(const std::vector<float>& hm, int w, int h, double scale) {
    const size_t n = (size_t)w * h;
    const float kx = (float)(w * scale / 8), ky = (float)(h * scale / 8);
    std::vector<float> dxr(n), dyr(n);
    for (int y = 0; y < h; ++y) {
        const int ym = y == 0 ? h - 1 : y - 1, yp = y == h - 1 ? 0 : y + 1;
        for (int x = 0; x < w; ++x) {
            const int xm = x == 0 ? w - 1 : x - 1, xp = x == w - 1 ? 0 : x + 1;
            dxr[(size_t)y * w + x] = hm[(size_t)y * w + xp] - hm[(size_t)y * w + xm];
            dyr[(size_t)y * w + x] = hm[(size_t)yp * w + x] - hm[(size_t)ym * w + x];
        }
    }
    std::vector<uint8_t> out(n * 2);
    for (int y = 0; y < h; ++y) {
        const int ym = y == 0 ? h - 1 : y - 1, yp = y == h - 1 ? 0 : y + 1;
        for (int x = 0; x < w; ++x) {
            const int xm = x == 0 ? w - 1 : x - 1, xp = x == w - 1 ? 0 : x + 1;
            const float dx = ((dxr[(size_t)ym * w + x] + 2.0f * dxr[(size_t)y * w + x]) + dxr[(size_t)yp * w + x]) * kx;
            const float dy = ((dyr[(size_t)y * w + xm] + 2.0f * dyr[(size_t)y * w + x]) + dyr[(size_t)y * w + xp]) * ky;
            const float len = std::sqrt(1.0f + dx * dx + dy * dy);
            const float nx = std::clamp((-dx / len) * 0.5f + 0.5f, 0.0f, 1.0f);
            const float ny = std::clamp((dy / len) * 0.5f + 0.5f, 0.0f, 1.0f);
            out[((size_t)y * w + x) * 2] = RintByte(nx * 255.0f);
            out[((size_t)y * w + x) * 2 + 1] = RintByte(ny * 255.0f);
        }
    }
    return out;
}

// Linear-interpolated percentile of |v| like np.percentile(..., 97).
float Percentile97Abs(std::vector<float> v) {
    for (auto& x : v) {
        x = std::fabs(x);
    }
    const double idx = 0.97 * (double)(v.size() - 1);
    const size_t k = (size_t)idx;
    const double gamma = idx - (double)k;
    std::nth_element(v.begin(), v.begin() + k, v.end());
    const double a = v[k];
    double b = a;
    if (k + 1 < v.size()) {
        b = *std::min_element(v.begin() + k + 1, v.end());
    }
    const double diff = b - a;
    return (float)(gamma >= 0.5 ? b - diff * (1 - gamma) : a + diff * gamma);
}

// pixels: RGBA8 w*h. Returns the _n (RG8 normal) and _r (R roughness, G relief) maps.
void GenerateMaps(const uint8_t* px, int w, int h, const MatRecipe& rc, std::vector<uint8_t>& normal,
                  std::vector<uint8_t>& rough) {
    const size_t n = (size_t)w * h;
    float lut[256];
    for (int v = 0; v < 256; ++v) {
        lut[v] = (float)(v <= 10 ? v / 255.0 / 12.92 : std::pow((v / 255.0 + 0.055) / 1.055, 2.4));
    }
    std::vector<float> lum(n);
    for (size_t i = 0; i < n; ++i) {
        lum[i] = ((0.0f + lut[px[i * 4]] * 0.2126f) + lut[px[i * 4 + 1]] * 0.7152f) + lut[px[i * 4 + 2]] * 0.0722f;
    }
    const int unit = std::min(w, h);
    auto radius = [&](double f) { return std::max(1, (int)std::nearbyint(unit * f)); };
    const int r0 = radius(1.0 / 512), r1 = radius(1.0 / 128), r2 = radius(1.0 / 32), rb = radius(1.0 / 8);
    const auto s0 = BoxBlur(lum, w, h, r0), s1 = BoxBlur(lum, w, h, r1), s2 = BoxBlur(lum, w, h, r2),
               broad = BoxBlur(lum, w, h, rb);
    std::vector<float> sq(n);
    for (size_t i = 0; i < n; ++i) {
        sq[i] = lum[i] * lum[i];
    }
    const auto ms = BoxBlur(sq, w, h, r1);
    lum.clear();
    lum.shrink_to_fit();
    sq.clear();
    sq.shrink_to_fit();

    const float sign = rc.invert ? -1.0f : 1.0f;
    const float lo = (float)rc.lo, span = (float)(rc.hi - rc.lo);
    std::vector<float> height(n), d(n), roughness(n);
    for (size_t i = 0; i < n; ++i) {
        const float dFine = sign * (((0.6f * s0[i] + 0.3f * s1[i]) + 0.1f * s2[i]) - broad[i]);
        height[i] = std::clamp(0.5f + dFine, 0.0f, 1.0f);
        // Parallax band-pass: joints and cracks, without fine noise or slab-scale swells.
        d[i] = sign * ((0.25f * s0[i] + 0.75f * s1[i]) - s2[i]);
        const float var = std::max(0.0f, ms[i] - s1[i] * s1[i]);
        roughness[i] = lo + span * std::clamp(std::sqrt(var) * 4.0f, 0.0f, 1.0f);
    }
    const float norm = std::max(Percentile97Abs(d), 1e-4f);
    const float reliefGain = (float)(0.5 * rc.relief);
    rough.assign(n * 2, 0);
    for (size_t i = 0; i < n; ++i) {
        const float g = 0.5f + reliefGain * std::clamp(d[i] / norm, -1.0f, 1.0f);
        rough[i * 2] = RintByte(roughness[i] * 255.0f);
        rough[i * 2 + 1] = RintByte(g * 255.0f);
    }
    normal = NormalMap(height, w, h, rc.heightScale * rc.normalStrength);
}

// 2x2 box average, round half to even (np.rint of the float mean).
std::vector<uint8_t> HalfRgba(const uint8_t* px, int w, int h) {
    const int hw = w / 2, hh = h / 2;
    std::vector<uint8_t> out((size_t)hw * hh * 4);
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            const uint8_t* p = px + ((size_t)y * 2 * w + (size_t)x * 2) * 4;
            for (int c = 0; c < 4; ++c) {
                const unsigned sum = p[c] + p[4 + c] + p[w * 4 + c] + p[w * 4 + 4 + c];
                unsigned q = sum >> 2;
                const unsigned rem = sum & 3;
                if (rem > 2 || (rem == 2 && (q & 1))) {
                    ++q;
                }
                out[((size_t)y * hw + x) * 4 + c] = (uint8_t)q;
            }
        }
    }
    return out;
}

// ------------------------------------------------------------------ recolor.py: YIQ hue, saturation, levels
struct RecRecipe {
    double hue, saturation, gamma, lift;
    double gain[3];
};

std::vector<uint8_t> Recolor(const uint8_t* src, size_t pixelCount, const RecRecipe& rc) {
    std::vector<uint8_t> out(src, src + pixelCount * 4); // alpha stays byte-for-byte
    const double radians = rc.hue * 3.14159265358979323846 / 180.0;
    const float ca = (float)std::cos(radians), sa = (float)std::sin(radians);
    const float sat = (float)rc.saturation, lift = (float)rc.lift, gamma = (float)rc.gamma;
    const float gain[3] = { (float)rc.gain[0], (float)rc.gain[1], (float)rc.gain[2] };
    for (size_t i = 0; i < pixelCount; ++i) {
        float c[3] = { src[i * 4] / 255.0f, src[i * 4 + 1] / 255.0f, src[i * 4 + 2] / 255.0f };
        if (rc.hue != 0.0) {
            const float y = c[0] * 0.299f + c[1] * 0.587f + c[2] * 0.114f;
            const float in = c[0] * 0.596f + c[1] * -0.274f + c[2] * -0.322f;
            const float q = c[0] * 0.211f + c[1] * -0.523f + c[2] * 0.312f;
            const float i2 = in * ca - q * sa, q2 = in * sa + q * ca;
            c[0] = y + i2 * 0.956f + q2 * 0.621f;
            c[1] = y + i2 * -0.272f + q2 * -0.647f;
            c[2] = y + i2 * -1.106f + q2 * 1.703f;
        }
        const float lum = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
        for (int k = 0; k < 3; ++k) {
            float v = lum + (c[k] - lum) * sat;
            v = std::max(v * gain[k] + lift, 0.0f);
            if (gamma != 1.0f) {
                v = std::pow(v, gamma);
            }
            out[i * 4 + k] = RintByte(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }
    }
    return out;
}

// ------------------------------------------------------------------ recipes (our data in 2ship.o2r)
struct Job {
    std::string resource;
    uint64_t area = 1u << 22; // classification size; biggest first keeps the cores busy to the end
    bool hasMat = false, hasRec = false;
    MatRecipe mat{};
    RecRecipe rec{};
};

struct Data {
    json materials, classification, recolor;
    uint64_t hash = 0;
};

bool LoadData(Data& data, std::string& error) {
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
    data.hash = 14695981039346656037ULL;
    return load("materials.json", data.materials) && load("material-classification.json", data.classification) &&
           load("recolor.json", data.recolor);
}

bool Finite(const json& v, double lo, double hi) {
    return v.is_number() && std::isfinite(v.get<double>()) && v.get<double>() >= lo && v.get<double>() <= hi;
}

bool ToMatRecipe(const json& r, MatRecipe& out) {
    if (!Finite(r["relief"], 0, 1) || !Finite(r["height_scale"], 0, 1e9) || !Finite(r["normal_strength"], 0, 1e9) ||
        !r["invert"].is_boolean() || !r["roughness_range"].is_array() || r["roughness_range"].size() != 2 ||
        !Finite(r["roughness_range"][0], 0, 1) || !Finite(r["roughness_range"][1], 0, 1) ||
        r["roughness_range"][0].get<double>() > r["roughness_range"][1].get<double>()) {
        return false;
    }
    out = { r["height_scale"].get<double>(),
            r["normal_strength"].get<double>(),
            r["roughness_range"][0].get<double>(),
            r["roughness_range"][1].get<double>(),
            r["relief"].get<double>(),
            r["invert"].get<bool>() };
    return true;
}

bool ToRecRecipe(const json& r, RecRecipe& out) {
    if (!Finite(r["hue"], -180, 180) || !Finite(r["saturation"], 0, 2) || !Finite(r["gamma"], 0.5, 2) ||
        !Finite(r["lift"], -0.2, 0.2) || !r["gain"].is_array() || r["gain"].size() != 3) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!Finite(r["gain"][i], 0.5, 1.5)) {
            return false;
        }
        out.gain[i] = r["gain"][i].get<double>();
    }
    out.hue = r["hue"].get<double>();
    out.saturation = r["saturation"].get<double>();
    out.gamma = r["gamma"].get<double>();
    out.lift = r["lift"].get<double>();
    return true;
}

// materials.py --classification: defaults | manifest defaults | class | per-texture overrides; classification
// entries that are not excluded, then any manifest-only entries. recolor.py: the explicit shortlist.
bool BuildJobs(const Data& data, std::vector<Job>& jobs, std::string& error) {
    json base = { { "height_scale", 0.025 },
                  { "normal_strength", 1.0 },
                  { "invert", false },
                  { "roughness_range", { 0.65, 0.95 } },
                  { "relief", 0.0 } };
    if (data.materials.contains("defaults")) {
        base.update(data.materials["defaults"]);
    }
    const json noClasses = json::object();
    const json& classes = data.materials.contains("classes") ? data.materials["classes"] : noClasses;
    auto recipeFor = [&](const json& cls, const json& overrides) {
        json r = base;
        if (cls.is_string() && classes.contains(cls.get<std::string>())) {
            r.update(classes[cls.get<std::string>()]);
        }
        if (overrides.is_object()) {
            r.update(overrides);
        }
        return r;
    };

    std::unordered_map<std::string, size_t> index;
    auto jobFor = [&](const std::string& resource) -> Job& {
        auto it = index.find(resource);
        if (it == index.end()) {
            index[resource] = jobs.size();
            jobs.push_back({});
            jobs.back().resource = resource;
        }
        return jobs[index[resource]];
    };

    const json& items = data.materials.at("materials");
    std::unordered_map<std::string, size_t> manifest;
    for (size_t i = 0; i < items.size(); ++i) {
        manifest[items[i].at("resource").get<std::string>()] = i;
    }
    std::vector<bool> used(items.size(), false);
    for (const auto& entry : data.classification.at("textures")) {
        if (!entry.at("excluded").is_null()) {
            continue;
        }
        const std::string resource = entry.at("resource").get<std::string>();
        json overrides = json::object();
        auto m = manifest.find(resource);
        if (m != manifest.end()) {
            used[m->second] = true;
            overrides = items[m->second].value("overrides", json::object());
        }
        Job& job = jobFor(resource);
        job.hasMat = true;
        job.area = (uint64_t)entry.at("size")[0].get<uint64_t>() * entry.at("size")[1].get<uint64_t>();
        if (!ToMatRecipe(recipeFor(entry.at("class"), overrides), job.mat)) {
            error = "invalid material recipe for " + resource;
            return false;
        }
    }
    for (size_t i = 0; i < items.size(); ++i) {
        if (used[i]) {
            continue;
        }
        const std::string resource = items[i].at("resource").get<std::string>();
        Job& job = jobFor(resource);
        job.hasMat = true;
        if (!ToMatRecipe(recipeFor(items[i].value("class", json()), items[i].value("overrides", json::object())),
                         job.mat)) {
            error = "invalid material recipe for " + resource;
            return false;
        }
    }

    json recBase = {
        { "hue", 0.0 }, { "saturation", 1.0 }, { "gamma", 1.0 }, { "gain", { 1.0, 1.0, 1.0 } }, { "lift", 0.0 }
    };
    const json& recClasses = data.recolor.at("classes");
    for (const auto& item : data.recolor.at("textures")) {
        json r = recBase;
        r.update(recClasses.at(item.at("class").get<std::string>()));
        r.update(item.value("overrides", json::object()));
        const std::string resource = item.at("resource").get<std::string>();
        Job& job = jobFor(resource);
        job.hasRec = true;
        if (!ToRecRecipe(r, job.rec)) {
            error = "invalid recolour recipe for " + resource;
            return false;
        }
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.area > b.area; });
    return true;
}

// ------------------------------------------------------------------ sources and signature
bool IsOurs(const std::shared_ptr<Ship::Archive>& a) {
    const auto root = Root().lexically_normal().generic_string();
    return fs::path(a->GetPath()).lexically_normal().generic_string().rfind(root, 0) == 0;
}

// Archives that can supply albedos, highest priority first (the resource manager's own last-wins order),
// without our generated ones, so regenerating while they are mounted never recolours a recolour.
std::vector<std::shared_ptr<Ship::Archive>> SourceArchives() {
    std::vector<std::shared_ptr<Ship::Archive>> result;
    auto all = Archives()->GetArchives();
    for (auto it = all->rbegin(); it != all->rend(); ++it) {
        // A hand-built recolour archive from the Python tools (tools/newdawn/recolor.py) would be recoloured a second
        // time; ours is generated from the pack and mounted above it, so skip it as a source.
        if (!IsOurs(*it) && !(*it)->HasFile("newdawn-recolor.json")) {
            result.push_back(*it);
        }
    }
    return result;
}

// Recipe version + recipe data + which archives are loaded (name, size, mtime; 2ship.o2r is covered by the data hash).
std::string Signature(const Data& data, const std::vector<std::shared_ptr<Ship::Archive>>& sources) {
    uint64_t h = Fnv1a(std::string(kRecipeVersion), 14695981039346656037ULL);
    h = Fnv1a(&data.hash, sizeof(data.hash), h);
    for (const auto& a : sources) {
        const fs::path p(a->GetPath());
        if (p.filename() == "2ship.o2r") {
            continue;
        }
        std::error_code ec;
        h = Fnv1a(p.filename().generic_string(), h);
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
        SPDLOG_ERROR("NewDawn materials: cannot move {} to {}: {}", Staging().string(), Live().string(), ec.message());
        return false;
    }
    return true;
}

void Mount(const json& stamp) {
    for (const char* name : { "newdawn-recolor.o2r", "newdawn-materials.o2r" }) { // recolour first: materials last
        const fs::path p = Live() / name;
        if (fs::exists(p)) {
            Archives()->AddArchive(p.generic_string());
        }
    }
    std::lock_guard<std::mutex> lock(gMutex);
    gMounted = true;
    gMaterialCount = stamp.value("materials", 0);
    gRecolorCount = stamp.value("recolored", 0);
    gSeconds = stamp.value("seconds", 0.0);
    gState = State::Ready;
}

// ------------------------------------------------------------------ the generator
struct Outcome {
    std::atomic<uint32_t> materials{ 0 }, recolored{ 0 }, missing{ 0 }, unusable{ 0 };
    std::mutex mutex;
    std::vector<json> entries;
};

void RunJob(const Job& job, const std::vector<std::shared_ptr<Ship::Archive>>& sources, ZipWriter& matZip,
            ZipWriter& recZip, Outcome& out) {
    const std::string entryName = "alt/" + job.resource;
    std::shared_ptr<Ship::File> file;
    for (const auto& a : sources) {
        if (a->HasFile(entryName) && (file = a->LoadFile(entryName)) && file->Buffer) {
            break;
        }
        file = nullptr;
    }
    if (!file) {
        ++out.missing;
        return;
    }
    Tex tex;
    if (!DecodeAlbedo(*file->Buffer, tex)) {
        ++out.unusable;
        return;
    }
    const size_t count = (size_t)tex.width * tex.height;
    std::vector<uint8_t> recolored;
    const uint8_t* pixels = tex.pixels;
    if (job.hasRec) {
        recolored = Recolor(tex.pixels, count, job.rec);
        auto encoded = EncodeTexture(tex.kind, tex.width, tex.height, tex.flags, tex.hScale, tex.vScale,
                                     recolored.data(), recolored.size());
        recZip.Add(entryName, encoded.data(), encoded.size());
        pixels = recolored.data();
        ++out.recolored;
    }
    if (!job.hasMat) {
        return;
    }
    if (tex.width % 2 || tex.height % 2) { // half-axis maps need even sources
        ++out.unusable;
        return;
    }
    // The renderer ties the maps to the winning albedo by this fingerprint (the recoloured one, if any).
    const uint64_t fingerprint = Fnv1a(pixels, count * 4);
    const int hw = (int)tex.width / 2, hh = (int)tex.height / 2;
    const auto half = HalfRgba(pixels, (int)tex.width, (int)tex.height);
    std::vector<uint8_t> normal, rough;
    GenerateMaps(half.data(), hw, hh, job.mat, normal, rough);
    for (auto [suffix, data] : { std::pair<const char*, const std::vector<uint8_t>*>{ "_n", &normal },
                                 std::pair<const char*, const std::vector<uint8_t>*>{ "_r", &rough } }) {
        auto encoded = EncodeTexture(9, hw, hh, 0, 1.0f, 1.0f, data->data(), data->size());
        matZip.Add(entryName + suffix, encoded.data(), encoded.size());
    }
    json meta = { { "resource", job.resource },
                  { "source_pixels_fnv1a64", Hex16(fingerprint) },
                  { "source_size", { tex.width, tex.height } },
                  { "output_size", { hw, hh } },
                  { "source_type", tex.kind },
                  { "source_flags", tex.flags },
                  { "source_h_byte_scale", tex.hScale },
                  { "source_v_pixel_scale", tex.vScale } };
    std::lock_guard<std::mutex> lock(out.mutex);
    out.entries.push_back(std::move(meta));
    ++out.materials;
}

void Generate(bool boot) {
    const auto start = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& why) {
        SPDLOG_ERROR("NewDawn materials: {}", why);
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
        if (!BuildJobs(data, jobs, error)) {
            return fail(error);
        }
        const auto sources = SourceArchives();
        const std::string signature = Signature(data, sources);

        std::error_code ec;
        fs::remove_all(Staging(), ec);
        fs::create_directories(Staging(), ec);
        ZipWriter matZip, recZip;
        if (!matZip.Open(Staging() / "newdawn-materials.o2r") || !recZip.Open(Staging() / "newdawn-recolor.o2r")) {
            return fail("cannot write to " + Staging().string());
        }

        gTotal = (uint32_t)jobs.size();
        gDone = 0;
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const unsigned threads = std::clamp(boot ? hw : std::max(1u, hw / 2), 1u, 16u);
        SPDLOG_INFO("NewDawn materials: {} textures from {} archives on {} threads", jobs.size(), sources.size(),
                    threads);
        Outcome out;
        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) {
            pool.emplace_back([&]() {
                for (size_t i; (i = next++) < jobs.size();) {
                    try {
                        RunJob(jobs[i], sources, matZip, recZip, out);
                    } catch (const std::exception& e) {
                        SPDLOG_WARN("NewDawn materials: {}: {}", jobs[i].resource, e.what());
                        ++out.unusable;
                    }
                    ++gDone;
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }

        std::sort(out.entries.begin(), out.entries.end(),
                  [](const json& a, const json& b) { return a["resource"] < b["resource"]; });
        const std::string meta = json{
            { "generator", "newdawn-materials" },
            { "version", kRecipeVersion },
            { "resolution_divisor", 2 },
            { "materials", out.entries }
        }.dump();
        matZip.Add("newdawn-materials.json", reinterpret_cast<const uint8_t*>(meta.data()), meta.size());
        const bool matOk = matZip.Close(), recOk = recZip.Close();
        if (!matOk || !recOk) {
            return fail("could not finish the archives (disk full?)");
        }
        if (out.materials == 0) {
            fs::remove(Staging() / "newdawn-materials.o2r", ec);
        }
        if (out.recolored == 0) {
            fs::remove(Staging() / "newdawn-recolor.o2r", ec);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const bool any = out.materials > 0 || out.recolored > 0;
        WriteStamp(Staging(), { { "recipe_version", kRecipeVersion },
                                { "signature", signature },
                                { "state", any ? "ok" : "nosource" },
                                { "materials", out.materials.load() },
                                { "recolored", out.recolored.load() },
                                { "missing_sources", out.missing.load() },
                                { "unusable_sources", out.unusable.load() },
                                { "seconds", seconds } });
        SPDLOG_INFO("NewDawn materials: {} material maps, {} recoloured, {} missing from the HD packs, {} unusable, "
                    "{:.1f}s",
                    out.materials.load(), out.recolored.load(), out.missing.load(), out.unusable.load(), seconds);
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gStagedOk = true;
            gMaterialCount = out.materials;
            gRecolorCount = out.recolored;
            gSeconds = seconds;
            gState = any ? State::Ready : State::NoSource;
            gPendingRestart = !boot && any;
        }
    } catch (const std::exception& e) { return fail(e.what()); }
    gRunning = false;
}

} // namespace

// ------------------------------------------------------------------ public API
bool BootNeedsGeneration() {
    if (CVarGetInteger("gNewDawn.Materials.Generate", 1) == 0) {
        SetState(State::Disabled);
        return false;
    }
    std::error_code ec;
    fs::create_directories(Root(), ec);
    Data data;
    std::string error;
    if (!LoadData(data, error)) {
        SetState(State::Failed, error);
        SPDLOG_ERROR("NewDawn materials: {}", error);
        return false;
    }
    const std::string signature = Signature(data, SourceArchives());

    // A background regeneration from the last session finished: use it if it still matches this install.
    json staged = ReadStamp(Staging());
    if (staged.is_object()) {
        if (staged.value("signature", "") == signature && staged.value("recipe_version", "") == kRecipeVersion) {
            SPDLOG_INFO("NewDawn materials: using the maps regenerated last session");
            Promote();
        } else {
            fs::remove_all(Staging(), ec);
        }
    } else if (fs::exists(Staging())) {
        fs::remove_all(Staging(), ec); // interrupted run
    }

    json stamp = ReadStamp(Live());
    if (stamp.is_object() && stamp.value("signature", "") == signature &&
        stamp.value("recipe_version", "") == kRecipeVersion) {
        const std::string state = stamp.value("state", "");
        if (state == "ok" && fs::exists(Live() / "newdawn-materials.o2r")) {
            Mount(stamp);
            SPDLOG_INFO("NewDawn materials: mounted the cache ({} maps, {} recoloured)", gMaterialCount, gRecolorCount);
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
    // Detached on purpose: closing the window mid-generation must not wait for or terminate over a joinable thread.
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
        return; // failed: the game runs without maps
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
            snprintf(buf, sizeof(buf), "Generating material maps... %u%% (%u of %u textures)",
                     p.total ? p.done * 100 / p.total : 0, p.done, p.total);
            return buf;
        case State::Ready:
            if (p.pendingRestart) {
                snprintf(buf, sizeof(buf), "Regenerated %u maps in %.0fs. Restart the game to use them.",
                         gMaterialCount, gSeconds);
            } else {
                snprintf(buf, sizeof(buf), "Generated: %u material maps, %u recoloured textures (built in %.0fs).",
                         gMaterialCount, gRecolorCount, gSeconds);
            }
            return buf;
        case State::Disabled:
            return "Disabled (gNewDawn.Materials.Generate = 0).";
        case State::NoSource:
            return "No HD texture pack is loaded, so there is nothing to generate maps from.";
        case State::Failed:
            return "Generation failed: " + gMessage;
        default:
            return "Missing. Press Regenerate (or restart the game).";
    }
}

void Regenerate() {
    StartGeneration(false);
}

} // namespace NewDawnMaterials
