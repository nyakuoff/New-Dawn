#include "NewDawnPostFx.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/newdawn_postfx.h>

extern "C" {
#include "global.h"
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
};
static const NewDawnWaterScene sNewDawnWaterScenes[] = {
    { SCENE_20SICHITAI, 0.30f }, // Southern Swamp (poisoned)
};

static void NewDawnFillWaterParams(PlayState* play, NewDawnPostFxParams* params) {
    params->waterEnabled = 0;
    if (!CVarGetInteger("gNewDawn.Water.Enabled", 0) || play->colCtx.colHeader == NULL) {
        return;
    }
    const CollisionHeader* col = play->colCtx.colHeader;
    // Scenes with more boxes than fit (Great Bay Temple has 16 and dungeons can have more) keep the nearest to the camera.
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

    float sceneFactor = 1.0f;
    for (const auto& entry : sNewDawnWaterScenes) {
        if (entry.sceneId == play->sceneId) {
            sceneFactor = entry.strength;
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
    params->waterDebug = CVarGetInteger("gNewDawn.Water.Debug", 0);
    params->waterStrength =
        NewDawnClampF(CVarGetFloat("gNewDawn.Water.Strength", 0.5f), 0.0f, 1.0f) * sceneFactor * skyScale;
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
    NewDawnFillWaterParams(play, params);
    return params;
}

extern "C" void NewDawnShadows_Begin(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.Shadows.Enabled", 0) && !CVarGetInteger("gNewDawn.Water.Enabled", 0)) {
        return;
    }

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->shadowEnabled && !params->waterEnabled) {
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
    if (!postFxAo && !params->shadowEnabled) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_OPAQUE_END, params);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void NewDawnPostFx_SceneEnd(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1) && !CVarGetInteger("gNewDawn.Water.Enabled", 0)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, NEWDAWN_POSTFX_STAGE_SCENE_END, NewDawnPostFx_AllocParams(play));
    CLOSE_DISPS(play->state.gfxCtx);
}
