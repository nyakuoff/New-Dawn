#include "NewDawnPresets.h"

#include "BenGui.hpp"
#include "Notification.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <cmath>

namespace NewDawn {

namespace {

// One row per cvar, one column per tier: Ultra, High, Medium, Low, Original. "Original" is the N64 path: every
// New Dawn feature off, so the renderer produces what plain 2Ship produced.
struct Row {
    const char* name;
    bool isFloat;
    float v[static_cast<int>(Tier::Count)];
};

constexpr bool I = false;
constexpr bool F = true;

// clang-format off
const Row kRows[] = {
    //                                                   Ultra  High   Med    Low    Original
    // Resolution, anti-aliasing and frame pacing.
    { "gSettings.InternalResolution",                  F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gSettings.MSAAValue",                           I, { 4,     1,     2,     1,     1 } },
    { "gNewDawn.TAA.Enabled",                          I, { 1,     1,     0,     0,     0 } },
    { "gMatchRefreshRate",                             I, { 1,     1,     1,     1,     0 } },
    // Neural rendering is the player's own switch and in no look (gNewDawn.Neural.Enabled is not here). These are
    // the settings it runs with when they turn it on: measured to hold 60 fps on a Radeon RX 7800 XT.
    { "gNewDawn.Neural.Height",                        I, { 540,   540,   540,   540,   540 } },
    { "gNewDawn.Neural.Interleave",                    I, { 2,     2,     2,     2,     2 } },
    // Textures. Asset mods (gEnhancements.Mods.AlternateAssets) are the player's own choice and never part of a look.
    { "gNewDawn.Textures.Remastered",                  I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Textures.AsyncAltLoad",                I, { 1,     1,     1,     1,     1 } },
    { "gNewDawn.Textures.Mipmaps",                     I, { 1,     1,     1,     1,     1 } },
    { "gNewDawn.Textures.Anisotropy",                  I, { 16,    16,    8,     4,     16 } },
    { "gNewDawn.Textures.AlphaToCoverage",             I, { 1,     1,     1,     0,     1 } },
    { "gNewDawn.Textures.LodBias",                     F, { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    // Scene post-processing.
    { "gNewDawn.PostFx.Enabled",                       I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.PostFx.AO.Enabled",                    I, { 1,     1,     1,     0,     1 } },
    { "gNewDawn.PostFx.AO.Strength",                   F, { 1.2f,  1.2f,  1.2f,  1.2f,  1.4f } },
    { "gNewDawn.PostFx.AO.Radius",                     F, { 40.0f, 40.0f, 40.0f, 40.0f, 40.0f } },
    { "gNewDawn.PostFx.Bloom.Enabled",                 I, { 1,     1,     1,     1,     1 } },
    { "gNewDawn.PostFx.Bloom.Strength",                F, { 0.18f, 0.18f, 0.18f, 0.18f, 0.25f } },
    { "gNewDawn.PostFx.Bloom.Threshold",               F, { 1.0f,  1.0f,  1.0f,  1.0f,  0.90f } },
    { "gNewDawn.PostFx.Grade.Enabled",                 I, { 1,     1,     1,     1,     1 } },
    { "gNewDawn.PostFx.Grade.Contrast",                F, { 1.08f, 1.08f, 1.08f, 1.08f, 1.06f } },
    { "gNewDawn.PostFx.Grade.Saturation",              F, { 1.12f, 1.12f, 1.12f, 1.12f, 1.08f } },
    { "gNewDawn.Grade.ArtDirection",                   I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Grade.ArtStrength",                    F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Look.Profiles",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.PostFx.Debug.View",                    I, { 0,     0,     0,     0,     0 } },
    { "gNewDawn.PostFx.Dither.Enabled",                I, { 1,     1,     1,     1,     0 } },
    // Modern (linear) lighting pipeline and display transform; SDR output.
    { "gNewDawn.HDR.Enabled",                          I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.HDR.Exposure",                         F, { 0.05f, 0.05f, 0.05f, 0.05f, -0.2f } },
    { "gNewDawn.HDR.SunIntensity",                     F, { 1.2f,  1.2f,  1.2f,  1.2f,  1.8f } },
    { "gNewDawn.HDR.AmbientFill",                      F, { 0.72f, 0.72f, 0.72f, 0.72f, 0.4f } },
    { "gNewDawn.HDR.SkyIntensity",                     F, { 0.85f, 0.85f, 0.85f, 0.85f, 1.0f } },
    { "gNewDawn.HDR.FogIntensity",                     F, { 0.85f, 0.85f, 0.85f, 0.85f, 1.0f } },
    // Sky.
    { "gNewDawn.Sky.Physical",                         I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Sky.Flare",                            I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Sky.Strength",                         F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Sky.Ambient",                          I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Sky.Reflections",                      I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Sky.Special",                          I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Sky.Night",                            I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Sky.Moonlight",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Sky.Clouds",                           I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Sky.Coverage",                         F, { 0.55f, 0.55f, 0.55f, 0.55f, 0.55f } },
    { "gNewDawn.Sky.CloudStrength",                    F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    // Atmosphere and effects (render-only).
    { "gNewDawn.Atmos.Particles",                      I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Atmos.Weather",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Atmos.Wind",                           I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Vfx.Enabled",                          I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Vfx.Sparkles",                         I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Vfx.Frost",                            I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Vfx.BubblePop",                        I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Vfx.MaskFlash",                        I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Grass.Enabled",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Grass.Density",                        F, { 1.0f,  0.8f,  0.5f,  0.3f,  1.0f } },
    { "gNewDawn.Grass.Distance",                       F, { 1000.0f, 850.0f, 650.0f, 450.0f, 1000.0f } },
    { "gNewDawn.Grass.Bend",                           I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Grass.Flowers",                        I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Foliage.Light",                        I, { 1,     1,     1,     0,     0 } },
    // Lighting and materials.
    { "gNewDawn.Lighting.PerPixel",                    I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Lighting.PointLightQuality",           F, { 0.5f,  0.5f,  0.5f,  0.25f, 0.0f } },
    { "gNewDawn.Lighting.Characters",                  I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Characters.LightNeutral",              F, { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f } },
    { "gNewDawn.Characters.LightShoulder",             F, { 0.5f,  0.5f,  0.5f,  0.0f,  0.0f } },
    { "gNewDawn.Characters.LightWrap",                 F, { 0.35f, 0.35f, 0.35f, 0.0f,  0.0f } },
    { "gNewDawn.Characters.GradeRelief",               F, { 0.8f,  0.8f,  0.8f,  0.0f,  0.0f } },
    { "gNewDawn.Characters.Materials",                 F, { 0.8f,  0.8f,  0.8f,  0.0f,  0.0f } },
    { "gNewDawn.Characters.Relief",                    F, { 0.6f,  0.6f,  0.0f,  0.0f,  0.0f } },
    { "gNewDawn.Characters.Smooth",                    I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Characters.SmoothStrength",            F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Lighting.Local",                       I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Lighting.FlameLight",                  I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Interior.Windows",                     I, { 0,     0,     0,     0,     0 } },
    { "gNewDawn.Interior.Emissive",                    I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Materials.Enabled",                    I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Materials.Strength",                   F, { 1.5f,  1.5f,  1.5f,  1.5f,  1.0f } },
    { "gNewDawn.Materials.Parallax",                   I, { 1,     0,     0,     0,     0 } },
    // Shadows, fog, shafts, GI.
    { "gNewDawn.Shadows.Enabled",                      I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Shadows.Strength",                     F, { 0.85f, 0.85f, 0.85f, 0.85f, 1.0f } },
    { "gNewDawn.Shadows.Floor",                        F, { 0.30f, 0.30f, 0.30f, 0.30f, 0.55f } },
    { "gNewDawn.Shadows.Radius",                       F, { 1500.0f, 1500.0f, 1500.0f, 1500.0f, 1500.0f } },
    { "gNewDawn.Shadows.MapSize",                      I, { 4096,  2048,  2048,  1024,  4096 } },
    { "gNewDawn.Shadows.Cascades",                     I, { 4,     3,     2,     1,     1 } },
    { "gNewDawn.Shadows.Foliage",                      I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Shadows.Coverage",                     I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Shadows.PointLights",                  I, { 2,     1,     0,     0,     0 } },
    { "gNewDawn.Fog.Volumetric",                       I, { 1,     0,     0,     0,     0 } },
    { "gNewDawn.SunShafts.Enabled",                    I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.SunShafts.Mode",                       I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.SunShafts.Strength",                   F, { 0.30f, 0.30f, 0.30f, 0.30f, 0.4f } },
    { "gNewDawn.GI.Enabled",                           I, { 1,     0,     0,     0,     0 } },
    { "gNewDawn.GI.World",                             I, { 2,     1,     0,     0,     0 } },
    // Water.
    { "gNewDawn.Water.Enabled",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Water.Strength",                       F, { 0.5f,  0.5f,  0.5f,  0.5f,  0.5f } },
    { "gNewDawn.Water.Reflections",                    I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Water.ReflectionStrength",             F, { 0.7f,  0.7f,  0.7f,  0.7f,  0.7f } },
    { "gNewDawn.Water.Foam",                           I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Water.FoamStrength",                   F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Water.Refraction",                     I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Water.RefractionStrength",             F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Water.Restyle",                        I, { 1,     1,     1,     1,     0 } },
    { "gNewDawn.Water.Caustics",                       I, { 1,     0,     0,     0,     0 } },
    { "gNewDawn.Water.Underwater",                     I, { 1,     1,     1,     0,     0 } },
    { "gNewDawn.Water.Waves",                          I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Water.WaveStrength",                   F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Water.Ripples",                        I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Water.RippleStrength",                 F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Water.Rivers",                         I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Water.RiverStrength",                  F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
    { "gNewDawn.Water.Waterfalls",                     I, { 1,     1,     0,     0,     0 } },
    { "gNewDawn.Water.FallStrength",                   F, { 1.0f,  1.0f,  1.0f,  1.0f,  1.0f } },
};
// clang-format on

constexpr const char* kSetupVersionCvar = "gNewDawn.Setup.Version";
constexpr const char* kLookCvar = "gNewDawn.Setup.Look";

} // namespace

const char* TierName(Tier tier) {
    static const char* names[] = { "Ultra", "High", "Medium", "Low", "Original look" };
    return names[static_cast<int>(tier)];
}

const char* TierBlurb(Tier tier) {
    // Costs: mean GPU ms per frame over eight heavy scenes at 1080p on a Radeon RX 7800 XT (docs/PERFORMANCE.md).
    static const char* blurbs[] = {
        "Everything on: world and screen-space bounce light, volumetric fog and clouds, 4 shadow cascades, caustics, "
        "dense grass, 4x MSAA with temporal AA. About 4.6 ms of GPU time per frame (reference card).",
        "Ultra without screen-space bounce light, volumetric fog, caustics and parallax; lighter shadows and grass; "
        "temporal AA instead of MSAA. About 2.8 ms, 61% of Ultra.",
        "Modern lighting, sky, shadows (2 cascades), ambient occlusion, water foam and refraction, "
        "grass and effects; no bounce light, fog, clouds or temporal AA; 2x MSAA. About 1.7 ms, 38% of Ultra.",
        "The core remaster look: modern lighting and tonemap, physical sky, per-pixel lighting, materials, one shadow "
        "cascade, local lights. About 1.3 ms, 29% of Ultra (the floor of that look).",
        "Every New Dawn effect off. The game renders exactly like the N64 original path. About 0.2 ms.",
    };
    return blurbs[static_cast<int>(tier)];
}

void ApplyTier(Tier tier, bool notify) {
    const int col = static_cast<int>(tier);
    for (const Row& row : kRows) {
        if (row.isFloat) {
            CVarSetFloat(row.name, row.v[col]);
        } else {
            CVarSetInteger(row.name, static_cast<int32_t>(row.v[col]));
        }
    }
    CVarSetInteger(kLookCvar, col);
    // The window keeps its own copy of these two, which only the widget callbacks normally refresh.
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    window->SetResolutionMultiplier(CVarGetFloat("gSettings.InternalResolution", 1.0f));
    window->SetMsaaLevel(CVarGetInteger("gSettings.MSAAValue", 1));
    CVarSave();
    if (notify) {
        Notification::Emit({ .prefix = "New Dawn", .message = std::string(TierName(tier)) + " look applied." });
    }
}

int DetectTier() {
    // Reads ~70 cvars per tier; callers cache the result.
    for (int t = 0; t < static_cast<int>(Tier::Count); t++) {
        bool match = true;
        for (const Row& row : kRows) {
            if (row.isFloat) {
                if (fabsf(CVarGetFloat(row.name, row.v[static_cast<int>(Tier::Original)]) - row.v[t]) > 0.001f) {
                    match = false;
                    break;
                }
            } else if (CVarGetInteger(row.name, static_cast<int32_t>(row.v[static_cast<int>(Tier::Original)])) !=
                       static_cast<int32_t>(row.v[t])) {
                match = false;
                break;
            }
        }
        if (match) {
            return t;
        }
    }
    return -1;
}

void InitFirstRun(bool configExisted) {
    if (CVarGetInteger(kSetupVersionCvar, 0) >= 1) {
        return;
    }
    CVarSetInteger(kSetupVersionCvar, 1);
    if (!configExisted) {
        ApplyTier(Tier::Ultra, false);
        // A game-style menu should be playable from the pad.
        CVarSetInteger(CVAR_IMGUI_CONTROLLER_NAV, 1);
        CVarSave();
        Notification::Emit({ .prefix = "New Dawn", .message = "Welcome. The Ultra look is on; change it in Esc > Graphics." });
        return;
    }
    CVarSave();
    if (CVarGetInteger("gNewDawn.HDR.Enabled", 0) != 0) {
        return; // the New Dawn look is already set up in this config
    }
    BenGui::RegisterPopup(
        "Welcome to New Dawn",
        "New Dawn found your existing settings and left them as they are.\n\n"
        "Would you like to switch on the New Dawn look (the Ultra look: modern lighting, sky, shadows, water,\n"
        "grass and effects)? Only graphics settings change. You can pick another look any time in\n"
        "Esc > Graphics, including \"Original look\".",
        "Apply the New Dawn look", "Keep my settings", []() { ApplyTier(Tier::Ultra); },
        []() { CVarSetInteger(kLookCvar, -1); CVarSave(); });
}

} // namespace NewDawn
