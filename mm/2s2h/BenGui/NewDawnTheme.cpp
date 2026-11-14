#include "NewDawnTheme.h"

#include <imgui_internal.h>
#include <fast/Fast3dGui.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/Window.h>
#include "BenPort.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <string>
#include <vector>

namespace NewDawn::Theme {

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 WithAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

float Unit() {
    return ImGui::GetFontSize() / 20.0f;
}

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    c[ImGuiCol_Text] = Parchment;
    c[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.46f, 0.58f, 1.0f);
    c[ImGuiCol_WindowBg] = WithAlpha(Panel, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.080f, 0.168f, 0.98f);
    c[ImGuiCol_Border] = Outline;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = Frame;
    c[ImGuiCol_FrameBgHovered] = FrameHot;
    c[ImGuiCol_FrameBgActive] = Mix(FrameHot, Gold, 0.18f);
    c[ImGuiCol_TitleBg] = Ink;
    c[ImGuiCol_TitleBgActive] = Mix(Ink, Dusk, 0.30f);
    c[ImGuiCol_TitleBgCollapsed] = WithAlpha(Ink, 0.8f);
    c[ImGuiCol_MenuBarBg] = Ink;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = WithAlpha(Dusk, 0.45f);
    c[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(Dusk, 0.75f);
    c[ImGuiCol_ScrollbarGrabActive] = Gold;
    c[ImGuiCol_CheckMark] = Gold;
    c[ImGuiCol_SliderGrab] = Gold;
    c[ImGuiCol_SliderGrabActive] = GoldHot;
    c[ImGuiCol_Button] = Mix(PanelRaised, Dusk, 0.25f);
    c[ImGuiCol_ButtonHovered] = Mix(PanelRaised, Dusk, 0.50f);
    c[ImGuiCol_ButtonActive] = Mix(PanelRaised, Gold, 0.35f);
    c[ImGuiCol_Header] = WithAlpha(Dusk, 0.30f);
    c[ImGuiCol_HeaderHovered] = WithAlpha(Dusk, 0.50f);
    c[ImGuiCol_HeaderActive] = WithAlpha(Gold, 0.35f);
    c[ImGuiCol_Separator] = Outline;
    c[ImGuiCol_SeparatorHovered] = GoldDim;
    c[ImGuiCol_SeparatorActive] = Gold;
    c[ImGuiCol_ResizeGrip] = WithAlpha(Dusk, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = WithAlpha(Dusk, 0.60f);
    c[ImGuiCol_ResizeGripActive] = Gold;
    c[ImGuiCol_Tab] = Mix(Ink, Dusk, 0.25f);
    c[ImGuiCol_TabHovered] = Mix(Ink, Dusk, 0.60f);
    c[ImGuiCol_TabSelected] = Mix(Ink, Gold, 0.40f);
    c[ImGuiCol_TabSelectedOverline] = Gold;
    c[ImGuiCol_TabDimmed] = Mix(Ink, Dusk, 0.12f);
    c[ImGuiCol_TabDimmedSelected] = Mix(Ink, Gold, 0.22f);
    c[ImGuiCol_TextSelectedBg] = WithAlpha(Gold, 0.35f);
    c[ImGuiCol_TextLink] = GoldHot;
    c[ImGuiCol_NavCursor] = Gold;
    c[ImGuiCol_NavWindowingHighlight] = WithAlpha(Gold, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.06f, 0.65f);
    c[ImGuiCol_TableHeaderBg] = PanelRaised;
    c[ImGuiCol_TableBorderStrong] = Outline;
    c[ImGuiCol_TableBorderLight] = WithAlpha(Outline, 0.3f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_PlotHistogram] = Gold;
    c[ImGuiCol_PlotLines] = Dusk;

    s.WindowRounding = 14.0f;
    s.ChildRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 12.0f;
    s.GrabRounding = 8.0f;
    s.TabRounding = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(16.0f, 14.0f);
    s.FramePadding = ImVec2(12.0f, 8.0f);
    s.ItemSpacing = ImVec2(12.0f, 10.0f);
    s.ItemInnerSpacing = ImVec2(10.0f, 6.0f);
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 18.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(20.0f, 4.0f);
}

void PushHeading(int size) {
    ImFont* font = nullptr;
    if (OTRGlobals::Instance != nullptr) {
        font = size >= 36   ? OTRGlobals::Instance->fontHeadingLarge
               : size >= 24 ? OTRGlobals::Instance->fontHeading
                            : OTRGlobals::Instance->fontHeadingSmall;
    }
    ImGui::PushFont(font != nullptr ? font : ImGui::GetFont());
}

void PopHeading() {
    ImGui::PopFont();
}

void DrawBackdrop(float dim) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 a = vp->Pos, b = vp->Pos + vp->Size;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(WithAlpha(Ink, dim)));
    // Vignette: the edges fall off to near black so the centre panel reads as a lit window.
    const float edge = vp->Size.y * 0.35f;
    ImU32 dark = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.55f * dim));
    ImU32 clear = ImGui::GetColorU32(ImVec4(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + edge), dark, dark, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - edge), b, clear, clear, dark, dark);
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + edge, b.y), dark, clear, clear, dark);
    dl->AddRectFilledMultiColor(ImVec2(b.x - edge, a.y), b, clear, dark, dark, clear);
}

