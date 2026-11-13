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
    // Interiors, poison/cursed skies, underwater views, final-day skies and scripted lighting retain native fog.
    if (!CVarGetInteger("gNewDawn.Fog.Volumetric", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        !params->shadowEnabled || play->skyboxId != SKYBOX_NORMAL_SKY || play->envCtx.skyboxDisabled ||
        play->envCtx.lightSettingOverride != LIGHT_SETTING_OVERRIDE_NONE || play->envCtx.changeSkyboxState != 0 ||
        play->envCtx.sandstormState != SANDSTORM_OFF || play->envCtx.skyboxFilterColor[3] != 0 ||
        (GET_ACTIVE_CAM(play)->stateFlags & CAM_STATE_UNDERWATER) || play->lightCtx.fogNear < 0 ||
        gSaveContext.save.day < 1 || gSaveContext.save.day >= 3 || gSaveContext.sceneLayer != 0 ||
        play->csCtx.state != CS_STATE_IDLE || Play_InCsMode(play)) {
        return;
    }
    params->fogBaseHeight = 0.0f; // fixed world altitude, never follows the player or camera
    params->fogHeight = 500.0f;
    params->fogDensityScale = 1.0f;
    switch (play->sceneId) {
        case SCENE_00KEIKOKU:
            break;
        case SCENE_30GYOSON:
            params->fogHeight = 350.0f;
            break;
        case SCENE_20SICHITAI2:
        case SCENE_21MITURINMAE:
            params->fogHeight = 300.0f;
            params->fogDensityScale = 1.3f;
            break;
        case SCENE_CLOCKTOWER:
        case SCENE_TOWN:
        case SCENE_ICHIBA:
        case SCENE_BACKTOWN:
            params->fogDensityScale = 0.45f;
            break;
        case SCENE_IKANA:
            params->fogBaseHeight = 400.0f; // only the normal, cleared sky; cursed SKYBOX_3 falls back above
            break;
        default:
            return;
    }
    params->fogVolumetric = 1;
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
    if (!CVarGetInteger("gNewDawn.HDR.Enabled", 0) || !CVarGetInteger("gNewDawn.Sky.Flare", 0))
        return;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    auto* params = static_cast<NewDawnPostFxParams*>(GRAPH_ALLOC(gfxCtx, sizeof(NewDawnPostFxParams)));
    memset(params, 0, sizeof(*params));
    params->flareSunPos[0] = sunPos->x;
    params->flareSunPos[1] = sunPos->y;
    params->flareSunPos[2] = sunPos->z;
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
        !CVarGetInteger("gNewDawn.Atmos.Particles", 0) && !CVarGetInteger("gNewDawn.Atmos.Weather", 0)) {
        return;
    }

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->shadowEnabled && !params->waterEnabled && !params->atmosBiome) {
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
        params->waterRefraction <= 0.0f && params->waterRestyle <= 0.0f && !params->atmosBiome) {
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
