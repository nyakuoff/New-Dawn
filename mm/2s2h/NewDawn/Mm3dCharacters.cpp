// See Mm3dCharacters.h and docs/MM3D_CHARACTERS.md. A C++ port of the spike converter (tools/newdawn/mm3d/chars.py):
// every 3DS skeleton whose limb hierarchy equals the N64 one is baked into limb-local display lists, so the original
// N64 animation data drives the 3DS mesh. Seams are welded with the N64's own Flex matrix switching (segment 0xD).
#include "Mm3dCharacters.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
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
#include "2s2h/resource/type/Skeleton.h"
#include "Mm3dFormats.h"
#include "SpanUpscale.h"
#include "ZipWriter.h"

namespace fs = std::filesystem;
using NewDawn::ZipWriter;
using json = nlohmann::json;

namespace NewDawnMm3d {
namespace {

// Bump when the conversion changes; the table and the dump's file identities are hashed into the signature too.
constexpr const char* kConverterVersion = "1.11.0";

struct Entry {
    const char* object;
    const char* skeleton;
    const char* archive; // zelda2_* actor archive in the dump
    const char* cmb; // GAR entry name
    const char* skip; // comma separated: "3" a CMB material the N64 draws elsewhere, "d12" a draw (mesh) to leave out, "k15" an N64 limb whose display lists stay, "s5=0.66:0.85@12" a draw scaled about a bone (along its axis : across it)
    bool reviewed;
    float lightRef, saturation; // colour overrides for this model, 0 = the global defaults
    const char* rig;            // "bone:limb,..." when the 3DS rig differs from the N64 one (see ParseRig); "" = same hierarchy
    const char* face;           // eye and mouth states (see ParseFace); "" = the faces stay at their first frame
    const char* matcls;         // "texture name part=class,...": character material classes that differ from the defaults (see ClassOfTexture)
    const char* tint;           // "material:hue degrees:saturation factor[:value factor[:from hue:to hue]],..." per CMB material (hair that reads hotter than the N64's)
};
const Entry kTable[] = {
#include "Mm3dCharacterTable.inc"
};
constexpr size_t kTableCount = sizeof(kTable) / sizeof(kTable[0]);

// ------------------------------------------------------------------ state
std::mutex gMutex;
State gState = State::None;
std::atomic<uint32_t> gDone{ 0 }, gTotal{ 0 };
std::atomic<bool> gRunning{ false };
bool gMounted = false, gPendingRestart = false, gStagedOk = false, gOverlayOn = false;
std::string gMessage;
uint32_t gConverted = 0, gFailed = 0, gEnabled = 0;
uint32_t gLinkForms = 0; // bit per Player form whose 3DS model is converted and enabled (0 Fierce Deity .. 4 human)
double gSeconds = 0;

void SetState(State s, const std::string& message = "") {
    std::lock_guard<std::mutex> lock(gMutex);
    gState = s;
    gMessage = message;
}

fs::path Root() {
    return fs::path(Ship::Context::GetPathRelativeToAppDirectory("cache/newdawn-mm3d", appShortName));
}
fs::path Live() {
    return Root() / "live";
}
fs::path Staging() {
    return Root() / "staging";
}
constexpr const char* kArchiveName = "mm3d-characters.o2r";

std::shared_ptr<Ship::ArchiveManager> Archives() {
    return Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
}

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

// The path hash display lists use for resources (ship/utils/StrHash64: CRC-64, poly 0x42f0e1eba9ea3693, no xorout).
uint64_t Crc64(const std::string& name) {
    uint64_t v = ~0ULL;
    for (unsigned char c : name) {
        v ^= (uint64_t)c << 56;
        for (int i = 0; i < 8; ++i) {
            v = (v & (1ULL << 63)) ? (v << 1) ^ 0x42f0e1eba9ea3693ULL : v << 1;
        }
    }
    return v;
}

struct DrawScale {
    float along = 1, across = 1; // scale along the axis from the pivot to the draw's centre, and across it
    int pivotBone = -1;          // the draw is scaled about this bone's origin in the model's bind pose
};
struct SkipSpec {
    std::set<int> materials, draws, keepLimbs, unlit; // "u5": CMB material 5 is drawn without lighting (glowing eyes)
    std::map<int, float> offsets; // "o22=300": draw 22 pushed 300 units along its vertex normals (eye planes that sit inside the 3DS face)
    std::map<int, DrawScale> scales; // "s5=0.66:0.85@12": draw 5 shortened and narrowed about bone 12's origin (a hat bigger than the N64's)
};
SkipSpec ParseSkip(const char* s) {
    SkipSpec out;
    std::string all(s ? s : "");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        end = end == std::string::npos ? all.size() : end;
        const std::string tok = all.substr(pos, end - pos);
        pos = end + 1;
        if (tok.empty()) {
            continue;
        }
        if (tok[0] == 's') {
            const size_t eq = tok.find('='), at = tok.find('@');
            if (eq != std::string::npos && at != std::string::npos) {
                const size_t colon = tok.find(':', eq);
                const float along = (float)std::atof(tok.c_str() + eq + 1);
                const float across = colon != std::string::npos && colon < at ? (float)std::atof(tok.c_str() + colon + 1) : along;
                out.scales[std::atoi(tok.c_str() + 1)] = { along, across, std::atoi(tok.c_str() + at + 1) };
            }
        } else if (tok[0] == 'u') {
            out.unlit.insert(std::atoi(tok.c_str() + 1));
        } else if (tok[0] == 'o') {
            const size_t eq = tok.find('=');
            if (eq != std::string::npos) {
                out.offsets[std::atoi(tok.c_str() + 1)] = (float)std::atof(tok.c_str() + eq + 1);
            }
        } else if (tok[0] == 'd') {
            out.draws.insert(std::atoi(tok.c_str() + 1));
        } else if (tok[0] == 'k') {
            out.keepLimbs.insert(std::atoi(tok.c_str() + 1));
        } else {
            out.materials.insert(std::atoi(tok.c_str()));
        }
    }
    return out;
}

// "bone:limb,bone:limb": the pairs of a skeleton whose 3DS rig is not the N64 one. 3DS bones that are not listed follow
// their nearest listed ancestor; N64 limbs that are not listed get an empty display list (docs/MM3D_CHARACTERS.md).
// "bone:limb>drive": the bone's frame is that of a pivot limb without a display list; its mesh is drawn under the limb
// "drive", which sits at the same joint (Kafei's and Link's torso hangs off the upper-root pivot).
struct RigSpec {
    std::map<int, int> frameLimb;  // bone -> limb whose frame it is
    std::map<int, int> driveLimb;  // bone -> limb whose matrix draws its mesh, when that is another limb
};
RigSpec ParseRig(const char* s) {
    RigSpec out;
    std::string all(s ? s : "");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        end = end == std::string::npos ? all.size() : end;
        const std::string tok = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const int bone = std::atoi(tok.c_str());
        const size_t gt = tok.find('>');
        const int limb = std::atoi(tok.c_str() + colon + 1);
        out.frameLimb[bone] = limb;
        if (gt != std::string::npos) {
            out.driveLimb[bone] = std::atoi(tok.c_str() + gt + 1);
        }
    }
    return out;
}

// "E:8:misc/x_eye.cmab:gNameTex=2,gOtherTex=0|M:9:..." : per face part, the segment the N64 limb display list reads its texture
// from (the actor points it at one of the state textures), the CMAB of the 3DS frames, and which frame replaces each N64 state
// texture. The converted display list reads the same segment, and the state textures are overridden by the 3DS frames.
struct FacePart {
    int seg = 0;
    std::string cmab;
    std::vector<std::pair<std::string, int>> states;
};
std::vector<FacePart> ParseFace(const char* s) {
    std::vector<FacePart> out;
    std::string all(s ? s : "");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find('|', pos);
        end = end == std::string::npos ? all.size() : end;
        const std::string part = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t a = part.find(':'), b = a == std::string::npos ? a : part.find(':', a + 1),
                     c = b == std::string::npos ? b : part.find(':', b + 1);
        if (c == std::string::npos) {
            continue;
        }
        FacePart f;
        f.seg = std::atoi(part.substr(a + 1, b - a - 1).c_str());
        f.cmab = part.substr(b + 1, c - b - 1);
        size_t q = c + 1;
        while (q < part.size()) {
            size_t comma = part.find(',', q);
            comma = comma == std::string::npos ? part.size() : comma;
            const std::string kv = part.substr(q, comma - q);
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                f.states.emplace_back(kv.substr(0, eq), std::atoi(kv.c_str() + eq + 1));
            }
            q = comma + 1;
        }
        out.push_back(std::move(f));
    }
    return out;
}