static const char* kWordmarkName = "NewDawn_Wordmark";
static const char* kWordmarkPath = "textures/newdawn/wordmark.png";

float DrawWordmark(float width) {
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    if (gui == nullptr) {
        return 0.0f;
    }
    static bool tried = false;
    if (!tried && !gui->HasTextureByName(kWordmarkName)) {
        tried = true;
        auto archive = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
        if (archive != nullptr && archive->HasFile(kWordmarkPath)) {
            gui->LoadTextureFromRawImage(kWordmarkName, kWordmarkPath);
        }
    }
    if (!gui->HasTextureByName(kWordmarkName)) {
        return 0.0f;
    }
    ImVec2 tex = gui->GetTextureSize(kWordmarkName);
    float height = width * tex.y / tex.x;
    ImGui::Image(gui->GetTextureByName(kWordmarkName), ImVec2(width, height));
    return height;
}

static float Ease(ImGuiID id, float target, float speed = 14.0f) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float v = st->GetFloat(id, 0.0f);
    float k = ImMin(1.0f, ImGui::GetIO().DeltaTime * speed);
    v += (target - v) * k;
    if (fabsf(target - v) < 0.002f) {
        v = target;
    }
    st->SetFloat(id, v);
    return v;
}

bool RailItem(const char* strId, const char* icon, const char* label, bool selected, float width, float height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float u = Unit();
    const ImGuiID id = window->GetID(strId);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(width, height * u));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    float hot = Ease(id, hovered ? (held ? 1.4f : 1.0f) : 0.0f);
    float sel = Ease(window->GetID("##sel") ^ id, selected ? 1.0f : 0.0f, 12.0f);

    ImDrawList* dl = window->DrawList;
    float round = 10.0f * u;
    ImVec4 fill = Mix(WithAlpha(Dusk, 0.0f), WithAlpha(Dusk, 0.26f), ImClamp(hot * 0.8f, 0.0f, 1.0f));
    fill = Mix(fill, WithAlpha(Gold, 0.15f), sel);
    dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), round);
    if (sel > 0.01f) {
        float barH = 24.0f * u * sel;
        float cy = (bb.Min.y + bb.Max.y) * 0.5f;
        dl->AddRectFilled(ImVec2(bb.Min.x + 2.0f * u, cy - barH * 0.5f), ImVec2(bb.Min.x + 5.0f * u, cy + barH * 0.5f),
                          ImGui::GetColorU32(Gold), 2.0f * u);
    }
    ImVec4 col = Mix(Mix(TextDim, Parchment, ImClamp(hot, 0.0f, 1.0f)), GoldHot, sel);
    ImU32 colU = ImGui::GetColorU32(col);
    float x = bb.Min.x + 18.0f * u;
    if (icon != nullptr && icon[0] != '\0') {
        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(x, (bb.Min.y + bb.Max.y) * 0.5f - isz.y * 0.5f), colU, icon);
        x += 34.0f * u;
    }
    PushHeading(24);
    ImVec2 tsz = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(x + 2.0f * u * hot, (bb.Min.y + bb.Max.y) * 0.5f - tsz.y * 0.5f), colU, label);
    PopHeading();
    ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_Compact);
    return pressed;
}

