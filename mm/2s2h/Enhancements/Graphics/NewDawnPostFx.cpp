#include "NewDawnPostFx.h"
#include "NewDawnAtmos.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/newdawn_postfx.h>
#include <spdlog/spdlog.h>

extern "C" {
#include "global.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
}

static float NewDawnClampF(float value, float low, float high) {
    return (value < low) ? low : ((value > high) ? high : value);
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
    Vec3f dir = { (f32)sun->x, (f32)sun->y, (f32)sun->z };
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 0.001f) {
        return;
    }
    dir.x /= len;
    dir.y /= len;
    dir.z /= len;

    // Fade out at night and close to the horizon; the moon is a later pass.
    float horizonFade = NewDawnClampF((dir.y - 0.03f) / 0.25f, 0.0f, 1.0f);
    float luminance = (sun->color[0] + sun->color[1] + sun->color[2]) / (3.0f * 255.0f);
    float colorFade = NewDawnClampF(luminance * 3.0f, 0.0f, 1.0f);
    float fade = horizonFade * colorFade;
    float strength = NewDawnClampF(CVarGetFloat("gNewDawn.Shadows.Strength", 1.0f), 0.0f, 1.0f) * fade;
    // Sun shafts ride on the shadow map and the same fades.
    float shaftStrength = 0.0f;
    if (CVarGetInteger("gNewDawn.SunShafts.Enabled", 0)) {
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
    params->shadowSunDir[0] = dir.x;
    params->shadowSunDir[1] = dir.y;
    params->shadowSunDir[2] = dir.z;
    params->shadowAmbient[0] = play->lightCtx.ambientColor[0] / 255.0f;
    params->shadowAmbient[1] = play->lightCtx.ambientColor[1] / 255.0f;
    params->shadowAmbient[2] = play->lightCtx.ambientColor[2] / 255.0f;
    params->shadowDirColor[0] = sun->color[0] / 255.0f;
    params->shadowDirColor[1] = sun->color[1] / 255.0f;
    params->shadowDirColor[2] = sun->color[2] / 255.0f;
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
static const NewDawnWaterPalette sNewDawnWaterGeneric = { -1,
                                                          { 0.020f, 0.100f, 0.140f },
                                                          { 1 / 80.0f, 1 / 240.0f, 1 / 320.0f } };
static const NewDawnWaterPalette sNewDawnWaterPalettes[] = {
    // Great Bay: clear tropical shallows into deep blue-teal.
    { SCENE_30GYOSON, { 0.015f, 0.130f, 0.190f }, { 1 / 110.0f, 1 / 300.0f, 1 / 520.0f } },
    // Termina Field, Clock Town, ranch: clear, slightly green.
    { SCENE_00KEIKOKU, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_CLOCKTOWER, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_ICHIBA, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_BACKTOWN, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_TOWN, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_ALLEY, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    { SCENE_F01, { 0.040f, 0.150f, 0.100f }, { 1 / 130.0f, 1 / 380.0f, 1 / 260.0f } },
    // Cleared swamp: tannin brown-green, clear near the surface.
    { SCENE_20SICHITAI2, { 0.075f, 0.110f, 0.040f }, { 1 / 220.0f, 1 / 170.0f, 1 / 60.0f } },
    // Poisoned swamp (and the poison water in Woodfall Temple): sickly purple-magenta, nearly opaque.
    { SCENE_20SICHITAI, { 0.130f, 0.030f, 0.160f }, { 1 / 130.0f, 1 / 70.0f, 1 / 110.0f } },
    { SCENE_MITURIN, { 0.130f, 0.030f, 0.160f }, { 1 / 130.0f, 1 / 70.0f, 1 / 110.0f } },
    // Zora Hall: crystal blue.
    { SCENE_33ZORACITY, { 0.035f, 0.160f, 0.210f }, { 1 / 160.0f, 1 / 380.0f, 1 / 500.0f } },
    // Temples by theme: Great Bay green-teal, Snowhead icy blue, Stone Tower slate.
    { SCENE_SEA, { 0.020f, 0.150f, 0.170f }, { 1 / 90.0f, 1 / 200.0f, 1 / 170.0f } },
    { SCENE_HAKUGIN, { 0.040f, 0.220f, 0.360f }, { 1 / 90.0f, 1 / 300.0f, 1 / 600.0f } },
    { SCENE_INISIE_N, { 0.030f, 0.130f, 0.170f }, { 1 / 90.0f, 1 / 210.0f, 1 / 260.0f } },
    { SCENE_INISIE_R, { 0.030f, 0.130f, 0.170f }, { 1 / 90.0f, 1 / 210.0f, 1 / 260.0f } },
};

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
    // Without a time of day there is no sun or moon, so glints alone would only add the faint sky tint: interiors
    // skip the pass unless reflections, foam or refraction are on.
    if ((!glints && !reflections && !foam && !refraction && !restyle) || play->colCtx.colHeader == NULL ||
        (play->envCtx.lightMode != LIGHT_MODE_TIME && !reflections && !foam && !refraction && !restyle)) {
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
    if (count == 0) {
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
    if (restyle) {
        const NewDawnWaterPalette* palette = &sNewDawnWaterGeneric;
        for (const auto& entry : sNewDawnWaterPalettes) {
            if (entry.sceneId == play->sceneId) {
                palette = &entry;
            }
        }
        params->waterRestyle = 1.0f;
        for (int i = 0; i < 3; i++) {
            params->waterScatter[i] = palette->scatter[i];
            params->waterAbsorb[i] = palette->absorb[i];
        }
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
        params->fogDensityScale = 1.6f;
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
    NewDawnFillShadowParams(play, params);
    NewDawnFillFogParams(play, params);
    NewDawnFillWaterParams(play, params);
    NewDawnFillArtParams(play, params);
    NewDawnAtmos_Fill(play, params);
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

extern "C" void NewDawnHDR_WindScope(PlayState* play, int begin) {
    if (!CVarGetInteger("gNewDawn.Atmos.Wind", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) return;
    static NewDawnPostFxParams sUnused; // the command only needs a non-null pointer
    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, begin ? NEWDAWN_POSTFX_STAGE_WIND_BEGIN : NEWDAWN_POSTFX_STAGE_WIND_END, &sUnused);
    gSPNewDawnPostFx(POLY_XLU_DISP++, begin ? NEWDAWN_POSTFX_STAGE_WIND_BEGIN : NEWDAWN_POSTFX_STAGE_WIND_END, &sUnused);
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

static void NewDawnSky_Emit(PlayState* play, bool winterPass);

extern "C" void NewDawnSky_Draw(PlayState* play) {
    NewDawnSky_Emit(play, false);
}

// Winter Mountain Village has no skybox (SKYBOX_2): the game paints an opaque fog-colour fill. The overcast deck is
// emitted right after that fill and blends over it, so the fill stays as the fallback if the pass cannot run.
extern "C" void NewDawnSky_DrawOvercast(PlayState* play) {
    NewDawnSky_Emit(play, true);
}

static void NewDawnSky_Emit(PlayState* play, bool winterPass) {
    static u32 lastLogFrame = UINT32_MAX;
    static s16 lastLogScene = -1;
    if (CVarGetInteger("gNewDawn.Sky.Physical", 0) && CVarGetInteger("gNewDawn.PostFx.Debug.Stats", 0) &&
        play->gameplayFrames % 120 == 0 && (lastLogFrame != play->gameplayFrames || lastLogScene != play->sceneId)) {
        lastLogFrame = play->gameplayFrames;
        lastLogScene = play->sceneId;
        SPDLOG_INFO("NewDawnSky: scene {} sky {} config {} layer {} day {} mode {} sunDisabled {} lateSky {} override "
                    "{} cutscene {}",
                    play->sceneId, play->skyboxId, play->envCtx.skyboxConfig, gSaveContext.sceneLayer,
                    gSaveContext.save.day, play->envCtx.lightMode, play->envCtx.sunDisabled, play->skyboxCtx.shouldDraw,
                    play->envCtx.lightSettingOverride, Play_InCsMode(play));
    }
    const bool winter = winterPass && play->skyboxId == SKYBOX_2 && play->sceneId == SCENE_10YUKIYAMANOMURA;
    if (!CVarGetInteger("gNewDawn.Sky.Physical", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        CVarGetFloat("gNewDawn.Sky.Strength", 1.0f) <= 0.0f ||
        (winterPass ? !winter : play->skyboxId != SKYBOX_NORMAL_SKY) || play->skyboxCtx.shouldDraw ||
        play->envCtx.skyboxDisabled || play->envCtx.sunDisabled || play->envCtx.lightMode != LIGHT_MODE_TIME ||
        play->envCtx.lightSettingOverride != LIGHT_SETTING_OVERRIDE_NONE || play->envCtx.changeSkyboxState != 0 ||
        play->envCtx.skyboxConfig != play->envCtx.changeSkyboxNextConfig ||
        play->envCtx.sandstormState != SANDSTORM_OFF || play->envCtx.skyboxFilterColor[3] != 0 ||
        gSaveContext.save.day < 1 || gSaveContext.save.day >= 3 || gSaveContext.sceneLayer != 0 ||
        play->csCtx.state != CS_STATE_IDLE || Play_InCsMode(play)) {
        return;
    }

    // Explicitly known normal outdoor locations. Special skies (notably cursed Ikana's SKYBOX_3),
    // dungeons, cutscenes, the final day and unclassified scenes retain the original skybox.
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
        default:
            return;
    }
    const auto& sun = play->envCtx.dirLight1.params.dir;
    float len = sqrtf((float)(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z));
    if (len < 0.001f) {
        return;
    }
    auto* params = NewDawnPostFx_AllocParams(play);
    params->skySunDir[0] = sun.x / len;
    params->skySunDir[1] = sun.y / len;
    params->skySunDir[2] = sun.z / len;
    for (int i = 0; i < 3; ++i) {
        params->skyFogColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    // SKYBOX_2 never loads a palette; the overcast deck takes its hue from the fog.
    params->skyPaletteColor[0] = winter ? params->skyFogColor[0] : play->skyboxCtx.prim.r / 255.0f;
    params->skyPaletteColor[1] = winter ? params->skyFogColor[1] : play->skyboxCtx.prim.g / 255.0f;
    params->skyPaletteColor[2] = winter ? params->skyFogColor[2] : play->skyboxCtx.prim.b / 255.0f;
    params->skyOvercast = winter ? 1.0f : 0.0f;
    // Representative render-only ground albedo, used for the lower-hemisphere sky bounce.
    const bool grass = play->sceneId == SCENE_00KEIKOKU || play->sceneId == SCENE_F01 ||
                       play->sceneId == SCENE_20SICHITAI2 || play->sceneId == SCENE_22DEKUCITY;
    params->skyGroundAlbedo[0] = winter ? 0.70f : grass ? 0.24f : 0.42f;
    params->skyGroundAlbedo[1] = winter ? 0.74f : grass ? 0.30f : 0.32f;
    params->skyGroundAlbedo[2] = winter ? 0.80f : grass ? 0.10f : 0.18f; // fresh snow
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
        !CVarGetInteger("gNewDawn.Atmos.Particles", 0) && !CVarGetInteger("gNewDawn.Atmos.Weather", 0) &&
        !CVarGetInteger("gNewDawn.Fog.Volumetric", 0)) {
        return;
    }

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->shadowEnabled && !params->waterEnabled && !params->atmosBiome && !params->fogVolumetric) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_SHADOW_BEGIN, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_OpaqueEnd(PlayState* play) {
    const bool postFxAo =
        CVarGetInteger("gNewDawn.PostFx.Enabled", 1) && CVarGetInteger("gNewDawn.PostFx.AO.Enabled", 1);

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!postFxAo && !params->shadowEnabled && !params->waterReflections && params->waterFoam <= 0.0f &&
        params->waterRefraction <= 0.0f && params->waterRestyle <= 0.0f && !params->fogVolumetric &&
        !params->atmosBiome) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_OPAQUE_END, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_SceneEnd(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1) && !CVarGetInteger("gNewDawn.Water.Enabled", 0) &&
        !CVarGetInteger("gNewDawn.Water.Reflections", 0) && !CVarGetInteger("gNewDawn.Water.Foam", 0) &&
        !CVarGetInteger("gNewDawn.Water.Refraction", 0) && !CVarGetInteger("gNewDawn.Water.Restyle", 0) &&
        !CVarGetInteger("gNewDawn.HDR.Enabled", 0)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, NEWDAWN_POSTFX_STAGE_SCENE_END, NewDawnPostFx_AllocParams(play));
    CLOSE_DISPS(play->state.gfxCtx);
}
