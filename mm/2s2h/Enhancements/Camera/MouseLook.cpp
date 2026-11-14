#include "MouseLook.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

static float sDeltaX = 0.0f;
static float sDeltaY = 0.0f;
static bool sActive = false;

extern "C" float MouseLook_GetDeltaX(void) {
    return sDeltaX;
}
extern "C" float MouseLook_GetDeltaY(void) {
    return sDeltaY;
}
extern "C" int MouseLook_IsActive(void) {
    return sActive;
}

// Capture follows the cvars; opening the menu releases it via Gui -> UpdateMouseCapture.
static void UpdateCapture() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window == nullptr) {
        return;
    }
    window->SetAutoCaptureMouse((CVarGetInteger("gSettings.EnableMouse", 0) &&
                                 CVarGetInteger("gSettings.AutoCaptureMouse", 1)) ||
                                CVarGetInteger("gEnhancements.Camera.MouseLook.Enable", 0));
    window->GetMouseStateManager()->UpdateMouseCapture();
}

static void RegisterMouseLook() {
    UpdateCapture();

    COND_HOOK(OnGameStateUpdate, CVarGetInteger("gEnhancements.Camera.MouseLook.Enable", 0), []() {
        auto window = Ship::Context::GetRawInstance()->GetWindow();
        // Capture implies the window is focused and the menu is closed.
        sActive = window->IsMouseCaptured() && !window->GetGui()->GetMenuOrMenubarVisible();
        Ship::Coords delta = window->GetMouseDelta();
        if (!sActive) {
            sDeltaX = sDeltaY = 0.0f;
            return;
        }
        // 20 binang (~0.11 deg) per pixel at sensitivity 1
        float sens = 20.0f * CVarGetFloat("gEnhancements.Camera.MouseLook.Sensitivity", 1.0f);
        sDeltaX = delta.x * sens;
        sDeltaY = delta.y * sens * (CVarGetInteger("gEnhancements.Camera.MouseLook.InvertY", 0) ? -1.0f : 1.0f);
    });
    if (!CVarGetInteger("gEnhancements.Camera.MouseLook.Enable", 0)) {
        sDeltaX = sDeltaY = 0.0f;
        sActive = false;
    }
}

static RegisterShipInitFunc initFunc(RegisterMouseLook, { "gEnhancements.Camera.MouseLook.Enable" });
