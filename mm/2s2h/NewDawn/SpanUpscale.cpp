// See SpanUpscale.h. Network (from the ONNX export of 4x-PBRify_UpscalerSPANV4):
//   f0 = conv_1(x); per block b = 1..6: o3 = c3(silu(c2(silu(c1(in))))), out = (o3 + in) * (sigmoid(o3) - 0.5)
//   f2 = conv_2(out6); y = upsampler(conv_cat(concat(f0, f2, out1, silu(c1 of block 6)))); result = pixel shuffle 4x (CRD)
#include "SpanUpscale.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace NewDawnMm3d {
namespace {

constexpr int kC = 48;
constexpr int kPad = 8;

struct Spec {
    int cin, cout, k;
};
// Layer order of the weight file (export_span.py ORDER).
std::vector<Spec> Layers() {
    std::vector<Spec> l;
    l.push_back({ 3, kC, 3 });
    for (int i = 0; i < 18; ++i) {
        l.push_back({ kC, kC, 3 });
    }
    l.push_back({ kC, kC, 3 });     // conv_2
    l.push_back({ 4 * kC, kC, 1 }); // conv_cat
    l.push_back({ kC, kC, 3 });     // upsampler.0
    return l;
}

using Tensor = std::vector<float>; // H x W x C, channels last

// Direct convolution, zero padded; the inner loop runs over the 48 output channels so it vectorises.
void RunConv(const SpanUpscaler*, const float* w, const float* bias, int cin, int cout, int k, const Tensor& in, Tensor& out,
             int H, int W) {
    out.assign((size_t)H * W * cout, 0.0f);
    const int r = k / 2;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float acc[kC];
            for (int o = 0; o < cout; ++o) {
                acc[o] = bias[o];
            }
            for (int ky = 0; ky < k; ++ky) {
                const int iy = y + ky - r;
                if (iy < 0 || iy >= H) {
                    continue;
                }
                for (int kx = 0; kx < k; ++kx) {
                    const int ix = x + kx - r;
                    if (ix < 0 || ix >= W) {
                        continue;
                    }
                    const float* src = &in[((size_t)iy * W + ix) * cin];
                    const float* wt = w + (size_t)(ky * k + kx) * cin * cout;
                    for (int ci = 0; ci < cin; ++ci) {
                        const float v = src[ci];
                        const float* wr = wt + (size_t)ci * cout;
                        for (int o = 0; o < cout; ++o) {
                            acc[o] += v * wr[o];
                        }
                    }
                }
            }
            std::memcpy(&out[((size_t)y * W + x) * cout], acc, sizeof(float) * cout);
        }
    }
}

inline float Sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

void Silu(Tensor& t) {
    for (float& v : t) {
        v = v * Sigmoid(v);
    }
}

} // namespace

bool SpanUpscaler::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 8 || std::memcmp(data.data(), "SPAN", 4) != 0) {
        return false;
    }
    uint32_t version;
    std::memcpy(&version, &data[4], 4);
    if (version != 1) {
        return false;
    }
    uint64_t h = 14695981039346656037ULL;
    for (char c : data) {
        h = (h ^ (uint8_t)c) * 1099511628211ULL;
    }
    mFingerprint = h;
    size_t pos = 8;
    mConvs.clear();
    for (const Spec& s : Layers()) {
        const size_t wn = (size_t)s.cout * s.cin * s.k * s.k, bn = s.cout;
        if (pos + (wn + bn) * 4 > data.size()) {
            mConvs.clear();
            return false;
        }
        const float* src = reinterpret_cast<const float*>(&data[pos]);
        Conv c;
        c.cin = s.cin, c.cout = s.cout, c.k = s.k;
        c.w.resize(wn);
        // file: [out][in][ky][kx]  ->  [ky*k+kx][in][out]
        for (int o = 0; o < s.cout; ++o) {
            for (int i = 0; i < s.cin; ++i) {
                for (int t = 0; t < s.k * s.k; ++t) {
                    c.w[((size_t)t * s.cin + i) * s.cout + o] = src[((size_t)o * s.cin + i) * s.k * s.k + t];
                }
            }
        }
        c.b.assign(src + wn, src + wn + bn);
        mConvs.push_back(std::move(c));
        pos += (wn + bn) * 4;
    }
    return pos == data.size();
}

