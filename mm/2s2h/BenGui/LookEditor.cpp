#include "LookEditor.h"
#include "2s2h/NewDawn/NewDawnLook.h"

#include <libultraship/libultraship.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <string>

extern "C" {
#include "z64.h"
#include "variables.h"
extern PlayState* gPlayState;
}

using namespace NewDawn::Look;

namespace {

enum Target { TARGET_BASE, TARGET_CLOCK, TARGET_LIGHT, TARGET_OVERCAST, TARGET_DREAD };

int sChainPick = -1; // which profile of the chain is edited; -1 = the most specific
int sTarget = -1;    // -1 = the scene's natural block (a clock key outdoors, the base otherwise)
float sKeyHour = 12.0f;
int sLight = 0;
std::string sStatus;

void SetCvar(const char* name, int value) {
    CVarSetInteger(name, value);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void SetCvar(const char* name, float value) {
    CVarSetFloat(name, value);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

// A preview input that only the look resolver reads: -1 follows the game.
void Preview(const char* label, const char* cvar, float live) {
    float value = CVarGetFloat(cvar, -1.0f);
    bool on = value >= 0.0f;
    ImGui::PushID(cvar);
    if (ImGui::Checkbox("##preview", &on)) {
        SetCvar(cvar, on ? live : -1.0f);
        value = on ? live : -1.0f;
    }
    ImGui::SameLine();
    float shown = on ? value : live;
    ImGui::BeginDisabled(!on);
    if (ImGui::SliderFloat(label, &shown, 0.0f, 1.0f, on ? "%.2f (preview)" : "%.2f (game)")) {
        SetCvar(cvar, shown);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

} // namespace

void LookEditorWindow::DrawElement() {
    bool profilesOn = CVarGetInteger("gNewDawn.Look.Profiles", 0) != 0;
    if (ImGui::Checkbox("Look profiles", &profilesOn)) {
        SetCvar("gNewDawn.Look.Profiles", profilesOn ? 1 : 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload files")) {
        Reload();
        sStatus = "Reloaded; unsaved changes dropped.";
    }
    std::vector<Profile>& profiles = Profiles();
    ImGui::SameLine();
    ImGui::Text("%d profiles", (int)profiles.size());
    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
    if (gPlayState == nullptr || profiles.empty()) {
        ImGui::TextWrapped(profiles.empty() ? "No profile files found under <gNewDawn.Assets.Path>/looks."
                                            : "Enter a scene to see and edit its look.");
        return;
    }
    const Fast::NewDawnLookFrame& frame = Resolve(gPlayState);
    if (!frame.on) {
        ImGui::TextWrapped(
            "Profiles are off (they also need the Modern Lighting Pipeline). The game is using its cvars "
            "and built-in tables.");
        return;
    }

    // What decided this frame.
    const Inputs& in = LastInputs();
    ImGui::SeparatorText("This frame");
    const int minutes = (int)(in.hour * 60.0f);
    if (in.clockLit) {
        ImGui::Text("%s, lit by the clock, %02d:%02d", SceneName(in.scene), minutes / 60, minutes % 60);
    } else {
        ImGui::Text("%s, fixed light setting %d > %d (%.0f%%)", SceneName(in.scene), in.lightA, in.lightB,
                    in.lightBlend * 100.0f);
    }
    Preview("Overcast", "gNewDawn.Look.PreviewOvercast", in.overcast);
    Preview("Dread", "gNewDawn.Look.PreviewDread", in.dread);

    const std::vector<int> chain = Chain(in);
    if (chain.empty()) {
        return;
    }
    ImGui::SeparatorText("Profile");
    if (sChainPick >= (int)chain.size()) {
        sChainPick = -1;
    }
    for (size_t i = 0; i < chain.size(); i++) {
        const Profile& p = profiles[chain[i]];
        if (i > 0) {
            ImGui::SameLine();
            ImGui::TextUnformatted(">");
            ImGui::SameLine();
        }
        const bool picked = (sChainPick < 0 ? chain.size() - 1 : (size_t)sChainPick) == i;
        if (ImGui::RadioButton((p.name + (p.dirty ? " *" : "")).c_str(), picked)) {
            sChainPick = (int)i;
        }
    }
    Profile& profile = profiles[chain[sChainPick < 0 ? chain.size() - 1 : sChainPick]];

    // Which block of the profile the sliders write.
    int target = sTarget >= 0 ? sTarget : in.clockLit ? TARGET_CLOCK : TARGET_BASE;
    ImGui::RadioButton("Base", &target, TARGET_BASE);
    ImGui::SameLine();
    ImGui::BeginDisabled(!in.clockLit);
    ImGui::RadioButton("Clock key", &target, TARGET_CLOCK);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(in.clockLit);
    ImGui::RadioButton("Light setting", &target, TARGET_LIGHT);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::RadioButton("Overcast", &target, TARGET_OVERCAST);
    ImGui::SameLine();
    ImGui::RadioButton("Dread", &target, TARGET_DREAD);
    sTarget = target;

    Fields* fields = &profile.base;
    if (target == TARGET_CLOCK) {
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Key hour", &sKeyHour, 0.0f, 23.5f, "%.1f");
        sKeyHour = std::fmod(std::round(sKeyHour * 2.0f) / 2.0f, 24.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("Now")) {
            sKeyHour = std::fmod(std::round(in.hour * 2.0f) / 2.0f, 24.0f);
        }
        for (int p : chain) { // the hours that already have keys, in this profile and its parents
            for (const auto& [hour, unused] : profiles[p].clock) {
                char label[24];
                snprintf(label, sizeof(label), "%g##key%d", hour, p);
                ImGui::SameLine();
                if (ImGui::SmallButton(label)) {
                    sKeyHour = hour;
                }
            }
        }
        const auto key = profile.clock.find(sKeyHour);
        static Fields none;
        fields = key != profile.clock.end() ? &key->second : &none;
    } else if (target == TARGET_LIGHT) {
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("Light setting", &sLight);
        ImGui::SameLine();
        if (ImGui::SmallButton("Current")) {
            sLight = in.lightB;
        }
        const auto light = profile.lights.find(sLight);
        static Fields none;
        fields = light != profile.lights.end() ? &light->second : &none;
    } else if (target == TARGET_OVERCAST) {
        fields = &profile.overcast;
    } else if (target == TARGET_DREAD) {
        fields = &profile.dread;
    }
    // A slider that is moved states its field in the chosen block (creating the key when needed).
    const auto state = [&](int id, float value) {
        Fields& block = target == TARGET_CLOCK   ? profile.clock[sKeyHour]
                        : target == TARGET_LIGHT ? profile.lights[sLight]
                                                 : *fields;
        block[id] = value;
        profile.dirty = true;
    };

    ImGui::SeparatorText("Fields (ticked = stated in this block)");
    std::string group;
    bool open = false;
    for (int id = 0; id < NEWDAWN_LOOK_COUNT; id++) {
        const Fast::NewDawnLookField& info = Fast::NewDawnLookFieldInfo(id);
        const std::string name = info.name;
        const size_t dot = name.find('.');
        if (name.substr(0, dot) != group) {
            group = name.substr(0, dot);
            open = ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        }
        if (!open) {
            continue;
        }
        ImGui::PushID(id);
        const auto it = fields->find(id);
        bool stated = it != fields->end();
        float value = stated ? it->second : frame.v[id];
        if (ImGui::Checkbox("##stated", &stated)) {
            if (stated) {
                state(id, value);
            } else if (it != fields->end()) {
                fields->erase(it);
                profile.dirty = true;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(name.c_str() + dot + 1, &value, info.lo, info.hi, "%.3f")) {
            state(id, value);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("now %.3f", frame.v[id]);
        if (info.cvar != nullptr && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The player's %s stays a %s on top.", info.cvar, info.offset ? "shift" : "multiplier");
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!profile.dirty);
    if (ImGui::Button(("Save " + profile.name + ".json").c_str())) {
        std::string error;
        sStatus = NewDawn::Look::Save(profile, &error)
                      ? "Saved " + profile.name + ".json to " + CVarGetString("gNewDawn.Look.SavePath", "newdawn/looks")
                      : "Save failed: " + error;
    }
    ImGui::EndDisabled();
}
