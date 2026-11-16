// See TextureSynth.h. Port of tools/newdawn/stylelib/{core,pbr,stylelib}.py (the round-2 spike the maintainer approved).
#include "TextureSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>

#include <stb_image.h>

namespace NewDawnSynth {
namespace {

// ------------------------------------------------------------------ rng (own, so results do not depend on the C++ library)
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) {
        Next();
    }
    uint64_t Next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double Uniform() {
        return (double)(Next() >> 11) * (1.0 / 9007199254740992.0);
    }
    bool has = false;
    float cached = 0;
    float Normal() {
        if (has) {
            has = false;
            return cached;
        }
        double u1 = std::max(Uniform(), 1e-12), u2 = Uniform();
        double r = std::sqrt(-2.0 * std::log(u1));
        cached = (float)(r * std::sin(6.283185307179586 * u2));
        has = true;
        return (float)(r * std::cos(6.283185307179586 * u2));
    }
};

inline float Clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}
inline float Smooth(float a, float lo, float hi) {
    float t = Clamp01((a - lo) / (hi - lo + 1e-9f));
    return t * t * (3 - 2 * t);
}
inline float Aa(float d, float w = 1.2f) {
    return Clamp01(d / w + .5f);
}
inline float Mix(float a, float b, float t) {
    return a + (b - a) * t;
}
inline int Wrap(int i, int n) {
    i %= n;
    return i < 0 ? i + n : i;
}

// ------------------------------------------------------------------ periodic gaussian blur (3 box passes per axis)
void BoxPassH(const float* in, float* out, int W, int H, int r) {
    const float inv = 1.0f / (2 * r + 1);
    for (int y = 0; y < H; ++y) {
        const float* row = in + (size_t)y * W;
        float* o = out + (size_t)y * W;
        double sum = 0;
        for (int k = -r; k <= r; ++k) {
            sum += row[Wrap(k, W)];
        }
        for (int x = 0; x < W; ++x) {
            o[x] = (float)sum * inv;
            sum += row[Wrap(x + r + 1, W)] - row[Wrap(x - r, W)];
        }
    }
}
void BoxPassV(const float* in, float* out, int W, int H, int r) {
    const float inv = 1.0f / (2 * r + 1);
    std::vector<double> sum(W, 0.0);
    for (int k = -r; k <= r; ++k) {
        const float* row = in + (size_t)Wrap(k, H) * W;
        for (int x = 0; x < W; ++x) {
            sum[x] += row[x];
        }
    }
    for (int y = 0; y < H; ++y) {
        float* o = out + (size_t)y * W;
        for (int x = 0; x < W; ++x) {
            o[x] = (float)sum[x] * inv;
        }
        const float* add = in + (size_t)Wrap(y + r + 1, H) * W;
        const float* sub = in + (size_t)Wrap(y - r, H) * W;
        for (int x = 0; x < W; ++x) {
            sum[x] += add[x] - sub[x];
        }
    }
}
// Kovesi's box widths for an n=3 gaussian approximation of the given sigma.
void BoxRadii(float sigma, int radii[3]) {
    const double n = 3;
    double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1);
    int wl = (int)std::floor(wIdeal);
    if (wl % 2 == 0) {
        --wl;
    }
    int wu = wl + 2;
    double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4.0 * wl - 4);
    int m = (int)std::lround(mIdeal);
    for (int i = 0; i < 3; ++i) {
        radii[i] = ((i < m ? wl : wu) - 1) / 2;
    }
}
void BlurAxis(Field& a, int W, int H, float sigma, bool horizontal) {
    if (sigma < .45f) {
        return;
    }
    int radii[3];
    BoxRadii(sigma, radii);
    const int len = horizontal ? W : H;
    Field tmp(a.size());
    for (int r : radii) {
        r = std::min(r, (len - 1) / 2);
        if (r < 1) {
            continue;
        }
        if (horizontal) {
            BoxPassH(a.data(), tmp.data(), W, H, r);
        } else {
            BoxPassV(a.data(), tmp.data(), W, H, r);
        }
        a.swap(tmp);
    }
}
Field Blur(Field a, int W, int H, float sx, float sy = -1) {
    if (sy < 0) {
        sy = sx;
    }
    BlurAxis(a, W, H, sx, true);
    BlurAxis(a, W, H, sy, false);
    return a;
}

double StdDev(const Field& a) {
    double m = 0, v = 0;
    for (float x : a) {
        m += x;
    }
    m /= a.size();
    for (float x : a) {
        v += (x - m) * (x - m);
    }
    return std::sqrt(v / a.size());
}
double Mean(const Field& a) {
    double m = 0;
    for (float x : a) {
        m += x;
    }
    return m / a.size();
}

// Periodic bilinear upsample of a (w x h) field to (W x H), sample centres aligned.
Field Upsample(const Field& a, int w, int h, int W, int H) {
    Field o((size_t)W * H);
    for (int y = 0; y < H; ++y) {
        float fy = ((y + .5f) * h / H) - .5f;
        int y0 = (int)std::floor(fy);
        float ty = fy - y0;
        int ya = Wrap(y0, h), yb = Wrap(y0 + 1, h);
        for (int x = 0; x < W; ++x) {
            float fx = ((x + .5f) * w / W) - .5f;
            int x0 = (int)std::floor(fx);
            float tx = fx - x0;
            int xa = Wrap(x0, w), xb = Wrap(x0 + 1, w);
            o[(size_t)y * W + x] = Mix(Mix(a[(size_t)ya * w + xa], a[(size_t)ya * w + xb], tx),
                                       Mix(a[(size_t)yb * w + xa], a[(size_t)yb * w + xb], tx), ty);
        }
    }
    return o;
}

// Gaussian-filtered periodic white noise, unit std (core.noise). Smooth noise is generated at a lower resolution and
// upsampled: the field has no detail below its own sigma, and this is 10x faster for the large features.
Field Noise(int W, int H, uint64_t seed, float sx, float sy = -1) {
    if (sy < 0) {
        sy = sx;
    }
    const float m = std::min(sx, sy);
    int f = m >= 12.f ? 4 : (m >= 5.f ? 2 : 1);
    while (f > 1 && (W % f || H % f)) {
        f /= 2;
    }
    const int w = W / f, h = H / f;
    Rng r(seed);
    Field n((size_t)w * h);
    for (float& v : n) {
        v = r.Normal();
    }
    n = Blur(std::move(n), w, h, sx / f, sy / f);
    if (f > 1) {
        n = Upsample(n, w, h, W, H);
    }
    const double mean = Mean(n), sd = StdDev(n) + 1e-9;
    for (float& v : n) {
        v = (float)((v - mean) / sd);
    }
    return n; // core.noise divides by the std only; the mean is ~0 for filtered noise, removing it is harmless
}

Field Fbm(int W, int H, uint64_t seed, float size, int octaves = 4, float gain = .5f, float aniso = 1.f) {
    Field out((size_t)W * H, 0.f);
    float a = 1.f, tot = 0.f;
    for (int o = 0; o < octaves; ++o) {
        float s = std::max(size / (float)(1 << o), .6f);
        Field n = Noise(W, H, seed * 131 + o, s * aniso, s);
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] += a * n[i];
        }
        tot += a * a;
        a *= gain;
    }
    const float inv = 1.f / std::sqrt(tot);
    for (float& v : out) {
        v *= inv;
    }
    return out;
}

