// See UniqueArt.h.
#include "UniqueArt.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace NewDawnUnique {
namespace {

bool ReadAll(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), {});
    return !out.empty();
}

bool AllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// <scene>_info.zsi or <scene>_<n>_info.zsi
bool IsSceneFile(const std::string& name, const std::string& scene) {
    const std::string tail = "_info.zsi";
    if (name.size() <= tail.size() || name.compare(name.size() - tail.size(), tail.size(), tail) != 0 ||
        name.compare(0, scene.size() + 1, scene + "_") != 0) {
        return false;
    }
    const std::string mid = name.substr(scene.size() + 1, name.size() - tail.size() - scene.size() - 1);
    return mid.empty() || AllDigits(mid);
}

float Luma(const uint8_t* p) {
    return p[0] * .299f + p[1] * .587f + p[2] * .114f;
}

// Edge seam of an axis against the typical neighbouring step: tiling textures have a seam no bigger than their detail.
bool Tiles(const Mm3d::Image& im, bool horizontal) {
    const int w = im.width, h = im.height;
    const int n = horizontal ? w : h, m = horizontal ? h : w;
    if (n < 4) {
        return false;
    }
    double seam = 0, step = 0;
    for (int k = 0; k < m; ++k) {
        for (int c = 0; c < 3; ++c) {
            auto at = [&](int i) {
                return (float)im.rgba[(horizontal ? ((size_t)k * w + i) : ((size_t)i * w + k)) * 4 + c];
            };
            seam += std::fabs(at(0) - at(n - 1));
            for (int i = 1; i < n; ++i) {
                step += std::fabs(at(i) - at(i - 1)) / (n - 1);
            }
        }
    }
    return seam <= 1.6 * step + 1e-3;
}

// Pads by `pad` pixels on every side, wrapping or replicating per axis.
Mm3d::Image Pad(const Mm3d::Image& in, int pad, bool wrapX, bool wrapY) {
    Mm3d::Image out;
    out.width = in.width + 2 * pad;
    out.height = in.height + 2 * pad;
    out.rgba.resize((size_t)out.width * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        int sy = y - pad;
        sy = wrapY ? ((sy % in.height) + in.height) % in.height : std::clamp(sy, 0, in.height - 1);
        for (int x = 0; x < out.width; ++x) {
            int sx = x - pad;
            sx = wrapX ? ((sx % in.width) + in.width) % in.width : std::clamp(sx, 0, in.width - 1);
            std::memcpy(&out.rgba[((size_t)y * out.width + x) * 4], &in.rgba[((size_t)sy * in.width + sx) * 4], 4);
        }
    }
    return out;
}

Mm3d::Image Crop(const Mm3d::Image& in, int x0, int y0, int w, int h) {
    Mm3d::Image out;
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(&out.rgba[(size_t)y * w * 4], &in.rgba[((size_t)(y + y0) * in.width + x0) * 4], (size_t)w * 4);
    }
    return out;
}

// Bilinear resize at pixel centres (box-averaged first when shrinking by more than 2x).
Mm3d::Image Resize(const Mm3d::Image& in, int w, int h) {
    Mm3d::Image out;
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) {
        const float fy = std::clamp((y + .5f) * in.height / h - .5f, 0.f, (float)in.height - 1);
        const int y0 = (int)fy, y1 = std::min(y0 + 1, in.height - 1);
        const float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            const float fx = std::clamp((x + .5f) * in.width / w - .5f, 0.f, (float)in.width - 1);
            const int x0 = (int)fx, x1 = std::min(x0 + 1, in.width - 1);
            const float tx = fx - x0;
            for (int c = 0; c < 4; ++c) {
                auto a = [&](int yy, int xx) { return (float)in.rgba[((size_t)yy * in.width + xx) * 4 + c]; };
                const float v = (a(y0, x0) * (1 - tx) + a(y0, x1) * tx) * (1 - ty) + (a(y1, x0) * (1 - tx) + a(y1, x1) * tx) * ty;
                out.rgba[((size_t)y * w + x) * 4 + c] = (uint8_t)std::lround(v);
            }
        }
    }
    return out;
}

