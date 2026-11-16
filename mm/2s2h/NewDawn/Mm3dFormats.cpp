// See Mm3dFormats.h. Port of tools/newdawn/mm3d/{binary,cmb,texture}.py.
#include "Mm3dFormats.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace Mm3d {
namespace {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Reader {
    const uint8_t* d;
    size_t n;
    Reader(const uint8_t* data, size_t size) : d(data), n(size) {
    }
    void Need(size_t off, size_t len) const {
        if (off > n || len > n - off) {
            throw FormatError("out of bounds read");
        }
    }
    template <typename T> T Get(size_t off) const {
        Need(off, sizeof(T));
        T v;
        std::memcpy(&v, d + off, sizeof(T));
        return v;
    }
    uint8_t R8(size_t o) const {
        return Get<uint8_t>(o);
    }
    uint16_t R16(size_t o) const {
        return Get<uint16_t>(o);
    }
    int16_t Ri16(size_t o) const {
        return Get<int16_t>(o);
    }
    uint32_t R32(size_t o) const {
        return Get<uint32_t>(o);
    }
    float Rf(size_t o) const {
        return Get<float>(o);
    }
    void Magic(size_t off, const char* m) const {
        Need(off, 4);
        if (std::memcmp(d + off, m, 4) != 0) {
            throw FormatError(std::string("expected ") + m);
        }
    }
    std::string Str(size_t off, size_t limit) const {
        std::string s;
        for (size_t i = 0; i < limit && off + i < n && d[off + i]; ++i) {
            s.push_back((char)d[off + i]);
        }
        return s;
    }
};

// ---------------------------------------------------------------- textures
enum class Fmt { ETC1, ETC1A4, RGB8, RGBA8, RGBA4444, RGBA5551, RGB565, A8, A4, L8, L4, LA8, LA4, HILO8 };

bool FormatOf(uint32_t gl, Fmt& f, std::string& name) {
    struct E {
        uint32_t gl;
        Fmt f;
        const char* n;
    };
    static const E table[] = { { 0x675A, Fmt::ETC1, "ETC1" },         { 0x675B, Fmt::ETC1A4, "ETC1A4" },
                               { 0x14016754, Fmt::RGB8, "RGB8" },     { 0x14016752, Fmt::RGBA8, "RGBA8" },
                               { 0x80336752, Fmt::RGBA4444, "RGBA4444" }, { 0x80346752, Fmt::RGBA5551, "RGBA5551" },
                               { 0x83636754, Fmt::RGB565, "RGB565" }, { 0x14016756, Fmt::A8, "A8" },
                               { 0x67616756, Fmt::A4, "A4" },         { 0x14016757, Fmt::L8, "L8" },
                               { 0x67616757, Fmt::L4, "L4" },         { 0x14016758, Fmt::LA8, "LA8" },
                               { 0x67606758, Fmt::LA4, "LA4" },       { 0x14016759, Fmt::HILO8, "HILO8" } };
    for (const auto& e : table) {
        if (e.gl == gl) {
            f = e.f;
            name = e.n;
            return true;
        }
    }
    return false;
}

int Bpp(Fmt f) {
    switch (f) {
        case Fmt::RGBA8: return 32;
        case Fmt::RGB8: return 24;
        case Fmt::RGBA4444: case Fmt::RGBA5551: case Fmt::RGB565: case Fmt::HILO8: case Fmt::LA8: return 16;
        case Fmt::A8: case Fmt::L8: case Fmt::LA4: return 8;
        case Fmt::L4: case Fmt::A4: case Fmt::ETC1: return 4;
        case Fmt::ETC1A4: return 8;
    }
    return 8;
}

size_t ByteSize(Fmt f, int w, int h) { // mips smaller than 8x8 still occupy a whole PICA tile
    return (size_t)((w + 7) / 8) * (size_t)((h + 7) / 8) * 64 * Bpp(f) / 8;
}

const int kEtcMod[8][4] = { { 2, 8, -2, -8 },    { 5, 17, -5, -17 },  { 9, 29, -9, -29 },    { 13, 42, -13, -42 },
                            { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 } };

// One 4x4 ETC1 block; out[y][x][rgb]. PICA stores the pixel selectors column-major.
void EtcBlock(uint32_t control, uint32_t selectors, uint8_t out[4][4][3]) {
    int base[2][3];
    if (control & 2) {
        const int shifts[3] = { 27, 19, 11 };
        for (int c = 0; c < 3; ++c) {
            int b = (control >> shifts[c]) & 31;
            int delta = (control >> (shifts[c] - 3)) & 7;
            if (delta & 4) {
                delta -= 8;
            }
            const int other = b + delta;
            if (other < 0 || other > 31) {
                throw FormatError("ETC1 invalid differential colour");
            }
            base[0][c] = (b << 3) | (b >> 2);
            base[1][c] = (other << 3) | (other >> 2);
        }
    } else {
        const int shifts[3] = { 28, 20, 12 };
        for (int c = 0; c < 3; ++c) {
            base[0][c] = ((control >> shifts[c]) & 15) * 17;
            base[1][c] = ((control >> (shifts[c] - 4)) & 15) * 17;
        }
    }
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int k = x * 4 + y;
            const int sel = ((selectors >> k) & 1) | (((selectors >> (k + 16)) & 1) << 1);
            const int half = ((control & 1) ? y : x) / 2;
            const int table = half == 0 ? (control >> 5) & 7 : (control >> 2) & 7;
            for (int c = 0; c < 3; ++c) {
                out[y][x][c] = (uint8_t)std::clamp(base[half][c] + kEtcMod[table][sel], 0, 255);
            }
        }
    }
}