bool TabItem(const char* strId, const char* label, bool selected) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float u = Unit();
    const ImGuiID id = window->GetID(strId);
    const ImVec2 tsz = ImGui::CalcTextSize(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(tsz.x + 28.0f * u, tsz.y + 18.0f * u));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    float hot = Ease(id, hovered ? 1.0f : 0.0f);
    float sel = Ease(window->GetID("##tsel") ^ id, selected ? 1.0f : 0.0f, 12.0f);
    ImDrawList* dl = window->DrawList;
    if (hot > 0.01f) {
        dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(WithAlpha(Dusk, 0.18f * hot)), 8.0f * u);
    }
    ImVec4 col = Mix(Mix(TextDim, Parchment, hot), GoldHot, sel);
    dl->AddText(bb.Min + ImVec2(14.0f * u, 9.0f * u), ImGui::GetColorU32(col), label);
    float lineW = (bb.GetWidth() - 20.0f * u) * sel;
    float cx = (bb.Min.x + bb.Max.x) * 0.5f;
    if (lineW > 0.5f) {
        dl->AddRectFilled(ImVec2(cx - lineW * 0.5f, bb.Max.y - 3.0f * u), ImVec2(cx + lineW * 0.5f, bb.Max.y - 1.0f * u),
                          ImGui::GetColorU32(Gold), 1.5f * u);
    }
    ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_Compact);
    return pressed;
}

void SectionHeader(const char* label) {
    const float u = Unit();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }
    ImGui::Dummy(ImVec2(0, 4.0f * u));
    PushHeading(20);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 tsz = ImGui::CalcTextSize(label);
    float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = window->DrawList;
    dl->AddText(pos, ImGui::GetColorU32(Gold), label);
    float lineX0 = pos.x + tsz.x + 14.0f * u;
    float lineY = pos.y + tsz.y * 0.55f;
    if (lineX0 < pos.x + avail) {
        dl->AddLine(ImVec2(lineX0, lineY), ImVec2(pos.x + avail, lineY), ImGui::GetColorU32(WithAlpha(Gold, 0.28f)),
                    1.0f);
    }
    ImGui::Dummy(ImVec2(avail, tsz.y));
    PopHeading();
    ImGui::Dummy(ImVec2(0, 2.0f * u));
}

void BeginSectionCard(const char* id) {
    const float u = Unit();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(PanelRaised, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(Outline, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f * u);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * u, 14.0f * u));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndSectionCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// Capture aid: gSettings.Menu.DebugClicks="x,y;x,y;..." replays real mouse clicks through ImGui's input queue (one
// every two seconds, starting four seconds after the first menu or popup draw), so headless captures can exercise the widgets.
void ReplayDebugClicks() {
    static std::vector<ImVec2> clicks;
    static bool parsed = false;
    static double startTime = -1.0;
    if (!parsed) {
        parsed = true;
        std::string spec = CVarGetString("gSettings.Menu.DebugClicks", "");
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t end = spec.find(';', pos);
            std::string item = spec.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            float x, y;
            if (sscanf(item.c_str(), "%f,%f", &x, &y) == 2) {
                clicks.push_back({ x, y });
            }
            pos = end == std::string::npos ? spec.size() : end + 1;
        }
    }
    if (clicks.empty()) {
        return;
    }
    if (startTime < 0.0) {
        startTime = ImGui::GetTime();
    }
    const double t = ImGui::GetTime() - startTime - 4.0;
    if (t < 0.0) {
        return;
    }
    const size_t index = static_cast<size_t>(t / 2.0);
    if (index >= clicks.size()) {
        return;
    }
    const double phase = t - index * 2.0;
    static int lastAction = -1;
    const int action = phase < 0.3 ? 0 : (phase < 0.5 ? 1 : 2);
    const int token = static_cast<int>(index) * 3 + action;
    if (token == lastAction) {
        return;
    }
    lastAction = token;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(clicks[index].x, clicks[index].y);
    if (action == 1) {
        io.AddMouseButtonEvent(0, true);
    } else if (action == 2) {
        io.AddMouseButtonEvent(0, false);
    }
}

} // namespace NewDawn::Theme
