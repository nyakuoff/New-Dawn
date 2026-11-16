// New Dawn texture synthesis (ND-016): a C++ port of the round-2 spike (tools/newdawn/stylelib/pbr.py + core.py).
// Art-directs the shipped CC0 base library (assets/newdawn/materials/) into a replacement for one original texture:
// our periodic cell layout (matched to the original's UV scale), chipped hand-hewn edges, crevice grime, gravity streaks,
// edge wear and moss, recoloured to the palette of the player's own original. Pure functions, no engine dependency.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace NewDawnSynth {

using Field = std::vector<float>;

// Luminance-ordered colour ramp (256 entries) sampled from an original texture: the colour identity of the replacement.
struct Palette {
    float lut[256][3];
    static Palette FromRgba(const uint8_t* rgba, size_t pixelCount, float sat = 1.12f);
    static Palette Gray();
};

// Numeric and string art-direction parameters of one recipe (the JSON of recipes.json / textures.json, flattened).
struct Params {
    std::unordered_map<std::string, double> num;
    std::unordered_map<std::string, std::string> str;
    // The original texture's luminance (0..1), used for "native_mottle": its own large tonal patches ride on the generated tone map.
    std::shared_ptr<const std::vector<float>> native;
    int nativeW = 0, nativeH = 0;
    double Get(const char* k, double def) const {
        auto it = num.find(k);
        return it == num.end() ? def : it->second;
    }
    std::string Str(const char* k, const char* def) const {
        auto it = str.find(k);
        return it == str.end() ? std::string(def) : it->second;
    }
};

// One CC0 material of the base library at a given resolution, as float fields (see Library).
struct Surface {
    int px = 0;
    Field rgb;              // 3*px*px, 0..1
    Field fine, disp, rough, ao; // px*px (fine: pores and grain only, soft-clipped unit std)
    Field nx, ny;           // px*px, -1..1 (GL, +Y up)
};

// Loads <dir>/<id>.{albedo,normal,rough,height}.jpg lazily, thread safe, one Surface per (id, px).
class Library {
  public:
    // One manifest entry (assets/newdawn/materials/manifest.json): the library grows by adding entries, not code.
    struct Entry {
        std::string id, cls;
        std::vector<std::string> roles, regions; // regions: scene groups the set suits ("any" = all)
    };
    explicit Library(std::string dir, std::vector<Entry> entries = {}) : mDir(std::move(dir)), mEntries(std::move(entries)) {}
    // "role:face" / "class:rock" (several with "|": "class:dirt|class:mud") pick one material of the manifest by seed: sets tagged for
    // `region` first, then "any", then any other; a plain id is returned as it is. Empty when nothing matches.
    std::string Resolve(const std::string& selector, uint32_t seed, const std::string& region = "") const;
    // Never returns null: a missing material is replaced by a neutral grey surface (and reported once via Missing()).
    std::shared_ptr<const Surface> Get(const std::string& id, int px);
    std::vector<std::string> Missing();

  private:
    static constexpr size_t kBudgetBytes = 768ull << 20;
    std::string mDir;
    std::vector<Entry> mEntries;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const Surface>> mCache;
    std::vector<std::string> mMissing;
};

// The generated material. alb: 3*w*h (0..1); height in px of this tile; normal: xy of the unit normal (2*w*h) or empty
// (derive from height).
struct Mat {
    bool valid = true; // false: a material selector found nothing (the texture is left native)
    int w = 0, h = 0;
    Field alb, height, rough, normal;
    float relief = .3f, nstrength = .8f;
};

// Periodicity of an original texture (luminance, w*h): rows and columns of bricks or boards by circular autocorrelation of
// the edge profiles. The scores are 0..1; callers use the counts only above a score of about .5 (rows) and .25 (columns).
struct Layout {
    int rows = 1, cols = 1;
    float rowScore = 0, colScore = 0;
};
Layout MeasureLayout(const std::vector<float>& luminance, int w, int h);

// gen: "masonry" (brick, ashlar, cobble layouts) or "direct" (one CC0 set tiled onto the tile). Returns false on bad input.
bool Generate(const std::string& gen, Library& lib, const Palette& pal, const Params& p, int w, int h, uint32_t seed, const std::string& region, Mat& out);

// Encodes the albedo as RGBA8 and the companion maps (materials.py convention: _n = normal xy, _r = roughness + relief)
// at half resolution of the albedo.
struct Encoded {
    int w = 0, h = 0;     // albedo
    int mw = 0, mh = 0;   // maps
    std::vector<uint8_t> rgba, normal, rough;
};
Encoded Encode(const Mat& m);

} // namespace NewDawnSynth