Image DecodeTexture(Fmt fmt, int width, int height, const uint8_t* data, size_t avail) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        throw FormatError("invalid texture dimensions");
    }
    const size_t need = ByteSize(fmt, width, height);
    if (need > avail) {
        throw FormatError("texture data out of bounds");
    }
    const int pw = (width + 7) / 8 * 8, ph = (height + 7) / 8 * 8;
    std::vector<uint8_t> out((size_t)pw * ph * 4, 255);
    auto put = [&](int x, int y, const uint8_t* rgba) { std::memcpy(&out[((size_t)y * pw + x) * 4], rgba, 4); };
    if (fmt == Fmt::ETC1 || fmt == Fmt::ETC1A4) {
        size_t off = 0;
        for (int yy = 0; yy < ph; yy += 8) {
            for (int xx = 0; xx < pw; xx += 8) {
                for (int q = 0; q < 4; ++q) {
                    const int by = (q >> 1) * 4, bx = (q & 1) * 4;
                    uint64_t alpha = 0;
                    if (fmt == Fmt::ETC1A4) {
                        std::memcpy(&alpha, data + off, 8);
                        off += 8;
                    }
                    uint32_t selectors, control;
                    std::memcpy(&selectors, data + off, 4);
                    std::memcpy(&control, data + off + 4, 4);
                    off += 8;
                    uint8_t blk[4][4][3];
                    EtcBlock(control, selectors, blk);
                    for (int y = 0; y < 4; ++y) {
                        for (int x = 0; x < 4; ++x) {
                            uint8_t px[4] = { blk[y][x][0], blk[y][x][1], blk[y][x][2], 255 };
                            if (fmt == Fmt::ETC1A4) {
                                px[3] = (uint8_t)(((alpha >> (4 * (4 * x + y))) & 15) * 17);
                            }
                            put(xx + bx + x, yy + by + y, px);
                        }
                    }
                }
            }
        }
    } else {
        const size_t count = (size_t)pw * ph;
        std::vector<uint8_t> values(count * 4, 255);
        auto rgba = [&](size_t i) { return &values[i * 4]; };
        for (size_t i = 0; i < count; ++i) {
            uint8_t* v = rgba(i);
            switch (fmt) {
                case Fmt::RGBA8:
                    v[0] = data[i * 4 + 3], v[1] = data[i * 4 + 2], v[2] = data[i * 4 + 1], v[3] = data[i * 4];
                    break;
                case Fmt::RGB8:
                    v[0] = data[i * 3 + 2], v[1] = data[i * 3 + 1], v[2] = data[i * 3];
                    break;
                case Fmt::RGBA4444: case Fmt::RGBA5551: case Fmt::RGB565: {
                    uint16_t w;
                    std::memcpy(&w, data + i * 2, 2);
                    const int bits4[4] = { 4, 4, 4, 4 }, bits5[4] = { 5, 5, 5, 1 }, bits6[4] = { 5, 6, 5, 0 };
                    const int* bits = fmt == Fmt::RGBA4444 ? bits4 : (fmt == Fmt::RGBA5551 ? bits5 : bits6);
                    const int channels = fmt == Fmt::RGB565 ? 3 : 4;
                    int shift = 16;
                    for (int c = 0; c < channels; ++c) {
                        const int b = bits[c];
                        shift -= b;
                        const int raw = (w >> shift) & ((1 << b) - 1);
                        v[c] = (uint8_t)(b == 5 ? (raw << 3) | (raw >> 2) : b == 6 ? (raw << 2) | (raw >> 4)
                                                  : raw * (b == 4 ? 17 : 255));
                    }
                    break;
                }
                case Fmt::LA8:
                    v[0] = v[1] = v[2] = data[i * 2 + 1];
                    v[3] = data[i * 2];
                    break;
                case Fmt::HILO8:
                    v[0] = data[i * 2 + 1], v[1] = data[i * 2];
                    break;
                default: { // 8 and 4 bit luminance / alpha
                    int val;
                    if (fmt == Fmt::L4 || fmt == Fmt::A4) {
                        val = ((i & 1) ? data[i / 2] >> 4 : data[i / 2] & 15) * 17;
                    } else {
                        val = data[i];
                    }
                    if (fmt == Fmt::L8 || fmt == Fmt::L4 || fmt == Fmt::LA4) {
                        const int lum = fmt == Fmt::LA4 ? (val >> 4) * 17 : val;
                        v[0] = v[1] = v[2] = (uint8_t)lum;
                    }
                    if (fmt == Fmt::A8 || fmt == Fmt::A4) {
                        v[3] = (uint8_t)val;
                    } else if (fmt == Fmt::LA4) {
                        v[3] = (uint8_t)((val & 15) * 17);
                    } else {
                        v[3] = (uint8_t)val; // PICA luminance is replicated to alpha too
                    }
                    break;
                }
            }
        }
        // 8x8 Morton tiles
        for (size_t i = 0; i < count; ++i) {
            const int local = (int)(i % 64);
            const int x = (local & 1) | ((local >> 1) & 2) | ((local >> 2) & 4);
            const int y = ((local >> 1) & 1) | ((local >> 2) & 2) | ((local >> 3) & 4);
            const int tile = (int)(i / 64);
            put((tile % (pw / 8)) * 8 + x, (tile / (pw / 8)) * 8 + y, rgba(i));
        }
    }
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize((size_t)width * height * 4);
    for (int y = 0; y < height; ++y) {
        std::memcpy(&img.rgba[(size_t)y * width * 4], &out[(size_t)y * pw * 4], (size_t)width * 4);
    }
    return img;
}

