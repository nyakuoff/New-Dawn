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

#ifdef __cplusplus
}
#endif
#endif