// Central differences with wrap (px units).
void Gradient(const Field& h, int W, int H, Field& gx, Field& gy) {
    gx.assign(h.size(), 0);
    gy.assign(h.size(), 0);
    for (int y = 0; y < H; ++y) {
        const int ym = Wrap(y - 1, H), yp = Wrap(y + 1, H);
        for (int x = 0; x < W; ++x) {
            const int xm = Wrap(x - 1, W), xp = Wrap(x + 1, W);
            gx[(size_t)y * W + x] = (h[(size_t)y * W + xp] - h[(size_t)y * W + xm]) * .5f;
            gy[(size_t)y * W + x] = (h[(size_t)yp * W + x] - h[(size_t)ym * W + x]) * .5f;
        }
    }
}

// Lambert term of a height field in px, light from the top left.
Field Lit(const Field& h, int W, int H, float strength = 1.f) {
    static const float L[3] = { -.52f, -.62f, .58f };
    static const float inv = 1.f / std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    Field gx, gy;
    Gradient(h, W, H, gx, gy);
    Field o(h.size());
    for (size_t i = 0; i < h.size(); ++i) {
        const float nx = -gx[i] * strength, ny = -gy[i] * strength;
        const float len = std::sqrt(nx * nx + ny * ny + 1.f);
        o[i] = (nx * L[0] + ny * L[1] + 1.f * L[2]) * inv / len;
    }
    return o;
}

// ------------------------------------------------------------------ periodic cell layouts
struct Voronoi {
    Field edge, ry;
    std::vector<int> cid;
    float cs = 1;
    int n = 0;
};

Voronoi MakeVoronoi(int W, int H, int nx, int ny, uint64_t seed, float jit) {
    Rng r(seed);
    std::vector<float> sx((size_t)nx * ny), sy((size_t)nx * ny);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            sx[(size_t)j * nx + i] = i + .5f + ((float)r.Uniform() - .5f) * jit;
            sy[(size_t)j * nx + i] = j + .5f + ((float)r.Uniform() - .5f) * jit;
        }
    }
    const float kx = (float)W / nx, ky = (float)H / ny;
    Voronoi v;
    v.edge.resize((size_t)W * H);
    v.ry.resize((size_t)W * H);
    v.cid.resize((size_t)W * H);
    v.cs = std::sqrt(kx * ky);
    v.n = nx * ny;
    for (int y = 0; y < H; ++y) {
        const float py = (y + .5f) / H * ny;
        const int cy = (int)std::floor(py);
        for (int x = 0; x < W; ++x) {
            const float px = (x + .5f) / W * nx;
            const int cx = (int)std::floor(px);
            float b1 = 1e9f, b2 = 1e9f, a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            int best = 0;
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    const int gi = Wrap(cx + di, nx), gj = Wrap(cy + dj, ny);
                    const float qx = sx[(size_t)gj * nx + gi] + (cx + di - gi), qy = sy[(size_t)gj * nx + gi] + (cy + dj - gj);
                    const float dx = (qx - px) * kx, dy = (qy - py) * ky;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    if (d < b1) {
                        a2 = a0;
                        a3 = a1;
                        a0 = qx * kx;
                        a1 = qy * ky;
                        b2 = b1;
                        b1 = d;
                        best = gj * nx + gi;
                    } else if (d < b2) {
                        a2 = qx * kx;
                        a3 = qy * ky;
                        b2 = d;
                    }
                }
            }
            const float sep = std::sqrt((a0 - a2) * (a0 - a2) + (a1 - a3) * (a1 - a3)) + 1e-6f;
            const size_t i = (size_t)y * W + x;
            v.edge[i] = (b2 * b2 - b1 * b1) / (2 * sep);
            v.ry[i] = py * ky - a1;
            v.cid[i] = best;
        }
    }
    return v;
}

struct Ashlar {
    Field fx, fy, bw, bh;
    std::vector<int> cid;
    int n = 0;
};

std::vector<float> Partition(Rng& r, int n, float jit) {
    std::vector<float> w(n);
    float sum = 0;
    for (float& v : w) {
        v = 1 + jit * ((float)r.Uniform() * 2 - 1);
        sum += v;
    }
    std::vector<float> b(n + 1, 0.f);
    for (int i = 0; i < n; ++i) {
        b[i + 1] = b[i] + w[i] / sum;
    }
    b[n] = 1.f;
    return b;
}
int SearchRight(const std::vector<float>& b, float v) { // np.searchsorted(b, v, 'right') - 1, clipped
    int i = (int)(std::upper_bound(b.begin(), b.end(), v) - b.begin()) - 1;
    return std::clamp(i, 0, (int)b.size() - 2);
}

Ashlar MakeAshlar(int W, int H, int rows, int cols, uint64_t seed, float rowJit, float colJit, float stagger, float warp) {
    Rng r(seed);
    Field nu, nv;
    if (warp != 0) {
        nu = Noise(W, H, seed + 7, 40, 40);
        nv = Noise(W, H, seed + 8, 40, 40);
    }
    const auto rb = Partition(r, rows, rowJit);
    std::vector<std::vector<float>> cb(rows);
    std::vector<float> off(rows);
    for (int i = 0; i < rows; ++i) {
        cb[i] = Partition(r, cols, colJit);
        off[i] = stagger >= 0 ? (i % 2) * stagger / cols : (float)r.Uniform();
    }
    Ashlar a;
    const size_t N = (size_t)W * H;
    a.fx.resize(N);
    a.fy.resize(N);
    a.bw.resize(N);
    a.bh.resize(N);
    a.cid.resize(N);
    a.n = rows * cols;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            float u = (x + .5f) / W, v = (y + .5f) / H;
            if (warp != 0) {
                u = u + nu[i] * warp / W;
                v = v + nv[i] * warp / H;
                u -= std::floor(u);
                v -= std::floor(v);
            }
            const int row = SearchRight(rb, v);
            a.fy[i] = (v - rb[row]) / (rb[row + 1] - rb[row]);
            a.bh[i] = (rb[row + 1] - rb[row]) * H;
            float xx = u - off[row];
            xx -= std::floor(xx);
            const int c = SearchRight(cb[row], xx);
            a.fx[i] = (xx - cb[row][c]) / (cb[row][c + 1] - cb[row][c]);
            a.bw[i] = (cb[row][c + 1] - cb[row][c]) * W;
            a.cid[i] = row * cols + c;
        }
    }
    return a;
}

float RectEdge(float fx, float fy, float bw, float bh, float radius) {
    const float qx = std::min(fx, 1 - fx) * bw, qy = std::min(fy, 1 - fy) * bh;
    const float R = std::min(radius, std::min(bw, bh) * .5f);
    const float cx = std::max(R - qx, 0.f), cy = std::max(R - qy, 0.f);
    return (cx > 0 && cy > 0) ? R - std::sqrt(cx * cx + cy * cy) : std::min(qx, qy);
}

