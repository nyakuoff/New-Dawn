#ifndef NEWDAWN_POSTFX_H
#define NEWDAWN_POSTFX_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// New Dawn scene post-processing markers. Each one appends a marker command to the display list that makes the
// renderer run a stage (see libultraship/include/fast/newdawn_postfx.h).

// Start recording opaque world/actor shadow casters. Call just before Scene_Draw.
void NewDawnHDR_Begin(PlayState* play);
void NewDawnHDR_SunFlare(PlayState* play, const Vec3f* sunPos, int begin);
void NewDawnHDR_WeatherScope(PlayState* play, int begin);
void NewDawnHDR_WindScope(PlayState* play, int begin);
void NewDawnHDR_Sky(GraphicsContext* gfxCtx, int begin);
void NewDawnSky_Draw(PlayState* play);
void NewDawnSky_DrawOvercast(PlayState* play);

void NewDawnShadows_Begin(PlayState* play);

// Room geometry is drawn, actors are not yet: the grass takes its ground colour here. Call right before Actor_DrawAll.
void NewDawnPostFx_GroundEnd(PlayState* play);

// Ambient occlusion and/or real-time shadows of everything opaque drawn so far. Call right after Actor_DrawAll,
// which is the end of the opaque list's world drawing.
void NewDawnPostFx_OpaqueEnd(PlayState* play);

// Water surface pass (foam, refraction, restyle, glints, reflections). Call after the actors have drawn and before the
// soft-sprite effects (splashes, ripples, dust), so those effects composite above the water instead of under it.
void NewDawnPostFx_WaterXlu(PlayState* play);

// Bloom, colour grade and dithering of the finished world. Call at the end of the world drawing, before anything that
// belongs to the HUD.
void NewDawnPostFx_SceneEnd(PlayState* play);

#ifdef __cplusplus
};
#endif

#endif // NEWDAWN_POSTFX_H