bool SpanUpscaler::Upscale4x(const Mm3d::Image& in, Mm3d::Image& out) const {
    if (mConvs.size() != 22 || in.width <= 0 || in.height <= 0 || in.width > 512 || in.height > 512) {
        return false;
    }
    const int W = in.width + 2 * kPad, H = in.height + 2 * kPad;
    Tensor x((size_t)H * W * 3);
    for (int y = 0; y < H; ++y) {
        for (int xx = 0; xx < W; ++xx) {
            const int sy = std::clamp(y - kPad, 0, in.height - 1), sx = std::clamp(xx - kPad, 0, in.width - 1);
            for (int c = 0; c < 3; ++c) {
                x[((size_t)y * W + xx) * 3 + c] = in.rgba[((size_t)sy * in.width + sx) * 4 + c] / 255.0f;
            }
        }
    }
    auto conv = [&](int idx, const Tensor& src, Tensor& dst) {
        const Conv& c = mConvs[idx];
        RunConv(this, c.w.data(), c.b.data(), c.cin, c.cout, c.k, src, dst, H, W);
    };
    Tensor f0, cur, o1, o2, o3, out1, a1last, tmp;
    conv(0, x, f0);
    cur = f0;
    for (int b = 0; b < 6; ++b) {
        conv(1 + b * 3, cur, o1);
        Silu(o1);
        if (b == 5) {
            a1last = o1;
        }
        conv(2 + b * 3, o1, o2);
        Silu(o2);
        conv(3 + b * 3, o2, o3);
        for (size_t i = 0; i < o3.size(); ++i) {
            const float att = Sigmoid(o3[i]) - 0.5f;
            o3[i] = (o3[i] + cur[i]) * att;
        }
        cur.swap(o3);
        if (b == 0) {
            out1 = cur;
        }
    }
    Tensor f2;
    conv(19, cur, f2);
    Tensor cat((size_t)H * W * 4 * kC);
    for (size_t p = 0; p < (size_t)H * W; ++p) {
        std::memcpy(&cat[p * 4 * kC], &f0[p * kC], sizeof(float) * kC);
        std::memcpy(&cat[p * 4 * kC + kC], &f2[p * kC], sizeof(float) * kC);
        std::memcpy(&cat[p * 4 * kC + 2 * kC], &out1[p * kC], sizeof(float) * kC);
        std::memcpy(&cat[p * 4 * kC + 3 * kC], &a1last[p * kC], sizeof(float) * kC);
    }
    conv(20, cat, tmp);
    Tensor y;
    conv(21, tmp, y);

    out.width = in.width * 4;
    out.height = in.height * 4;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    for (int hy = 0; hy < in.height; ++hy) {
        for (int hx = 0; hx < in.width; ++hx) {
            const float* p = &y[((size_t)(hy + kPad) * W + (hx + kPad)) * kC];
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    uint8_t* d = &out.rgba[((size_t)(hy * 4 + i) * out.width + (hx * 4 + j)) * 4];
                    for (int c = 0; c < 3; ++c) { // DepthToSpace, CRD: channel = c * 16 + i * 4 + j
                        d[c] = (uint8_t)std::lround(std::clamp(p[c * 16 + i * 4 + j], 0.0f, 1.0f) * 255.0f);
                    }
                }
            }
        }
    }
    // alpha: bilinear at pixel centres
    for (int oy = 0; oy < out.height; ++oy) {
        const float fy = std::clamp((oy + 0.5f) / 4.0f - 0.5f, 0.0f, (float)in.height - 1);
        const int y0 = (int)fy, y1 = std::min(y0 + 1, in.height - 1);
        const float ty = fy - y0;
        for (int ox = 0; ox < out.width; ++ox) {
            const float fx = std::clamp((ox + 0.5f) / 4.0f - 0.5f, 0.0f, (float)in.width - 1);
            const int x0 = (int)fx, x1 = std::min(x0 + 1, in.width - 1);
            const float tx = fx - x0;
            auto a = [&](int yy, int xx) { return (float)in.rgba[((size_t)yy * in.width + xx) * 4 + 3]; };
            const float v = (a(y0, x0) * (1 - tx) + a(y0, x1) * tx) * (1 - ty) + (a(y1, x0) * (1 - tx) + a(y1, x1) * tx) * ty;
            out.rgba[((size_t)oy * out.width + ox) * 4 + 3] = (uint8_t)std::lround(v);
        }
    }
    return true;
}

} // namespace NewDawnMm3d