// ------------------------------------------------------------------ image resampling
// Separable triangle-filter resample (area-averaging when shrinking), (sw x sh) -> (dw x dh), `c` interleaved channels.
Field Resample(const Field& src, int sw, int sh, int dw, int dh, int c) {
    if (sw == dw && sh == dh) {
        return src;
    }
    auto pass = [&](const Field& in, int iw, int ih, int ow, int oh, bool horiz) {
        Field out((size_t)ow * oh * c);
        const int srcLen = horiz ? iw : ih, dstLen = horiz ? ow : oh;
        const float scale = (float)srcLen / dstLen, support = std::max(1.f, scale);
        for (int o = 0; o < dstLen; ++o) {
            const float center = (o + .5f) * scale;
            const int lo = std::max(0, (int)std::floor(center - support)), hi = std::min(srcLen - 1, (int)std::ceil(center + support));
            std::vector<float> w;
            float sum = 0;
            for (int i = lo; i <= hi; ++i) {
                float t = 1 - std::fabs((i + .5f - center) / support);
                t = std::max(t, 0.f);
                w.push_back(t);
                sum += t;
            }
            if (sum <= 0) {
                w.assign(1, 1.f);
                sum = 1;
            }
            for (int k = 0; k < (horiz ? oh : ow); ++k) {
                for (int ch = 0; ch < c; ++ch) {
                    float acc = 0;
                    for (int i = lo; i <= hi; ++i) {
                        const size_t idx = horiz ? ((size_t)k * iw + i) * c + ch : ((size_t)i * iw + k) * c + ch;
                        acc += in[idx] * w[i - lo];
                    }
                    const size_t oidx = horiz ? ((size_t)k * ow + o) * c + ch : ((size_t)o * ow + k) * c + ch;
                    out[oidx] = acc / sum;
                }
            }
        }
        return out;
    };
    Field a = pass(src, sw, sh, dw, sh, true);
    return pass(a, dw, sh, dw, dh, false);
}

// Bilinear wrap sample of a (S x S) single-channel field at float source coordinates.
inline float SampleWrap(const Field& a, int S, float u, float v) {
    u -= S * std::floor(u / S);
    v -= S * std::floor(v / S);
    int u0 = (int)u, v0 = (int)v;
    const float fu = u - u0, fv = v - v0;
    if (u0 >= S) {
        u0 = S - 1;
    }
    if (v0 >= S) {
        v0 = S - 1;
    }
    const int u1 = (u0 + 1) % S, v1 = (v0 + 1) % S;
    return (a[(size_t)v0 * S + u0] * (1 - fu) + a[(size_t)v0 * S + u1] * fu) * (1 - fv) +
           (a[(size_t)v1 * S + u0] * (1 - fu) + a[(size_t)v1 * S + u1] * fu) * fv;
}

// ------------------------------------------------------------------ the CC0 library
bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), {});
    return !out.empty();
}

bool LoadJpeg(const std::string& path, int channels, std::vector<uint8_t>& px, int& size) {
    std::vector<uint8_t> file;
    if (!ReadFile(path, file)) {
        return false;
    }
    int w, h, n;
    uint8_t* data = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &n, channels);
    if (!data) {
        return false;
    }
    bool ok = w == h && w >= 16;
    if (ok) {
        size = w;
        px.assign(data, data + (size_t)w * h * channels);
    }
    stbi_image_free(data);
    return ok;
}

Field ToField(const std::vector<uint8_t>& px, int size, int channels, int outPx) {
    Field f(px.size());
    for (size_t i = 0; i < px.size(); ++i) {
        f[i] = px[i] / 255.f;
    }
    return Resample(f, size, size, outPx, outPx, channels);
}

std::shared_ptr<Surface> BuildSurface(const std::string& dir, const std::string& id, int px) {
    auto s = std::make_shared<Surface>();
    s->px = px;
    std::vector<uint8_t> a, n, r, h;
    int sa = 0, sn = 0, sr = 0, sh = 0;
    const std::string base = dir + "/" + id;
    const bool ok = LoadJpeg(base + ".albedo.jpg", 3, a, sa) && LoadJpeg(base + ".normal.jpg", 3, n, sn) &&
                    LoadJpeg(base + ".rough.jpg", 1, r, sr) && LoadJpeg(base + ".height.jpg", 1, h, sh);
    const size_t N = (size_t)px * px;
    if (!ok) {
        s->rgb.assign(N * 3, .5f);
        s->fine.assign(N, 0.f);
        s->disp.assign(N, .5f);
        s->rough.assign(N, .8f);
        s->ao.assign(N, 1.f);
        s->nx.assign(N, 0.f);
        s->ny.assign(N, 0.f);
        return nullptr;
    }
    s->rgb = ToField(a, sa, 3, px);
    Field nrm = ToField(n, sn, 3, px);
    s->nx.resize(N);
    s->ny.resize(N);
    for (size_t i = 0; i < N; ++i) {
        s->nx[i] = nrm[i * 3] * 2 - 1;
        s->ny[i] = nrm[i * 3 + 1] * 2 - 1;
    }
    s->rough = ToField(r, sr, 1, px);
    s->disp = ToField(h, sh, 1, px);
    Field y(N);
    for (size_t i = 0; i < N; ++i) {
        y[i] = s->rgb[i * 3] * .299f + s->rgb[i * 3 + 1] * .587f + s->rgb[i * 3 + 2] * .114f;
    }
    const float k = (float)px / 1024.f;
    Field fz = Blur(y, px, px, 5.f * k);
    for (size_t i = 0; i < N; ++i) {
        fz[i] = y[i] - fz[i];
    }
    const float sd = (float)StdDev(fz) + 1e-6f;
    s->fine.resize(N);
    for (size_t i = 0; i < N; ++i) {
        s->fine[i] = 2.f * std::tanh(fz[i] / sd / 2.f);
    }
    // the library ships no AO: approximate it from the height (cavities darker), only its variation is used
    Field bl = Blur(s->disp, px, px, 6.f * k);
    s->ao.resize(N);
    Field cav(N);
    for (size_t i = 0; i < N; ++i) {
        cav[i] = std::max(bl[i] - s->disp[i], 0.f);
    }
    const float cs = (float)StdDev(cav) + 1e-6f;
    for (size_t i = 0; i < N; ++i) {
        s->ao[i] = Clamp01(1.f - .5f * cav[i] / (2 * cs));
    }
    return s;
}

// A grey, flat stand-in so a missing library file degrades instead of failing the whole generation.
std::shared_ptr<Surface> NeutralSurface(int px) {
    auto s = std::make_shared<Surface>();
    const size_t N = (size_t)px * px;
    s->px = px;
    s->rgb.assign(N * 3, .5f);
    s->fine.assign(N, 0.f);
    s->disp.assign(N, .5f);
    s->rough.assign(N, .8f);
    s->ao.assign(N, 1.f);
    s->nx.assign(N, 0.f);
    s->ny.assign(N, 0.f);
    return s;
}

} // namespace

