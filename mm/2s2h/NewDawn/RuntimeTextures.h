// New Dawn remastered textures (ND-016, docs/TEXTURE_PIPELINE.md).
//
// Replaces textures by art-directed CC0 materials instead of upscaling them: on the player's machine, first launch, every
// texture of the classification tables is regenerated from the shipped base library (assets/newdawn/materials/), recoloured
// to the palette of the player's own N64 original and laid out at the original's UV scale. Output (never committed or
// shipped): <app dir>/cache/newdawn-textures/live/newdawn-textures.o2r with alt/<resource> albedos and their _n/_r maps.
// The archive is mounted after every mod; the "Remastered textures" switch (gNewDawn.Textures.Remastered) lets the
// resource manager use it without turning on the player's alternate-asset (mod) switch.
#pragma once

#include <cstdint>
#include <string>

namespace NewDawnTextures {

enum class State { None, Running, Ready, NoSource, Failed, Disabled };

struct Progress {
    State state = State::None;
    uint32_t done = 0;
    uint32_t total = 0;
    bool mounted = false;
    bool pendingRestart = false;
};

// Boot, after every mod archive is mounted. gNewDawn.Textures.Generate=0 (developer switch, default 1) skips everything.
// Promotes a finished staging directory, then mounts the cached archive when it matches this install. Returns true when
// it must be (re)generated first.
bool BootNeedsGeneration();
void StartGeneration(bool boot);
bool IsRunning();
void FinishBoot();
Progress GetProgress();
std::string StatusText();
// Settings button: drop the stamp and regenerate in the background; applied on the next launch.
void Regenerate();
// Every frame: follows gNewDawn.Textures.Remastered (and whether the archive is mounted).
void ApplyToggle();
// Texture count of the mounted archive (0 when none) and its generation time, for the menu.
uint32_t MountedCount();

} // namespace NewDawnTextures
