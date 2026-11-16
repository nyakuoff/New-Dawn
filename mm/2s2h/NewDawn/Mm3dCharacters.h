// 3DS character models (docs/MM3D_CHARACTERS.md, ND-015): the player's own Majora's Mask 3D (3DS) NPC and enemy models,
// converted on their machine to display lists for the N64 skeleton, so the original animations drive them.
//
//   <app dir>/cache/newdawn-mm3d/live/mm3d-characters.o2r   generated, never committed or shipped
//
// The archive stores everything under "mm3d/" and is enabled by its own switch (gNewDawn.MM3D.Characters) through
// the resource manager's overlay prefix; it does not depend on gEnhancements.Mods.AlternateAssets and is not a mod.
#pragma once

#include <cstdint>
#include <string>

namespace NewDawnMm3d {

enum class State { None, Running, Ready, NoDump, Failed, Disabled };

struct Progress {
    State state = State::None;
    uint32_t done = 0;
    uint32_t total = 0;
    bool mounted = false;        // the cached archive is part of this session's archive set
    bool pendingRestart = false; // a background regeneration finished; it is mounted on the next launch
};

// gNewDawn.MM3D.RomFS: the folder holding the decrypted 3DS RomFS ("actors/" inside it). Empty when not set.
std::string DumpPath();
// True when DumpPath() holds a usable RomFS (actors/zelda2_sdn.gar.lzs exists).
bool DumpValid(const std::string& path);

// Boot, after every mod archive is mounted. Promotes a finished background run, then mounts the cache when it matches
// this install. Returns true when the models must be (re)generated first (dump present, cache missing or stale).
bool BootNeedsGeneration();
void StartGeneration(bool boot);
bool IsRunning();
// Boot only: join the generator and mount its output.
void FinishBoot();
Progress GetProgress();
std::string StatusText();
// Settings button: convert again in the background; mounted on the next launch.
void Regenerate();
// Per frame: switch the overlay on or off from gNewDawn.MM3D.Characters (needs a mounted archive).
void ApplyToggle();
// True when the switch can do anything (archive mounted).
bool Available();

} // namespace NewDawnMm3d