std::shared_ptr<const Surface> Library::Get(const std::string& id, int px) {
    const std::string key = id + "@" + std::to_string(px);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mCache.find(key);
        if (it != mCache.end()) {
            return it->second;
        }
    }
    auto s = BuildSurface(mDir, id, px); // outside the lock: decoding is the slow part
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mCache.find(key);
    if (it != mCache.end()) {
        return it->second;
    }
    if (!s) {
        mMissing.push_back(id);
        s = NeutralSurface(px);
    }
    mCache[key] = s;
    // Bounded memory: drop surfaces that no job is using (10 floats per texel, 40 MB at 1024 px) until under budget.
    size_t bytes = 0;
    for (const auto& kv : mCache) {
        bytes += (size_t)kv.second->px * kv.second->px * 40;
    }
    for (auto it = mCache.begin(); bytes > kBudgetBytes && it != mCache.end();) {
        if (it->second.use_count() == 1 && it->second != s) {
            bytes -= (size_t)it->second->px * it->second->px * 40;
            it = mCache.erase(it);
        } else {
            ++it;
        }
    }
    return s;
}

std::string Library::Resolve(const std::string& selector, uint32_t seed, const std::string& region) const {
    if (selector.rfind("role:", 0) != 0 && selector.rfind("class:", 0) != 0) {
        return selector;
    }
    std::vector<std::string> regionList; // "interiors,clock_town": the first region with a match wins
    for (size_t at = 0; at <= region.size();) {
        const size_t comma = region.find(',', at);
        const std::string r = region.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!r.empty()) regionList.push_back(r);
        at = comma == std::string::npos ? region.size() + 1 : comma + 1;
    }
    std::vector<std::vector<const Entry*>> local(regionList.size());
    std::vector<const Entry*> any, other;
    size_t from = 0;
    while (from <= selector.size()) {
        const size_t bar = selector.find('|', from);
        const std::string one = selector.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
        from = bar == std::string::npos ? selector.size() + 1 : bar + 1;
        const bool byRole = one.rfind("role:", 0) == 0;
        const std::string key = one.substr(one.find(':') + 1);
        for (const auto& e : mEntries) {
            const bool match = byRole ? std::find(e.roles.begin(), e.roles.end(), key) != e.roles.end() : e.cls == key;
            if (!match) {
                continue;
            }
            bool placed = false;
            for (size_t i = 0; i < regionList.size() && !placed; ++i) {
                if (std::find(e.regions.begin(), e.regions.end(), regionList[i]) != e.regions.end()) {
                    local[i].push_back(&e);
                    placed = true;
                }
            }
            if (!placed) {
                const bool isAny = e.regions.empty() || std::find(e.regions.begin(), e.regions.end(), "any") != e.regions.end();
                (isAny ? any : other).push_back(&e);
            }
        }
    }
    auto byId = [](const Entry* a, const Entry* b) { return a->id < b->id; };
    std::vector<std::vector<const Entry*>*> order;
    for (auto& l : local) order.push_back(&l);
    order.push_back(&any);
    order.push_back(&other);
    for (auto* v : order) {
        std::sort(v->begin(), v->end(), byId);
        if (!v->empty()) {
            return (*v)[seed % v->size()]->id;
        }
    }
    return "";
}

std::vector<std::string> Library::Missing() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mMissing;
}

// ------------------------------------------------------------------ palette
Palette Palette::Gray() {
    Palette p;
    for (int i = 0; i < 256; ++i) {
        const float v = .08f + .84f * i / 255.f;
        p.lut[i][0] = p.lut[i][1] = p.lut[i][2] = v;
    }
    return p;
}

Palette Palette::FromRgba(const uint8_t* rgba, size_t n, float sat) {
    struct Px {
        float y, c[3];
    };
    std::vector<Px> px;
    px.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (rgba[i * 4 + 3] > 127) {
            Px p;
            for (int c = 0; c < 3; ++c) {
                p.c[c] = rgba[i * 4 + c] / 255.f;
            }
            p.y = p.c[0] * .299f + p.c[1] * .587f + p.c[2] * .114f;
            px.push_back(p);
        }
    }
    if (px.empty()) {
        for (size_t i = 0; i < n; ++i) {
            Px p;
            for (int c = 0; c < 3; ++c) {
                p.c[c] = rgba[i * 4 + c] / 255.f;
            }
            p.y = p.c[0] * .299f + p.c[1] * .587f + p.c[2] * .114f;
            px.push_back(p);
        }
    }
    std::stable_sort(px.begin(), px.end(), [](const Px& a, const Px& b) { return a.y < b.y; });
    const int bins = 24;
    float cols[bins][3];
    const size_t total = px.size();
    size_t pos = 0;
    for (int b = 0; b < bins; ++b) { // np.array_split: the first (total % bins) parts get one extra pixel
        const size_t cnt = total / bins + ((size_t)b < total % bins ? 1 : 0);
        double acc[3] = { 0, 0, 0 };
        for (size_t i = 0; i < cnt; ++i) {
            for (int c = 0; c < 3; ++c) {
                acc[c] += px[pos + i].c[c];
            }
        }
        pos += cnt;
        for (int c = 0; c < 3; ++c) {
            cols[b][c] = cnt ? (float)(acc[c] / cnt) : 0.f;
        }
    }
    for (int b = 0; b < bins; ++b) {
        const float m = (cols[b][0] + cols[b][1] + cols[b][2]) / 3.f;
        for (int c = 0; c < 3; ++c) {
            cols[b][c] = Clamp01(m + (cols[b][c] - m) * sat);
        }
    }
    float raw[256][3];
    for (int i = 0; i < 256; ++i) { // np.interp over linspace(0,1,bins)
        const float t = i / 255.f * (bins - 1);
        const int i0 = std::min((int)t, bins - 2);
        const float f = t - i0;
        for (int c = 0; c < 3; ++c) {
            raw[i][c] = Mix(cols[i0][c], cols[i0 + 1][c], f);
        }
    }
    Palette p;
    float g[49], gs = 0;
    for (int k = -24; k <= 24; ++k) {
        g[k + 24] = std::exp(-.5f * (k / 9.f) * (k / 9.f));
        gs += g[k + 24];
    }
    for (int i = 0; i < 256; ++i) { // gaussian smoothing with edge padding (CI textures have few colours)
        for (int c = 0; c < 3; ++c) {
            float acc = 0;
            for (int k = -24; k <= 24; ++k) {
                acc += raw[std::clamp(i + k, 0, 255)][c] * g[k + 24];
            }
            p.lut[i][c] = acc / gs;
        }
    }
    return p;
}