// GAR names carry either slash ("misc\\ange_eye.cmab" in some archives).
const std::vector<uint8_t>* FindGarFile(const Mm3d::Gar& gar, const std::string& name) {
    for (const auto& kv : gar.files) {
        std::string k = kv.first;
        std::replace(k.begin(), k.end(), '\\', '/');
        if (k == name) {
            return &kv.second;
        }
    }
    return nullptr;
}

// The upscaler weights ship with the game (assets/newdawn/mm3d/span_v4_4x.bin, CC0); gNewDawn.MM3D.Upscale=0 skips them.
std::string UpscalerPath() {
    const char* root = CVarGetString("gNewDawn.Assets.Path", "assets/newdawn");
    return (fs::path(root ? root : "assets/newdawn") / "mm3d" / "span_v4_4x.bin").string();
}
bool UpscaleWanted() {
    return CVarGetInteger("gNewDawn.MM3D.Upscale", 1) != 0;
}
// "Include unreviewed 3DS models" (gNewDawn.MM3D.ReviewAll, off by default): also writes the models not yet reviewed in game.
// The table leaves out the known broken ones, so they stay excluded.
bool Enabled(const Entry& e) {
    return e.reviewed || CVarGetInteger("gNewDawn.MM3D.ReviewAll", 0) != 0;
}

// ------------------------------------------------------------------ N64 skeleton (from the loaded game archives)
struct Limb {
    std::string name, dl, dl2;
    int16_t trans[3] = {};
    int child = -1, sibling = -1, parent = -1;
};

std::string NStr(const std::vector<uint8_t>& d, size_t& p) {
    uint32_t n;
    if (p + 4 > d.size()) {
        throw std::runtime_error("short resource");
    }
    std::memcpy(&n, &d[p], 4);
    if (p + 4 + n > d.size()) {
        throw std::runtime_error("short resource string");
    }
    std::string s((const char*)&d[p + 4], n);
    p += 4 + n;
    return s;
}

bool ReadArchiveFile(const std::string& path, std::vector<uint8_t>& out) {
    auto file = Archives()->LoadFile(path);
    if (!file || !file->Buffer) {
        return false;
    }
    out.assign(file->Buffer->begin(), file->Buffer->end());
    return true;
}

bool ReadSkeleton(const std::string& path, std::vector<Limb>& limbs, std::string& error) {
    try {
        std::vector<uint8_t> d;
        if (!ReadArchiveFile(path, d) || d.size() < 80) {
            error = "skeleton resource missing";
            return false;
        }
        if (d[64] != 1) {
            error = "not a Flex skeleton";
            return false;
        }
        uint32_t count;
        std::memcpy(&count, &d[64 + 11], 4);
        size_t p = 64 + 15;
        std::vector<std::string> table;
        for (uint32_t i = 0; i < count; ++i) {
            table.push_back(NStr(d, p));
        }
        for (const std::string& s : table) {
            std::vector<uint8_t> b;
            if (!ReadArchiveFile(s, b)) {
                error = "limb resource missing: " + s;
                return false;
            }
            Limb l;
            l.name = s.substr(s.rfind('/') + 1);
            p = 64 + 2;
            NStr(b, p);
            p += 2;
            uint32_t modif;
            std::memcpy(&modif, &b[p], 4);
            if (modif) {
                error = "skin limbs are not supported";
                return false;
            }
            p += 4;
            NStr(b, p);
            p += 12 + 6;
            NStr(b, p);
            NStr(b, p);
            l.dl = NStr(b, p);
            l.dl2 = NStr(b, p);
            if (p + 8 > b.size()) {
                error = "short limb";
                return false;
            }
            std::memcpy(l.trans, &b[p], 6);
            l.child = b[p + 6] == 255 ? -1 : b[p + 6];
            l.sibling = b[p + 7] == 255 ? -1 : b[p + 7];
            limbs.push_back(std::move(l));
        }
        for (size_t i = 0; i < limbs.size(); ++i) {
            for (int c = limbs[i].child; c >= 0 && c < (int)limbs.size(); c = limbs[c].sibling) {
                limbs[c].parent = (int)i;
            }
        }
        return !limbs.empty();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// Index of each limb's matrix in the Flex skeleton's array (segment 0xD): one per limb with a display list, in draw order
// (limb, its child subtree, then its sibling), as SkelAnime_DrawFlexLimbOpa allocates them.
std::map<int, int> MatrixSlots(const std::vector<Limb>& limbs) {
    std::map<int, int> slots;
    int n = 0;
    std::function<void(int)> visit = [&](int i) {
        if (i < 0 || i >= (int)limbs.size()) {
            return;
        }
        if (!limbs[i].dl.empty()) {
            slots[i] = n++;
        }
        visit(limbs[i].child);
        visit(limbs[i].sibling);
    };
    visit(0);
    return slots;
}

// A pivot limb has no display list and no matrix: its weight goes to the nearest limb below it that draws, else above.
int NearestDrawnLimb(const std::vector<Limb>& limbs, const std::map<int, int>& slots, int limb) {
    if (slots.count(limb)) {
        return limb;
    }
    std::function<int(int)> below = [&](int i) -> int {
        for (int c = limbs[i].child; c >= 0; c = limbs[c].sibling) {
            if (slots.count(c)) {
                return c;
            }
            const int r = below(c);
            if (r >= 0) {
                return r;
            }
        }
        return -1;
    };
    const int d = below(limb);
    if (d >= 0) {
        return d;
    }
    for (int p = limbs[limb].parent; p >= 0; p = limbs[p].parent) {
        if (slots.count(p)) {
            return p;
        }
    }
    return -1;
}

// ------------------------------------------------------------------ bone to limb
std::string Canon(const std::map<int, std::vector<int>>& kids, int node) {
    std::vector<std::string> parts;
    auto it = kids.find(node);
    if (it != kids.end()) {
        for (int c : it->second) {
            parts.push_back(Canon(kids, c));
        }
    }
    std::sort(parts.begin(), parts.end());
    std::string s = "(";
    for (const auto& p : parts) {
        s += p;
    }
    return s + ")";
}

// Same rooted tree required: children are paired by identical subtree shape, then by the nearest joint offset.
bool MatchBones(const std::vector<Limb>& limbs, const std::vector<Mm3d::Bone>& bones, std::map<int, int>& boneToLimb,
                std::string& error) {
    std::map<int, std::vector<int>> boneKids, limbKids;
    std::map<int, const Mm3d::Bone*> byId;
    for (const auto& b : bones) {
        boneKids[b.parent].push_back(b.id);
        byId[b.id] = &b;
    }
    for (size_t i = 0; i < limbs.size(); ++i) {
        limbKids[limbs[i].parent].push_back((int)i);
    }
    if (boneKids[-1].size() != 1 || limbKids[-1].size() != 1) {
        error = "skeleton has several roots";
        return false;
    }
    std::function<bool(int, int)> walk = [&](int bone, int limb) -> bool {
        boneToLimb[bone] = limb;
        std::vector<int> bk = boneKids[bone], lk = limbKids[limb];
        if (bk.size() != lk.size()) {
            error = "child counts differ";
            return false;
        }
        for (int c : bk) {
            const std::string shape = Canon(boneKids, c);
            int best = -1;
            double bestD = 1e30;
            for (int j : lk) {
                if (Canon(limbKids, j) != shape) {
                    continue;
                }
                double d = 0;
                for (int k = 0; k < 3; ++k) {
                    const double t = byId[c]->translation[k] - limbs[j].trans[k];
                    d += t * t;
                }
                if (d < bestD) {
                    bestD = d, best = j;
                }
            }
            if (best < 0) {
                error = "no N64 child with the same subtree shape";
                return false;
            }
            lk.erase(std::find(lk.begin(), lk.end(), best));
            if (!walk(c, best)) {
                return false;
            }
        }
        return true;
    };
    return walk(boneKids[-1][0], limbKids[-1][0]);
}

// ------------------------------------------------------------------ resource writers
void Put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v.push_back((uint8_t)(x >> (8 * i)));
    }
}

std::vector<uint8_t> Header(const char tag[4], uint32_t version) {
    std::vector<uint8_t> h(64, 0);
    std::memcpy(&h[4], tag, 4);
    h[8] = (uint8_t)version;
    const uint8_t id[8] = { 0xef, 0xbe, 0xad, 0xde, 0xef, 0xbe, 0xad, 0xde };
    std::memcpy(&h[12], id, 8);
    return h;
}