// ---------------------------------------------------------------- matrices
void Identity(double m[16]) {
    std::fill(m, m + 16, 0.0);
    m[0] = m[5] = m[10] = m[15] = 1.0;
}
void Mul(const double a[16], const double b[16], double out[16]) {
    double t[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double s = 0;
            for (int k = 0; k < 4; ++k) {
                s += a[r * 4 + k] * b[k * 4 + c];
            }
            t[r * 4 + c] = s;
        }
    }
    std::memcpy(out, t, sizeof(t));
}

} // namespace

// ---------------------------------------------------------------- LzS, GAR
bool Inflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error) {
    if (in.size() < 4 || std::memcmp(in.data(), "LzS\x01", 4) != 0) {
        out = in;
        return true;
    }
    if (in.size() < 16) {
        error = "short LzS header";
        return false;
    }
    uint32_t size, packed;
    std::memcpy(&size, &in[8], 4);
    std::memcpy(&packed, &in[12], 4);
    if (size > 512u * 1024 * 1024 || packed != in.size() - 16) {
        error = "invalid LzS sizes";
        return false;
    }
    std::vector<uint8_t> ring(4096, 0);
    size_t cursor = 4078, src = 16;
    out.clear();
    out.reserve(size);
    auto emit = [&](uint8_t v) {
        out.push_back(v);
        ring[cursor] = v;
        cursor = (cursor + 1) & 4095;
    };
    while (out.size() < size) {
        if (src >= in.size()) {
            error = "LzS truncated";
            return false;
        }
        const uint8_t flags = in[src++];
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if (flags & (1 << bit)) {
                if (src >= in.size()) {
                    error = "LzS truncated";
                    return false;
                }
                emit(in[src++]);
            } else {
                if (src + 2 > in.size()) {
                    error = "LzS truncated";
                    return false;
                }
                const uint8_t a = in[src], b = in[src + 1];
                src += 2;
                const size_t pos = a | ((b & 240) << 4);
                const size_t count = (b & 15) + 3;
                if (out.size() + count > size) {
                    error = "LzS match exceeds declared size";
                    return false;
                }
                for (size_t j = 0; j < count; ++j) {
                    emit(ring[(pos + j) & 4095]);
                }
            }
        }
    }
    return true;
}