namespace {

inline void PalAt(const Palette& p, float t, float out[3]) {
    const float i = Clamp01(t) * 255.f;
    const int i0 = (int)i, i1 = std::min(i0 + 1, 255);
    const float f = i - i0;
    for (int c = 0; c < 3; ++c) {
        out[c] = p.lut[i0][c] * (1 - f) + p.lut[i1][c] * f;
    }
}

// ------------------------------------------------------------------ maps shared by the generators
Field N2HeightNormal(const Field& h, int W, int H, float strength) { // (-gx, +gy) * strength, 2 channels
    Field gx, gy;
    Gradient(h, W, H, gx, gy);
    Field o(h.size() * 2);
    for (size_t i = 0; i < h.size(); ++i) {
        o[i * 2] = -gx[i] * strength;
        o[i * 2 + 1] = gy[i] * strength;
    }
    return o;
}

void FinishNormal(Field& nxy) { // slope pair -> xy of the unit normal
    for (size_t i = 0; i < nxy.size() / 2; ++i) {
        const float ln = std::max(std::sqrt(1 + nxy[i * 2] * nxy[i * 2] + nxy[i * 2 + 1] * nxy[i * 2 + 1]), 1e-6f);
        nxy[i * 2] /= ln;
        nxy[i * 2 + 1] /= ln;
    }
}

struct MossLayer {
    Field m, col, h;
};
MossLayer MakeMoss(int W, int H, uint64_t seed, float amount, const Surface& ms, float /*s*/, const Field& cav, const Field* shade,
                   float scale = 1.3f) {
    Field f = Fbm(W, H, seed + 61, 70.f * (W / 1024.f), 4);
    MossLayer L;
    L.m.resize(f.size());
    L.col.resize(f.size() * 3);
    L.h.resize(f.size());
    Field r(ms.px * (size_t)ms.px), g(r.size()), b(r.size());
    for (size_t i = 0; i < r.size(); ++i) {
        r[i] = ms.rgb[i * 3];
        g[i] = ms.rgb[i * 3 + 1];
        b[i] = ms.rgb[i * 3 + 2];
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            L.m[i] = Smooth(f[i] * .55f + cav[i] * .9f + (shade ? (*shade)[i] * .5f : 0.f), 1.1f - amount, 1.5f - amount);
            L.col[i * 3] = SampleWrap(r, ms.px, x * scale, y * scale);
            L.col[i * 3 + 1] = SampleWrap(g, ms.px, x * scale, y * scale);
            L.col[i * 3 + 2] = SampleWrap(b, ms.px, x * scale, y * scale);
            L.h[i] = SampleWrap(ms.disp, ms.px, x * scale, y * scale);
        }
    }
    return L;
}

