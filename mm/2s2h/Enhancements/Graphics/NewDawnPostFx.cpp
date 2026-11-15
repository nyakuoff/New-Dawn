#include "NewDawnPostFx.h"
#include "NewDawnAtmos.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/newdawn_postfx.h>
#include <fast/newdawn_wind.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <string>

extern "C" {
#include "global.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Fall/z_en_fall.h"
s32 Environment_IsSceneUpsideDown(PlayState* play);
}

static float NewDawnClampF(float value, float low, float high) {
    return (value < low) ? low : ((value > high) ? high : value);
}

// Unit vector towards the sun. The game stores the light direction as s8 (steps of 1/120, about 0.5 degrees), so the
// stored value jumps and the whole shadow set teleports with it (a 0.5 degree step moves a far shadow by ~60 units).
// Under time-of-day lighting the game derives it from the clock with the formula in Environment_UpdateLights; evaluate
// the same formula without the s8 truncation. If the stored value disagrees (blending, overrides) it is used as is.
// Read-only: nothing is written back to the game.
static bool NewDawnSunDirection(PlayState* play, float out[3]) {
    const LightDirectional* sun = &play->envCtx.dirLight1.params.dir;
    float v[3] = { (float)sun->x, (float)sun->y, (float)sun->z };
    if (CVarGetInteger("gNewDawn.Shadows.SmoothSun", 1) && play->envCtx.lightMode == LIGHT_MODE_TIME &&
        play->envCtx.lightSettingOverride == LIGHT_SETTING_OVERRIDE_NONE) {
        u16 time = CURRENT_TIME + (Environment_IsSceneUpsideDown(play) ? CLOCK_TIME(12, 0) : 0);
        s16 angle = (u16)(time - CLOCK_TIME(12, 0));
        float exact[3] = { -(Math_SinS(angle) * 120.0f), Math_CosS(angle) * 120.0f, Math_CosS(angle) * 20.0f };
        if (fabsf(exact[0] - v[0]) < 1.01f && fabsf(exact[1] - v[1]) < 1.01f && fabsf(exact[2] - v[2]) < 1.01f) {
            v[0] = exact[0], v[1] = exact[1], v[2] = exact[2];
        }
    }
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 0.001f) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        out[i] = v[i] / len;
    }
    return true;
}

// The visible giant Moon (En_Fall): read-only. Its scale grows with the day and on the final day it descends, so using
// the actor's own position follows the game's Moon state exactly. Returns the unit direction from the eye, and the
// angular radius of its disc in radians. Scenes without a Moon actor return false.
static bool NewDawnFindMoon(PlayState* play, float dir[3], float* angularRadius) {
    bool found = false;
    float best = 0.0f;
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first; actor; actor = actor->next) {
        if (actor->id != ACTOR_EN_FALL) {
            continue;
        }
        switch (EN_FALL_TYPE(actor)) {
            case EN_FALL_TYPE_TERMINA_FIELD_MOON:
            case EN_FALL_TYPE_TITLE_SCREEN_MOON:
            case EN_FALL_TYPE_CRASHING_MOON:
            case EN_FALL_TYPE_LODMOON_NO_LERP:
            case EN_FALL_TYPE_LODMOON:
            case EN_FALL_TYPE_STOPPED_MOON_CLOSED_MOUTH:
            case EN_FALL_TYPE_CLOCK_TOWER_MOON:
            case EN_FALL_TYPE_STOPPED_MOON_OPEN_MOUTH:
            case EN_FALL_TYPE_LODMOON_INVERTED_STONE_TOWER:
                break;
            default:
                continue;
        }
        const float dx = actor->world.pos.x - play->view.eye.x, dy = actor->world.pos.y - play->view.eye.y,
                    dz = actor->world.pos.z - play->view.eye.z;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < 1.0f || actor->scale.x <= 0.0f) {
            continue;
        }
        const float radius = atanf(2200.0f * actor->scale.x / dist);
        if (!found || radius > best) {
            best = radius;
            dir[0] = dx / dist;
            dir[1] = dy / dist;
            dir[2] = dz / dist;
            found = true;
        }
    }
    *angularRadius = best;
    return found;
}

