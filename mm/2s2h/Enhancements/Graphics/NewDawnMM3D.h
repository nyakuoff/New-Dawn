#ifndef NEWDAWN_MM3D_H
#define NEWDAWN_MM3D_H

#include "z64.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The caller must run the original room draw between these markers. Only its
// triangles are suppressed; display-list state, actors and collision stay native.
bool NewDawnMM3D_BeginRoom(PlayState* play, Room* room, u32 flags);
void NewDawnMM3D_EndRoom(PlayState* play, Room* room, u32 flags);

// Trial: swap the scene collision's vertex and polygon lists for the packed MM3D mesh (surface types, cameras and
// water boxes stay N64). Returns `original` unless gNewDawn.MM3D.Collision is on, the scene is on the collision
// trial list and its pack loads. The original resource is never modified.
CollisionHeader* NewDawnMM3D_CollisionOverride(PlayState* play, CollisionHeader* original);

#ifdef __cplusplus
}
#endif
#endif