// ------------------------------------------------------------------ masonry (brick, ashlar, cobble)
Mat Masonry(Library& lib, const Palette& pal, const Params& p, int W, int H, uint64_t seed, const std::string& region) {
    const float s = W / 1024.f;
    const bool brick = p.Str("layout", "brick") == "brick";
    const int rows = (int)p.Get("rows", 4), cols = (int)p.Get("cols", 3), nCob = (int)p.Get("n", 7);
    const float gapF = (float)p.Get("gap", .08), bevelF = (float)p.Get("bevel", .07), radiusF = (float)p.Get("radius", .12);
    const float toneLo = (float)p.Get("tone0", .28), toneHi = (float)p.Get("tone1", .72), gapTone = (float)p.Get("gap_tone", 1.0);
    const float var = (float)p.Get("var", .7), chip = (float)p.Get("chip", 1.0), grime = (float)p.Get("grime", 1.0);
    const float streaks = (float)p.Get("streaks", .5), moss = (float)p.Get("moss", 0.), faceSc = (float)p.Get("face_sc", 1.0);
    const float faceGain = (float)p.Get("face_gain", .09), relief = (float)p.Get("relief", 22.), nstrength = (float)p.Get("nstrength", 1.4);
    const float wear = (float)p.Get("wear", 1.0), gapDesat = (float)p.Get("gap_desat", .55), edgeDark = (float)p.Get("edge_dark", .35);
    const size_t N = (size_t)W * H;
    Rng r(seed);

    Field d(N), fy(N);
    std::vector<int> cid;
    int ncell;
    float cellpx;
    if (brick) {
        Ashlar A = MakeAshlar(W, H, rows, cols, seed, (float)p.Get("row_jit", 0.), (float)p.Get("col_jit", .08), (float)p.Get("stagger", .5), 2.f * s);
        cellpx = (float)H / rows;
        ncell = A.n;
        cid = std::move(A.cid);
        for (size_t i = 0; i < N; ++i) {
            d[i] = RectEdge(A.fx[i], A.fy[i], A.bw[i], A.bh[i], radiusF * cellpx);
            fy[i] = A.fy[i];
        }
    } else {
        const int ny = std::max(1, (int)std::lround((double)nCob * H / W));
        Voronoi V = MakeVoronoi(W, H, nCob, ny, seed, (float)p.Get("jit", .75));
        cellpx = V.cs;
        ncell = V.n;
        cid = std::move(V.cid);
        for (size_t i = 0; i < N; ++i) {
            d[i] = V.edge[i];
            fy[i] = Clamp01(.5f + V.ry[i] / (cellpx * 1.4f));
        }
    }
    // hand-hewn edges: low-frequency wobble + bites out of the arrises
    {
        Field w1 = Fbm(W, H, seed + 21, 26 * s, 3), w2 = Noise(W, H, seed + 22, 2.0f * s);
        for (size_t i = 0; i < N; ++i) {
            d[i] += w1[i] * 2.2f * s + w2[i] * .7f * s;
        }
    }
    Field bites = Fbm(W, H, seed + 23, 11 * s, 3);
    for (size_t i = 0; i < N; ++i) {
        bites[i] = Smooth(bites[i], 1.15f, 1.9f) * chip * (d[i] < 16 * s ? 1.f : 0.f);
        d[i] -= bites[i] * 7 * s;
    }
    const float gp = gapF * cellpx, bv = bevelF * cellpx;
    Field inner(N), tEdge(N), m(N);
    for (size_t i = 0; i < N; ++i) {
        inner[i] = d[i] - gp * .5f;
        tEdge[i] = Clamp01(inner[i] / bv);
        m[i] = Aa(inner[i], 1.3f * s);
    }
    // CC0 surfaces: per-cell random window of the face set, the mortar set tiled
    const std::string faceId = lib.Resolve(p.Str("face", "rock050"), (uint32_t)seed, region), gapId = lib.Resolve(p.Str("gap_set", "concrete034"), (uint32_t)seed + 1, region);
    if (faceId.empty() || gapId.empty()) {
        Mat bad;
        bad.valid = false;
        return bad;
    }
    auto F = lib.Get(faceId, W);
    auto G = lib.Get(gapId, W);
    const int S = W;
    std::vector<float> off0(ncell), off1(ncell), rc(ncell), rt1(ncell);
    for (int c = 0; c < ncell; ++c) {
        off0[c] = (float)r.Uniform() * S;
        off1[c] = (float)r.Uniform() * S;
    }
    for (int c = 0; c < ncell; ++c) {
        r.Uniform(); // rt[0] is unused (kept to mirror the reference draw order)
    }
    for (int c = 0; c < ncell; ++c) {
        rt1[c] = ((float)r.Uniform() - .5f) * .05f;
    }
    for (int c = 0; c < ncell; ++c) {
        rc[c] = (float)r.Uniform();
    }
    Field hs(N), gap(N), fl(N), fd(N), fr(N), fao(N), gl(N), gd(N), gr(N), fn(N * 2), gn(N * 2);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            const int c = cid[i];
            const float fu = x * faceSc + off0[c], fv = y * faceSc + off1[c];
            fl[i] = SampleWrap(F->fine, S, fu, fv);
            fd[i] = SampleWrap(F->disp, S, fu, fv);
            fr[i] = SampleWrap(F->rough, S, fu, fv);
            fao[i] = SampleWrap(F->ao, S, fu, fv);
            fn[i * 2] = SampleWrap(F->nx, S, fu, fv);
            fn[i * 2 + 1] = SampleWrap(F->ny, S, fu, fv);
            gl[i] = SampleWrap(G->fine, S, (float)x, (float)y);
            gd[i] = SampleWrap(G->disp, S, (float)x, (float)y);
            gr[i] = SampleWrap(G->rough, S, (float)x, (float)y);
            gn[i * 2] = SampleWrap(G->nx, S, (float)x, (float)y);
            gn[i * 2 + 1] = SampleWrap(G->ny, S, (float)x, (float)y);
            const float pill = std::sqrt(std::max(1 - (1 - tEdge[i]) * (1 - tEdge[i]), 0.f));
            float v = pill * bv * .45f + (fy[i] - .5f) * rt1[c] * bv * 8 - bites[i] * 3 * s;
            hs[i] = inner[i] > 0 ? v : std::min(v, 0.f);
        }
    }
    Field hb = Blur(hs, W, H, .8f * s);
    Mat out;
    out.w = W;
    out.h = H;
    out.height.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.height[i] = hb[i] + m[i] * (fd[i] - .5f) * relief * s + (1 - m[i]) * (gd[i] - .5f) * relief * .6f * s;
    }
    Field Ls = Lit(hb, W, H, 1.f);
    const float lsMean = (float)Mean(Ls);
    // grime: gravity streaks below the joints (gaps rolled down by 10 px, blurred long and thin)
    Field gm(N);
    const int shift = (int)(10 * s);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            gm[(size_t)y * W + x] = 1 - m[(size_t)Wrap(y - shift, H) * W + x];
        }
    }
    Field stk = Blur(gm, W, H, 5 * s, 70 * s);
    Field stkN = Noise(W, H, seed + 41, 4 * s, 50 * s);
    for (size_t i = 0; i < N; ++i) {
        stk[i] *= .65f + .35f * Smooth(stkN[i], -.6f, .9f);
    }
    out.alb.resize(N * 3);
    Field roughF(N), nrm = N2HeightNormal(Blur(hs, W, H, .7f * s), W, H, 1.f);
    for (size_t i = 0; i < N; ++i) {
        const int c = cid[i];
        const float base = toneLo + (toneHi - toneLo) * (.5f + (rc[c] - .5f) * var);
        float faceT = base + faceGain * fl[i] + .22f * (Ls[i] - lsMean) + (fao[i] - .5f) * .12f + bites[i] * .08f;
        const float aoGap = std::exp(-std::max(-inner[i], 0.f) / (gp * .45f + 1e-3f));
        const float gapT = gapTone + gl[i] * faceGain * .8f - aoGap * (gapTone > .5f ? .22f : 0.f);
        const float near = std::exp(-std::max(inner[i], 0.f) / (bv * 2.2f));
        const float omf = 1 - fy[i];
        faceT -= grime * (edgeDark * .2f * near + streaks * .22f * stk[i] + .10f * omf * omf * omf);
        const float te = 1 - tEdge[i];
        faceT += wear * .10f * te * te * Smooth(Ls[i], lsMean + .02f, lsMean + .2f);
        const float tone = Mix(gapT, faceT, m[i]);
        float rgb[3];
        PalAt(pal, tone, rgb);
        if (gapDesat != 0) {
            const float gray = rgb[0] * .299f + rgb[1] * .587f + rgb[2] * .114f, k = (1 - m[i]) * gapDesat;
            for (float& v : rgb) {
                v = Mix(v, gray, k);
            }
        }
        for (int k = 0; k < 3; ++k) {
            out.alb[i * 3 + k] = Clamp01(rgb[k] + bites[i] * .06f);
        }
        nrm[i * 2] += Mix(gn[i * 2], fn[i * 2], m[i]) * nstrength;
        nrm[i * 2 + 1] += Mix(gn[i * 2 + 1], fn[i * 2 + 1], m[i]) * nstrength;
        roughF[i] = std::clamp(Mix(gr[i] * .3f + .72f, fr[i] * .5f + .5f, m[i]) - te * te * te * .15f * m[i] + grime * .06f * stk[i], .25f, 1.f);
    }
    if (moss > 0) {
        auto MS = lib.Get(lib.Resolve(p.Str("moss_set", "moss002"), (uint32_t)seed, region), W);
        Field cav(N), shade(N);
        for (size_t i = 0; i < N; ++i) {
            cav[i] = Smooth(std::exp(-std::max(inner[i], 0.f) / (bv * 1.6f)) + stk[i] * .5f, .3f, 1.f);
            shade[i] = (1 - fy[i]) * m[i];
        }
        MossLayer L = MakeMoss(W, H, seed, moss, *MS, s, cav, &shade);
        for (size_t i = 0; i < N; ++i) {
            for (int k = 0; k < 3; ++k) {
                out.alb[i * 3 + k] = Mix(out.alb[i * 3 + k], Clamp01(L.col[i * 3 + k] * .85f), L.m[i] * .8f);
            }
            out.height[i] += L.m[i] * (L.h[i] - .5f) * 16 * s;
            nrm[i * 2] *= 1 - L.m[i] * .4f;
            nrm[i * 2 + 1] *= 1 - L.m[i] * .4f;
            roughF[i] = Mix(roughF[i], .95f, L.m[i] * .6f);
        }
    }
    FinishNormal(nrm);
    out.rough = std::move(roughF);
    out.normal = std::move(nrm);
    out.relief = .55f;
    out.nstrength = nstrength;
    return out;
}

// ------------------------------------------------------------------ direct (one CC0 set tiled onto the tile)
// Rows [r0, r1) of a plank set that span n planks between two plank gaps, so the crop still tiles vertically.
bool PlankRows(const Surface& S, int n, uint32_t seed, int& r0, int& r1) {
    const int L = S.px;
    std::vector<float> prof(L, 0.f);
    for (int y = 0; y < L; ++y) {
        double a = 0;
        for (int x = 0; x < L; ++x) {
            a += S.disp[(size_t)y * L + x];
        }
        prof[y] = (float)(a / L);
    }
    std::vector<float> sm(L, 0.f); // np.convolve(prof, ones(5)/5, 'same')
    for (int y = 0; y < L; ++y) {
        float a = 0;
        for (int k = -2; k <= 2; ++k) {
            const int yy = y + k;
            a += (yy >= 0 && yy < L) ? prof[yy] : 0.f;
        }
        sm[y] = a / 5.f;
    }
    double mean = 0, var = 0;
    for (float v : sm) {
        mean += v;
    }
    mean /= L;
    for (float v : sm) {
        var += (v - mean) * (v - mean);
    }
    const double thr = mean - .5 * std::sqrt(var / L);
    std::vector<int> g;
    const int win = std::max(1, (int)std::lround(50.0 * L / 1024));
    for (int i = 0; i < L; ++i) {
        if (sm[i] < thr) {
            float mn = 1e9f;
            for (int k = std::max(0, i - win); k <= std::min(L - 1, i + win); ++k) {
                mn = std::min(mn, sm[k]);
            }
            if (sm[i] == mn) {
                g.push_back(i);
            }
        }
    }
    if ((int)g.size() < n + 1) {
        return false;
    }
    const int i0 = (int)(seed % (g.size() - n));
    r0 = g[i0];
    r1 = g[i0 + n];
    return r1 - r0 >= 8;
}