static void NewDawnFillShadowParams(PlayState* play, NewDawnPostFxParams* params) {
    params->shadowEnabled = 0;
    params->shadowStrength = 0.0f;
    params->shadowRadius = 1500.0f;
    params->shadowMapSize = 4096;

    if (!CVarGetInteger("gNewDawn.Shadows.Enabled", 0) || play->envCtx.sunDisabled ||
        play->envCtx.lightMode != LIGHT_MODE_TIME) {
        return;
    }

    const LightDirectional* sun = &play->envCtx.dirLight1.params.dir;
    Vec3f dir;
    if (!NewDawnSunDirection(play, &dir.x)) {
        return;
    }

    // Fade out at night and close to the horizon; the Moon takes over below (gNewDawn.Sky.Moonlight).
    float horizonFade = NewDawnClampF((dir.y - 0.03f) / 0.25f, 0.0f, 1.0f);
    float luminance = (sun->color[0] + sun->color[1] + sun->color[2]) / (3.0f * 255.0f);
    float colorFade = NewDawnClampF(luminance * 3.0f, 0.0f, 1.0f);
    float fade = horizonFade * colorFade;
    // Moonlight: when the sun is down the visible Moon becomes the shadow light. Cool, soft shadows and a faint cool
    // lift on the lit side ground silhouettes without washing out the dark night. Time-lit outdoor scenes only; fades
    // in as the sun sinks and with the Moon's elevation.
    bool moonLit = false;
    float moonFade = 0.0f;
    if (fade <= 0.001f && CVarGetInteger("gNewDawn.Sky.Moonlight", 0) && CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        const float dark = NewDawnClampF((-dir.y - 0.03f) / 0.27f, 0.0f, 1.0f);
        moonFade = dark * dark * (3.0f - 2.0f * dark) * NewDawnClampF((-dir.y - 0.05f) / 0.25f, 0.0f, 1.0f);
        // Aim at the visible Moon when the scene has one, otherwise opposite the sun (the game's own moon light).
        float moonDir[3] = { -dir.x, -dir.y, -dir.z }, moonRadius;
        NewDawnFindMoon(play, moonDir, &moonRadius);
        moonFade *= NewDawnClampF((moonDir[1] - 0.05f) / 0.25f, 0.0f, 1.0f);
        // Shadows from a Moon straight overhead would hide under their casters: lean the light to at most about 53
        // degrees, keeping its azimuth (a stylised, longer, more readable shadow). The visible Moon is not moved.
        if (moonDir[1] > 0.80f) {
            const float h = sqrtf(moonDir[0] * moonDir[0] + moonDir[2] * moonDir[2]);
            const float k = h > 1.0e-4f ? 0.60f / h : 0.0f;
            moonDir[0] = h > 1.0e-4f ? moonDir[0] * k : 0.60f;
            moonDir[2] = moonDir[2] * k;
            moonDir[1] = 0.80f;
        }
        if (moonFade > 0.001f) {
            moonLit = true;
            dir.x = moonDir[0];
            dir.y = moonDir[1];
            dir.z = moonDir[2];
            fade = moonFade * 0.8f;
        }
    }
    float strength = NewDawnClampF(CVarGetFloat("gNewDawn.Shadows.Strength", 1.0f), 0.0f, 1.0f) * fade;
    // Sun shafts ride on the shadow map and the same fades.
    float shaftStrength = 0.0f;
    if (!moonLit && CVarGetInteger("gNewDawn.SunShafts.Enabled", 0)) {
        shaftStrength = NewDawnClampF(CVarGetFloat("gNewDawn.SunShafts.Strength", 0.4f), 0.0f, 1.0f) * fade;
    }
    if (strength <= 0.001f && shaftStrength <= 0.001f) {
        return;
    }

    Vec3f focus;
    Player* player = GET_PLAYER(play);
    if (player != NULL) {
        focus = player->actor.world.pos;
    } else {
        focus = play->view.eye;
    }

    float radius = NewDawnClampF(CVarGetFloat("gNewDawn.Shadows.Radius", 1500.0f), 200.0f, 5000.0f);
    int mapSize = CVarGetInteger("gNewDawn.Shadows.MapSize", 4096);
    mapSize = (int)NewDawnClampF((float)mapSize, 512.0f, 4096.0f);

    params->shaftEnabled = shaftStrength > 0.001f;
    params->shaftStrength = shaftStrength;
    for (int i = 0; i < 3; i++) {
        params->shaftFogColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    params->shadowEnabled = 1;
    params->shadowStrength = strength;
    params->shadowRadius = radius;
    params->shadowMapSize = mapSize;
    // 1 = the single map above; 3-4 split the map into tiles that cover the view out to Distance (view-axis units).
    params->shadowCascades = (int)NewDawnClampF((float)CVarGetInteger("gNewDawn.Shadows.Cascades", 1), 1.0f, 4.0f);
    params->shadowDistance = NewDawnClampF(CVarGetFloat("gNewDawn.Shadows.Distance", 7000.0f), 1000.0f, 12000.0f);
    params->shadowSunDir[0] = dir.x;
    params->shadowSunDir[1] = dir.y;
    params->shadowSunDir[2] = dir.z;
    params->shadowAmbient[0] = play->lightCtx.ambientColor[0] / 255.0f;
    params->shadowAmbient[1] = play->lightCtx.ambientColor[1] / 255.0f;
    params->shadowAmbient[2] = play->lightCtx.ambientColor[2] / 255.0f;
    params->shadowDirColor[0] = sun->color[0] / 255.0f;
    params->shadowDirColor[1] = sun->color[1] / 255.0f;
    params->shadowDirColor[2] = sun->color[2] / 255.0f;
    if (moonLit) {
        params->shadowIsMoon = 1;
        params->shadowDirColor[0] = 0.30f;
        params->shadowDirColor[1] = 0.40f;
        params->shadowDirColor[2] = 0.68f;
    }
    params->shadowFocus[0] = focus.x;
    params->shadowFocus[1] = focus.y;
    params->shadowFocus[2] = focus.z;
    params->shadowCameraEye[0] = play->view.eye.x;
    params->shadowCameraEye[1] = play->view.eye.y;
    params->shadowCameraEye[2] = play->view.eye.z;
}

// Per-scene feel of the water effect: how much of the global strength a scene gets. Scenes not listed get 1.0.
// Poisoned water is meant to look dull and sickly, so it only gets a faint sheen.
struct NewDawnWaterScene {
    s16 sceneId;
    float strength;
    float foam;
    float refraction;
};
static const NewDawnWaterScene sNewDawnWaterScenes[] = {
    { SCENE_20SICHITAI, 0.30f, 0.50f, 0.10f }, // Southern Swamp (poisoned): murky, almost opaque
};

// Water colour palettes for the Restyle option: linear in-scatter colour of deep water and per-channel absorption
// (1/world units; red goes first). Scenes not listed get the generic clear blue-green.
struct NewDawnWaterPalette {
    s16 sceneId;
    float scatter[3];
    float absorb[3];
};
// Retuned for #96: the deep body colour carries little chroma (water takes its colour from depth absorption and the sky it
// reflects, not from a flat tint), so open water reads as a dark, desaturated blue-green and only shallows over a bright
// bed turn teal.
static const NewDawnWaterPalette sNewDawnWaterGeneric = { -1,
                                                          { 0.030f, 0.075f, 0.090f },
                                                          { 1 / 120.0f, 1 / 260.0f, 1 / 330.0f } };
static const NewDawnWaterPalette sNewDawnWaterPalettes[] = {
    // Great Bay and Zora Cape: clear shallows over pale sand turn turquoise (green transmits further than before), deep
    // water goes to a dark grey-blue-green.
    { SCENE_30GYOSON, { 0.030f, 0.078f, 0.094f }, { 1 / 170.0f, 1 / 270.0f, 1 / 340.0f } },
    { SCENE_31MISAKI, { 0.030f, 0.078f, 0.094f }, { 1 / 170.0f, 1 / 270.0f, 1 / 340.0f } },
    // Termina Field, Clock Town, ranch: clear, slightly green.
    { SCENE_00KEIKOKU, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_CLOCKTOWER, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_ICHIBA, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_BACKTOWN, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_TOWN, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_ALLEY, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    { SCENE_F01, { 0.045f, 0.095f, 0.075f }, { 1 / 120.0f, 1 / 320.0f, 1 / 230.0f } },
    // Cleared swamp: tannin brown-green, clear near the surface.
    { SCENE_20SICHITAI2, { 0.070f, 0.090f, 0.045f }, { 1 / 220.0f, 1 / 170.0f, 1 / 60.0f } },
    // Poisoned swamp (and the poison water in Woodfall Temple): sickly purple-magenta, nearly opaque.
    { SCENE_20SICHITAI, { 0.095f, 0.045f, 0.110f }, { 1 / 130.0f, 1 / 70.0f, 1 / 110.0f } },
    { SCENE_MITURIN, { 0.095f, 0.045f, 0.110f }, { 1 / 130.0f, 1 / 70.0f, 1 / 110.0f } },
    // Zora Hall: crystal blue.
    { SCENE_33ZORACITY, { 0.035f, 0.105f, 0.130f }, { 1 / 150.0f, 1 / 330.0f, 1 / 420.0f } },
    // Temples by theme: Great Bay green-teal, Snowhead icy blue, Stone Tower slate.
    { SCENE_SEA, { 0.025f, 0.100f, 0.112f }, { 1 / 90.0f, 1 / 200.0f, 1 / 170.0f } },
    { SCENE_HAKUGIN, { 0.050f, 0.135f, 0.200f }, { 1 / 90.0f, 1 / 300.0f, 1 / 500.0f } },
    { SCENE_INISIE_N, { 0.035f, 0.090f, 0.105f }, { 1 / 90.0f, 1 / 210.0f, 1 / 260.0f } },
    { SCENE_INISIE_R, { 0.035f, 0.090f, 0.105f }, { 1 / 90.0f, 1 / 210.0f, 1 / 260.0f } },
};

// Ocean scenes get the swell (amplitude factor); lakes, ponds, rivers and interiors stay calm.
struct NewDawnOceanScene {
    s16 sceneId;
    float amplitude;
};
static const NewDawnOceanScene sNewDawnOceanScenes[] = {
    { SCENE_30GYOSON, 1.0f }, // Great Bay Coast
    { SCENE_31MISAKI, 0.9f }, // Zora Cape
};

static void NewDawnAddRipple(NewDawnPostFxParams* params, float x, float z, float radius, float strength) {
    if (params->waterRippleCount >= NEWDAWN_MAX_WATER_RIPPLES) {
        return;
    }
    float* out = params->waterRippleSrc[params->waterRippleCount++];
    out[0] = x;
    out[1] = z;
    out[2] = radius;
    out[3] = strength;
}

// Swell parameters (wind from the shared wind, #50) and the read-only ripple sources: Link, other actors in the water and
// the game's own splash/ripple effects. Nothing here writes game state; the sources only feed the GPU height field.
static void NewDawnFillWaveParams(PlayState* play, NewDawnPostFxParams* params, bool restyle) {
    params->waterWaves = 0.0f;
    params->waterRipples = 0.0f;
    params->waterRippleCount = 0;
    if (!restyle) {
        return; // the displaced surface is re-lit by the restyle pass
    }
    const bool waves = CVarGetInteger("gNewDawn.Water.Waves", 0) != 0;
    const bool ripples = CVarGetInteger("gNewDawn.Water.Ripples", 0) != 0;
    if (!waves && !ripples) {
        return;
    }
    if (waves) {
        float ocean = 0.0f;
        for (const auto& entry : sNewDawnOceanScenes) {
            if (entry.sceneId == play->sceneId) {
                ocean = entry.amplitude;
            }
        }
        const Fast::NewDawnWindState wind = Fast::NewDawnWindAt(Fast::NewDawnWindSeconds());
        const float gust = CVarGetInteger("gNewDawn.Atmos.Wind", 0) ? NewDawnClampF((wind.gust - 0.3f) / 1.3f, 0.0f, 1.0f) : 0.5f;
        params->waterWaves = ocean * NewDawnClampF(CVarGetFloat("gNewDawn.Water.WaveStrength", 1.0f), 0.0f, 2.0f) *
                             (0.8f + 0.4f * gust);
        params->waterWaveDir[0] = wind.dirX;
        params->waterWaveDir[1] = wind.dirZ;
        params->waterWaveGust = gust;
    }
    if (!ripples) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }
    params->waterRipples = NewDawnClampF(CVarGetFloat("gNewDawn.Water.RippleStrength", 1.0f), 0.0f, 2.0f);
    params->waterRippleFocus[0] = player->actor.world.pos.x;
    params->waterRippleFocus[1] = player->actor.world.pos.z;
    // An alternating sign turns a held source into outgoing rings rather than a growing mound.
    const float pulse = sinf((float)play->gameplayFrames * (1.0f / 20.0f) * 6.2831853f * 1.7f);
    const Actor* link = &player->actor;
    if (link->depthInWater > 0.0f) {
        const float speed = NewDawnClampF(link->speed / 4.0f, 0.0f, 1.5f);
        NewDawnAddRipple(params, link->world.pos.x, link->world.pos.z, 15.0f, (50.0f + 300.0f * speed) * pulse);
    }
    for (int cat : { ACTORCAT_NPC, ACTORCAT_ENEMY, ACTORCAT_BOSS, ACTORCAT_MISC, ACTORCAT_PROP }) {
        for (Actor* actor = play->actorCtx.actorLists[cat].first; actor != NULL; actor = actor->next) {
            if (actor == link || actor->depthInWater <= 0.0f || actor->speed < 0.8f) {
                continue;
            }
            const float dx = actor->world.pos.x - link->world.pos.x, dz = actor->world.pos.z - link->world.pos.z;
            if (dx * dx + dz * dz > 600.0f * 600.0f) {
                continue;
            }
            NewDawnAddRipple(params, actor->world.pos.x, actor->world.pos.z, 14.0f,
                             (40.0f + 140.0f * NewDawnClampF(actor->speed / 4.0f, 0.0f, 1.5f)) * pulse);
        }
    }
    // The game's own splash (8 frames) and ripple effects: an impulse while they are young.
    const EffectSs* table = EffectSs_GetTable();
    if (table != NULL) {
        for (int i = 0; i < 100; i++) {
            const EffectSs* effect = &table[i];
            if (effect->life <= 0) {
                continue;
            }
            if (effect->type == EFFECT_SS_G_SPLASH && effect->life >= 5) {
                NewDawnAddRipple(params, effect->pos.x, effect->pos.z, 20.0f, 420.0f);
            } else if (effect->type == EFFECT_SS_G_RIPPLE && (effect->regs[11] + 20) - effect->life <= 2) {
                NewDawnAddRipple(params, effect->pos.x, effect->pos.z, 12.0f, 150.0f);
            }
        }
    }
}

static void NewDawnFillWaterParams(PlayState* play, NewDawnPostFxParams* params) {
    params->waterEnabled = 0;
    const bool glints = CVarGetInteger("gNewDawn.Water.Enabled", 0) != 0;
    const bool reflections = CVarGetInteger("gNewDawn.Water.Reflections", 0) != 0 &&
                             CVarGetFloat("gNewDawn.Water.ReflectionStrength", 0.7f) > 0.0f;
    // Screen fades are drawn after the opaque capture. Do not replace them with an unfaded bed.
    const bool surfaceVisible = play->transitionMode == TRANS_MODE_OFF &&
                                play->transitionTrigger == TRANS_TRIGGER_OFF &&
                                !(play->envCtx.fillScreen && play->envCtx.screenFillColor[3] != 0) &&
                                play->worldCoverAlpha == 0 && !R_PLAY_FILL_SCREEN_ON;
    const bool foam = surfaceVisible && CVarGetInteger("gNewDawn.Water.Foam", 0) != 0;
    const bool refraction = surfaceVisible && CVarGetInteger("gNewDawn.Water.Refraction", 0) != 0;
    const bool restyle = surfaceVisible && CVarGetInteger("gNewDawn.Water.Restyle", 0) != 0;
    const bool caustics = CVarGetInteger("gNewDawn.Water.Caustics", 0) != 0 &&
                          CVarGetFloat("gNewDawn.Water.CausticsStrength", 1.0f) > 0.0f;
    const bool underwater = surfaceVisible && CVarGetInteger("gNewDawn.Water.Underwater", 0) != 0 &&
                            CVarGetFloat("gNewDawn.Water.UnderwaterStrength", 1.0f) > 0.0f;
    // Smoothed camera-in-water state: snaps in when the camera dips below a surface and relaxes over a few frames
    // after leaving it, so crossing the surface never pops.
    static float sUnderwaterState = 0.0f;
    static float sUnderwaterSurfaceY = 0.0f;
    static float sUnderwaterRect[4] = {};
    static uint32_t sUnderwaterFrame = UINT32_MAX;
    // Without a time of day there is no sun or moon, so glints alone would only add the faint sky tint: interiors
    // skip the pass unless reflections, foam or refraction are on.
    if ((!glints && !reflections && !foam && !refraction && !restyle && !caustics && !underwater) ||
        play->colCtx.colHeader == NULL ||
        (play->envCtx.lightMode != LIGHT_MODE_TIME && !reflections && !foam && !refraction && !restyle && !caustics &&
         !underwater)) {
        sUnderwaterState = 0.0f;
        return;
    }
    const CollisionHeader* col = play->colCtx.colHeader;
    // Scenes with more boxes than fit (Great Bay Temple has 16 and dungeons can have more) keep the nearest to the
    // camera.
    struct Candidate {
        float distSq;
        const WaterBox* box;
    };
    Candidate candidates[NEWDAWN_MAX_WATER_BOXES];
    int count = 0;
    for (int i = 0; i < col->numWaterBoxes; i++) {
        const WaterBox* box = &col->waterBoxes[i];
        float dx = NewDawnClampF(play->view.eye.x, box->minPos.x, box->minPos.x + box->xLength) - play->view.eye.x;
        float dz = NewDawnClampF(play->view.eye.z, box->minPos.z, box->minPos.z + box->zLength) - play->view.eye.z;
        float distSq = dx * dx + dz * dz;
        if (distSq > play->view.zFar * play->view.zFar) {
            continue; // beyond the far plane, can never be seen
        }
        int slot = count;
        if (count == NEWDAWN_MAX_WATER_BOXES) {
            slot = 0; // replace the farthest
            for (int j = 1; j < count; j++) {
                if (candidates[j].distSq > candidates[slot].distSq) {
                    slot = j;
                }
            }
            if (candidates[slot].distSq <= distSq) {
                continue;
            }
        } else {
            count++;
        }
        candidates[slot] = { distSq, box };
    }
    for (int i = 0; i < count; i++) {
        const WaterBox* box = candidates[i].box;
        float* out = params->waterBoxes[i];
        out[0] = box->minPos.x;
        out[1] = box->minPos.z;
        out[2] = box->minPos.x + box->xLength;
        out[3] = box->minPos.z + box->zLength;
        out[4] = box->minPos.y;
    }
    // Camera below a plane whose XZ rectangle contains it (the highest such surface nearest above the eye).
    bool eyeUnder = false;
    {
        float best = 1.0e9f;
        for (int i = 0; i < count; i++) {
            const float* box = params->waterBoxes[i];
            const float gap = box[4] - play->view.eye.y;
            if (gap > 0.0f && gap < 1500.0f && gap < best && play->view.eye.x >= box[0] && play->view.eye.x <= box[2] &&
                play->view.eye.z >= box[1] && play->view.eye.z <= box[3]) {
                best = gap;
                eyeUnder = true;
                sUnderwaterSurfaceY = box[4];
                for (int j = 0; j < 4; j++) {
                    sUnderwaterRect[j] = box[j];
                }
            }
        }
    }
    if (underwater) {
        // Advance once per game frame so fade-out time does not depend on how often the display list is rebuilt.
        if (sUnderwaterFrame != play->gameplayFrames) {
            sUnderwaterFrame = play->gameplayFrames;
            sUnderwaterState = eyeUnder ? 1.0f : std::max(sUnderwaterState - 0.25f, 0.0f);
        } else if (eyeUnder) {
            sUnderwaterState = 1.0f;
        }
    } else {
        sUnderwaterState = 0.0f;
    }
    if (count == 0 && sUnderwaterState <= 0.0f) {
        return;
    }

    float sceneFactor = 1.0f, foamFactor = 1.0f, refractionFactor = 1.0f;
    for (const auto& entry : sNewDawnWaterScenes) {
        if (entry.sceneId == play->sceneId) {
            sceneFactor = entry.strength;
            foamFactor = entry.foam;
            refractionFactor = entry.refraction;
        }
    }

    // The sun by day, a weak cool light from the opposite side of the sky by night. The glints fade with the same
    // curves as the shadows. Scenes with fixed lighting (no time of day) get the sky tint only.
    float light[3] = { 0.0f, 1.0f, 0.0f };
    float lightColor[3] = { 0.0f, 0.0f, 0.0f };
    float skyScale = 0.5f;
    if (!play->envCtx.sunDisabled && play->envCtx.lightMode == LIGHT_MODE_TIME) {
        const LightDirectional* sun = &play->envCtx.dirLight1.params.dir;
        float len = sqrtf((f32)(sun->x * sun->x + sun->y * sun->y + sun->z * sun->z));
        if (len > 0.001f) {
            float dir[3] = { sun->x / len, sun->y / len, sun->z / len };
            float dayFade = NewDawnClampF((dir[1] - 0.03f) / 0.25f, 0.0f, 1.0f);
            float luminance = (sun->color[0] + sun->color[1] + sun->color[2]) / (3.0f * 255.0f);
            dayFade *= NewDawnClampF(luminance * 3.0f, 0.0f, 1.0f);
            float nightFade = NewDawnClampF((-dir[1] - 0.05f) / 0.25f, 0.0f, 1.0f) * 0.25f;
            if (dayFade > 0.0f) {
                for (int i = 0; i < 3; i++) {
                    light[i] = dir[i];
                    lightColor[i] = (sun->color[i] / 255.0f) * dayFade;
                }
            } else if (nightFade > 0.0f) {
                light[0] = -dir[0];
                light[1] = -dir[1];
                light[2] = -dir[2];
                lightColor[0] = 0.55f * nightFade;
                lightColor[1] = 0.65f * nightFade;
                lightColor[2] = 1.00f * nightFade;
            }
            skyScale = 1.0f;
        }
    }

    params->waterEnabled = 1;
    params->waterReflections = reflections;
    params->waterReflectionStrength =
        reflections ? NewDawnClampF(CVarGetFloat("gNewDawn.Water.ReflectionStrength", 0.7f), 0.0f, 1.0f) * sceneFactor
                    : 0.0f;
    params->waterDebug = CVarGetInteger("gNewDawn.Water.Debug", 0);
    params->waterStrength = NewDawnClampF(CVarGetFloat("gNewDawn.Water.Strength", 0.5f), 0.0f, 1.0f) * sceneFactor *
                            skyScale * (glints ? 1.0f : 0.0f);
    params->waterFoam =
        foam ? NewDawnClampF(CVarGetFloat("gNewDawn.Water.FoamStrength", 1.0f), 0.0f, 1.0f) * foamFactor : 0.0f;
    params->waterRefraction =
        refraction
            ? NewDawnClampF(CVarGetFloat("gNewDawn.Water.RefractionStrength", 1.0f), 0.0f, 1.0f) * refractionFactor
            : 0.0f;
    if (restyle || caustics || underwater) {
        const NewDawnWaterPalette* palette = &sNewDawnWaterGeneric;
        for (const auto& entry : sNewDawnWaterPalettes) {
            if (entry.sceneId == play->sceneId) {
                palette = &entry;
            }
        }
        params->waterRestyle = restyle ? 1.0f : 0.0f;
        for (int i = 0; i < 3; i++) {
            params->waterScatter[i] = palette->scatter[i];
            params->waterAbsorb[i] = palette->absorb[i];
        }
    }
    // Caustic light: the sun (or moon) outdoors, where the sun shadow map applies. Interiors fall back to the scene's
    // first directional light, tilted towards the vertical so a pool still receives a pattern.
    {
        float causticDir[3] = { light[0], light[1], light[2] };
        float causticColor[3] = { lightColor[0], lightColor[1], lightColor[2] };
        params->waterCausticSun = 1;
        if (lightColor[0] + lightColor[1] + lightColor[2] <= 0.0f) {
            params->waterCausticSun = 0;
            const LightDirectional* dir = &play->envCtx.dirLight1.params.dir;
            float len = sqrtf((f32)(dir->x * dir->x + dir->y * dir->y + dir->z * dir->z));
            if (len > 0.001f) {
                float d[3] = { dir->x / len, dir->y / len, dir->z / len };
                d[1] = fabsf(d[1]) * 0.5f + 0.6f;
                float l2 = sqrtf(d[0] * d[0] * 0.25f + d[1] * d[1] + d[2] * d[2] * 0.25f);
                causticDir[0] = d[0] * 0.5f / l2;
                causticDir[1] = d[1] / l2;
                causticDir[2] = d[2] * 0.5f / l2;
                for (int i = 0; i < 3; i++) {
                    causticColor[i] = (dir->color[i] / 255.0f) * 0.6f;
                }
            }
        }
        for (int i = 0; i < 3; i++) {
            params->waterCausticDir[i] = causticDir[i];
            params->waterCausticColor[i] = causticColor[i];
        }
    }
    params->waterCaustics =
        caustics ? NewDawnClampF(CVarGetFloat("gNewDawn.Water.CausticsStrength", 1.0f), 0.0f, 2.0f) : 0.0f;
    params->waterUnderwater =
        underwater ? NewDawnClampF(CVarGetFloat("gNewDawn.Water.UnderwaterStrength", 1.0f), 0.0f, 1.0f) : 0.0f;
    params->waterUnderwaterState = sUnderwaterState;
    params->waterEyeSurfaceY = sUnderwaterSurfaceY;
    for (int i = 0; i < 4; i++) {
        params->waterEyeRect[i] = sUnderwaterRect[i];
    }
    // The native glare is a full-screen XLU fill after the bed capture. Attenuate only our replacement by its
    // transmission so looking into the sun cannot reveal an unglared rectangle of water.
    params->waterSurfaceVisibility = 1.0f - NewDawnClampF(play->envCtx.glareAlpha, 0.0f, 255.0f) / 255.0f;
    params->waterTime = (float)play->gameplayFrames * (1.0f / 20.0f);
    params->waterBoxCount = count;
    for (int i = 0; i < 3; i++) {
        params->waterLightDir[i] = light[i];
        params->waterLightColor[i] = lightColor[i];
        params->waterSkyColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    params->waterCameraEye[0] = play->view.eye.x;
    params->waterCameraEye[1] = play->view.eye.y;
    params->waterCameraEye[2] = play->view.eye.z;
    NewDawnFillWaveParams(play, params, restyle);
}

static void NewDawnFillFogParams(PlayState* play, NewDawnPostFxParams* params) {
    if (!CVarGetInteger("gNewDawn.Fog.Volumetric", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        (GET_ACTIVE_CAM(play)->stateFlags & CAM_STATE_UNDERWATER) || play->lightCtx.fogNear < 0 ||
        play->csCtx.state != CS_STATE_IDLE || Play_InCsMode(play)) {
        return;
    }
    const bool temple = play->sceneId == SCENE_MITURIN && play->envCtx.lightMode == LIGHT_MODE_SETTINGS;
    if (temple) {
        params->fogVolumetric = 2;
        params->fogBaseHeight = -1200.0f;
        params->fogHeight = 800.0f;
        params->fogDensityScale = 0.5f; // was 1.6: the teal medium veiled the whole temple
        float scores[NEWDAWN_FOG_POINT_LIGHTS];
        for (auto& score : scores)
            score = 1e30f;
        for (LightNode* node = play->lightCtx.listHead; node; node = node->next) {
            const LightInfo* info = node->info;
            if (!info || (info->type != LIGHT_POINT_NOGLOW && info->type != LIGHT_POINT_GLOW) ||
                info->params.point.radius <= 0)
                continue;
            const auto& light = info->params.point;
            const float dx = light.x - play->view.eye.x, dy = light.y - play->view.eye.y,
                        dz = light.z - play->view.eye.z;
            const float score = (dx * dx + dy * dy + dz * dz) / (float)(light.radius * light.radius);
            int slot = 0;
            for (int i = 1; i < NEWDAWN_FOG_POINT_LIGHTS; ++i)
                if (scores[i] > scores[slot])
                    slot = i;
            if (score >= scores[slot])
                continue;
            scores[slot] = score;
            params->fogPointPosRadius[slot][0] = light.x;
            params->fogPointPosRadius[slot][1] = light.y;
            params->fogPointPosRadius[slot][2] = light.z;
            params->fogPointPosRadius[slot][3] = light.radius;
            for (int i = 0; i < 3; ++i)
                params->fogPointColor[slot][i] = light.color[i] / 255.0f;
            params->fogPointCount = (params->fogPointCount > slot) ? params->fogPointCount : slot + 1;
        }
    } else {
        // Scripted/poison/cursed/final-day skies retain their native fog.
        if (!params->shadowEnabled || play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled ||
            play->envCtx.lightSettingOverride != LIGHT_SETTING_OVERRIDE_NONE || play->envCtx.changeSkyboxState != 0 ||
            play->envCtx.sandstormState != SANDSTORM_OFF || play->envCtx.skyboxFilterColor[3] != 0 ||
            gSaveContext.save.day < 1 || gSaveContext.save.day >= 3 || gSaveContext.sceneLayer != 0)
            return;
        params->fogBaseHeight = 0.0f;
        params->fogHeight = 700.0f;
        params->fogDensityScale = 1.0f;
        switch (play->sceneId) {
            case SCENE_00KEIKOKU:
                break;
            case SCENE_30GYOSON:
                params->fogHeight = 450.0f;
                params->fogDensityScale = 0.7f;
                break;
            case SCENE_20SICHITAI2:
            case SCENE_21MITURINMAE:
                params->fogHeight = 420.0f;
                params->fogDensityScale = 1.3f;
                break;
            case SCENE_CLOCKTOWER:
            case SCENE_TOWN:
            case SCENE_ICHIBA:
            case SCENE_BACKTOWN:
                params->fogDensityScale = 0.35f;
                break;
            case SCENE_IKANA:
                params->fogBaseHeight = 400.0f;
                break;
            default:
                return;
        }
        params->fogVolumetric = 1;
    }
    for (int i = 0; i < 3; ++i) {
        params->fogAmbient[i] = play->lightCtx.ambientColor[i] / 255.0f;
        params->fogColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    params->shadowCameraEye[0] = play->view.eye.x;
    params->shadowCameraEye[1] = play->view.eye.y;
    params->shadowCameraEye[2] = play->view.eye.z;
}

// ---------------------------------------------------------------------------------------------------------------
// Art-directed grade (gNewDawn.Grade.ArtDirection). A "look" is a small set of identity-centred colour controls.
// The final look = region mood + time-of-day delta (+ per-band extras), cross-faded when the scene changes and
// scaled by gNewDawn.Grade.ArtStrength. Render-only: it reads the scene id, the clock and the Woodfall clear flag.
// ---------------------------------------------------------------------------------------------------------------
struct NewDawnLook {
    float gain, warmth, tint, lift, gamma, contrast, sat; // warmth + = orange, - = blue; tint + = green, - = magenta
    float shadow[3], high[3];                               // additive split tone (display space)
    float greenShift, greenSat;                             // green hue range: + towards yellow, - towards teal
};
static constexpr int kArtFloats = sizeof(NewDawnLook) / sizeof(float);
static_assert(sizeof(NewDawnLook) == 15 * sizeof(float));
static constexpr NewDawnLook kArtIdentity = { 1, 0, 0, 0, 1, 0, 1, { 0, 0, 0 }, { 0, 0, 0 }, 0, 1 };
// Deltas are expressed against the identity, so adding them to a mood is a plain float-wise sum.
static NewDawnLook ArtAdd(const NewDawnLook& a, const NewDawnLook& d, float w) {
    NewDawnLook out;
    // Copy to actual arrays: indexing past a scalar struct member is undefined in C++.
    float pa[kArtFloats], pd[kArtFloats], pi[kArtFloats], po[kArtFloats];
    memcpy(pa, &a, sizeof(a));
    memcpy(pd, &d, sizeof(d));
    memcpy(pi, &kArtIdentity, sizeof(kArtIdentity));
    for (int i = 0; i < kArtFloats; i++) {
        po[i] = pa[i] + w * (pd[i] - pi[i]);
    }
    memcpy(&out, po, sizeof(out));
    return out;
}
static NewDawnLook ArtMix(const NewDawnLook& a, const NewDawnLook& b, float t) {
    NewDawnLook out;
    float pa[kArtFloats], pb[kArtFloats], po[kArtFloats];
    memcpy(pa, &a, sizeof(a));
    memcpy(pb, &b, sizeof(b));
    for (int i = 0; i < kArtFloats; i++) {
        po[i] = pa[i] + (pb[i] - pa[i]) * t;
    }
    memcpy(&out, po, sizeof(out));
    return out;
}

enum { ART_DAWN, ART_DAY, ART_DUSK, ART_NIGHT };
// What the clock does to every outdoor scene: warm low sun at dawn and dusk, cool blue moonlight at night.
static const NewDawnLook sArtTod[4] = {
    // gain warm tint lift gamma contr sat    shadow                  high                  grn   grnSat
    { 1.00f, 0.25f, 0.0f, 0.000f, 1.00f, 0.00f, 1.02f, { 0.000f, 0.000f, 0.014f }, { 0.018f, 0.004f, -0.012f }, 0.10f, 1.00f }, // dawn
    { 1.00f, 0.00f, 0.0f, 0.000f, 1.00f, 0.00f, 1.00f, { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.000f, 0.000f }, 0.00f, 1.00f },  // day
    { 0.99f, 0.35f, 0.0f, 0.000f, 1.00f, 0.02f, 1.02f, { 0.012f, 0.000f, 0.022f }, { 0.022f, 0.004f, -0.020f }, 0.10f, 1.00f }, // dusk
    { 1.02f, -0.60f, 0.0f, 0.006f, 1.00f, 0.03f, 0.92f, { -0.006f, 0.004f, 0.026f }, { 0.004f, 0.004f, 0.006f }, -0.20f, 0.95f }, // night
};

struct NewDawnArtMood {
    NewDawnLook base;
    NewDawnLook extra[4]; // per time-of-day band, added on top of sArtTod; identity = nothing
};
#define ART_ID kArtIdentity
#define ART_NO_EXTRA { ART_ID, ART_ID, ART_ID, ART_ID }
//                                            gain   warm   tint   lift   gamma  contr  sat    shadow                    high                     grn    grnSat
static const NewDawnArtMood sArtOutdoor = { { 1.00f, 0.25f, 0.00f, 0.000f, 0.98f, 0.08f, 1.10f, { 0, 0, 0.010f }, { 0.012f, 0.006f, -0.010f }, 0.12f, 1.12f }, ART_NO_EXTRA };
// Termina Field, ranches, roads: lush, a touch golden.
static const NewDawnArtMood sArtField = { { 1.00f, 0.24f, 0.00f, 0.000f, 0.97f, 0.09f, 1.12f, { 0, 0, 0.012f }, { 0.018f, 0.010f, -0.012f }, 0.20f, 1.12f }, ART_NO_EXTRA };
// Clock Town: warm plaster and sun by day; at night cool moonlit shadows with warm lamplight in the highlights.
static const NewDawnArtMood sArtClockTown = {
    { 1.00f, 0.40f, 0.00f, 0.000f, 0.97f, 0.09f, 1.14f, { 0, 0, 0.010f }, { 0.020f, 0.010f, -0.014f }, 0.14f, 1.10f },
    { ART_ID, ART_ID, ART_ID, { 1.00f, -0.20f, 0.0f, 0.000f, 1.00f, 0.04f, 1.08f, { 0.000f, 0.006f, 0.020f }, { 0.070f, 0.030f, -0.050f }, 0.0f, 1.0f } } };
// Southern Swamp while poisoned (and Woodfall before its temple is cleared): sickly but beautiful. Murky violet
// shadows, toxic yellow-green foliage, drained saturation elsewhere.
static const NewDawnArtMood sArtPoison = { { 1.03f, -0.14f, 0.10f, 0.014f, 1.00f, 0.10f, 0.82f, { 0.024f, 0.006f, 0.045f }, { 0.030f, 0.030f, -0.030f }, 0.65f, 1.06f }, ART_NO_EXTRA };
// Cleared swamp / Woodfall / Deku Palace: lush, golden-green, sun-dappled.
static const NewDawnArtMood sArtLush = {
    { 1.02f, 0.32f, 0.00f, 0.000f, 0.94f, 0.10f, 1.13f, { 0, 0.006f, 0.014f }, { 0.040f, 0.022f, -0.030f }, 0.25f, 1.14f },
    // The original night vertex colours are strongly green: restrain them rather than amplifying lime grass.
    { ART_ID, ART_ID, ART_ID, { 1.00f, 0.05f, 0.0f, 0.000f, 1.00f, 0.00f, 0.88f, { 0, 0, 0 }, { 0, 0, 0 }, -0.10f, 0.78f } } };
// Woods of Mystery, Woodfall temple: mossy, humid green-gold.
static const NewDawnArtMood sArtMoss = { { 1.02f, 0.24f, 0.06f, 0.012f, 0.94f, 0.10f, 1.12f, { 0, 0.012f, 0.024f }, { 0.040f, 0.026f, -0.030f }, 0.24f, 1.12f }, ART_NO_EXTRA };
// Ikana: bleached and haunted. Chalky highlights, cold violet-teal shadows, little saturation.
static const NewDawnArtMood sArtIkana = { { 1.08f, -0.25f, 0.00f, 0.026f, 0.94f, 0.10f, 0.64f, { 0.010f, 0.022f, 0.050f }, { 0.022f, 0.022f, 0.018f }, -0.10f, 0.65f }, ART_NO_EXTRA };
// Stone Tower (outside) and its temple: parched gold and rust below, cold mystic violet when inverted.
static const NewDawnArtMood sArtStoneTower = { { 1.00f, 0.30f, 0.00f, 0.006f, 1.04f, 0.14f, 1.14f, { 0.016f, 0.000f, 0.040f }, { 0.030f, 0.012f, -0.020f }, 0.0f, 1.0f }, ART_NO_EXTRA };
static const NewDawnArtMood sArtStoneInverted = { { 1.00f, -0.70f, -0.08f, 0.010f, 1.04f, 0.15f, 1.16f, { 0.020f, 0.000f, 0.060f }, { 0.024f, 0.008f, 0.026f }, -0.2f, 1.0f }, ART_NO_EXTRA };
// Great Bay (coast, cape, temple): clear teal-leaning blues, sunlit.
static const NewDawnArtMood sArtBay = { { 1.00f, 0.15f, 0.00f, 0.000f, 0.97f, 0.09f, 1.20f, { -0.006f, 0.012f, 0.024f }, { 0.018f, 0.012f, -0.006f }, -0.30f, 1.14f }, ART_NO_EXTRA };
static const NewDawnArtMood sArtBayTemple = { { 1.00f, -0.22f, 0.04f, 0.010f, 0.94f, 0.12f, 1.12f, { -0.010f, 0.022f, 0.036f }, { 0.024f, 0.028f, 0.010f }, -0.35f, 1.16f }, ART_NO_EXTRA };
// Snowhead and its mountains in winter: crisp blue-white, cool shadows, clean highlights.
static const NewDawnArtMood sArtSnow = {
    { 1.03f, -0.50f, 0.00f, 0.006f, 0.98f, 0.10f, 0.88f, { -0.008f, 0.008f, 0.050f }, { 0.004f, 0.008f, 0.010f }, -0.2f, 0.85f },
    // Snow is already violet-blue at night; retain the cold mood without doubling that cast.
    { ART_ID, ART_ID, ART_ID, { 1.00f, 0.35f, 0.0f, 0.000f, 1.00f, 0.00f, 1.00f, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 1.0f } } };
// Interiors and everything else enclosed: warm, a little more contrast and colour than the flat original.
static const NewDawnArtMood sArtInterior = { { 1.00f, 0.32f, 0.00f, 0.006f, 0.97f, 0.12f, 1.08f, { 0.006f, 0.000f, 0.020f }, { 0.024f, 0.010f, -0.016f }, 0.10f, 1.10f }, ART_NO_EXTRA };
static const NewDawnArtMood sArtDungeon = { { 1.00f, 0.10f, 0.00f, 0.006f, 1.00f, 0.15f, 1.16f, { 0.000f, 0.010f, 0.034f }, { 0.026f, 0.016f, -0.012f }, 0.08f, 1.14f }, ART_NO_EXTRA };

static const NewDawnArtMood* NewDawnArtMoodFor(s16 sceneId, bool woodfallCleared) {
    switch (sceneId) {
        case SCENE_CLOCKTOWER:
        case SCENE_TOWN:
        case SCENE_ICHIBA:
        case SCENE_BACKTOWN:
        case SCENE_ALLEY:
        case SCENE_OPENINGDAN:
        case SCENE_OKUJOU:
            return &sArtClockTown;
        case SCENE_00KEIKOKU:
        case SCENE_F01:
        case SCENE_ROMANYMAE:
        case SCENE_24KEMONOMITI:
            return &sArtField;
        case SCENE_20SICHITAI:
            return &sArtPoison;
        case SCENE_21MITURINMAE:
            return woodfallCleared ? &sArtLush : &sArtPoison;
        case SCENE_20SICHITAI2:
        case SCENE_22DEKUCITY:
            return &sArtLush;
        case SCENE_26SARUNOMORI:
        case SCENE_MITURIN:
        case SCENE_MITURIN_BS:
            return &sArtMoss;
        case SCENE_IKANA:
        case SCENE_IKANAMAE:
        case SCENE_BOTI:
        case SCENE_HAKASHITA:
        case SCENE_DANPEI2TEST:
        case SCENE_CASTLE:
        case SCENE_IKNINSIDE:
        case SCENE_REDEAD:
            return &sArtIkana;
        case SCENE_F40:
        case SCENE_INISIE_N:
        case SCENE_INISIE_BS:
            return &sArtStoneTower;
        case SCENE_F41:
        case SCENE_INISIE_R:
            return &sArtStoneInverted;
        case SCENE_30GYOSON:
        case SCENE_31MISAKI:
        case SCENE_35TAKI:
        case SCENE_33ZORACITY:
            return &sArtBay;
        case SCENE_SEA:
        case SCENE_SEA_BS:
        case SCENE_KAIZOKU:
        case SCENE_PIRATE:
        case SCENE_TORIDE:
            return &sArtBayTemple;
        case SCENE_10YUKIYAMANOMURA:
        case SCENE_11GORONNOSATO:
        case SCENE_17SETUGEN:
        case SCENE_14YUKIDAMANOMITI:
        case SCENE_12HAKUGINMAE:
        case SCENE_13HUBUKINOMITI:
        case SCENE_HAKUGIN:
        case SCENE_HAKUGIN_BS:
        case SCENE_GORON_HAKA:
            return &sArtSnow;
        case SCENE_10YUKIYAMANOMURA2:
        case SCENE_11GORONNOSATO2:
        case SCENE_17SETUGEN2:
            return &sArtLush;
        case SCENE_LAST_BS:
        case SCENE_LAST_DEKU:
        case SCENE_LAST_GORON:
        case SCENE_LAST_ZORA:
        case SCENE_LAST_LINK:
            return &sArtDungeon;
        default:
            return &sArtOutdoor;
    }
}

// Weights of dawn/day/dusk/night for an hour 0..24: night plateau, a smooth ramp through each band.
static void NewDawnArtTodWeights(float hour, float w[4]) {
    static const struct {
        float hour;
        int band;
    } keys[] = { { 0.0f, ART_NIGHT }, { 4.5f, ART_NIGHT },  { 6.0f, ART_DAWN },  { 9.0f, ART_DAY },
                 { 15.5f, ART_DAY },  { 17.5f, ART_DUSK },  { 19.5f, ART_NIGHT }, { 24.0f, ART_NIGHT } };
    w[0] = w[1] = w[2] = w[3] = 0.0f;
    for (int i = 0; i < 7; i++) {
        if (hour < keys[i + 1].hour) {
            float t = (hour - keys[i].hour) / (keys[i + 1].hour - keys[i].hour);
            t = t * t * (3.0f - 2.0f * t);
            w[keys[i].band] += 1.0f - t;
            w[keys[i + 1].band] += t;
            return;
        }
    }
    w[ART_NIGHT] = 1.0f;
}

static void NewDawnFillArtParams(PlayState* play, NewDawnPostFxParams* params) {
    // Written to the per-call params; the cross-fade state advances once per game frame.
    static NewDawnLook sCurrent;
    static bool sInit = false;
    static u32 sLastFrame = 0;
    static bool sLastValid = false;

    params->artEnabled = 0;
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1) || !CVarGetInteger("gNewDawn.Grade.ArtDirection", 0)) {
        sInit = false;
        return;
    }
    float strength = NewDawnClampF(CVarGetFloat("gNewDawn.Grade.ArtStrength", 1.0f), 0.0f, 1.5f);
    if (strength <= 0.0f) {
        return;
    }

    if (!sInit || !sLastValid || sLastFrame != play->gameplayFrames) {
        const NewDawnArtMood* mood = NewDawnArtMoodFor(play->sceneId, CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE));
        // Only outdoor scenes that follow the clock get the time-of-day treatment; enclosed scenes keep one look.
        bool outdoor = play->envCtx.lightMode == LIGHT_MODE_TIME;
        NewDawnLook target = mood->base;
        if (outdoor) {
            float w[4];
            NewDawnArtTodWeights(gSaveContext.save.time * (24.0f / 65536.0f), w);
            for (int b = 0; b < 4; b++) {
                if (w[b] > 0.0f) {
                    target = ArtAdd(target, sArtTod[b], w[b]);
                    target = ArtAdd(target, mood->extra[b], w[b]);
                }
            }
        } else if (mood == &sArtOutdoor) {
            target = sArtInterior.base;
        }
        if (!sInit) {
            sCurrent = target;
            sInit = true;
        } else {
            sCurrent = ArtMix(sCurrent, target, 0.06f); // ~1 s time constant at 20 Hz: scene changes and clock steps ease in
        }
        sLastFrame = play->gameplayFrames;
        sLastValid = true;
    }

    NewDawnLook look = ArtMix(kArtIdentity, sCurrent, strength);
    params->artEnabled = 1;
    params->artSlope[0] = look.gain * (1.0f + 0.30f * look.warmth);
    params->artSlope[1] = look.gain * (1.0f + 0.15f * look.tint);
    params->artSlope[2] = look.gain * (1.0f - 0.30f * look.warmth);
    params->artOffset = look.lift;
    params->artPower = look.gamma;
    params->artContrast = look.contrast;
    params->artSat = look.sat;
    for (int i = 0; i < 3; i++) {
        params->artShadowTint[i] = look.shadow[i];
        params->artHighTint[i] = look.high[i];
    }
    params->artGreen[0] = look.greenShift;
    params->artGreen[1] = look.greenSat;
}

// Read-only snapshot of the game's point lights for the renderer's HDR local lighting. Nearest-first by distance over
// radius so linked-list order cannot evict a close light; nothing here writes game state.
static void NewDawnFillLocalLightParams(PlayState* play, NewDawnPostFxParams* params) {
    if (!CVarGetInteger("gNewDawn.Lighting.Local", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        return;
    }
    params->localEnabled = 1;
    params->localStrength = NewDawnClampF(CVarGetFloat("gNewDawn.Lighting.LocalStrength", 1.0f), 0.0f, 3.0f);
    params->localFlicker = NewDawnClampF(CVarGetFloat("gNewDawn.Lighting.LocalFlicker", 1.0f), 0.0f, 1.0f);
    // Darkening of the unlit scene, from the game's own clock and light mode (read only). Outdoors (time-of-day
    // lighting) it follows a smooth dusk/dawn ramp; interiors and dungeons (fixed light settings) get a milder, constant
    // pull so their lamps shape the room. Cutscenes get a slightly lighter version of the same.
    {
        const float hour = gSaveContext.save.time * (24.0f / 65536.0f);
        const auto smooth = [](float a, float b, float x) {
            const float t = NewDawnClampF((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        const float night = hour >= 12.0f ? smooth(17.5f, 19.5f, hour) : 1.0f - smooth(4.5f, 6.5f, hour);
        const bool outdoor = play->envCtx.lightMode == LIGHT_MODE_TIME;
        float darken = outdoor ? 0.62f * night : 0.58f;
        params->localGain = outdoor ? 1.0f : 1.5f;
        // Cutscenes (most of the opening) get the same moody treatment, a little lighter, so their flat authored light is
        // not left grey. The value eases over about a second so a cutscene starting or ending never pops.
        if (play->csCtx.state != CS_STATE_IDLE || Play_InCsMode(play)) {
            darken *= 0.85f;
        }
        static float sDarken = 0.0f;
        static s16 sScene = -1;
        static u32 sFrame = 0;
        if (sScene != play->sceneId || play->gameplayFrames < sFrame) {
            sDarken = darken; // scene change (fades through black): start from the target
            sScene = play->sceneId;
        } else if (play->gameplayFrames != sFrame) {
            sDarken += (darken - sDarken) * 0.08f;
        }
        sFrame = play->gameplayFrames;
        params->localDarken = sDarken * NewDawnClampF(CVarGetFloat("gNewDawn.Lighting.LocalContrast", 1.0f), 0.0f, 1.5f);
    }
    float scores[NEWDAWN_LOCAL_LIGHTS];
    for (float& score : scores)
        score = 1e30f;
    for (const LightNode* node = play->lightCtx.listHead; node; node = node->next) {
        const LightInfo* info = node->info;
        if (!info || (info->type != LIGHT_POINT_NOGLOW && info->type != LIGHT_POINT_GLOW) ||
            info->params.point.radius <= 0)
            continue;
        const auto& light = info->params.point;
        if (light.color[0] + light.color[1] + light.color[2] == 0)
            continue;
        const float dx = light.x - play->view.eye.x, dy = light.y - play->view.eye.y, dz = light.z - play->view.eye.z;
        const float score = (dx * dx + dy * dy + dz * dz) / (float)(light.radius * light.radius);
        int slot = 0;
        for (int i = 1; i < NEWDAWN_LOCAL_LIGHTS; ++i)
            if (scores[i] > scores[slot])
                slot = i;
        if (score >= scores[slot])
            continue;
        scores[slot] = score;
        params->localLights[slot][0] = light.x;
        params->localLights[slot][1] = light.y;
        params->localLights[slot][2] = light.z;
        params->localLights[slot][3] = light.radius;
        for (int i = 0; i < 3; ++i)
            params->localColors[slot][i] = light.color[i] / 255.0f;
        params->localBoost[slot] = 1.0f;
        // Tatl and other fairies light with pure white (255, 255, 255): as a point light that is a white floodlight.
        // Give them a soft glow in the fairy's own colour (the actor-category colour Tatl is currently showing).
        if (light.color[0] == 255 && light.color[1] == 255 && light.color[2] == 255) {
            const Attention* att = &play->actorCtx.attention;
            float tint[3] = { 0.35f * att->tatlInnerColor.r + 0.65f * att->tatlOuterColor.r,
                              0.35f * att->tatlInnerColor.g + 0.65f * att->tatlOuterColor.g,
                              0.35f * att->tatlInnerColor.b + 0.65f * att->tatlOuterColor.b };
            const float peak = fmaxf(tint[0], fmaxf(tint[1], tint[2]));
            if (peak > 1.0f) {
                for (int i = 0; i < 3; ++i)
                    params->localColors[slot][i] = tint[i] / peak;
            }
            params->localLights[slot][3] = light.radius * 0.75f;
            params->localBoost[slot] = 0.3f;
        }
        if (slot >= params->localCount)
            params->localCount = slot + 1;
    }
}

// Enclosed scenes (the game's own "indoors" restriction set: no Song of Storms or Double Time) with a time-of-day light
// mode still exist, e.g. the Mayor's Residence. They get no grass and no room-geometry sway.
static bool NewDawnSceneIsIndoors(PlayState* play) {
    return play->envCtx.lightMode != LIGHT_MODE_TIME ||
           (play->interfaceCtx.restrictions.songOfStorms == 3 && play->interfaceCtx.restrictions.songOfDoubleTime == 3);
}

// Render-only 3D grass on grass-textured ground. Reads the player's position and the sun; changes nothing.
static void NewDawnFillGrassParams(PlayState* play, NewDawnPostFxParams* params) {
    // Outdoor time-of-day scenes only: interiors reuse "grass"-coloured rugs, planters and moss.
    params->grassEnabled = CVarGetInteger("gNewDawn.Grass.Enabled", 0) && CVarGetInteger("gNewDawn.HDR.Enabled", 0) &&
                           !NewDawnSceneIsIndoors(play);
    if (!params->grassEnabled) {
        return;
    }
    params->grassDensity = NewDawnClampF(CVarGetFloat("gNewDawn.Grass.Density", 1.0f), 0.0f, 2.0f);
    params->grassDistance = NewDawnClampF(CVarGetFloat("gNewDawn.Grass.Distance", 1000.0f), 200.0f, 4000.0f);
    params->grassHeight = NewDawnClampF(CVarGetFloat("gNewDawn.Grass.Height", 1.0f), 0.2f, 4.0f);
    Vec3f focus = play->view.eye;
    Player* player = GET_PLAYER(play);
    if (player != NULL) {
        focus = player->actor.world.pos;
    }
    params->grassFocus[0] = focus.x;
    params->grassFocus[1] = focus.y;
    params->grassFocus[2] = focus.z;
    if (!play->envCtx.sunDisabled && play->envCtx.lightMode == LIGHT_MODE_TIME) {
        const LightDirectional* sun = &play->envCtx.dirLight1.params.dir;
        float len = sqrtf((f32)(sun->x * sun->x + sun->y * sun->y + sun->z * sun->z));
        if (len > 0.001f) {
            params->grassSunDir[0] = sun->x / len;
            params->grassSunDir[1] = sun->y / len;
            params->grassSunDir[2] = sun->z / len;
            for (int i = 0; i < 3; i++) {
                params->grassSunColor[i] = sun->color[i] / 255.0f;
            }
        }
    }
}

// Render-only windows: these interiors have no real windows, so the remaster adds a few (a visible pane with its frame,
// and the light it lets in). Coordinates are world space, n is the horizontal inward normal, half extents in world
// units, panes across and down. Placement was checked on screen against each room.
struct NewDawnWindow {
    s16 scene;
    float x, y, z, nx, nz, hw, hh;
    u8 panesU, panesV;
};
static const NewDawnWindow sNewDawnWindows[] = {
    // Stock Pot Inn lobby (side walls x = 30 and 330)
    { SCENE_YADOYA, 30.6f, 100, 175, 1, 0, 34, 30, 2, 2 },
    { SCENE_YADOYA, 329.4f, 100, 205, -1, 0, 34, 30, 2, 2 },
    // Mayor's Residence lobby (a hall 210 wide, walls x = +-105)
    { SCENE_SONCHONOIE, 104.4f, 102, 190, -1, 0, 36, 30, 2, 2 },
    { SCENE_SONCHONOIE, -104.4f, 102, 140, 1, 0, 26, 28, 2, 2 },
    // Milk Bar (left wall x = -140)
    { SCENE_MILK_BAR, -139.4f, 104, 70, 1, 0, 32, 28, 2, 2 },
    { SCENE_MILK_BAR, -139.4f, 104, 235, 1, 0, 32, 28, 2, 2 },
    // Swordsman's School: clerestory windows high on both long walls (x = +-195)
    { SCENE_DOUJOU, -194.4f, 120, 60, 1, 0, 38, 24, 2, 2 },
    { SCENE_DOUJOU, -194.4f, 120, 235, 1, 0, 38, 24, 2, 2 },
    { SCENE_DOUJOU, 194.4f, 120, 60, -1, 0, 38, 24, 2, 2 },
    { SCENE_DOUJOU, 194.4f, 120, 235, -1, 0, 38, 24, 2, 2 },
    // Bomb Shop (left wall x = -15, back wall z = 390)
    { SCENE_BOMYA, -14.4f, 100, 230, 1, 0, 32, 28, 2, 2 },
    { SCENE_BOMYA, 190, 100, 389.4f, 0, -1, 40, 24, 2, 2 },
    // Trading Post (walls x = +-210)
    { SCENE_8ITEMSHOP, -209.4f, 125, 40, 1, 0, 40, 30, 2, 2 },
    { SCENE_8ITEMSHOP, 209.4f, 125, 40, -1, 0, 40, 30, 2, 2 },
    // Post Office (walls x = +-200)
    { SCENE_POSTHOUSE, -199.4f, 125, 100, 1, 0, 36, 28, 2, 2 },
    { SCENE_POSTHOUSE, 199.4f, 125, 100, -1, 0, 36, 28, 2, 2 },
};

// Interior polish (render only): the light that comes through windows and doorways, and the emissive surfaces.
// Reads the game clock and weather; the openings themselves are recorded by the renderer from the draw stream.
static void NewDawnFillInteriorParams(PlayState* play, NewDawnPostFxParams* params) {
    if (!CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        return;
    }
    const float hour = gSaveContext.save.time * (24.0f / 65536.0f);
    const auto smooth = [](float a, float b, float x) {
        const float t = NewDawnClampF((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    params->interiorDebug = CVarGetInteger("gNewDawn.Interior.Debug", 0);
    params->emissiveEnabled = CVarGetInteger("gNewDawn.Interior.Emissive", 0);
    params->emissiveStrength = NewDawnClampF(CVarGetFloat("gNewDawn.Interior.EmissiveStrength", 1.0f), 0.0f, 3.0f);
    params->emissiveNight = hour >= 12.0f ? smooth(17.5f, 19.5f, hour) : 1.0f - smooth(4.5f, 6.5f, hour);
    if (!CVarGetInteger("gNewDawn.Interior.Windows", 0) || !NewDawnSceneIsIndoors(play)) {
        return;
    }
    // Light enters at the sun's elevation (high at noon, low near dawn and dusk); the horizontal direction follows each
    // opening's own normal, since interior scenes have no meaningful compass.
    const float day = smooth(5.5f, 7.5f, hour) * (1.0f - smooth(16.5f, 18.5f, hour));
    const float dayPhase = NewDawnClampF((hour - 6.0f) / 12.0f, 0.0f, 1.0f);
    const float elevation = sinf(dayPhase * 3.14159265f);
    params->interiorSinElevation = 0.24f + 0.62f * elevation;
    const float warm = 1.0f - sqrtf(elevation);
    const float sun[3] = { 1.0f, 0.93f - 0.30f * warm, 0.80f - 0.46f * warm };
    const float moon[3] = { 0.42f, 0.55f, 1.0f };
    // Overcast and rain soften the light; the moon is a fraction of the sun.
    const float rain = NewDawnClampF(play->envCtx.precipitation[PRECIP_RAIN_CUR] / 60.0f, 0.0f, 1.0f);
    const float snow = NewDawnClampF(play->envCtx.precipitation[PRECIP_SNOW_CUR] / 128.0f, 0.0f, 1.0f);
    const float weather = 1.0f - 0.8f * fmaxf(rain, snow);
    for (int i = 0; i < 3; ++i) {
        params->interiorColor[i] = sun[i] * day * weather + moon[i] * (1.0f - day) * 0.07f;
    }
    params->interiorStrength = NewDawnClampF(CVarGetFloat("gNewDawn.Interior.Strength", 1.0f), 0.0f, 3.0f);
    params->interiorMotes = CVarGetInteger("gNewDawn.Interior.Motes", 1) ? 1.0f : 0.0f;
    params->interiorEnabled = params->interiorStrength > 0.0f;
    if (!params->interiorEnabled) {
        return;
    }
    for (const NewDawnWindow& w : sNewDawnWindows) {
        if (w.scene != play->sceneId || params->interiorWindowCount >= NEWDAWN_INTERIOR_WINDOWS) {
            continue;
        }
        float* o = params->interiorWindows[params->interiorWindowCount++];
        o[0] = w.x, o[1] = w.y, o[2] = w.z;
        o[3] = w.nx, o[4] = 0.0f, o[5] = w.nz;
        o[6] = w.nz, o[7] = 0.0f, o[8] = -w.nx; // horizontal axis along the wall
        o[9] = w.hw, o[10] = w.hh, o[11] = w.panesU, o[12] = w.panesV, o[13] = 2.0f;
    }
}

static NewDawnPostFxParams* NewDawnPostFx_AllocParams(PlayState* play) {
    // The display list is walked once per interpolated output frame, so the parameters live in the per-frame graphics
    // pool, which stays valid until the frame is done.
    auto* params = static_cast<NewDawnPostFxParams*>(GRAPH_ALLOC(play->state.gfxCtx, sizeof(NewDawnPostFxParams)));
    memset(params, 0, sizeof(*params));

    // The same values Play_DrawMain hands to the projection (View_SetPerspective) and to Play_SetFog.
    s32 fogFar = play->lightCtx.zFar * (5.0f / 64.0f);

    params->zNear = play->view.zNear;
    params->zFar = play->view.zFar;
    params->fovyDegrees = play->view.fovy;
    params->fogNear = play->lightCtx.fogNear;
    params->fogFar = (fogFar <= 1000) ? 1000 : fogFar;
    params->flags = 0;
    params->giEnabled = CVarGetInteger("gNewDawn.GI.Enabled", 0) && CVarGetInteger("gNewDawn.HDR.Enabled", 0);
    params->giStrength = NewDawnClampF(CVarGetFloat("gNewDawn.GI.Strength", 1.0f), 0.0f, 2.0f);
    params->giSceneId = play->sceneId;
    params->giCameraEye[0] = play->view.eye.x;
    params->giCameraEye[1] = play->view.eye.y;
    params->giCameraEye[2] = play->view.eye.z;
    NewDawnFillShadowParams(play, params);
    NewDawnFillFogParams(play, params);
    NewDawnFillWaterParams(play, params);
    NewDawnFillArtParams(play, params);
    NewDawnFillInteriorParams(play, params);
    NewDawnAtmos_Fill(play, params);
    NewDawnFillLocalLightParams(play, params);
    NewDawnFillGrassParams(play, params);
    params->vfxEnabled = CVarGetInteger("gNewDawn.Vfx.Enabled", 0) && CVarGetInteger("gNewDawn.HDR.Enabled", 0);
    params->vfxStrength = CVarGetFloat("gNewDawn.Vfx.Strength", 1.0f);
    params->vfxEmbers = CVarGetFloat("gNewDawn.Vfx.Embers", 1.0f);
    return params;
}

extern "C" void NewDawnHDR_WeatherScope(PlayState* play, int begin) {
    if (!CVarGetInteger("gNewDawn.Atmos.Weather", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) return;
    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->atmosWeather) return;
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, begin ? NEWDAWN_POSTFX_STAGE_WEATHER_BEGIN : NEWDAWN_POSTFX_STAGE_WEATHER_END, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnHDR_WindScope(PlayState* play, int begin, int room) {
    if (!CVarGetInteger("gNewDawn.Atmos.Wind", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) return;
    // Room geometry only sways outdoors (and then only plant textures); foliage actors keep their own scope anywhere.
    if (room && NewDawnSceneIsIndoors(play)) return;
    static NewDawnPostFxParams sUnused; // the command only needs a non-null pointer
    OPEN_DISPS(play->state.gfxCtx);
    const int stage = begin ? NEWDAWN_POSTFX_STAGE_WIND_BEGIN : NEWDAWN_POSTFX_STAGE_WIND_END;
    Gfx* opa = POLY_OPA_DISP++;
    gSPNewDawnPostFx(opa, stage, &sUnused);
    opa->words.w0 |= (uint32_t)(room != 0) << NEWDAWN_POSTFX_WIND_ROOM_SHIFT;
    Gfx* xlu = POLY_XLU_DISP++;
    gSPNewDawnPostFx(xlu, stage, &sUnused);
    xlu->words.w0 |= (uint32_t)(room != 0) << NEWDAWN_POSTFX_WIND_ROOM_SHIFT;
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnHDR_SunFlare(PlayState* play, const Vec3f* sunPos, int begin) {
    const bool screenShafts = CVarGetInteger("gNewDawn.SunShafts.Enabled", 0) &&
                               CVarGetInteger("gNewDawn.SunShafts.Mode", 0) == 1;
    if (!CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        (!CVarGetInteger("gNewDawn.Sky.Flare", 0) && !screenShafts))
        return;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    auto* params = static_cast<NewDawnPostFxParams*>(GRAPH_ALLOC(gfxCtx, sizeof(NewDawnPostFxParams)));
    memset(params, 0, sizeof(*params));
    params->flareSunPos[0] = sunPos->x;
    params->flareSunPos[1] = sunPos->y;
    params->flareSunPos[2] = sunPos->z;
    if (screenShafts && !play->envCtx.sunDisabled && play->envCtx.lightMode == LIGHT_MODE_TIME &&
        play->skyboxId == SKYBOX_NORMAL_SKY && !play->envCtx.skyboxDisabled &&
        play->envCtx.lightSettingOverride == LIGHT_SETTING_OVERRIDE_NONE &&
        play->envCtx.skyboxFilterColor[3] == 0 && play->envCtx.sandstormState == SANDSTORM_OFF &&
        !(GET_ACTIVE_CAM(play)->stateFlags & CAM_STATE_UNDERWATER)) {
        const Vec3f& sun = play->envCtx.sunPos;
        const float len = sqrtf(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
        float elevation = sun.y / ((len > 0.001f) ? len : 1.0f);
        float horizon = NewDawnClampF(elevation / 0.08f, 0.0f, 1.0f);
        float lowSun = 1.0f - NewDawnClampF(elevation / 0.65f, 0.0f, 1.0f);
        params->screenShaftStrength = NewDawnClampF(CVarGetFloat("gNewDawn.SunShafts.Strength", 0.4f), 0.0f, 1.0f) *
                                     horizon * (0.15f + 0.85f * lowSun * lowSun);
        for (int i = 0; i < 3; ++i)
            params->screenShaftColor[i] = (0.75f * play->envCtx.dirLight1.params.dir.color[i] +
                                           0.25f * play->lightCtx.fogColor[i]) / 255.0f;
    }
    OPEN_DISPS(gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, begin ? NEWDAWN_POSTFX_STAGE_SUN_FLARE_BEGIN : NEWDAWN_POSTFX_STAGE_SUN_FLARE_END,
                    params);
    CLOSE_DISPS(gfxCtx);
}

extern "C" void NewDawnHDR_Sky(GraphicsContext* gfxCtx, int begin) {
    if (!CVarGetInteger("gNewDawn.HDR.Enabled", 0))
        return;
    auto* params = static_cast<NewDawnPostFxParams*>(GRAPH_ALLOC(gfxCtx, sizeof(NewDawnPostFxParams)));
    memset(params, 0, sizeof(*params));
    OPEN_DISPS(gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, begin ? NEWDAWN_POSTFX_STAGE_SKY_BEGIN : NEWDAWN_POSTFX_STAGE_SKY_END, params);
    CLOSE_DISPS(gfxCtx);
}

static void NewDawnSky_Emit(PlayState* play, bool fillPass);

// True for the frame in which the physical sky pass (with the real star field) was emitted: the native sprite stars
// are then skipped. Reset by every NewDawnSky_Draw and consumed once by Environment_DrawSkyboxStars.
static bool sNightSkyReplacesStars = false;

static bool NewDawnNightSkyAssetsPresent() {
    static std::string checkedRoot;
    static bool present = false;
    const std::string root = CVarGetString("gNewDawn.Assets.Path", "assets/newdawn");
    if (root != checkedRoot) {
        checkedRoot = root;
        present = std::filesystem::exists(std::filesystem::path(root) / "nightsky" / "stars.bin") &&
                  std::filesystem::exists(std::filesystem::path(root) / "nightsky" / "milkyway_eq.jpg");
    }
    return present;
}

extern "C" int NewDawnNightSky_ReplacesStars(void) {
    const bool replaced = sNightSkyReplacesStars;
    sNightSkyReplacesStars = false;
    return replaced;
}

extern "C" void NewDawnSky_Draw(PlayState* play) {
    sNightSkyReplacesStars = false;
    NewDawnSky_Emit(play, false);
}

// gNewDawn.Sky.Special variants (values shared with the renderer's skyVariant).
enum { SKY_VARIANT_NORMAL, SKY_VARIANT_WINTER, SKY_VARIANT_FINAL, SKY_VARIANT_CURSED, SKY_VARIANT_SWAMP };

// Final-day intensity 0..1, read-only from the clock: ominous from dawn of day 3, deeper through the evening, and
// building through the final hours (00:00-06:00) to the peak just before the Moon falls.
static float NewDawnFinalDayMood() {
    const float hours = gSaveContext.save.time * (24.0f / 65536.0f);
    if (hours >= 6.0f) {
        const float t = NewDawnClampF((hours - 6.0f) / 18.0f, 0.0f, 1.0f);
        return 0.40f + 0.25f * t * t * (3.0f - 2.0f * t);
    }
    return 0.65f + 0.35f * (hours / 6.0f);
}

// Winter Mountain Village (SKYBOX_2) and cursed Ikana (SKYBOX_3) have no skybox drawn: the game paints an opaque
// fog-colour fill. The overcast deck is emitted right after that fill and blends over it, so the fill stays as the
// fallback if the pass cannot run.
extern "C" void NewDawnSky_DrawOvercast(PlayState* play) {
    NewDawnSky_Emit(play, true);
}

static void NewDawnSky_Emit(PlayState* play, bool fillPass) {
    static u32 lastLogFrame = UINT32_MAX;
    static s16 lastLogScene = -1;
    if (CVarGetInteger("gNewDawn.Sky.Physical", 0) && CVarGetInteger("gNewDawn.PostFx.Debug.Stats", 0) &&
        play->gameplayFrames % 120 == 0 && (lastLogFrame != play->gameplayFrames || lastLogScene != play->sceneId)) {
        lastLogFrame = play->gameplayFrames;
        lastLogScene = play->sceneId;
        SPDLOG_INFO("NewDawnSky: scene {} sky {} config {} layer {} day {} mode {} sunDisabled {} lateSky {} override "
                    "{} cutscene {} time {} cursed {:.2f}",
                    play->sceneId, play->skyboxId, play->envCtx.skyboxConfig, gSaveContext.sceneLayer,
                    gSaveContext.save.day, play->envCtx.lightMode, play->envCtx.sunDisabled, play->skyboxCtx.shouldDraw,
                    play->envCtx.lightSettingOverride, Play_InCsMode(play), gSaveContext.save.time, D_801F4E74);
    }
    const bool special = CVarGetInteger("gNewDawn.Sky.Special", 0) != 0;
    const bool winter = fillPass && play->skyboxId == SKYBOX_2 && play->sceneId == SCENE_10YUKIYAMANOMURA;
    // Fully cursed Ikana only: while the curse lifts (D_801F4E74 fading) the native sky cross-fades in.
    const bool cursed = special && fillPass && play->skyboxId == SKYBOX_3 && D_801F4E74 >= 1.0f;
    const bool finalDay = special && !fillPass && gSaveContext.save.day == 3;
    // Special mode keeps the physical sky through cutscenes, conversations, lighting overrides and skybox config
    // changes: it only reads the fog colour, palette, sun direction and clock, which the scripts already drive.
    // Scripted filter fills and fades are drawn by the game after this pass and cover it exactly as they cover the
    // native sky. Without Special, any of these states falls back to the native skybox, as before.
    const bool scripted = !special && (play->csCtx.state != CS_STATE_IDLE || Play_InCsMode(play) ||
                                       play->envCtx.lightSettingOverride != LIGHT_SETTING_OVERRIDE_NONE ||
                                       play->envCtx.changeSkyboxState != 0 ||
                                       play->envCtx.skyboxConfig != play->envCtx.changeSkyboxNextConfig ||
                                       play->envCtx.skyboxFilterColor[3] != 0);
    const bool dayBlocks = (finalDay || cursed) ? false : (gSaveContext.save.day < 1 || gSaveContext.save.day >= 3);
    const bool layerBlocks = (finalDay || cursed) ? false : gSaveContext.sceneLayer != 0;
    if (!CVarGetInteger("gNewDawn.Sky.Physical", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        CVarGetFloat("gNewDawn.Sky.Strength", 1.0f) <= 0.0f ||
        (fillPass ? !(winter || cursed) : play->skyboxId != SKYBOX_NORMAL_SKY) || play->skyboxCtx.shouldDraw ||
        play->envCtx.skyboxDisabled || (play->envCtx.sunDisabled && !cursed) ||
        play->envCtx.lightMode != LIGHT_MODE_TIME || play->envCtx.sandstormState != SANDSTORM_OFF || dayBlocks ||
        layerBlocks || scripted) {
        return;
    }

    // Explicitly known normal outdoor locations. Other special skies, dungeons, cutscene-only sky maps and
    // unclassified scenes retain the original skybox. The poisoned swamp is classified only for Special mode.
    // Poisoned swamp, and Woodfall's exterior until its temple is cleared (the same poison palette as the water).
    const bool swamp = special && !fillPass &&
                       (play->sceneId == SCENE_20SICHITAI ||
                        (play->sceneId == SCENE_21MITURINMAE && !CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE)));
    if (!fillPass && !swamp) {
        switch (play->sceneId) {
            case SCENE_00KEIKOKU:
            case SCENE_CLOCKTOWER:
            case SCENE_TOWN:
            case SCENE_ICHIBA:
            case SCENE_BACKTOWN:
            case SCENE_30GYOSON:
            case SCENE_IKANA:
            case SCENE_10YUKIYAMANOMURA:
            case SCENE_10YUKIYAMANOMURA2:
            case SCENE_20SICHITAI2:
            case SCENE_22DEKUCITY:
            case SCENE_F01:
                break;
            case SCENE_OKUJOU: // Clock Tower rooftop: the Moon's approach, Special mode only
                if (special)
                    break;
                return;
            default:
                return;
        }
    }
    const auto& sun = play->envCtx.dirLight1.params.dir;
    float len = sqrtf((float)(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z));
    if (len < 0.001f) {
        return;
    }
    const int variant = winter ? SKY_VARIANT_WINTER
                        : cursed ? SKY_VARIANT_CURSED
                        : finalDay ? SKY_VARIANT_FINAL
                        : swamp  ? SKY_VARIANT_SWAMP
                                 : SKY_VARIANT_NORMAL;
    auto* params = NewDawnPostFx_AllocParams(play);
    params->skySunDir[0] = sun.x / len;
    params->skySunDir[1] = sun.y / len;
    params->skySunDir[2] = sun.z / len;
    for (int i = 0; i < 3; ++i) {
        params->skyFogColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    // SKYBOX_2/3 never load a palette; their decks take their hue from the fog.
    const bool fogPalette = winter || cursed;
    params->skyPaletteColor[0] = fogPalette ? params->skyFogColor[0] : play->skyboxCtx.prim.r / 255.0f;
    params->skyPaletteColor[1] = fogPalette ? params->skyFogColor[1] : play->skyboxCtx.prim.g / 255.0f;
    params->skyPaletteColor[2] = fogPalette ? params->skyFogColor[2] : play->skyboxCtx.prim.b / 255.0f;
    params->skyOvercast = winter ? 1.0f : 0.0f;
    params->skyVariant = variant;
    params->skyMood = finalDay ? NewDawnFinalDayMood() : 0.0f;
    // Night sky inputs (read-only). Stars/Milky Way: full in normal skies; dimmed under the final-day storm, absent in
    // cursed Ikana, the poisoned swamp and the winter overcast deck, as their art direction requires. The Moon glow
    // grows through the final day as the Moon falls, and is subdued by the storm.
    {
        const float hours = gSaveContext.save.time * (24.0f / 65536.0f);
        params->skyClock = gSaveContext.save.time / 65536.0f;
        params->skyStarVis = (winter || cursed || swamp) ? 0.0f : finalDay ? 0.2f * (1.0f - params->skyMood) : 1.0f;
        float moon = 1.0f;
        if (finalDay) {
            const float progress = hours >= 6.0f ? (hours - 6.0f) / 24.0f : (hours + 18.0f) / 24.0f;
            moon = (0.6f + 1.6f * progress) * (1.0f - 0.4f * params->skyMood);
        }
        params->skyMoonGlow = (winter || cursed || swamp) ? 0.0f : moon;
        float moonDir[3], moonRadius = 0.0f;
        if (NewDawnFindMoon(play, moonDir, &moonRadius)) {
            for (int i = 0; i < 3; ++i)
                params->skyMoonDir[i] = moonDir[i];
            params->skyMoonRadius = moonRadius;
            if (CVarGetInteger("gNewDawn.PostFx.Debug.Stats", 0) && play->gameplayFrames % 120 == 0)
                SPDLOG_INFO("NewDawnMoon: dir {:.3f} {:.3f} {:.3f} radius {:.4f} rad", moonDir[0], moonDir[1], moonDir[2], moonRadius);
        } else {
            params->skyMoonGlow = 0.0f;
        }
        if (!fillPass && !winter && !cursed && !swamp && CVarGetInteger("gNewDawn.Sky.Night", 0)) {
            sNightSkyReplacesStars = NewDawnNightSkyAssetsPresent();
        }
    }
    // Representative render-only ground albedo, used for the lower-hemisphere sky bounce.
    const bool grass = play->sceneId == SCENE_00KEIKOKU || play->sceneId == SCENE_F01 ||
                       play->sceneId == SCENE_20SICHITAI2 || play->sceneId == SCENE_22DEKUCITY;
    params->skyGroundAlbedo[0] = winter ? 0.70f : cursed ? 0.28f : swamp ? 0.14f : grass ? 0.24f : 0.42f;
    params->skyGroundAlbedo[1] = winter ? 0.74f : cursed ? 0.26f : swamp ? 0.16f : grass ? 0.30f : 0.32f;
    params->skyGroundAlbedo[2] = winter ? 0.80f : cursed ? 0.30f : swamp ? 0.12f : grass ? 0.10f : 0.18f;
    params->skySceneId = play->sceneId;
    params->skyWeather = NewDawnClampF(play->envCtx.precipitation[PRECIP_RAIN_CUR] / 40.0f +
                                           play->envCtx.precipitation[PRECIP_SNOW_CUR] / 128.0f,
                                       0.0f, 1.0f);
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_PHYSICAL_SKY, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnHDR_Begin(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        return;
    }
    // Deku bubbles reuse a CPU display-colour snapshot as an in-world texture. Until display-source
    // materials have an explicit shader classification, keep the whole frame in the compatible LDR path.
    for (Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first; actor; actor = actor->next) {
        if (actor->id == ACTOR_EN_ARROW && actor->params == ARROW_TYPE_DEKU_BUBBLE) {
            static bool logged = false;
            if (!logged)
                SPDLOG_INFO("NewDawnHDR: LDR fallback while Deku bubble framebuffer refraction is active");
            logged = true;
            return;
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_WORLD_BEGIN, NewDawnPostFx_AllocParams(play));
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnShadows_Begin(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.Shadows.Enabled", 0) && !CVarGetInteger("gNewDawn.Water.Enabled", 0) &&
        !CVarGetInteger("gNewDawn.Water.Reflections", 0) && !CVarGetInteger("gNewDawn.Water.Foam", 0) &&
        !CVarGetInteger("gNewDawn.Water.Refraction", 0) && !CVarGetInteger("gNewDawn.Water.Restyle", 0) &&
        !CVarGetInteger("gNewDawn.Water.Caustics", 0) && !CVarGetInteger("gNewDawn.Water.Underwater", 0) &&
        !CVarGetInteger("gNewDawn.Atmos.Particles", 0) && !CVarGetInteger("gNewDawn.Atmos.Weather", 0) &&
        !CVarGetInteger("gNewDawn.Fog.Volumetric", 0) && !CVarGetInteger("gNewDawn.GI.Enabled", 0) &&
        !CVarGetInteger("gNewDawn.Vfx.Enabled", 0) && !CVarGetInteger("gNewDawn.Grass.Enabled", 0) &&
        !CVarGetInteger("gNewDawn.Interior.Windows", 0) && !CVarGetInteger("gNewDawn.Interior.Emissive", 0)) {
        return;
    }

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->shadowEnabled && !params->waterEnabled && !params->atmosBiome && !params->fogVolumetric &&
        !params->vfxEnabled && !params->grassEnabled && !params->interiorEnabled && !params->emissiveEnabled) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_SHADOW_BEGIN, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_GroundEnd(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.Grass.Enabled", 0)) {
        return;
    }
    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->grassEnabled) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_GROUND_END, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_OpaqueEnd(PlayState* play) {
    const bool postFxAo =
        CVarGetInteger("gNewDawn.PostFx.Enabled", 1) && CVarGetInteger("gNewDawn.PostFx.AO.Enabled", 1);

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!postFxAo && !params->shadowEnabled && !params->waterReflections && params->waterFoam <= 0.0f &&
        params->waterRefraction <= 0.0f && params->waterRestyle <= 0.0f && params->waterCaustics <= 0.0f &&
        !params->fogVolumetric &&
        !params->atmosBiome && !params->giEnabled && !params->localEnabled && !params->grassEnabled &&
        !params->interiorEnabled && !params->emissiveEnabled) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_OPAQUE_END, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_WaterXlu(PlayState* play) {
    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->waterEnabled) {
        return;
    }
    // The native sun glare is a full-screen fill drawn after this marker, so it covers the water on its own.
    params->waterSurfaceVisibility = 1.0f;
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, NEWDAWN_POSTFX_STAGE_WATER_XLU, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_SceneEnd(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1) && !CVarGetInteger("gNewDawn.Water.Enabled", 0) &&
        !CVarGetInteger("gNewDawn.Water.Reflections", 0) && !CVarGetInteger("gNewDawn.Water.Foam", 0) &&
        !CVarGetInteger("gNewDawn.Water.Refraction", 0) && !CVarGetInteger("gNewDawn.Water.Restyle", 0) &&
        !CVarGetInteger("gNewDawn.Water.Caustics", 0) && !CVarGetInteger("gNewDawn.Water.Underwater", 0) &&
        !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, NEWDAWN_POSTFX_STAGE_SCENE_END, NewDawnPostFx_AllocParams(play));
    CLOSE_DISPS(play->state.gfxCtx);
}