struct Stats {
    double mean[3] = {}, sd[3] = {};
};
Stats StatsOf(const Mm3d::Image& im) {
    Stats s;
    double n = 0, sum[3] = {}, sq[3] = {};
    for (size_t i = 0; i < (size_t)im.width * im.height; ++i) {
        if (im.rgba[i * 4 + 3] < 128) {
            continue;
        }
        ++n;
        for (int c = 0; c < 3; ++c) {
            const double v = im.rgba[i * 4 + c] / 255.0;
            sum[c] += v;
            sq[c] += v * v;
        }
    }
    for (int c = 0; c < 3; ++c) {
        s.mean[c] = n ? sum[c] / n : .5;
        s.sd[c] = n ? std::sqrt(std::max(sq[c] / n - s.mean[c] * s.mean[c], 1e-6)) : .2;
    }
    return s;
}

} // namespace

const Mm3d::Image* Mm3dScenes::Find(const std::string& scene, const std::string& name) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mScenes.find(scene);
    if (it == mScenes.end()) {
        auto s = std::make_unique<Scene>();
        std::error_code ec;
        const fs::path dir = fs::path(mRoot) / "scenes";
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.is_regular_file(ec) && IsSceneFile(e.path().filename().string(), scene)) {
                files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            std::vector<uint8_t> raw, data;
            std::string error;
            if (!ReadAll(f, raw) || !Mm3d::Inflate(raw, data, error)) {
                continue;
            }
            // Rooms hold their meshes as embedded CMB models: find each "cmb " header and parse the model it sizes.
            for (size_t i = 0; i + 0x80 < data.size(); i += 4) {
                if (std::memcmp(&data[i], "cmb ", 4) != 0) {
                    continue;
                }
                uint32_t size;
                std::memcpy(&size, &data[i + 4], 4);
                if (size <= 0x80 || size > data.size() - i) {
                    continue;
                }
                Mm3d::Model model;
                std::string err;
                if (Mm3d::ParseCmb(std::vector<uint8_t>(data.begin() + i, data.begin() + i + size), model, err)) {
                    for (auto& t : model.textures) {
                        if (t.image.width > 0 && !s->textures.count(t.name)) {
                            s->textures.emplace(t.name, std::move(t.image));
                        }
                    }
                }
            }
        }
        ++mLoaded;
        SPDLOG_INFO("NewDawn unique art: {} 3DS textures from {} room files of {}", s->textures.size(), files.size(), scene);
        it = mScenes.emplace(scene, std::move(s)).first;
    }
    auto t = it->second->textures.find(name);
    return t == it->second->textures.end() ? nullptr : &t->second;
}

bool AcceptCandidate(const Mm3d::Image& n, const Mm3d::Image& c) {
    if (n.width <= 0 || n.height <= 0 || c.width <= 0 || c.height <= 0) {
        return false;
    }
    const double aspect = ((double)c.width / c.height) / ((double)n.width / n.height);
    const double ratio = (double)c.width / n.width;
    if (std::fabs(aspect - 1.0) > .03 || ratio < 1.99 || ratio > 4.01) {
        return false;
    }
    // A cut-out keeps its shape only through its own alpha: a 3DS replacement would turn it into an opaque rectangle.
    for (size_t i = 0; i < (size_t)n.width * n.height; ++i) {
        if (n.rgba[i * 4 + 3] < 250) {
            return false;
        }
    }
    const Stats a = StatsOf(n), b = StatsOf(c);
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(a.mean[k] - b.mean[k]) * 255.0 > 45.0) {
            return false;
        }
    }
    // The same art has the same coarse colour layout: compare 8x8 box means (a banner with its emblem on another flag, or a beam
    // against plain wood, differ by 48 to 67 levels; the verified matches by 16 to 29).
    constexpr int kG = 8;
    auto coarse = [&](const Mm3d::Image& im, float* out) {
        for (int gy = 0; gy < kG; ++gy) {
            for (int gx = 0; gx < kG; ++gx) {
                const int x0 = gx * im.width / kG, x1 = std::max(x0 + 1, (gx + 1) * im.width / kG);
                const int y0 = gy * im.height / kG, y1 = std::max(y0 + 1, (gy + 1) * im.height / kG);
                double acc[3] = {}, cnt = 0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        for (int ch = 0; ch < 3; ++ch) acc[ch] += im.rgba[((size_t)y * im.width + x) * 4 + ch];
                        ++cnt;
                    }
                }
                for (int ch = 0; ch < 3; ++ch) out[(gy * kG + gx) * 3 + ch] = (float)(acc[ch] / cnt);
            }
        }
    };
    float ca[kG * kG * 3], cc[kG * kG * 3];
    coarse(n, ca);
    coarse(c, cc);
    double l1 = 0;
    for (int i = 0; i < kG * kG * 3; ++i) {
        l1 += std::fabs(ca[i] - cc[i]);
    }
    return l1 / (kG * kG * 3) <= 36.0;
}