// Rotates a (w x h) interleaved field by `rot` quarter turns counter-clockwise; updates w,h.
Field Rot90(const Field& a, int& w, int& h, int c, int rot) {
    rot = ((rot % 4) + 4) % 4;
    Field cur = a;
    for (int k = 0; k < rot; ++k) {
        Field nx(cur.size());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (int ch = 0; ch < c; ++ch) { // ccw: new(h-1-... ) np.rot90 -> new[y'][x'] = old[x'][w-1-y']
                    nx[((size_t)(w - 1 - x) * h + y) * c + ch] = cur[((size_t)y * w + x) * c + ch];
                }
            }
        }
        cur.swap(nx);
        std::swap(w, h);
    }
    return cur;
}

// Resamples a (sw x sh) field to W x H repeating it k x k times.
Field TileTo(const Field& a, int sw, int sh, int W, int H, int k, int c) {
    k = std::max(1, k);
    Field t = Resample(a, sw, sh, W / k, H / k, c);
    Field o((size_t)W * H * c);
    const int tw = W / k, th = H / k;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                o[((size_t)y * W + x) * c + ch] = t[((size_t)(y % th) * tw + (x % tw)) * c + ch];
            }
        }
    }
    return o;
}

Mat Direct(Library& lib, const Palette& pal, const Params& p, int W, int H, uint64_t seed, const std::string& region) {
    const float s = W / 1024.f;
    const int k = std::max(1, (int)p.Get("k", 1)), rot = (int)p.Get("rot", 0), planks = (int)p.Get("planks", 0);
    const float contrast = (float)p.Get("contrast", .8), tr0 = (float)p.Get("trange0", .08), tr1 = (float)p.Get("trange1", .92);
    const float hf = (float)p.Get("hf", .8), chroma = (float)p.Get("chroma", .22), grime = (float)p.Get("grime", .35);
    const float wear = (float)p.Get("wear", .25), moss = (float)p.Get("moss", 0.), relief = (float)p.Get("relief", 18.);
    const float nstrength = (float)p.Get("nstrength", 1.4), aoK = (float)p.Get("ao", .6);
    const float rough0 = (float)p.Get("rough0", .55), rough1 = (float)p.Get("rough1", 1.0), toneShift = (float)p.Get("tone_shift", 0.);
    const float macro = (float)p.Get("macro", 0.), macroSize = (float)p.Get("macro_size", 110.);
    const size_t N = (size_t)W * H;
    const std::string setId = lib.Resolve(p.Str("set", "rock030"), (uint32_t)seed, region);
    if (setId.empty()) {
        Mat bad;
        bad.valid = false;
        return bad;
    }
    auto Sf = lib.Get(setId, std::max(16, std::max(W, H) / k));
    const int px = Sf->px;
    int r0 = 0, r1 = px;
    if (planks) {
        PlankRows(*Sf, planks, (uint32_t)seed, r0, r1);
    }
    const int sh = r1 - r0;
    auto crop = [&](const Field& f, int c) {
        return Field(f.begin() + (size_t)r0 * px * c, f.begin() + (size_t)r1 * px * c);
    };
    // gather the five maps as interleaved fields, rotate, tile
    Field rgbS = crop(Sf->rgb, 3), dispS = crop(Sf->disp, 1), roughS = crop(Sf->rough, 1), aoS = crop(Sf->ao, 1);
    Field nS((size_t)sh * px * 2);
    for (size_t i = 0; i < (size_t)sh * px; ++i) {
        nS[i * 2] = Sf->nx[(size_t)r0 * px + i];
        nS[i * 2 + 1] = Sf->ny[(size_t)r0 * px + i];
    }
    int w1 = px, h1 = sh, w2 = px, h2 = sh, w3 = px, h3 = sh, w4 = px, h4 = sh, w5 = px, h5 = sh;
    rgbS = Rot90(rgbS, w1, h1, 3, rot);
    dispS = Rot90(dispS, w2, h2, 1, rot);
    roughS = Rot90(roughS, w3, h3, 1, rot);
    aoS = Rot90(aoS, w4, h4, 1, rot);
    nS = Rot90(nS, w5, h5, 2, rot);
    for (int q = 0; q < ((rot % 4) + 4) % 4; ++q) { // the vectors rotate with the image: (nx, ny) -> (-ny, nx)
        for (size_t i = 0; i < nS.size() / 2; ++i) {
            const float nx = nS[i * 2], ny = nS[i * 2 + 1];
            nS[i * 2] = -ny;
            nS[i * 2 + 1] = nx;
        }
    }
    Field rgb = TileTo(rgbS, w1, h1, W, H, k, 3), disp = TileTo(dispS, w2, h2, W, H, k, 1), rg = TileTo(roughS, w3, h3, W, H, k, 1),
          aom = TileTo(aoS, w4, h4, W, H, k, 1), nxy = TileTo(nS, w5, h5, W, H, k, 2);

    Field y(N);
    for (size_t i = 0; i < N; ++i) {
        y[i] = rgb[i * 3] * .299f + rgb[i * 3 + 1] * .587f + rgb[i * 3 + 2] * .114f;
    }
    Field low = Blur(y, W, H, 40 * s);
    Field yy(N);
    for (size_t i = 0; i < N; ++i) {
        yy[i] = low[i] + (y[i] - low[i]) * hf; // controlled detail frequency
    }
    const float ym = (float)Mean(yy), ysd = (float)StdDev(yy) + 1e-6f;
    Field mac;
    if (macro != 0) {
        mac = Fbm(W, H, seed + 5, macroSize * s, 3);
    }
    Field bl = Blur(disp, W, H, 3 * s);
    Field cav(N);
    for (size_t i = 0; i < N; ++i) {
        cav[i] = bl[i] - disp[i];
    }
    const float csd = (float)StdDev(cav) + 1e-6f;
    for (float& v : cav) {
        v /= csd;
    }
    const float aoMean = (float)Mean(aom), dispMean = (float)Mean(disp);
    Mat out;
    out.w = W;
    out.h = H;
    out.alb.resize(N * 3);
    out.height.resize(N);
    out.rough.resize(N);
    Field nrm(N * 2);
    for (size_t i = 0; i < N; ++i) {
        float t = .5f + .47f * std::tanh((yy[i] - ym) / ysd * .55f * contrast);
        t = tr0 + t * (tr1 - tr0);
        if (macro != 0) {
            t += mac[i] * macro;
        }
        t = Clamp01(t + toneShift);
        t = t - grime * .06f * Smooth(cav[i], .3f, 2.5f) + wear * .05f * Smooth(-cav[i], .8f, 2.5f) + (aom[i] - aoMean) * aoK * .15f;
        float rgbp[3];
        PalAt(pal, t, rgbp);
        for (int c = 0; c < 3; ++c) {
            out.alb[i * 3 + c] = Clamp01(rgbp[c] + (rgb[i * 3 + c] - y[i]) * chroma);
        }
        out.height[i] = (disp[i] - dispMean) * relief * s;
        nrm[i * 2] = nxy[i * 2] * nstrength;
        nrm[i * 2 + 1] = nxy[i * 2 + 1] * nstrength;
        out.rough[i] = std::clamp(rg[i] * (rough1 - rough0) + rough0 - wear * .08f * Smooth(-cav[i], .8f, 2.5f), .2f, 1.f);
    }
    if (moss > 0) {
        auto MS = lib.Get(lib.Resolve(p.Str("moss_set", "moss002"), (uint32_t)seed, region), W);
        Field mcav(N);
        for (size_t i = 0; i < N; ++i) {
            mcav[i] = Smooth(cav[i], .2f, 2.0f);
        }
        MossLayer L = MakeMoss(W, H, seed, moss, *MS, s, mcav, nullptr);
        for (size_t i = 0; i < N; ++i) {
            const float mean = (L.col[i * 3] + L.col[i * 3 + 1] + L.col[i * 3 + 2]) / 3.f;
            const float dim = .55f + .45f * (mean > 0 ? 1.f : 0.f);
            for (int c = 0; c < 3; ++c) {
                out.alb[i * 3 + c] = Mix(out.alb[i * 3 + c], Clamp01(L.col[i * 3 + c] * dim), L.m[i] * .75f);
            }
            out.height[i] += L.m[i] * (L.h[i] - .5f) * 14 * s;
            nrm[i * 2] *= 1 - L.m[i] * .5f;
            nrm[i * 2 + 1] *= 1 - L.m[i] * .5f;
        }
    }
    FinishNormal(nrm);
    out.normal = std::move(nrm);
    out.relief = .45f;
    out.nstrength = nstrength;
    return out;
}

} // namespace

