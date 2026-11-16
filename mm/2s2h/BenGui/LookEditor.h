#ifndef LOOK_EDITOR_H
#define LOOK_EDITOR_H
#ifdef __cplusplus

#include <ship/window/gui/GuiWindow.h>

// Developer window for the look profiles (docs/LOOK_PLAN.md): shows what decides the look of the current frame, edits
// the profiles live and saves them as JSON. Cvar: gWindows.LookEditor.
class LookEditorWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void DrawElement() override;
    void UpdateElement() override {
    }
};

#endif
#endif
