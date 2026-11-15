#ifndef NEWDAWN_PRESETS_H
#define NEWDAWN_PRESETS_H

#include <cstddef>

// New Dawn quality tiers and the first-run setup. The table of per-tier values lives in NewDawnPresets.cpp and is
// documented in docs/MENU.md. Graphics settings only: gameplay, cheats, HUD and widescreen settings are never touched.
namespace NewDawn {

enum class Tier { Ultra, High, Medium, Low, Original, Count };

const char* TierName(Tier tier);
const char* TierBlurb(Tier tier);

// Writes every tier cvar, updates the window's own copies of resolution/MSAA and saves the config.
void ApplyTier(Tier tier, bool notify = true);
// The tier whose values match the current cvars exactly, or -1 when the settings are custom.
int DetectTier();

// Call once after the GUI exists. A missing config file means a first run: the Ultra look is applied. An existing
// config from plain 2Ship is left alone and the player is asked once whether to apply it.
void InitFirstRun(bool configExisted);


} // namespace NewDawn

#endif // NEWDAWN_PRESETS_H