bool ParseGar(const std::vector<uint8_t>& fileBytes, Gar& out, std::string& error) {
    try {
        std::vector<uint8_t> data;
        if (!Inflate(fileBytes, data, error)) {
            return false;
        }
        Reader r(data.data(), data.size());
        r.Magic(0, "GAR\x02");
        if (r.R32(4) != data.size()) {
            throw FormatError("GAR size mismatch");
        }
        const uint32_t count = r.R16(10), meta = r.R32(16), offsets = r.R32(20);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t size = r.R32(meta + i * 12), name = r.R32(meta + i * 12 + 8);
            const uint32_t off = r.R32(offsets + i * 4);
            r.Need(off, size);
            out.files[r.Str(name, 256)] = std::vector<uint8_t>(data.begin() + off, data.begin() + off + size);
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// ---------------------------------------------------------------- CMB
namespace {

void BindMatrices(std::vector<Bone>& bones) {
    std::map<int, size_t> byId;
    for (size_t i = 0; i < bones.size(); ++i) {
        byId[bones[i].id] = i;
    }
    std::vector<int> state(bones.size(), 0); // 0 todo, 1 busy, 2 done
    std::function<void(size_t)> solve = [&](size_t i) {
        if (state[i] == 2) {
            return;
        }
        if (state[i] == 1) {
            throw FormatError("cyclic bone hierarchy");
        }
        state[i] = 1;
        Bone& b = bones[i];
        const double cx = std::cos(b.rotation[0]), cy = std::cos(b.rotation[1]), cz = std::cos(b.rotation[2]);
        const double sx = std::sin(b.rotation[0]), sy = std::sin(b.rotation[1]), sz = std::sin(b.rotation[2]);
        const double rx[16] = { 1, 0, 0, 0, 0, cx, -sx, 0, 0, sx, cx, 0, 0, 0, 0, 1 };
        const double ry[16] = { cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0, 0, 0, 0, 1 };
        const double rz[16] = { cz, -sz, 0, 0, sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        double m[16], t[16];
        Mul(rz, ry, t);
        Mul(t, rx, m);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                m[r * 4 + c] *= b.scale[c];
            }
            m[r * 4 + 3] = b.translation[r];
        }
        if (b.parent >= 0) {
            auto it = byId.find(b.parent);
            if (it == byId.end()) {
                throw FormatError("missing parent bone");
            }
            solve(it->second);
            Mul(bones[it->second].bind, m, m);
        }
        std::memcpy(b.bind, m, sizeof(m));
        state[i] = 2;
    };
    for (size_t i = 0; i < bones.size(); ++i) {
        solve(i);
    }
}

void Inverse3(const double* m, double out[9]) { // m is a 4x4 row-major matrix, upper 3x3 used
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double inv = std::fabs(det) < 1e-12 ? 0.0 : 1.0 / det;
    out[0] = (e * i - f * h) * inv, out[1] = (c * h - b * i) * inv, out[2] = (b * f - c * e) * inv;
    out[3] = (f * g - d * i) * inv, out[4] = (a * i - c * g) * inv, out[5] = (c * d - a * f) * inv;
    out[6] = (d * h - e * g) * inv, out[7] = (b * g - a * h) * inv, out[8] = (a * e - b * d) * inv;
}

struct Attr {
    uint32_t start;
    float scale;
    uint16_t type, mode;
    float constant[4];
};

size_t TypeSize(uint16_t t) {
    switch (t) {
        case 0x1400: case 0x1401: return 1;
        case 0x1402: case 0x1403: return 2;
        case 0x1404: case 0x1405: case 0x1406: return 4;
    }
    throw FormatError("invalid vertex attribute type");
}

float ReadComponent(const Reader& s, size_t off, uint16_t type) {
    switch (type) {
        case 0x1400: return (float)(int8_t)s.R8(off);
        case 0x1401: return (float)s.R8(off);
        case 0x1402: return (float)s.Ri16(off);
        case 0x1403: return (float)s.R16(off);
        case 0x1404: return (float)(int32_t)s.R32(off);
        case 0x1405: return (float)s.R32(off);
        default: return s.Rf(off);
    }
}

} // namespace

bool ParseCmb(const std::vector<uint8_t>& raw, Model& out, std::string& error) {
    try {
        Reader top(raw.data(), raw.size());
        top.Magic(0, "cmb ");
        if (top.R32(8) != 10) {
            throw FormatError("only CMB v10 is supported");
        }
        const uint32_t total = top.R32(4);
        top.Need(0, total);
        Reader r(raw.data(), total);
        const uint32_t sk = r.R32(0x24), ma = r.R32(0x2c), tx = r.R32(0x30), model = r.R32(0x34), va = r.R32(0x3c),
                       ix = r.R32(0x40), td = r.R32(0x44);
        const size_t indexBytes = (size_t)r.R32(0x20) * 2;
        r.Need(ix, indexBytes);
        Reader indexData(raw.data() + ix, indexBytes);
        out.name = r.Str(16, 16);

        // skeleton
        r.Magic(sk, "skl ");
        const uint32_t boneCount = r.R32(sk + 8);
        for (uint32_t i = 0; i < boneCount; ++i) {
            const size_t p = sk + 16 + (size_t)i * 44;
            Bone b;
            b.id = r.R16(p) & 4095;
            b.parent = r.Ri16(p + 2);
            for (int k = 0; k < 3; ++k) {
                b.scale[k] = r.Rf(p + 4 + 4 * k);
                b.rotation[k] = r.Rf(p + 16 + 4 * k);
                b.translation[k] = r.Rf(p + 28 + 4 * k);
            }
            out.bones.push_back(b);
        }
        BindMatrices(out.bones);
        std::map<int, size_t> boneIndex;
        for (size_t i = 0; i < out.bones.size(); ++i) {
            boneIndex[out.bones[i].id] = i;
        }

        // materials
        r.Magic(ma, "mats");
        const uint32_t matCount = r.R32(ma + 8);
        for (uint32_t i = 0; i < matCount; ++i) {
            const size_t b = ma + 12 + (size_t)i * 0x16c;
            r.Need(b, 0x16c);
            Material m;
            m.layer = r.R8(b + 3);
            const uint8_t cullCode = r.R8(b + 4);
            m.cull = cullCode == 1 ? 1 : (cullCode == 2 ? 2 : 0);
            for (int j = 0; j < 3; ++j) {
                const size_t p = b + 16 + j * 24;
                m.texture[j] = r.Ri16(p);
                m.wrapS[j] = r.R16(p + 8);
                m.wrapT[j] = r.R16(p + 10);
            }
            m.uvSource = r.R8(b + 0x58);
            for (int k = 0; k < 5; ++k) {
                m.uvTransform[k] = r.Rf(b + 0x58 + 4 + 4 * k);
            }
            m.alphaTest = r.R8(b + 0x130) != 0;
            m.alphaRef = r.R8(b + 0x131);
            m.blendMode = r.R8(b + 0x138);
            m.alpha = m.blendMode ? Material::Blend : (m.alphaTest ? Material::Cutout : Material::Opaque);
            for (int k = 0; k < 4; ++k) {
                m.diffuse[k] = r.R8(b + 0xa8 + k);
                m.ambient[k] = r.R8(b + 0xa4 + k);
                m.emission[k] = r.R8(b + 0xa0 + k);
            }
            for (int j = 0; j < 6; ++j) {
                for (int k = 0; k < 4; ++k) {
                    m.constants[j][k] = r.R8(b + 0xb4 + 4 * j + k);
                }
            }
            m.fragmentLighting = r.R8(b) != 0;
            m.vertexLighting = r.R8(b + 1) != 0;
            const uint32_t count = r.R32(b + 0x120);
            if (count > 6) {
                throw FormatError("too many material combiner stages");
            }
            m.combinerCount = (int)count;
            const size_t combinerBase = ma + 12 + (size_t)matCount * 0x16c;
            for (uint32_t j = 0; j < count; ++j) {
                const size_t c = combinerBase + (size_t)r.R16(b + 0x124 + 2 * j) * 40;
                for (int k = 0; k < 20; ++k) {
                    m.combiners[j][k] = r.R16(c + 2 * k);
                }
            }
            out.materials.push_back(m);
        }

        // textures (mip level 0 only)
        r.Magic(tx, "tex ");
        const uint32_t texCount = r.R32(tx + 8);
        for (uint32_t i = 0; i < texCount; ++i) {
            const size_t off = tx + 12 + (size_t)i * 36;
            Texture t;
            const uint32_t start = r.R32(off + 16), glfmt = r.R32(off + 12);
            t.width = r.R16(off + 8);
            t.height = r.R16(off + 10);
            t.name = r.Str(off + 20, 16);
            Fmt fmt;
            if (!FormatOf(glfmt, fmt, t.format)) {
                throw FormatError("unknown texture format");
            }
            if (r.R8(off + 7)) { // cube map: not used by characters
                out.textures.push_back(std::move(t));
                continue;
            }
            r.Need(td + start, 0);
            t.image = DecodeTexture(fmt, t.width, t.height, raw.data() + td + start,
                                    total > td + start ? total - (td + start) : 0);
            out.textures.push_back(std::move(t));
        }
        for (const auto& m : out.materials) {
            for (int j = 0; j < 3; ++j) {
                if (m.texture[j] >= (int)out.textures.size()) {
                    throw FormatError("out of range material texture");
                }
            }
        }

        // meshes
        r.Magic(va, "vatr");
        Reader streams[9] = { Reader(nullptr, 0), Reader(nullptr, 0), Reader(nullptr, 0), Reader(nullptr, 0),
                              Reader(nullptr, 0), Reader(nullptr, 0), Reader(nullptr, 0), Reader(nullptr, 0),
                              Reader(nullptr, 0) };
        for (int i = 0; i < 9; ++i) {
            const uint32_t length = r.R32(va + 12 + i * 8), offset = r.R32(va + 16 + i * 8);
            r.Need(va + offset, length);
            streams[i] = Reader(raw.data() + va + offset, length);
        }
        r.Magic(model, "sklm");
        const size_t msh = model + r.R32(model + 8), shp = model + r.R32(model + 12);
        r.Magic(msh, "mshs");
        r.Magic(shp, "shp ");
        const uint32_t sectionCount = r.R32(shp + 8);
        struct Primitive {
            int skinning;
            std::vector<uint16_t> table;
            std::vector<uint32_t> indices;
        };
        struct Section {
            Attr attrs[9];
            int boneDim;
            std::vector<Primitive> prims;
        };
        std::vector<Section> sections(sectionCount);
        for (uint32_t s = 0; s < sectionCount; ++s) {
            const size_t b = shp + r.R16(shp + 16 + s * 2);
            r.Magic(b, "sepd");
            Section& sec = sections[s];
            for (int j = 0; j < 9; ++j) {
                const size_t p = b + 0x24 + j * 28;
                Attr& a = sec.attrs[j];
                a.start = r.R32(p);
                a.scale = r.Rf(p + 4);
                a.type = r.R16(p + 8);
                a.mode = r.R16(p + 10);
                for (int k = 0; k < 4; ++k) {
                    a.constant[k] = r.Rf(p + 12 + 4 * k);
                }
                if (a.mode > 1) {
                    throw FormatError("invalid vertex attribute mode");
                }
                TypeSize(a.type);
            }
            sec.boneDim = r.R16(b + 0x24 + 9 * 28);
            const uint32_t primCount = r.R16(b + 8);
            for (uint32_t j = 0; j < primCount; ++j) {
                const size_t p = b + r.R16(b + 0x24 + 9 * 28 + 4 + j * 2);
                r.Magic(p, "prms");
                if (r.R32(p + 8) != 1) {
                    throw FormatError("multiple prm per prms unsupported");
                }
                Primitive prim;
                prim.skinning = r.R16(p + 12);
                const uint32_t n = r.R16(p + 14);
                for (uint32_t k = 0; k < n; ++k) {
                    prim.table.push_back(r.R16(p + r.R32(p + 16) + k * 2));
                }
                const size_t q = p + r.R32(p + 20);
                r.Magic(q, "prm ");
                const uint16_t type = r.R16(q + 16), count = r.R16(q + 20);
                const size_t start = (size_t)r.R16(q + 22) * 2;
                if ((type != 0x1401 && type != 0x1403) || count % 3) {
                    throw FormatError("non-triangle or unsupported indices");
                }
                for (uint32_t k = 0; k < count; ++k) {
                    prim.indices.push_back(type == 0x1401 ? indexData.R8(start + k) : indexData.R16(start + k * 2));
                }
                sec.prims.push_back(std::move(prim));
            }
        }

        const uint32_t drawCount = r.R32(msh + 8);
        for (uint32_t i = 0; i < drawCount; ++i) {
            const size_t p = msh + 16 + (size_t)i * 12;
            const uint32_t section = r.R16(p), material = r.R8(p + 2);
            if (section >= sectionCount || material >= out.materials.size()) {
                throw FormatError("mesh references missing section/material");
            }
            const Section& sec = sections[section];
            const int uvAttr = 4 + std::clamp(out.materials[material].uvSource, 0, 2);
            for (const Primitive& prim : sec.prims) {
                if (prim.indices.empty()) {
                    continue;
                }
                if (prim.skinning > 2) {
                    throw FormatError("unknown skinning mode");
                }
                Draw d;
                d.material = (int)material;
                d.skinning = prim.skinning;
                std::map<uint32_t, uint32_t> remap;
                std::vector<uint32_t> used;
                for (uint32_t idx : prim.indices) {
                    if (remap.emplace(idx, (uint32_t)used.size()).second) {
                        used.push_back(idx);
                    }
                    d.indices.push_back(remap[idx]);
                }
                d.vertices.resize(used.size());
                auto fetch = [&](int a, int dims, uint32_t idx, float* dst) {
                    const Attr& at = sec.attrs[a];
                    const bool constant = at.mode == 1 || streams[a].n == 0 || (a == 7 && prim.skinning == 0) ||
                                          (a == 8 && prim.skinning != 2);
                    for (int k = 0; k < dims; ++k) {
                        if (constant) {
                            dst[k] = k < 4 ? at.constant[k] : 0.0f;
                        } else {
                            const size_t size = TypeSize(at.type);
                            float v = ReadComponent(streams[a], at.start + ((size_t)idx * dims + k) * size, at.type);
                            dst[k] = at.type != 0x1406 ? v * at.scale : v;
                        }
                    }
                };
                for (size_t v = 0; v < used.size(); ++v) {
                    Vertex& vx = d.vertices[v];
                    fetch(0, 3, used[v], vx.pos);
                    fetch(1, 3, used[v], vx.normal);
                    if (streams[uvAttr].n != 0) {
                        fetch(uvAttr, 2, used[v], vx.uv);
                    }
                    if (!std::isfinite(vx.uv[0]) || !std::isfinite(vx.uv[1])) {
                        vx.uv[0] = vx.uv[1] = 0;
                    }
                    float bi[4] = {}, bw[4] = {};
                    if (sec.boneDim < 1 || sec.boneDim > 4) {
                        throw FormatError("invalid bone dimension");
                    }
                    fetch(7, sec.boneDim, used[v], bi);
                    fetch(8, sec.boneDim, used[v], bw);
                    const auto& table = prim.table;
                    if (prim.skinning != 2 && !table.empty()) {
                        const int slot = prim.skinning == 1 ? (int)bi[0] : 0;
                        if (slot < 0 || slot >= (int)table.size()) {
                            throw FormatError("bone slot outside primitive palette");
                        }
                        auto it = boneIndex.find(table[slot]);
                        if (it == boneIndex.end()) {
                            throw FormatError("palette references a missing bone");
                        }
                        const double* m = out.bones[it->second].bind;
                        double inv[9];
                        Inverse3(m, inv);
                        float np[3], nn[3];
                        for (int r2 = 0; r2 < 3; ++r2) {
                            np[r2] = (float)(m[r2 * 4] * vx.pos[0] + m[r2 * 4 + 1] * vx.pos[1] + m[r2 * 4 + 2] * vx.pos[2] +
                                             m[r2 * 4 + 3]);
                            // row vector times the inverse: the inverse transpose applied to the normal
                            nn[r2] = (float)(inv[r2] * vx.normal[0] + inv[3 + r2] * vx.normal[1] + inv[6 + r2] * vx.normal[2]);
                        }
                        std::memcpy(vx.pos, np, sizeof(np));
                        std::memcpy(vx.normal, nn, sizeof(nn));
                        vx.bone[0] = table[slot];
                        vx.weight[0] = 1.0f;
                    } else if (prim.skinning == 2) {
                        float sum = 0;
                        for (int k = 0; k < sec.boneDim; ++k) {
                            const int slot = (int)bi[k];
                            if (slot >= 0 && slot < (int)table.size()) {
                                vx.bone[k] = table[slot];
                                vx.weight[k] = bw[k];
                                sum += bw[k];
                            }
                        }
                        for (int k = 0; k < 4 && sum > 0; ++k) { // some models store percent weights (sum 100)
                            vx.weight[k] /= sum;
                        }
                    }
                    const float len = std::sqrt(vx.normal[0] * vx.normal[0] + vx.normal[1] * vx.normal[1] +
                                                vx.normal[2] * vx.normal[2]);
                    for (int k = 0; k < 3; ++k) {
                        vx.normal[k] = len > 1e-10f ? vx.normal[k] / len : 0.0f;
                    }
                    if (!std::isfinite(vx.pos[0] + vx.pos[1] + vx.pos[2]) || std::fabs(vx.pos[0]) > 1e6 ||
                        std::fabs(vx.pos[1]) > 1e6 || std::fabs(vx.pos[2]) > 1e6) {
                        throw FormatError("nonfinite attributes or unreasonable position");
                    }
                }
                out.draws.push_back(std::move(d));
            }
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// CMAB: "cmab", the pixel data offset at 0x1c, and a "txpt" table (count, then 24-byte entries: size, levels, width, height,
// GL format, data offset, index).
bool ParseCmabFrames(const std::vector<uint8_t>& raw, std::vector<Image>& frames, std::string& error) {
    try {
        Reader r(raw.data(), raw.size());
        r.Magic(0, "cmab");
        const uint32_t base = r.R32(0x1c);
        size_t p = 0;
        for (size_t i = 0; i + 4 <= raw.size(); ++i) {
            if (std::memcmp(&raw[i], "txpt", 4) == 0) {
                p = i;
                break;
            }
        }
        if (!p) {
            throw FormatError("no texture table");
        }
        const uint32_t count = r.R32(p + 4);
        if (count > 256) {
            throw FormatError("implausible frame count");
        }
        for (uint32_t k = 0; k < count; ++k) {
            const size_t q = p + 8 + 24 * (size_t)k;
            const uint32_t size = r.R32(q), w = r.R16(q + 8), h = r.R16(q + 10), gl = r.R32(q + 12), off = r.R32(q + 16);
            Fmt fmt;
            std::string name;
            if (!FormatOf(gl, fmt, name)) {
                throw FormatError("unknown frame format");
            }
            r.Need((size_t)base + off, size);
            frames.push_back(DecodeTexture(fmt, (int)w, (int)h, raw.data() + base + off, size));
        }
        return !frames.empty();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace Mm3d
