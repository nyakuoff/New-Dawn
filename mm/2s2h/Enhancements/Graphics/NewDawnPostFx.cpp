#include "NewDawnPostFx.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/newdawn_postfx.h>

extern "C" {
#include "global.h"
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
    return params;
}

extern "C" void NewDawnPostFx_OpaqueEnd(PlayState* play) {
    if (!CVarGetInteger("gNewDawn.PostFx.Enabled", 1) || !CVarGetInteger("gNewDawn.PostFx.AO.Enabled", 1)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPNewDawnPostFx(POLY_OPA_DISP++, NEWDAWN_POSTFX_STAGE_OPAQUE_END, NewDawnPostFx_AllocParams(play));
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
