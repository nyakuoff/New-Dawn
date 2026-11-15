// New Dawn runtime material maps and texture recolour (docs/RUNTIME_MATERIALS.md, ND-008).
//
// Both are built on the player's machine from the HD textures that are actually loaded, never shipped:
//   newdawn-recolor.o2r    offline recolour of muddy/garish environment albedos (recipes: recolor.json)
//   newdawn-materials.o2r  normal / roughness / relief maps for ~2,200 world textures (materials.json +
//                          material-classification.json)
// They are cached under <app dir>/cache/newdawn-materials/live and mounted after every mod, so they win
// over the HD pack they were derived from. They are not mods and do not depend on the AlternateAssets toggle.
#pragma once

#include <cstdint>
#include <string>

namespace NewDawnMaterials {

enum class State { None, Running, Ready, NoSource, Failed, Disabled };

struct Progress {
    State state = State::None;
    uint32_t done = 0;
    uint32_t total = 0;
    bool mounted = false; // the cached archives are part of this session's archive set
    bool pendingRestart = false;
};

// Boot, after every mod archive is mounted. gNewDawn.Materials.Generate=0 (developer switch, default 1) skips
// everything, e.g. to test with hand-built archives from the Python tools. Promotes a finished staging directory, then
// mounts the cached archives when they match this install. Returns true when they must be (re)generated first.
bool BootNeedsGeneration();
// Starts generation on background threads. boot = all cores and mount when done (Finish);
// otherwise half the cores, result applied on the next launch.
void StartGeneration(bool boot);
bool IsRunning();
// Boot only: join the generator and mount its output.
void FinishBoot();
Progress GetProgress();
std::string StatusText();
// Settings button: drop the stamp and regenerate in the background; applied on the next launch.
void Regenerate();

} // namespace NewDawnMaterials
