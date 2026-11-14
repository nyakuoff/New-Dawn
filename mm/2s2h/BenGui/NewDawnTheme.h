#ifndef NEWDAWN_THEME_H
#define NEWDAWN_THEME_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

// New Dawn menu look: deep night blue and violet surfaces, warm moon-amber accents, parchment text.
namespace NewDawn::Theme {

// Palette.
constexpr ImVec4 Ink = { 0.035f, 0.042f, 0.095f, 1.0f };         // darkest, behind everything
constexpr ImVec4 Panel = { 0.062f, 0.072f, 0.150f, 1.0f };       // main panel
constexpr ImVec4 PanelRaised = { 0.098f, 0.112f, 0.215f, 1.0f }; // cards, frames
constexpr ImVec4 Frame = { 0.125f, 0.140f, 0.265f, 1.0f };
constexpr ImVec4 FrameHot = { 0.175f, 0.190f, 0.340f, 1.0f };
constexpr ImVec4 Outline = { 0.300f, 0.320f, 0.560f, 0.50f };
constexpr ImVec4 Dusk = { 0.470f, 0.390f, 0.800f, 1.0f };
constexpr ImVec4 Gold = { 0.930f, 0.740f, 0.340f, 1.0f };    // moon amber
constexpr ImVec4 GoldHot = { 1.000f, 0.860f, 0.520f, 1.0f }; // hovered amber
constexpr ImVec4 GoldDim = { 0.640f, 0.490f, 0.215f, 1.0f };
constexpr ImVec4 Parchment = { 0.930f, 0.900f, 0.820f, 1.0f }; // text
constexpr ImVec4 TextDim = { 0.640f, 0.640f, 0.740f, 1.0f };

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
ImVec4 WithAlpha(const ImVec4& c, float alpha);

// One layout unit: scales with the body font so the ImGui scale option keeps working.
float Unit();

// Installs the colours and metrics on the global ImGui style (called once, before ScaleImGui).
void ApplyStyle();

// Display fonts (Cinzel). Falls back to the current font until the archive provides them.
void PushHeading(int size); // 20, 24 or 36
void PopHeading();

// Dimmed game backdrop (vignette) drawn behind the menu panel.
void DrawBackdrop(float dim);

// Draws the wordmark scaled to the given width at the cursor; returns the drawn height (0 if unavailable).
float DrawWordmark(float width);

// Rail page entry: a rounded row with a gold marker and eased hover/press feedback.
bool RailItem(const char* id, const char* icon, const char* label, bool selected, float width, float height = 46.0f);
// Tab: text with an animated gold underline.
bool TabItem(const char* id, const char* label, bool selected);
// Section heading with a hairline: "SKY" ...........
void SectionHeader(const char* label);
// Soft rounded surface around a group of settings (an auto-height child window with padding).
void BeginSectionCard(const char* id);
void EndSectionCard();

// Capture aid: replays mouse clicks listed in gSettings.Menu.DebugClicks ("x,y;x,y") through ImGui's input queue.
void ReplayDebugClicks();

} // namespace NewDawn::Theme

#endif // NEWDAWN_THEME_H
