#pragma once
// New Dawn look profiles, game side (docs/LOOK_PLAN.md): loads the profile files, resolves the look of the current game
// frame from read-only game state, and lets the look editor change and save profiles. Nothing here writes game state.

#include <fast/newdawn_look.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct PlayState;

namespace NewDawn::Look {

using Fields = std::map<int, float>; // field id -> value: only what a profile states itself

struct Profile {
    std::string name;
    std::string inherits; // "" = the root for the scene's kind of light ("outdoor" or "interior")
    struct Scene {
        int id;
        std::string when; // "" or a story flag, "!" in front for its negation (see Flag() in the .cpp)
    };
    std::vector<Scene> scenes;
    Fields base;                   // constant values
    std::map<float, Fields> clock; // hour (0..24) -> values; scenes lit by the clock. Keys merge with the parents'.
    std::map<int, Fields> lights;  // the game's light-setting index -> values; scenes with fixed light settings
    Fields overcast, dread;        // where full rain or snow, and full dread, move the look
    bool dirty = false;            // changed in the editor, not saved
};

// Everything the look depends on. Resolve() reads it from the game; the self-check passes its own.
struct Inputs {
    int scene = -1;
    bool clockLit = true;
    float hour = 12.0f;
    int lightA = 0, lightB = 0; // previous and current light setting
    float lightBlend = 1.0f;    // 0 = A, 1 = B
    float overcast = 0.0f, dread = 0.0f;
    std::function<bool(const std::string&)> flag; // story flag by name
};

// The profiles that apply to these inputs, root first (indices into Profiles()); empty when none are loaded.
std::vector<int> Chain(const Inputs& in);
// The look for these inputs, before easing.
void Evaluate(const Inputs& in, float out[NEWDAWN_LOOK_COUNT]);
// Dread of the three-day cycle (docs/LOOK_BIBLE.md section 5) from the day (1..3) and the hour.
float Dread(int day, float hour);

// The look of this game frame, eased; on = false when profiles are off or none could be loaded.
const Fast::NewDawnLookFrame& Resolve(PlayState* play);
// What Resolve() last used (for the editor).
const Inputs& LastInputs();
// A scene's name as the profile files write it.
const char* SceneName(int id);

std::vector<Profile>& Profiles();
void Reload();
// Writes the profile as JSON into gNewDawn.Look.SavePath (default "newdawn/looks"); files there shadow the shipped
// ones.
bool Save(Profile& profile, std::string* error);
// Logic check of the resolver on built-in data; true when it holds. Run with gNewDawn.Look.SelfCheck=1.
bool SelfCheck();

} // namespace NewDawn::Look
