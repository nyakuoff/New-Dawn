#ifndef MOUSE_LOOK_H
#define MOUSE_LOOK_H

#ifdef __cplusplus
extern "C" {
#endif

// Mouse delta (binang, 20/pixel at sensitivity 1) for this game frame, already zero unless mouse look is on and the cursor is captured.
// x: right is positive, y: down is positive. Scaled by sensitivity; Y already flipped when Invert Y is on.
float MouseLook_GetDeltaX(void);
float MouseLook_GetDeltaY(void);
int MouseLook_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif
