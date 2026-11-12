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
    params->shadowMapSize = 2048;

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
    float strength =
        NewDawnClampF(CVarGetFloat("gNewDawn.Shadows.Strength", 1.0f), 0.0f, 1.0f) * horizonFade * colorFade;
    if (strength <= 0.001f) {
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
    int mapSize = CVarGetInteger("gNewDawn.Shadows.MapSize", 2048);
    mapSize = (int)NewDawnClampF((float)mapSize, 512.0f, 4096.0f);

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
    params->shadowCameraAt[0] = play->view.at.x;
    params->shadowCameraAt[1] = play->view.at.y;
    params->shadowCameraAt[2] = play->view.at.z;
    params->shadowCameraUp[0] = play->view.up.x;
    params->shadowCameraUp[1] = play->view.up.y;
    params->shadowCameraUp[2] = play->view.up.z;
}

static NewDawnPostFxParams* NewDawnPostFx_AllocParams(PlayState* play) {
    // The display list is walked once per interpolated output frame, so the parameters live in the per-frame graphics
    // pool, which stays valid until the frame is done.
    auto* params = static_cast<NewDawnPostFxParams*>(GRAPH_ALLOC(play->state.gfxCtx, sizeof(NewDawnPostFxParams)));

    // The same values Play_DrawMain hands to the projection (View_SetPerspective) and to Play_SetFog.
    s32 fogFar = play->lightCtx.zFar * (5.0f / 64.0f);

    params->zNear = play->view.zNear;
    params->zFar = play->view.zFar;
    params->fovyDegrees = play->view.fovy;
    params->fogNear = play->lightCtx.fogNear;
    params->fogFar = (fogFar <= 1000) ? 1000 : fogFar;
    params->flags = 0;
    NewDawnFillShadowParams(play, params);
    return params;
}

extern "C" void NewDawnShadows_Begin(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.Shadows.Enabled", 0)) {
        return;
    }

    auto* params = NewDawnPostFx_AllocParams(play);
    if (!params->shadowEnabled) {
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
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_XLU_DISP++, NEWDAWN_POSTFX_STAGE_SCENE_END, NewDawnPostFx_AllocParams(play));
    CLOSE_DISPS(play->state.gfxCtx);
}