bool Generate(const std::string& gen, Library& lib, const Palette& pal, const Params& p, int w, int h, uint32_t seed, const std::string& region, Mat& out) {
    if (w < 16 || h < 16 || w > 4096 || h > 4096) {
        return false;
    }
    if (gen == "masonry") {
        out = Masonry(lib, pal, p, w, h, seed, region);
    } else if (gen == "direct") {
        out = Direct(lib, pal, p, w, h, seed, region);
    } else {
        return false;
    }
    return out.valid;
}

namespace {
float CircAc(const std::vector<float>& p, double lag) {
    const int n = (int)p.size();
    double mean = 0;
    for (float v : p) mean += v;
    mean /= n;
    const int i0 = (int)std::floor(lag);
    const double f = lag - i0;
    double num = 0, den = 1e-9;
    for (int i = 0; i < n; ++i) {
        const double a = p[i] - mean, b = (1 - f) * (p[(i + i0) % n] - mean) + f * (p[(i + i0 + 1) % n] - mean);
        num += a * b;
        den += a * a;
    }
    return (float)(num / den);
}
// Number of repeats in a periodic profile: the largest R (smallest period) whose autocorrelation is within 85% of the best.
int CountRepeats(const std::vector<float>& p, float& score) {
    const int n = (int)p.size();
    std::vector<std::pair<int, float>> s;
    float best = 0;
    for (int R = 2; R <= 16 && n / (double)R >= 3; ++R) {
        const float v = CircAc(p, n / (double)R);
        s.push_back({ R, v });
        best = std::max(best, v);
    }
    score = std::max(best, 0.f);
    int pick = 1;
    for (const auto& [R, v] : s) {
        if (v >= .85f * best && v > .15f) pick = R;
    }
    return pick;
}
} // namespace

Layout MeasureLayout(const std::vector<float>& Y, int w, int h) {
    Layout L;
    std::vector<float> er(h, 0.f);
    for (int y = 0; y < h; ++y) {
        double a = 0;
        for (int x = 0; x < w; ++x) a += std::fabs(Y[(size_t)y * w + x] - Y[(size_t)((y + h - 1) % h) * w + x]);
        er[y] = (float)(a / w);
    }
    L.rows = CountRepeats(er, L.rowScore);
    // columns inside the first course only: staggered joints of the other courses would double the frequency
    const int y1 = std::max(3, h / L.rows - 1);
    std::vector<float> ec(w, 0.f);
    for (int x = 0; x < w; ++x) {
        double a = 0;
        for (int y = 1; y < std::min(y1, h); ++y) a += std::fabs(Y[(size_t)y * w + x] - Y[(size_t)y * w + (x + w - 1) % w]);
        ec[x] = (float)(a / std::max(1, std::min(y1, h) - 1));
    }
    L.cols = CountRepeats(ec, L.colScore);
    return L;
}

Encoded Encode(const Mat& m) {
    Encoded e;
    e.w = m.w;
    e.h = m.h;
    const size_t N = (size_t)m.w * m.h;
    e.rgba.resize(N * 4);
    for (size_t i = 0; i < N; ++i) {
        for (int c = 0; c < 3; ++c) {
            e.rgba[i * 4 + c] = (uint8_t)std::nearbyint(Clamp01(m.alb[i * 3 + c]) * 255.f);
        }
        e.rgba[i * 4 + 3] = 255;
    }
    // roughness (R) + relief (G) like materials.py: relief = band-passed height normalised by its robust 97th percentile
    Field bl = Blur(m.height, m.w, m.h, 14.f * m.w / 1024.f);
    Field d(N);
    for (size_t i = 0; i < N; ++i) {
        d[i] = std::fabs(m.height[i] - bl[i]);
    }
    Field sorted = d;
    const size_t k = (size_t)std::min<double>(N - 1, std::floor(.97 * (N - 1)));
    std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
    const float norm = std::max(sorted[k], 1e-4f);
    // maps at half resolution (2x2 box), as the HD-pack generator does
    e.mw = std::max(1, m.w / 2);
    e.mh = std::max(1, m.h / 2);
    e.normal.resize((size_t)e.mw * e.mh * 2);
    e.rough.resize((size_t)e.mw * e.mh * 2);
    for (int y = 0; y < e.mh; ++y) {
        for (int x = 0; x < e.mw; ++x) {
            double nx = 0, ny = 0, rr = 0, gg = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const size_t i = (size_t)std::min(y * 2 + dy, m.h - 1) * m.w + std::min(x * 2 + dx, m.w - 1);
                    if (!m.normal.empty()) {
                        nx += m.normal[i * 2];
                        ny += m.normal[i * 2 + 1];
                    }
                    rr += m.rough[i];
                    gg += .5f + .5f * m.relief * std::clamp((m.height[i] - bl[i]) / norm, -1.f, 1.f);
                }
            }
            const size_t o = (size_t)y * e.mw + x;
            e.normal[o * 2] = (uint8_t)std::nearbyint(Clamp01((float)(nx / 4) * .5f + .5f) * 255.f);
            e.normal[o * 2 + 1] = (uint8_t)std::nearbyint(Clamp01((float)(ny / 4) * .5f + .5f) * 255.f);
            e.rough[o * 2] = (uint8_t)std::nearbyint(Clamp01((float)(rr / 4)) * 255.f);
            e.rough[o * 2 + 1] = (uint8_t)std::nearbyint(Clamp01((float)(gg / 4)) * 255.f);
        }
    }
    return e;
}

} // namespace NewDawnSynth
