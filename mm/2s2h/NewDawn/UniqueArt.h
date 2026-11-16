// Unique painted art (murals, signs, emblems, carvings, faces) cannot be replaced by a generic material, so ND-016 upscales it
// instead: from the player's own Majora's Mask 3D texture when the classification found a counterpart that passes a coarse
// content test, otherwise from the N64 original. The result is graded to sit with the CC0 materials around it.
// Everything here runs on the player's machine; nothing from either game is ever committed or shipped (ND-008).
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Mm3dFormats.h"
#include "SpanUpscale.h"
#include "TextureSynth.h"

namespace NewDawnUnique {

// Textures of one MM3D scene, read lazily from <RomFS>/scenes/<scene>_info.zsi and its room files (the CMB models inside
// them), by the name the classification's mm3d_match.json uses. Thread safe.
class Mm3dScenes {
  public:
    explicit Mm3dScenes(std::string romfs) : mRoot(std::move(romfs)) {}
    const Mm3d::Image* Find(const std::string& scene, const std::string& name);
    uint32_t SceneCount() const {
        return mLoaded;
    }

  private:
    struct Scene {
        std::map<std::string, Mm3d::Image> textures;
    };
    std::string mRoot;
    std::mutex mMutex;
    std::map<std::string, std::unique_ptr<Scene>> mScenes;
    uint32_t mLoaded = 0;
};

// The 3DS rooms are atlases and redrawn, so a name match alone is not enough. The original must be fully opaque (a cut-out keeps its
// shape only through its own alpha) and the candidate must have its aspect (3 %), be 2x to 4x its size, have a mean colour within
// 45 levels per channel and the same coarse layout (8x8 box means within 36 levels on average). Tuned on the 15 confident matches of
// South Clock Town, Stock Pot Inn and Termina Field (hand-checked): exactly the three correct ones pass; see docs/TEXTURE_PIPELINE.md.
bool AcceptCandidate(const Mm3d::Image& original, const Mm3d::Image& candidate);

struct Options {
    const NewDawnMm3d::SpanUpscaler* span = nullptr;
    const NewDawnSynth::Surface* grain = nullptr; // a CC0 surface whose `fine` field adds a faint painted-surface grain
    float grainAmount = .035f;
    int maxLongSide = 1024;
};

// Upscales `source` 4x with SPAN (wrapping or replicating each edge as the texture tiles or not) and grades the result against
// `original` (the N64 texture): colour statistics follow it (fully when the source is a 3DS texture), a faint contrast and
// saturation lift and the grain match the CC0 materials. `out` has the source's aspect; false when SPAN is unavailable.
bool UpscaleAndGrade(const Mm3d::Image& source, const Mm3d::Image& original, bool fromMm3d, const Options& opt, Mm3d::Image& out);

} // namespace NewDawnUnique
