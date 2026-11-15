#ifndef PERF_OVERLAY_H
#define PERF_OVERLAY_H
#ifdef __cplusplus

#include <ship/window/gui/GuiWindow.h>

// Translucent performance overlay (F3). Cvars: gNewDawn.Perf.Overlay, .Corner (0 TL, 1 TR, 2 BL, 3 BR), .Detailed.
// The overlay cvar also switches on the GPU timer queries in libultraship; with it off nothing runs.
class PerfOverlay : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {}
    void DrawElement() override {}
    void UpdateElement() override {}
    void Draw() override;
};

#endif
#endif