struct Dl {
    std::vector<uint8_t> b;
    void W(uint32_t w0, uint32_t w1) {
        Put32(b, w0);
        Put32(b, w1);
    }
    void Ex(uint32_t w0, uint32_t w1, uint64_t hash) {
        W(w0, w1);
        W((uint32_t)(hash >> 32), (uint32_t)hash);
    }
    void Vtx(int n, int v0, uint32_t offset, uint64_t hash) {
        Ex(0x32000000u | ((uint32_t)n << 12) | ((uint32_t)(v0 + n) << 1), offset, hash);
    }
    void Tri2(const std::array<int, 3>& a, const std::array<int, 3>& c) {
        W(0x06000000u | (a[0] * 2 << 16) | (a[1] * 2 << 8) | a[2] * 2, (c[0] * 2 << 16) | (c[1] * 2 << 8) | c[2] * 2);
    }
    void Tri1(const std::array<int, 3>& a) {
        W(0x05000000u | (a[0] * 2 << 16) | (a[1] * 2 << 8) | a[2] * 2, 0);
    }
    // gDPLoadTextureBlock for an RGBA32 image declared as w x h texels; the resource keeps its real size.
    // seg >= 0: the image comes from that segment (the actor points it at an N64 face texture; the overlay swaps in the 3DS frame).
    void Tex(uint64_t hash, int seg, int w, int h, uint16_t wrapS, uint16_t wrapT) {
        auto mode = [](uint16_t wrap) { return wrap == 0x8370 ? 1 : (wrap == 0x812F ? 2 : 0); };
        auto mask = [](int n) { return (n & (n - 1)) == 0 ? (int)std::log2((double)n) : 0; };
        const int cms = mode(wrapS), cmt = mode(wrapT);
        const int ms = cms != 2 ? mask(w) : 0, mt = cmt != 2 ? mask(h) : 0;
        const int line = (w * 2 + 7) >> 3; // G_IM_SIZ_32b_LINE_BYTES = 2
        const uint32_t tile = (cmt << 18) | (mt << 14) | (cms << 8) | (ms << 4);
        W(0xE7000000, 0);
        if (seg >= 0) {
            W(0xFD000000u | (3 << 19), ((uint32_t)seg << 24) | 1u); // bit 0: the interpreter treats the address as segmented
        } else {
            Ex(0x20000000u | (3 << 19), 0, hash);
        }
        W(0xE8000000, 0);
        W(0xF5000000u | (3 << 19), (7u << 24) | tile);
        W(0xE6000000, 0);
        W(0xF3000000, (7u << 24) | (((uint32_t)(w * h - 1) & 0xFFF) << 12));
        W(0xE7000000, 0);
        W(0xF5000000u | (3 << 19) | (line << 9), tile);
        W(0xF2000000, (((uint32_t)(w - 1) << 2) << 12) | ((uint32_t)(h - 1) << 2));
    }
    std::vector<uint8_t> Resource() {
        W(0xDF000000, 0);
        std::vector<uint8_t> out = Header("TLDO", 0);
        out.push_back(0x04);
        out.insert(out.end(), 7, 0xff);
        out.insert(out.end(), b.begin(), b.end());
        return out;
    }
};

std::vector<uint8_t> VtxArray(const std::vector<std::array<uint8_t, 16>>& verts) {
    std::vector<uint8_t> out = Header("RRAO", 0);
    Put32(out, 0x19);
    Put32(out, (uint32_t)verts.size());
    for (const auto& v : verts) {
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

// Material class of a CMB texture, written into the texture's flags (TEX_FLAG_NEWDAWN_CHARCLASS): 1 skin, 2 costume (cloth, leather
// and metal told apart per texel in the shader), 3 hair, 4 eyes and mouth, 5 metal, 6 cloth, 7 leather, 0 none. Defaults by the
// texture's name and whether its material is lit; the table row overrides by name part ("link_child_f01=3").
int ClassOfTexture(const std::string& name, const Mm3d::Material& mat, const char* overrides) {
    std::string lower = name;
    for (char& c : lower) {
        c = (char)std::tolower((unsigned char)c);
    }
    std::string all(overrides ? overrides : "");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        end = end == std::string::npos ? all.size() : end;
        const std::string tok = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = tok.find('=');
        if (eq != std::string::npos && lower.find(tok.substr(0, eq)) != std::string::npos) {
            return std::atoi(tok.c_str() + eq + 1);
        }
    }
    auto tail = [&](const char* mark) { // "_e00", "_m00": the mark followed by a digit
        const size_t at = lower.find(mark);
        return at != std::string::npos && at + std::strlen(mark) < lower.size() && std::isdigit((unsigned char)lower[at + std::strlen(mark)]);
    };
    if (tail("_e") || tail("_m") || tail("_ev") || tail("_eb")) {
        return 4;
    }
    if (lower.find("hair") != std::string::npos) {
        return 3;
    }
    if (lower.find("skin") != std::string::npos) {
        return 1;
    }
    return mat.fragmentLighting || mat.vertexLighting ? 2 : 0;
}

std::vector<uint8_t> TextureResource(const Mm3d::Image& img, int materialClass = 0) {
    std::vector<uint8_t> out = Header("XETO", 1);
    float one = 1.0f;
    uint32_t oneBits;
    std::memcpy(&oneBits, &one, 4);
    Put32(out, 1); // RGBA32
    Put32(out, img.width);
    Put32(out, img.height);
    // TEX_FLAG_LOAD_AS_IMG: the resource's own size, not the declared one; bits 9 to 12 the material class.
    Put32(out, 2u | ((uint32_t)materialClass << 9));
    Put32(out, oneBits);
    Put32(out, oneBits);
    Put32(out, (uint32_t)img.rgba.size());
    out.insert(out.end(), img.rgba.begin(), img.rgba.end());
    return out;
}

void DeclaredSize(int w, int h, int& dw, int& dh) { // LOADBLOCK holds 4096 RGBA32 texels
    const int k = std::max(1, std::max(w, h) / 64);
    dw = std::max(1, w / k);
    dh = std::max(1, h / k);
}

// ------------------------------------------------------------------ colour
// The 3DS materials light their textures as texture * (ambient * 0.4 + diffuse * 0.5 * N.L): the combiner multiplies by
// the fragment-lighting term, whose coefficients are the material's ambient and diffuse constants (127 / 102 almost
// everywhere). The N64 display lists multiply by the actor's full light instead, so the same texture came out about
// half as bright and, being authored a little flat, washed out. The constants are folded into the texture here:
// gain = LightRef / (0.6 * diffuse + 0.4 * ambient), then a saturation lift. Both references are cvars (and part
// of the signature) while the look is tuned; the defaults are the calibrated values.
struct ColourParams {
    float lightRef, saturation;
};
ColourParams GetColourParams() {
    return { CVarGetFloat("gNewDawn.MM3D.LightRef", 0.92f), CVarGetFloat("gNewDawn.MM3D.Saturation", 1.0f) };
}

struct Tint {
    float hue = 0, sat = 1, val = 1;
    float from = 0, to = 360; // only pixels whose hue (before the shift) lies in [from, to]: the greens of a texture shared with gold and brown
};
std::map<int, Tint> ParseTint(const char* s) {
    std::map<int, Tint> out;
    std::string all(s ? s : "");
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        end = end == std::string::npos ? all.size() : end;
        const std::string tok = all.substr(pos, end - pos);
        pos = end + 1;
        std::vector<float> f;
        for (size_t q = tok.find(':'); q != std::string::npos; q = tok.find(':', q + 1)) {
            f.push_back((float)std::atof(tok.c_str() + q + 1));
        }
        if (f.size() >= 2) {
            Tint t;
            t.hue = f[0], t.sat = f[1];
            t.val = f.size() > 2 ? f[2] : 1.0f;
            t.from = f.size() > 3 ? f[3] : 0.0f;
            t.to = f.size() > 4 ? f[4] : 360.0f;
            out[std::atoi(tok.c_str())] = t;
        }
    }
    return out;
}
// Rotates the hue of every pixel and scales its saturation and value (after the colour mapping).
void ApplyTint(Mm3d::Image& img, const Tint& t) {
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const float r = img.rgba[i] / 255.0f, g = img.rgba[i + 1] / 255.0f, b = img.rgba[i + 2] / 255.0f;
        const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
        float h = 0;
        if (d > 1e-6f) {
            h = mx == r ? std::fmod((g - b) / d, 6.0f) : (mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f);
            h *= 60.0f;
        }
        if (h < t.from || h > t.to) {
            continue;
        }
        h = std::fmod(h + t.hue + 720.0f, 360.0f);
        const float sat = std::clamp((mx > 0 ? d / mx : 0.0f) * t.sat, 0.0f, 1.0f);
        const float v = std::clamp(mx * t.val, 0.0f, 1.0f);
        const float c = v * sat, x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
        float o[3] = { 0, 0, 0 };
        const int k = (int)(h / 60.0f) % 6;
        const float tab[6][3] = { { c, x, 0 }, { x, c, 0 }, { 0, c, x }, { 0, x, c }, { x, 0, c }, { c, 0, x } };
        for (int q = 0; q < 3; ++q) {
            o[q] = tab[k][q] + m;
        }
        img.rgba[i] = (uint8_t)std::lround(std::clamp(o[0], 0.0f, 1.0f) * 255.0f);
        img.rgba[i + 1] = (uint8_t)std::lround(std::clamp(o[1], 0.0f, 1.0f) * 255.0f);
        img.rgba[i + 2] = (uint8_t)std::lround(std::clamp(o[2], 0.0f, 1.0f) * 255.0f);
    }
}

void MapColour(Mm3d::Image& img, const Mm3d::Material& mat, const ColourParams& cp) {
    // The 3DS shows texture x scale x (ambient x A + diffuse x D x N.L): the combiner stages' RGB scales (2 for most characters, 1
    // for some) multiply, and the N64 shows texture x (a + d x N.L). Materials without lighting (eyes, glows, water) are
    // texture x scale and are drawn unlit.
    float scale = 1.0f;
    for (int j = 0; j < mat.combinerCount; ++j) {
        scale *= mat.combiners[j][2] == 2 ? 2.0f : (mat.combiners[j][2] == 4 ? 4.0f : 1.0f);
    }
    const bool lit = mat.fragmentLighting || mat.vertexLighting;
    const float coeff = (0.6f * mat.diffuse[0] + 0.4f * mat.ambient[0]) / 255.0f;
    const float gain = lit ? (coeff > 0.05f ? scale * coeff / cp.lightRef : 1.0f) : scale;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        float c[3] = { (float)img.rgba[i], (float)img.rgba[i + 1], (float)img.rgba[i + 2] };
        const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        for (float& v : c) {
            v = std::clamp((lum + (v - lum) * cp.saturation) * gain, 0.0f, 255.0f);
        }
        img.rgba[i] = (uint8_t)std::lround(c[0]);
        img.rgba[i + 1] = (uint8_t)std::lround(c[1]);
        img.rgba[i + 2] = (uint8_t)std::lround(c[2]);
    }
}

// ------------------------------------------------------------------ conversion
struct TriVert {
    float p[3], n[3], uv[2];
    int drive;
};
struct Tri {
    int material;
    TriVert v[3];
};

struct Rotation3 {
    double m[9];
};
Rotation3 RotXyz(double x, double y, double z) { // Rz * Ry * Rx
    const double cx = std::cos(x), cy = std::cos(y), cz = std::cos(z), sx = std::sin(x), sy = std::sin(y), sz = std::sin(z);
    const double rx[9] = { 1, 0, 0, 0, cx, -sx, 0, sx, cx }, ry[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy },
                 rz[9] = { cz, -sz, 0, sz, cz, 0, 0, 0, 1 };
    double t[9], r[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            t[i * 3 + j] = ry[i * 3] * rx[j] + ry[i * 3 + 1] * rx[3 + j] + ry[i * 3 + 2] * rx[6 + j];
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[i * 3 + j] = rz[i * 3] * t[j] + rz[i * 3 + 1] * t[3 + j] + rz[i * 3 + 2] * t[6 + j];
        }
    }
    Rotation3 out;
    std::memcpy(out.m, r, sizeof(r));
    return out;
}

// Inverse of the affine bind matrix (rotation x scale, translation) as 3x3 + translation.
struct Affine {
    double r[9], t[3];
};
Affine InvertBind(const double* m) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double inv = std::fabs(det) < 1e-12 ? 0.0 : 1.0 / det;
    Affine out;
    out.r[0] = (e * i - f * h) * inv, out.r[1] = (c * h - b * i) * inv, out.r[2] = (b * f - c * e) * inv;
    out.r[3] = (f * g - d * i) * inv, out.r[4] = (a * i - c * g) * inv, out.r[5] = (c * d - a * f) * inv;
    out.r[6] = (d * h - e * g) * inv, out.r[7] = (b * g - a * h) * inv, out.r[8] = (a * e - b * d) * inv;
    for (int k = 0; k < 3; ++k) {
        out.t[k] = -(out.r[k * 3] * m[3] + out.r[k * 3 + 1] * m[7] + out.r[k * 3 + 2] * m[11]);
    }
    return out;
}

struct Result {
    bool ok = false;
    std::string error;
    uint32_t limbs = 0, tris = 0, written = 0, stretched = 0, verts = 0, switches = 0, textures = 0, dropped = 0;
    double seconds = 0;
};

using Writer = std::function<void(const std::string&, const std::vector<uint8_t>&)>;