bool SampleRegion(const Mm3d::Image& src, const double m[6], int w, int h, int k, Mm3d::Image& out) {
    if (src.width <= 0 || src.height <= 0 || src.rgba.size() < (size_t)src.width * src.height * 4 || w <= 0 || h <= 0 || k < 1 || k > 4) {
        return false;
    }
    out.width = w * k;
    out.height = h * k;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    const int W = src.width, H = src.height;
    auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const double fx = (x + 0.5) / k, fy = (y + 0.5) / k;
            const double px = (m[0] * fx + m[1] * fy + m[2]) * W - 0.5;
            const double py = (1.0 - (m[3] * fx + m[4] * fy + m[5])) * H - 0.5;
            const int x0 = (int)std::floor(px), y0 = (int)std::floor(py);
            const double tx = px - x0, ty = py - y0;
            for (int ch = 0; ch < 3; ++ch) {
                auto at = [&](int yy, int xx) { return (double)src.rgba[((size_t)wrap(yy, H) * W + wrap(xx, W)) * 4 + ch]; };
                const double v = at(y0, x0) * (1 - tx) * (1 - ty) + at(y0, x0 + 1) * tx * (1 - ty) + at(y0 + 1, x0) * (1 - tx) * ty +
                                 at(y0 + 1, x0 + 1) * tx * ty;
                out.rgba[((size_t)y * out.width + x) * 4 + ch] = (uint8_t)std::clamp((int)std::lround(v), 0, 255);
            }
        }
    }
    return true;
}

// The colour stored under transparent pixels (palette entries, black) would bleed into the edges of a cut-out when it is upscaled:
// extend the opaque colours outward a few pixels first (the alpha itself is untouched).
static void BleedTransparent(Mm3d::Image& im) {
    const size_t n = (size_t)im.width * im.height;
    std::vector<uint8_t> known(n);
    size_t opaque = 0;
    double mean[3] = {};
    for (size_t i = 0; i < n; ++i) {
        known[i] = im.rgba[i * 4 + 3] >= 128;
        if (known[i]) {
            ++opaque;
            for (int c = 0; c < 3; ++c) mean[c] += im.rgba[i * 4 + c];
        }
    }
    if (opaque == 0 || opaque == n) {
        return;
    }
    for (int pass = 0; pass < 8; ++pass) {
        std::vector<uint8_t> next = known;
        std::vector<uint8_t> rgb = im.rgba;
        bool any = false;
        for (int y = 0; y < im.height; ++y) {
            for (int x = 0; x < im.width; ++x) {
                const size_t i = (size_t)y * im.width + x;
                if (known[i]) continue;
                int cnt = 0, sum[3] = {};
                const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
                for (int k = 0; k < 4; ++k) {
                    const int xx = x + dx[k], yy = y + dy[k];
                    if (xx < 0 || yy < 0 || xx >= im.width || yy >= im.height) continue;
                    const size_t j = (size_t)yy * im.width + xx;
                    if (!known[j]) continue;
                    ++cnt;
                    for (int c = 0; c < 3; ++c) sum[c] += im.rgba[j * 4 + c];
                }
                if (cnt) {
                    for (int c = 0; c < 3; ++c) rgb[i * 4 + c] = (uint8_t)(sum[c] / cnt);
                    next[i] = 1;
                    any = true;
                }
            }
        }
        im.rgba.swap(rgb);
        known.swap(next);
        if (!any) break;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!known[i]) {
            for (int c = 0; c < 3; ++c) im.rgba[i * 4 + c] = (uint8_t)(mean[c] / opaque);
        }
    }
}

