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

void NewDawnShadows_Begin(PlayState* play);

// Ambient occlusion and/or real-time shadows of everything opaque drawn so far. Call right after Actor_DrawAll,
// which is the end of the opaque list's world drawing.
void NewDawnPostFx_OpaqueEnd(PlayState* play);

// Bloom, colour grade and dithering of the finished world. Call at the end of the world drawing, before anything that
// belongs to the HUD.
void NewDawnPostFx_SceneEnd(PlayState* play);

#ifdef __cplusplus
};
#endif

#endif // NEWDAWN_POSTFX_H