Result Convert(const Entry& e, const std::vector<Limb>& limbs, Mm3d::Model& model, const Writer& write,
               const ColourParams& colour, const SpanUpscaler* upscaler, const Mm3d::Gar* gar) {
    Result res;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        std::string err;
        std::map<int, int> boneToLimb;
        const bool rigged = e.rig && e.rig[0];
        std::map<int, size_t> boneIndex;
        for (size_t i = 0; i < model.bones.size(); ++i) {
            boneIndex[model.bones[i].id] = i;
        }
        RigSpec rigSpec;
        if (rigged) {
            rigSpec = ParseRig(e.rig);
            boneToLimb = rigSpec.frameLimb;
            for (const auto& kv : boneToLimb) {
                if (!boneIndex.count(kv.first) || kv.second < 0 || kv.second >= (int)limbs.size()) {
                    res.error = "rig: bone " + std::to_string(kv.first) + " or limb " + std::to_string(kv.second) + " does not exist";
                    return res;
                }
            }
        } else if (!MatchBones(limbs, model.bones, boneToLimb, err)) {
            res.error = "bone match: " + err;
            return res;
        }
        const std::map<int, int> slots = MatrixSlots(limbs);
        std::map<int, int> limbToBone;
        for (const auto& kv : boneToLimb) {
            limbToBone[kv.second] = kv.first;
        }
        for (const auto& kv : rigSpec.driveLimb) { // the drive limb shares the frame of the bone
            limbToBone[kv.second] = kv.first;
        }
        if (rigged && limbToBone.count(0)) {
            // Stature: the N64 root limb sits at its rest height; scale the whole 3DS model about the origin so its root
            // bone does too (the feet then stay on the floor and the head where the N64 one is).
            const double rootHeight = model.bones[boneIndex[limbToBone[0]]].bind[7];
            const double k = rootHeight > 1 && limbs[0].trans[1] > 1 ? limbs[0].trans[1] / rootHeight : 1.0;
            if (std::fabs(k - 1.0) > 0.005) {
                for (Mm3d::Bone& b : model.bones) {
                    b.bind[3] *= k, b.bind[7] *= k, b.bind[11] *= k;
                    for (int c = 0; c < 3; ++c) {
                        b.translation[c] *= (float)k;
                    }
                }
                for (Mm3d::Draw& dr : model.draws) {
                    for (Mm3d::Vertex& v : dr.vertices) {
                        for (int c = 0; c < 3; ++c) {
                            v.pos[c] *= (float)k;
                        }
                    }
                }
            }
        }
        // Rigged: every 3DS bone follows its nearest listed ancestor (else a listed descendant, else the first listed bone).
        std::map<int, int> resolved;
        if (rigged) {
            for (const Mm3d::Bone& b : model.bones) {
                int cur = b.id;
                while (cur >= 0 && !boneToLimb.count(cur)) {
                    cur = model.bones[boneIndex[cur]].parent;
                }
                if (cur < 0) { // no listed ancestor: breadth-first below this bone
                    std::vector<int> queue = { b.id };
                    for (size_t qi = 0; qi < queue.size() && cur < 0; ++qi) {
                        if (boneToLimb.count(queue[qi])) {
                            cur = queue[qi];
                        }
                        for (const Mm3d::Bone& c : model.bones) {
                            if (c.parent == queue[qi]) {
                                queue.push_back(c.id);
                            }
                        }
                    }
                }
                resolved[b.id] = cur >= 0 ? cur : boneToLimb.begin()->first;
            }
        }
        // Per bone: inverse bind, and the rest-pose fix (where the 3DS joint offset differs from the N64 limb's, the
        // child's mesh is shifted so that at the 3DS bind rotation it sits where it does in the 3DS model).
        std::map<int, Affine> inv;
        std::map<int, std::array<double, 3>> shift;
        for (const auto& kv : boneToLimb) {
            const Mm3d::Bone& b = model.bones[boneIndex[kv.first]];
            inv[kv.first] = InvertBind(b.bind);
            std::array<double, 3> s = { 0, 0, 0 };
            if (rigged) {
                // Offset of this limb from its nearest listed ancestor limb on the N64 side (the unlisted limbs between
                // them are pivots, their rest rotation taken as identity), against the offset of the bone from the bone
                // paired with that ancestor, both in the ancestor's frame.
                int pl = limbs[kv.second].parent;
                double dn[3] = { (double)limbs[kv.second].trans[0], (double)limbs[kv.second].trans[1], (double)limbs[kv.second].trans[2] };
                while (pl >= 0 && !limbToBone.count(pl)) {
                    for (int k = 0; k < 3; ++k) {
                        dn[k] += limbs[pl].trans[k];
                    }
                    pl = limbs[pl].parent;
                }
                if (pl >= 0) {
                    const Mm3d::Bone& qa = model.bones[boneIndex[limbToBone[pl]]];
                    const Affine ia = InvertBind(qa.bind);
                    double rel[9], t3[3];
                    for (int i = 0; i < 3; ++i) {
                        for (int j = 0; j < 3; ++j) {
                            rel[i * 3 + j] = ia.r[i * 3] * b.bind[j] + ia.r[i * 3 + 1] * b.bind[4 + j] + ia.r[i * 3 + 2] * b.bind[8 + j];
                        }
                        t3[i] = ia.r[i * 3] * b.bind[3] + ia.r[i * 3 + 1] * b.bind[7] + ia.r[i * 3 + 2] * b.bind[11] + ia.t[i];
                    }
                    double d[3];
                    for (int k = 0; k < 3; ++k) {
                        d[k] = dn[k] - t3[k];
                    }
                    if (std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > 1) {
                        for (int k = 0; k < 3; ++k) { // -R^T d
                            s[k] = -(rel[k] * d[0] + rel[3 + k] * d[1] + rel[6 + k] * d[2]);
                        }
                    }
                }
            } else if (b.parent >= 0) {
                double d[3];
                for (int k = 0; k < 3; ++k) {
                    d[k] = (double)limbs[kv.second].trans[k] - b.translation[k];
                }
                if (std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > 1) {
                    const Rotation3 r = RotXyz(b.rotation[0], b.rotation[1], b.rotation[2]);
                    for (int k = 0; k < 3; ++k) { // -R^T d
                        s[k] = -(r.m[k] * d[0] + r.m[3 + k] * d[1] + r.m[6 + k] * d[2]);
                    }
                }
            }
            shift[kv.first] = s;
        }

        const SkipSpec skipSpec = ParseSkip(e.skip);
        const std::set<int>& skip = skipSpec.materials;
        std::vector<std::vector<Tri>> perLimb(limbs.size());
        for (size_t drawIndex = 0; drawIndex < model.draws.size(); ++drawIndex) {
            const Mm3d::Draw& dr = model.draws[drawIndex];
            if (skip.count(dr.material) || skipSpec.draws.count((int)drawIndex)) {
                continue;
            }
            DrawScale scaleSpec;
            float scaleC[3] = { 0, 0, 0 }, scaleU[3] = { 0, 1, 0 };
            const auto scaleIt = skipSpec.scales.find((int)drawIndex);
            if (scaleIt != skipSpec.scales.end() && boneIndex.count(scaleIt->second.pivotBone)) {
                scaleSpec = scaleIt->second;
                const Mm3d::Bone& pb = model.bones[boneIndex[scaleSpec.pivotBone]];
                scaleC[0] = (float)pb.bind[3], scaleC[1] = (float)pb.bind[7], scaleC[2] = (float)pb.bind[11];
                double mean[3] = { 0, 0, 0 };
                for (const Mm3d::Vertex& v : dr.vertices) {
                    for (int k = 0; k < 3; ++k) {
                        mean[k] += v.pos[k] / (double)dr.vertices.size();
                    }
                }
                double len = 0;
                for (int k = 0; k < 3; ++k) {
                    scaleU[k] = (float)(mean[k] - scaleC[k]);
                    len += (double)scaleU[k] * scaleU[k];
                }
                len = std::sqrt(len);
                for (int k = 0; k < 3; ++k) {
                    scaleU[k] = len > 1e-6 ? (float)(scaleU[k] / len) : (k == 1 ? 1.0f : 0.0f);
                }
            }
            const auto offsetIt = skipSpec.offsets.find((int)drawIndex);
            const float pushOut = offsetIt != skipSpec.offsets.end() ? offsetIt->second : 0.0f;
            for (size_t t = 0; t + 2 < dr.indices.size(); t += 3) {
                std::map<int, float> acc;
                int vdom[3];
                for (int c = 0; c < 3; ++c) {
                    const Mm3d::Vertex& v = dr.vertices[dr.indices[t + c]];
                    std::map<int, float> own;
                    for (int k = 0; k < 4; ++k) {
                        if (v.bone[k] < 0 || v.weight[k] <= 0) {
                            continue;
                        }
                        auto it = boneToLimb.find(rigged && resolved.count(v.bone[k]) ? resolved[v.bone[k]] : v.bone[k]);
                        if (it == boneToLimb.end()) {
                            continue;
                        }
                        const auto aliased = rigSpec.driveLimb.find(it->first);
                        const int limb = aliased != rigSpec.driveLimb.end() ? aliased->second
                                                                            : NearestDrawnLimb(limbs, slots, it->second);
                        if (limb < 0) {
                            continue;
                        }
                        if (!limbToBone.count(limb)) {
                            throw std::runtime_error("rig: limb " + std::to_string(limb) + " draws but has no bone");
                        }
                        acc[limb] += v.weight[k];
                        own[limb] += v.weight[k];
                    }
                    if (own.empty()) {
                        throw std::runtime_error("vertex without a usable bone");
                    }
                    vdom[c] = std::max_element(own.begin(), own.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
                }
                const int owner = std::max_element(acc.begin(), acc.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
                Tri tri;
                tri.material = dr.material;
                for (int c = 0; c < 3; ++c) {
                    const Mm3d::Vertex& v = dr.vertices[dr.indices[t + c]];
                    const int drive = vdom[c]; // flex-weld: every vertex follows its own dominant limb
                    const int bone = limbToBone.at(drive);
                    const Affine& a = inv[bone];
                    const auto& s = shift[bone];
                    TriVert& o = tri.v[c];
                    o.drive = drive;
                    float vp[3] = { v.pos[0] + v.normal[0] * pushOut, v.pos[1] + v.normal[1] * pushOut,
                                    v.pos[2] + v.normal[2] * pushOut };
                    if (scaleSpec.pivotBone >= 0) {
                        float d[3], along = 0;
                        for (int k = 0; k < 3; ++k) {
                            d[k] = vp[k] - scaleC[k];
                            along += d[k] * scaleU[k];
                        }
                        for (int k = 0; k < 3; ++k) {
                            vp[k] = scaleC[k] + scaleSpec.along * along * scaleU[k] + scaleSpec.across * (d[k] - along * scaleU[k]);
                        }
                    }
                    for (int k = 0; k < 3; ++k) {
                        o.p[k] = (float)(a.r[k * 3] * vp[0] + a.r[k * 3 + 1] * vp[1] + a.r[k * 3 + 2] * vp[2] + a.t[k] + s[k]);
                        o.n[k] = (float)(a.r[k * 3] * v.normal[0] + a.r[k * 3 + 1] * v.normal[1] + a.r[k * 3 + 2] * v.normal[2]);
                    }
                    const float len = std::sqrt(o.n[0] * o.n[0] + o.n[1] * o.n[1] + o.n[2] * o.n[2]);
                    for (int k = 0; k < 3; ++k) {
                        o.n[k] = len > 1e-9f ? o.n[k] / len : (k == 1 ? 1.0f : 0.0f);
                    }
                    o.uv[0] = v.uv[0];
                    o.uv[1] = v.uv[1];
                }
                res.stretched += (vdom[0] != vdom[1] || vdom[1] != vdom[2]);
                ++res.tris;
                perLimb[owner].push_back(tri);
            }
        }

        // faces: the eye and mouth materials read a segment; the 3DS frames replace the N64 state textures
        struct FaceUse {
            FacePart part;
            std::vector<Mm3d::Image> frames;
            int material = -1;
        };
        std::map<int, int> faceSeg; // material -> segment
        std::vector<FaceUse> faces;
        if (gar && e.face && e.face[0]) {
            for (FacePart& fp : ParseFace(e.face)) {
                const std::vector<uint8_t>* raw = FindGarFile(*gar, fp.cmab);
                FaceUse fu;
                std::string cerr;
                if (!raw || !Mm3d::ParseCmabFrames(*raw, fu.frames, cerr)) {
                    continue; // no animation: the face keeps its first frame
                }
                int bestTex = -1;
                double bestDiff = 1e30;
                const Mm3d::Image& f0 = fu.frames[0];
                for (size_t ti = 0; ti < model.textures.size(); ++ti) {
                    const Mm3d::Image& im = model.textures[ti].image;
                    if (im.width != f0.width || im.height != f0.height || im.rgba.size() != f0.rgba.size()) {
                        continue;
                    }
                    double d = 0;
                    for (size_t i = 0; i < im.rgba.size(); i += 4) {
                        for (int c = 0; c < 3; ++c) {
                            d += std::abs((int)im.rgba[i + c] - (int)f0.rgba[i + c]);
                        }
                    }
                    d /= (double)(im.rgba.size() / 4) * 3.0;
                    if (d < bestDiff) {
                        bestDiff = d, bestTex = (int)ti;
                    }
                }
                if (bestTex < 0 || bestDiff > 40.0) {
                    continue;
                }
                for (size_t mi = 0; mi < model.materials.size(); ++mi) {
                    int ti = -1;
                    for (int j = 0; j < 3 && ti < 0; ++j) {
                        ti = model.materials[mi].texture[j];
                    }
                    if (ti == bestTex && !skip.count((int)mi)) {
                        faceSeg[(int)mi] = fp.seg;
                        if (fu.material < 0) {
                            fu.material = (int)mi;
                        }
                    }
                }
                if (fu.material >= 0) {
                    fu.part = std::move(fp);
                    faces.push_back(std::move(fu));
                }
            }
        }
        for (const FaceUse& fu : faces) {
            for (const auto& st : fu.part.states) {
                if (st.second < 0 || st.second >= (int)fu.frames.size()) {
                    continue;
                }
                Mm3d::Image img = fu.frames[st.second];
                if (upscaler) {
                    Mm3d::Image big;
                    if (upscaler->Upscale4x(img, big)) {
                        img = std::move(big);
                    }
                }
                MapColour(img, model.materials[fu.material], colour);
                write(std::string("mm3d/objects/") + e.object + "/" + st.first, TextureResource(img, 4));
                ++res.textures;
            }
        }

        const std::map<int, Tint> tints = ParseTint(e.tint);

        // textures: one RGBA32 resource per CMB texture, named per skeleton
        const std::string base = std::string("mm3d/objects/") + e.object + "/mm3d_" + e.skeleton;
        std::map<int, std::string> texName;
        std::map<int, std::array<int, 2>> texDeclared;
        std::set<int> written;
        for (size_t mi = 0; mi < model.materials.size(); ++mi) {
            const Mm3d::Material& m = model.materials[mi];
            int ti = -1;
            for (int j = 0; j < 3 && ti < 0; ++j) {
                ti = m.texture[j];
            }
            if (skip.count((int)mi) || ti < 0 || ti >= (int)model.textures.size() || model.textures[ti].image.rgba.empty()) {
                continue; // a prop's texture is not needed
            }
            texName[(int)mi] = base + "_tex" + std::to_string(ti);
            DeclaredSize(model.textures[ti].image.width, model.textures[ti].image.height, texDeclared[(int)mi][0],
                         texDeclared[(int)mi][1]);
            if (!faceSeg.count((int)mi) && written.insert(ti).second) {
                Mm3d::Image img = model.textures[ti].image;
                if (upscaler) { // 4x; the display list keeps the declared size of the original, so the UVs still fit
                    Mm3d::Image big;
                    if (upscaler->Upscale4x(img, big)) {
                        img = std::move(big);
                    }
                }
                MapColour(img, m, colour);
                for (const auto& tk : tints) { // a tint keyed by any material that uses this texture applies to it
                    if (tk.first >= 0 && tk.first < (int)model.materials.size()) {
                        const Mm3d::Material& tm = model.materials[tk.first];
                        int tti = -1;
                        for (int j = 0; j < 3 && tti < 0; ++j) {
                            tti = tm.texture[j];
                        }
                        if (tti == ti) {
                            ApplyTint(img, tk.second);
                        }
                    }
                }
                write(texName[(int)mi], TextureResource(img, ClassOfTexture(model.textures[ti].name, m, e.matcls)));
                ++res.textures;
            }
        }

        for (size_t li = 0; li < limbs.size(); ++li) {
            const Limb& limb = limbs[li];
            if (limb.dl.empty() || skipSpec.keepLimbs.count((int)li)) { // kept limbs keep their N64 display lists
                continue;
            }
            ++res.limbs;
            const std::string dlName = "mm3d/" + limb.dl;
            if (perLimb[li].empty()) { // keep the N64 mesh from showing through
                Dl empty;
                write(dlName, empty.Resource());
                if (!limb.dl2.empty()) {
                    write("mm3d/" + limb.dl2, empty.Resource());
                }
                continue;
            }
            const std::string vtxName = base + "_limb" + std::to_string(li) + "_vtx";
            const uint64_t vtxHash = Crc64(vtxName);
            Dl dl;
            dl.Ex(0x33000000, 0xBEEFBEEF, Crc64(limb.dl)); // marker, keyed on the original display list
            dl.W(0xD7000002, 0xFFFFFFFF);
            dl.W(0xE7000000, 0);
            dl.W(0xFC127E60, 0xFFFFF3F8); // (TEXEL0 * SHADE) * PRIMITIVE, as the vanilla limb display lists
            dl.W(0xFA000080, 0xFFFFFFFF); // white primitive colour: without it the model draws black
            dl.W(0xE3001001, 0);
            std::vector<std::array<uint8_t, 16>> verts;
            std::map<int, std::vector<const Tri*>> groups;
            for (const Tri& t : perLimb[li]) {
                if (texName.count(t.material)) {
                    groups[t.material].push_back(&t);
                } else {
                    ++res.dropped;
                }
            }
            std::vector<int> order;
            for (auto& g : groups) {
                order.push_back(g.first);
            }
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                return model.materials[a].alpha < model.materials[b].alpha;
            });
            using Key = std::array<int, 10>;
            const auto mtxLoad = [&](int drive) { dl.W(0xDA380003, 0x0D000001u | (uint32_t)(slots.at(drive) * 0x40)); };
            for (int m : order) {
                const Mm3d::Material& mat = model.materials[m];
                const int dw = texDeclared[m][0], dh = texDeclared[m][1];
                const uint32_t cull = mat.cull == 1 ? 0x400 : (mat.cull == 2 ? 0x200 : 0);
                const bool unlit = skipSpec.unlit.count(m) != 0 || (!mat.fragmentLighting && !mat.vertexLighting);
                dl.W(0xD9000000, ((unlit ? 0x00210405u : 0x00230405u) & ~0x400u) | cull); // unlit: no G_LIGHTING, white vertex colours
                dl.W(0xE200001C, mat.alpha == Mm3d::Material::Opaque ? 0xC8112078u
                                 : mat.alpha == Mm3d::Material::Cutout ? 0xC81049D8u : 0xC8113078u);
                dl.Tex(faceSeg.count(m) ? 0 : Crc64(texName[m]), faceSeg.count(m) ? faceSeg[m] : -1, dw, dh, mat.wrapS[0],
                       mat.wrapT[0]);

                std::map<Key, std::pair<int, std::array<uint8_t, 16>>> cache; // key -> (drive limb, packed vertex)
                std::vector<std::array<Key, 3>> batch;
                auto flush = [&]() {
                    if (batch.empty()) {
                        cache.clear();
                        return;
                    }
                    std::vector<Key> keys;
                    for (auto& kv : cache) {
                        keys.push_back(kv.first);
                    }
                    std::stable_sort(keys.begin(), keys.end(), [&](const Key& a, const Key& b) { // the owner's vertices first
                        const int da = cache[a].first, db = cache[b].first;
                        const int ra = da != (int)li, rb = db != (int)li;
                        return ra != rb ? ra < rb : da < db;
                    });
                    std::map<Key, int> slotOf;
                    for (size_t i = 0; i < keys.size(); ++i) {
                        slotOf[keys[i]] = (int)i;
                    }
                    size_t run = 0;
                    bool switched = false;
                    while (run < keys.size()) {
                        const int drive = cache[keys[run]].first;
                        size_t end = run;
                        while (end < keys.size() && cache[keys[end]].first == drive) {
                            ++end;
                        }
                        if (drive != (int)li) {
                            mtxLoad(drive);
                            switched = true;
                            ++res.switches;
                        }
                        dl.Vtx((int)(end - run), (int)run, (uint32_t)((verts.size() + run) * 16), vtxHash);
                        run = end;
                    }
                    if (switched) {
                        mtxLoad((int)li);
                    }
                    for (const Key& k : keys) {
                        verts.push_back(cache[k].second);
                    }
                    std::vector<std::array<int, 3>> tris;
                    for (const auto& t : batch) {
                        tris.push_back({ slotOf[t[0]], slotOf[t[1]], slotOf[t[2]] });
                    }
                    for (size_t i = 0; i + 1 < tris.size(); i += 2) {
                        dl.Tri2(tris[i], tris[i + 1]);
                    }
                    if (tris.size() % 2) {
                        dl.Tri1(tris.back());
                    }
                    cache.clear();
                    batch.clear();
                };
                for (const Tri* t : groups[m]) {
                    std::array<Key, 3> ks;
                    std::array<std::array<uint8_t, 16>, 3> packed;
                    for (int c = 0; c < 3; ++c) {
                        const TriVert& v = t->v[c];
                        ks[c] = { (int)std::lround(v.p[0] * 10), (int)std::lround(v.p[1] * 10), (int)std::lround(v.p[2] * 10),
                                  (int)std::lround(v.n[0] * 100), (int)std::lround(v.n[1] * 100), (int)std::lround(v.n[2] * 100),
                                  (int)std::lround(v.uv[0] * 1000), (int)std::lround(v.uv[1] * 1000), v.drive, 0 };
                        int16_t pos[3];
                        for (int k = 0; k < 3; ++k) {
                            const long r = std::lround(v.p[k]);
                            if (r < -32768 || r > 32767) {
                                throw std::runtime_error("vertex outside the N64 s16 range");
                            }
                            pos[k] = (int16_t)r;
                        }
                        const auto clamp16 = [](long x) { return (int16_t)std::clamp(x, -32768L, 32767L); };
                        const int16_t s = clamp16(std::lround(v.uv[0] * dw * 32)), tt = clamp16(std::lround((1.0f - v.uv[1]) * dh * 32));
                        std::array<uint8_t, 16>& o = packed[c];
                        std::memcpy(&o[0], pos, 6);
                        o[6] = o[7] = 0;
                        std::memcpy(&o[8], &s, 2);
                        std::memcpy(&o[10], &tt, 2);
                        for (int k = 0; k < 3; ++k) {
                            o[12 + k] = unlit ? 255 : (uint8_t)(int8_t)std::clamp((int)std::lround(v.n[k] * 127), -128, 127);
                        }
                        o[15] = 255;
                    }
                    std::set<Key> fresh;
                    for (int c = 0; c < 3; ++c) {
                        if (!cache.count(ks[c])) {
                            fresh.insert(ks[c]);
                        }
                    }
                    if (cache.size() + fresh.size() > 32) {
                        flush();
                    }
                    for (int c = 0; c < 3; ++c) {
                        cache.emplace(ks[c], std::make_pair(t->v[c].drive, packed[c]));
                    }
                    batch.push_back(ks);
                }
                flush();
            }
            const std::vector<uint8_t> dlBytes = dl.Resource();
            write(dlName, dlBytes);
            if (!limb.dl2.empty()) { // LOD limbs: the far display list gets the same model
                write("mm3d/" + limb.dl2, dlBytes);
            }
            write(vtxName, VtxArray(verts));
            res.verts += (uint32_t)verts.size();
            ++res.written;
        }
        res.ok = true;
    } catch (const std::exception& ex) {
        res.error = ex.what();
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

// ------------------------------------------------------------------ the dump
fs::path ActorFile(const std::string& dump, const std::string& archive) {
    return fs::path(dump) / "actors" / (archive + ".gar.lzs");
}

// Signature: converter version, the table, the N64 archive and the dump's actor files (size and mtime).
std::string Signature(const std::string& dump) {
    uint64_t h = Fnv1a(std::string(kConverterVersion), 14695981039346656037ULL);
    const ColourParams cp = GetColourParams();
    h = Fnv1a(&cp, sizeof(cp), h);
    {
        SpanUpscaler up;
        const uint64_t fp = UpscaleWanted() && up.Load(UpscalerPath()) ? up.Fingerprint() : 0;
        h = Fnv1a(&fp, sizeof(fp), h);
    }
    std::set<std::string> archives;
    for (const Entry& e : kTable) {
        h = Fnv1a(std::string(e.object) + e.skeleton + e.archive + e.cmb + e.skip + (Enabled(e) ? "1" : "0") + e.rig + e.face + e.matcls + e.tint, h);
        h = Fnv1a(&e.lightRef, sizeof(e.lightRef), h);
        h = Fnv1a(&e.saturation, sizeof(e.saturation), h);
        archives.insert(e.archive);
    }
    std::error_code ec;
    for (const auto& a : archives) {
        const fs::path p = ActorFile(dump, a);
        const uint64_t size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
        const int64_t mtime = size ? fs::last_write_time(p, ec).time_since_epoch().count() : 0;
        h = Fnv1a(&size, sizeof(size), h);
        h = Fnv1a(&mtime, sizeof(mtime), h);
    }
    const auto loaded = Archives()->GetArchives(); // keep the shared list alive for the loop
    for (const auto& a : *loaded) {
        const fs::path p(a->GetPath());
        if (p.filename() == "mm.o2r") {
            const uint64_t size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
            h = Fnv1a(&size, sizeof(size), h);
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
        SPDLOG_ERROR("NewDawn MM3D: cannot move {} to {}: {}", Staging().string(), Live().string(), ec.message());
        return false;
    }
    return true;
}

void Mount(const json& stamp) {
    const fs::path p = Live() / kArchiveName;
    if (fs::exists(p)) {
        Archives()->AddArchive(p.generic_string());
    }
    std::lock_guard<std::mutex> lock(gMutex);
    gMounted = true;
    gConverted = stamp.value("converted", 0);
    gFailed = stamp.value("failed", 0);
    gEnabled = stamp.value("enabled", 0);
    gSeconds = stamp.value("seconds", 0.0);
    gState = State::Ready;
    gLinkForms = 0;
    for (const json& e : stamp.value("entries", json::array())) {
        if (!e.value("ok", false) || !e.value("reviewed", false)) {
            continue;
        }
        const std::string o = e.value("object", "");
        // Player forms: 0 Fierce Deity, 1 Goron, 2 Zora, 3 Deku, 4 human.
        gLinkForms |= o == "object_link_boy" ? 1 : o == "object_link_goron" ? 2 : o == "object_link_zora" ? 4 : o == "object_link_nuts" ? 8 :
                      o == "object_link_child" ? 16 : 0;
    }
}

// ------------------------------------------------------------------ the generator
void Generate(bool boot) {
    const auto t0 = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& why) {
        SPDLOG_ERROR("NewDawn MM3D: {}", why);
        SetState(State::Failed, why);
        gRunning = false;
    };
    try {
        const std::string dump = DumpPath();
        if (!DumpValid(dump)) {
            SetState(State::NoDump);
            gRunning = false;
            return;
        }
        std::error_code ec;
        fs::remove_all(Staging(), ec);
        fs::create_directories(Staging(), ec);
        ZipWriter zip;
        if (!zip.Open(Staging() / kArchiveName)) {
            return fail("cannot write " + (Staging() / kArchiveName).string());
        }
        gTotal = (uint32_t)kTableCount;

        // The game's own resources are read on this thread; the workers only use memory.
        struct Job {
            const Entry* e;
            std::vector<Limb> limbs;
            std::string error;
        };
        std::vector<Job> jobs(kTableCount);
        for (size_t i = 0; i < kTableCount; ++i) {
            jobs[i].e = &kTable[i];
            ReadSkeleton(std::string("objects/") + kTable[i].object + "/" + kTable[i].skeleton, jobs[i].limbs, jobs[i].error);
        }

        std::mutex garMutex;
        std::map<std::string, std::shared_ptr<Mm3d::Gar>> gars;
        std::map<std::string, std::string> garErrors;
        auto garFor = [&](const std::string& archive, std::string& err) -> std::shared_ptr<Mm3d::Gar> {
            std::lock_guard<std::mutex> lock(garMutex);
            auto it = gars.find(archive);
            if (it != gars.end()) {
                err = garErrors[archive];
                return it->second;
            }
            std::shared_ptr<Mm3d::Gar> g;
            std::ifstream in(ActorFile(dump, archive), std::ios::binary);
            if (!in) {
                err = "archive missing in the dump";
            } else {
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                g = std::make_shared<Mm3d::Gar>();
                if (!Mm3d::ParseGar(bytes, *g, err)) {
                    g.reset();
                }
            }
            gars[archive] = g;
            garErrors[archive] = err;
            return g;
        };

        const ColourParams colourParams = GetColourParams();
        SpanUpscaler spanUp;
        const SpanUpscaler* upscaler = UpscaleWanted() && spanUp.Load(UpscalerPath()) ? &spanUp : nullptr;
        if (UpscaleWanted() && !upscaler) {
            SPDLOG_WARN("NewDawn MM3D: upscaler weights not found at {}; textures stay at their 3DS size", UpscalerPath());
        }
        std::vector<json> report(kTableCount);
        std::atomic<size_t> next{ 0 };
        std::atomic<uint32_t> okCount{ 0 }, failCount{ 0 }, enabledCount{ 0 };
        std::mutex writeMutex;
        std::set<std::string> names; // duplicate display-list names (shared between skeletons) are written once
        const unsigned workers = boot ? std::max(1u, std::thread::hardware_concurrency()) : std::max(1u, std::thread::hardware_concurrency() / 2);
        auto work = [&]() {
            for (size_t i; (i = next++) < kTableCount;) {
                Job& job = jobs[i];
                const Entry& e = *job.e;
                ColourParams entryColour = colourParams;
                if (e.lightRef > 0) {
                    entryColour.lightRef = e.lightRef;
                }
                if (e.saturation > 0) {
                    entryColour.saturation = e.saturation;
                }
                Result res;
                if (!job.error.empty() || job.limbs.empty()) {
                    res.error = "N64 skeleton: " + job.error;
                } else {
                    std::string err;
                    auto gar = garFor(e.archive, err);
                    Mm3d::Model model;
                    const std::string key = e.cmb; // the entry name inside the GAR, e.g. "model/soldier.cmb"
                    if (!gar) {
                        res.error = err;
                    } else if (!gar->files.count(key)) {
                        res.error = "model missing in the archive";
                    } else if (!Mm3d::ParseCmb(gar->files.at(key), model, err)) {
                        res.error = "CMB: " + err;
                    } else {
                        // Reviewed skeletons are written to the archive; the others convert (and are counted) but stay out.
                        res = Convert(e, job.limbs, model, [&](const std::string& name, const std::vector<uint8_t>& data) {
                            if (!Enabled(e)) {
                                return;
                            }
                            {
                                std::lock_guard<std::mutex> lock(writeMutex);
                                if (!names.insert(name).second) {
                                    return;
                                }
                            }
                            zip.Add(name, data.data(), data.size());
                        }, entryColour, Enabled(e) ? upscaler : nullptr, gar.get());
                    }
                }
                (res.ok ? okCount : failCount)++;
                if (res.ok && Enabled(e)) {
                    ++enabledCount;
                }
                report[i] = json{ { "object", e.object }, { "skeleton", e.skeleton }, { "cmb", e.cmb },       { "ok", res.ok },
                              { "reviewed", Enabled(e) }, { "error", res.error },  { "triangles", res.tris }, { "limbs", res.limbs },
                              { "stretched", res.stretched }, { "textures", res.textures }, { "dropped", res.dropped },
                              { "seconds", res.seconds } };
                ++gDone;
            }
        };
        std::vector<std::thread> pool;
        for (unsigned i = 0; i < workers; ++i) {
            pool.emplace_back(work);
        }
        for (auto& t : pool) {
            t.join();
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        json stamp = { { "signature", Signature(dump) }, { "converter_version", kConverterVersion }, { "state", "ok" },
                       { "converted", okCount.load() }, { "failed", failCount.load() }, { "enabled", enabledCount.load() },
                       { "seconds", seconds }, { "entries", report } };
        const std::string manifest = stamp.dump(1);
        zip.Add("mm3d-characters.json", reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size());
        if (!zip.Close()) {
            return fail("cannot finish the archive");
        }
        WriteStamp(Staging(), stamp);
        SPDLOG_INFO("NewDawn MM3D: converted {} of {} skeletons ({} enabled, {} failed) in {:.1f}s", okCount.load(), kTableCount,
                    enabledCount.load(), failCount.load(), seconds);
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gStagedOk = true;
            gConverted = okCount;
            gFailed = failCount;
            gEnabled = enabledCount;
            gSeconds = seconds;
            gState = State::Ready;
            gPendingRestart = !boot;
        }
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    gRunning = false;
}

} // namespace

// ------------------------------------------------------------------ public API
std::string DumpPath() {
    const char* p = CVarGetString("gNewDawn.MM3D.RomFS", "");
    return p ? p : "";
}

bool DumpValid(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(ActorFile(path, "zelda2_sdn"), ec);
}

bool BootNeedsGeneration() {
    std::error_code ec;
    fs::create_directories(Root(), ec);
    const std::string dump = DumpPath();
    if (!DumpValid(dump)) {
        SetState(State::NoDump);
        return false;
    }
    const std::string signature = Signature(dump);
    json staged = ReadStamp(Staging());
    if (staged.is_object()) {
        if (staged.value("signature", "") == signature && staged.value("converter_version", "") == kConverterVersion) {
            SPDLOG_INFO("NewDawn MM3D: using the models converted last session");
            Promote();
        } else {
            fs::remove_all(Staging(), ec);
        }
    } else if (fs::exists(Staging())) {
        fs::remove_all(Staging(), ec); // interrupted run
    }
    json stamp = ReadStamp(Live());
    if (stamp.is_object() && stamp.value("signature", "") == signature &&
        stamp.value("converter_version", "") == kConverterVersion && stamp.value("state", "") == "ok" &&
        fs::exists(Live() / kArchiveName)) {
        Mount(stamp);
        SPDLOG_INFO("NewDawn MM3D: mounted the cache ({} converted, {} enabled)", gConverted, gEnabled);
        return false;
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
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (!gStagedOk) {
            return;
        }
    }
    if (Promote()) {
        const json stamp = ReadStamp(Live());
        if (stamp.value("state", "") == "ok") {
            Mount(stamp);
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
    char buf[320];
    switch (p.state) {
        case State::Running:
            snprintf(buf, sizeof(buf), "Converting 3DS character models... %u of %u skeletons", p.done, p.total);
            return buf;
        case State::Ready:
            if (p.pendingRestart) {
                snprintf(buf, sizeof(buf), "Converted %u skeletons (%u ready to use, %u failed) in %.0fs. Restart the game to use them.",
                         gConverted, gEnabled, gFailed, gSeconds);
            } else {
                snprintf(buf, sizeof(buf), "%u 3DS character models ready (%u skeletons converted, %u failed; built in %.0fs).",
                         gEnabled, gConverted, gFailed, gSeconds);
            }
            return buf;
        case State::NoDump:
            return "No 3DS RomFS set. Choose the folder that contains \"actors\" (decrypted Majora's Mask 3D RomFS from your own game).";
        case State::Failed:
            return "Conversion failed: " + gMessage;
        default:
            return "Not converted yet. Press Convert (or restart the game).";
    }
}

void Regenerate() {
    std::error_code ec;
    fs::remove_all(Staging(), ec);
    StartGeneration(false);
}

bool Available() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gMounted;
}

void ApplyToggle() {
    const bool want = Available() && CVarGetInteger("gNewDawn.MM3D.Characters", 1) != 0;
    if (want == gOverlayOn) {
        return;
    }
    gOverlayOn = want;
    Ship::Context::GetRawInstance()->GetResourceManager()->SetOverlayEnabled(want);
    gfx_texture_cache_clear();
    SOH::SkeletonPatcher::UpdateSkeletons();
}

bool LinkFormConverted(int form) {
    return gOverlayOn && form >= 0 && form < 5 && (gLinkForms & (1u << form)) != 0;
}

} // namespace NewDawnMm3d

// Masks that sit on the head are placed for the N64 head and cap. The 3DS head is shifted back and its cap is bigger, so a
// mask drawn with the head limb's matrix as is ends up inside the cap. Returns 1 and the extra transform (x, y, z, scale in
// head limb units, applied about the limb origin before the mask) when the form's 3DS model is on. The first rows are measured
// from the two heads (docs/MM3D_CHARACTERS.md, "Masks"); gNewDawn.MM3D.MaskFit.{X,Y,Z,S} override every mask while tuning.
extern "C" int NewDawnMm3d_MaskFit(int form, int mask, float* out) {
    if (!NewDawnMm3d::LinkFormConverted(form)) {
        return 0;
    }
    struct Fit {
        int form, mask;
        float x, y, z, s;
    };
    static const Fit kFits[] = {
        { 4, 15, 0.0f, 0.0f, 0.0f, 1.18f }, // Captain's Hat on human Link: 18% larger, so the crest clears the 3DS cap (tuned in game against the N64)
    };
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    for (const Fit& f : kFits) {
        if (f.form == form && f.mask == mask) {
            out[0] = f.x, out[1] = f.y, out[2] = f.z, out[3] = f.s;
        }
    }
    if (CVarGetInteger("gNewDawn.MM3D.MaskFit.On", 0) != 0) {
        out[0] = CVarGetFloat("gNewDawn.MM3D.MaskFit.X", out[0]);
        out[1] = CVarGetFloat("gNewDawn.MM3D.MaskFit.Y", out[1]);
        out[2] = CVarGetFloat("gNewDawn.MM3D.MaskFit.Z", out[2]);
        out[3] = CVarGetFloat("gNewDawn.MM3D.MaskFit.S", out[3]);
    }
    return out[0] != 0.0f || out[1] != 0.0f || out[2] != 0.0f || out[3] != 1.0f;
}