bool UpscaleAndGrade(const Mm3d::Image& sourceIn, const Mm3d::Image& original, bool fromMm3d, const Options& opt, Mm3d::Image& out) {
    if (!opt.span || sourceIn.width < 4 || sourceIn.height < 4) {
        return false;
    }
    Mm3d::Image source = sourceIn;
    BleedTransparent(source);
    const bool wrapX = Tiles(source, true), wrapY = Tiles(source, false);
    constexpr int kPad = 8;
    Mm3d::Image padded = Pad(source, kPad, wrapX, wrapY), big;
    if (!opt.span->Upscale4x(padded, big)) {
        return false;
    }
    Mm3d::Image up = Crop(big, kPad * 4, kPad * 4, source.width * 4, source.height * 4);
    // keep the long side within the budget (3DS sources are already 128-256 px)
    const int longSide = std::max(up.width, up.height);
    if (longSide > opt.maxLongSide) {
        const float k = (float)opt.maxLongSide / longSide;
        up = Resize(up, std::max(8, (int)std::lround(up.width * k)), std::max(8, (int)std::lround(up.height * k)));
    }
    // colour: the replacement keeps the original's statistics, so the surrounding scene's lighting and grade still fit it
    const Stats sref = StatsOf(original), sup = StatsOf(up);
    const float meanBlend = fromMm3d ? 1.0f : 0.35f; // a 3DS texture is redrawn in other colours; SPAN output already follows its source
    const float sdBlend = fromMm3d ? 1.0f : 0.5f;
    for (size_t i = 0; i < (size_t)up.width * up.height; ++i) {
        float v[3];
        for (int c = 0; c < 3; ++c) {
            float x = up.rgba[i * 4 + c] / 255.f;
            const float gain = std::clamp((float)(sref.sd[c] / sup.sd[c]), .75f, 1.3f);
            const float g = 1.f + (gain - 1.f) * sdBlend;
            x = (x - (float)sup.mean[c]) * g + (float)sup.mean[c] + ((float)sref.mean[c] - (float)sup.mean[c]) * meanBlend;
            v[c] = std::clamp(x, 0.f, 1.f);
        }
        // a faint S-curve and a little saturation, like the grade of the generated materials
        const float y = v[0] * .299f + v[1] * .587f + v[2] * .114f;
        for (int c = 0; c < 3; ++c) {
            float x = y + (v[c] - y) * 1.06f;
            x = std::clamp(x, 0.f, 1.f);
            x = x + (x * x * (3.f - 2.f * x) - x) * .06f;
            v[c] = x;
        }
        float grain = 1.f;
        if (opt.grain && opt.grain->px > 0) {
            const int px = opt.grain->px;
            const int gx = (int)(i % up.width) % px, gy = (int)(i / up.width) % px;
            grain += opt.grain->fine[(size_t)gy * px + gx] * opt.grainAmount * (1.f - std::fabs(y - .5f));
        }
        for (int c = 0; c < 3; ++c) {
            up.rgba[i * 4 + c] = (uint8_t)std::lround(std::clamp(v[c] * grain, 0.f, 1.f) * 255.f);
        }
    }
    // the output has the original's aspect exactly (a 3DS candidate may be off by a few percent)
    const int h = std::max(8, (int)std::lround((double)up.width * original.height / original.width));
    out = up.height == h ? std::move(up) : Resize(up, up.width, h);
    return true;
}

} // namespace NewDawnUnique
