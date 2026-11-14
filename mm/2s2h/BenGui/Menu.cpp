#include "Menu.h"
#include "UIWidgets.hpp"
#include "NewDawnTheme.h"
#include "NewDawnPresets.h"
#include "build.h"
#include "BenPort.h"
#include "BenInputEditorWindow.h"
#include <ship/window/gui/GuiElement.h>
#include "BenModals.h"
#include "Notification.h"
#include <variant>
#include <spdlog/fmt/fmt.h>
#include "variables.h"
#include <tuple>
#include <ship/config/Config.h>

#include <fast/Fast3dWindow.h>

extern "C" {
#include "z64.h"
#include "functions.h"
extern PlayState* gPlayState;
}
std::vector<ImVec2> windowTypeSizes = { {} };

extern std::unordered_map<s16, const char*> warpPointSceneList;
extern void Warp();

namespace BenGui {
extern std::shared_ptr<BenModalWindow> mModalWindow;
}
std::vector<SearchWidget> extraSearchWidgets = {};

namespace Ship {
std::string disabledTempTooltip;
const char* disabledTooltip;
bool disabledValue = false;

bool operator==(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return l.r == r.r && l.g == r.g && l.b == r.b;
}

bool operator==(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return l.r == r.r && l.g == r.g && l.b == r.b && l.a == r.a;
}

bool operator<(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return (l.r < r.r && l.g <= r.g && l.b <= r.b) || (l.r <= r.r && l.g < r.g && l.b <= r.b) ||
           (l.r <= r.r && l.g <= r.g && l.b < r.b);
}

bool operator<(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return (l.r < r.r && l.g <= r.g && l.b <= r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g < r.g && l.b <= r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g <= r.g && l.b < r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g <= r.g && l.b <= r.b && l.a < r.a);
}

bool operator>(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return (l.r > r.r && l.g >= r.g && l.b >= r.b) || (l.r >= r.r && l.g > r.g && l.b >= r.b) ||
           (l.r >= r.r && l.g >= r.g && l.b > r.b);
}

bool operator>(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return (l.r > r.r && l.g >= r.g && l.b >= r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g > r.g && l.b >= r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g >= r.g && l.b > r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g >= r.g && l.b >= r.b && l.a > r.a);
}

uint32_t GetVectorIndexOf(std::vector<std::string>& vector, std::string value) {
    return std::distance(vector.begin(), std::find(vector.begin(), vector.end(), value));
}

void Menu::InsertSidebarSearch() {
    menuEntries["Settings"].sidebars.emplace("Search", searchSidebarEntry);
    uint32_t curIndex = 0;
    if (!Ship_IsCStringEmpty(CVarGetString(menuEntries["Settings"].sidebarCvar, ""))) {
        curIndex = GetVectorIndexOf(menuEntries["Settings"].sidebarOrder,
                                    CVarGetString(menuEntries["Settings"].sidebarCvar, ""));
    }
    menuEntries["Settings"].sidebarOrder.insert(menuEntries["Settings"].sidebarOrder.begin() + searchSidebarIndex,
                                                "Search");
    if (curIndex > searchSidebarIndex) {
        CVarSetString(menuEntries["Settings"].sidebarCvar, menuEntries["Settings"].sidebarOrder.at(curIndex).c_str());
    }
}

void Menu::RemoveSidebarSearch() {
    uint32_t curIndex =
        GetVectorIndexOf(menuEntries["Settings"].sidebarOrder, CVarGetString(menuEntries["Settings"].sidebarCvar, ""));
    menuEntries["Settings"].sidebars.erase("Search");
    std::erase_if(menuEntries["Settings"].sidebarOrder, [](std::string& name) { return name == "Search"; });
    if (curIndex > searchSidebarIndex) {
        curIndex--;
    } else if (curIndex >= menuEntries["Settings"].sidebarOrder.size()) {
        curIndex = menuEntries["Settings"].sidebarOrder.size() - 1;
    }
    CVarSetString(menuEntries["Settings"].sidebarCvar, menuEntries["Settings"].sidebarOrder.at(curIndex).c_str());
}

void Menu::UpdateWindowBackendObjects() {
    int32_t runningWindowBackend = Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend();
    int32_t configWindowBackendId = Ship::Context::GetRawInstance()->GetConfig()->GetInt("Window.Backend.Id", -1);
    if (Ship::Context::GetRawInstance()->GetWindow()->IsAvailableWindowBackend(configWindowBackendId)) {
        configWindowBackend = static_cast<Fast::WindowBackend>(configWindowBackendId);
    } else {
        configWindowBackend = static_cast<Fast::WindowBackend>(runningWindowBackend);
    }

    availableWindowBackends = Ship::Context::GetRawInstance()->GetWindow()->GetAvailableWindowBackends();
    for (auto& backend : *availableWindowBackends) {
        availableWindowBackendsMap[backend] = windowBackendsMap.at(backend);
    }
}

bool Menu::IsMenuPopped() {
    return popped;
}

UIWidgets::Colors Menu::GetMenuThemeColor() {
    return menuThemeIndex;
}

Menu::Menu(const std::string& cVar, const std::string& name, uint8_t searchSidebarIndex_,
           UIWidgets::Colors defaultThemeIndex_)
    : GuiWindow(cVar, name), searchSidebarIndex(searchSidebarIndex_), defaultThemeIndex(defaultThemeIndex_) {
}

void Menu::InitElement() {
    popped = false; // the New Dawn menu is always a full-window overlay
    poppedSize.x = CVarGetInteger("gSettings.Menu.PoppedWidth", 1280);
    poppedSize.y = CVarGetInteger("gSettings.Menu.PoppedHeight", 800);
    poppedPos.x = CVarGetInteger("gSettings.Menu.PoppedPos.x", 0);
    poppedPos.y = CVarGetInteger("gSettings.Menu.PoppedPos.y", 0);

    UpdateWindowBackendObjects();
}

void Menu::UpdateElement() {
    menuThemeIndex = static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", defaultThemeIndex));
}

static uint32_t WrapChars() {
    // WrappedText() wraps by character count; derive it from the free width so labels never run off the card.
    float avail = ImGui::GetContentRegionAvail().x - 90.0f * NewDawn::Theme::Unit();
    return static_cast<uint32_t>(std::max(28.0f, avail / (ImGui::GetFontSize() * 0.56f)));
}

static std::string NormalizeSearch(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    return text;
}

uint32_t Menu::DrawSearchResults(std::string& menuSearchText) {
    auto accent = static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", defaultThemeIndex));
    const std::string needle = NormalizeSearch(menuSearchText);
    uint32_t searchCount = 0;
    const bool developer = NewDawn::IsDeveloperMode();
    for (auto& page : menuPages) {
        if (page.devOnly && !developer) {
            continue;
        }
        for (auto& tab : page.tabs) {
            auto entryIt = menuEntries.find(tab.entry);
            if (entryIt == menuEntries.end() || !entryIt->second.sidebars.contains(tab.sidebar)) {
                continue;
            }
            auto& sidebar = entryIt->second.sidebars.at(tab.sidebar);
            for (size_t i = 0; i < sidebar.columnWidgets.size(); i++) {
                for (auto& info : sidebar.columnWidgets.at(i)) {
                    if (info.type == WIDGET_SEARCH || info.type == WIDGET_SEPARATOR ||
                        info.type == WIDGET_SEPARATOR_TEXT || info.isHidden || info.hideInSearch) {
                        continue;
                    }
                    const char* tooltip = info.options->tooltip;
                    std::string widgetStr =
                        NormalizeSearch(info.name + std::string(tooltip != nullptr ? tooltip : "") + info.group);
                    if (widgetStr.find(needle) == std::string::npos) {
                        continue;
                    }
                    MenuDrawItem(info, WrapChars(), accent);
                    ImGui::PushStyleColor(ImGuiCol_Text, NewDawn::Theme::TextDim);
                    ImGui::Text("   %s  >  %s", page.label.c_str(), tab.label.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0, 4.0f * NewDawn::Theme::Unit()));
                    searchCount++;
                }
            }
        }
    }
    for (auto& entry : extraSearchWidgets) {
        if (entry.info.type == WIDGET_SEARCH || entry.info.type == WIDGET_SEPARATOR ||
            entry.info.type == WIDGET_SEPARATOR_TEXT || entry.info.isHidden || entry.info.hideInSearch) {
            continue;
        }
        std::string widgetStr =
            NormalizeSearch(entry.info.name + entry.info.options->tooltip + entry.extraTerms + entry.sidebarName);
        if (widgetStr.find(needle) != std::string::npos) {
            MenuDrawItem(entry.info, WrapChars(), accent);
            ImGui::PushStyleColor(ImGuiCol_Text, NewDawn::Theme::TextDim);
            ImGui::Text("   %s  >  %s, %s", entry.menuName.c_str(), entry.sidebarName.c_str(), entry.location.c_str());
            ImGui::PopStyleColor();
            searchCount++;
        }
    }
    return searchCount;
}

void Menu::AddMenuEntry(std::string entryName, const char* entryCvar) {
    menuEntries.emplace(entryName, MainMenuEntry{ entryName, entryCvar });
    menuOrder.push_back(entryName);
}

void Menu::AddPage(MenuPage page) {
    menuPages.push_back(std::move(page));
}

void Menu::AddSearchWidget(SearchWidget widget) {
    extraSearchWidgets.push_back(widget);
}

std::unordered_map<uint32_t, disabledInfo>& Menu::GetDisabledMap() {
    return disabledMap;
}

void Menu::MenuDrawItem(WidgetInfo& widget, uint32_t width, UIWidgets::Colors menuThemeIndex) {
    disabledTempTooltip = "This setting is disabled because: \n\n";
    disabledValue = false;
    disabledTooltip = " ";

    if (widget.preFunc != nullptr) {
        widget.ResetDisables();
        widget.preFunc(widget);
        if (widget.isHidden) {
            return;
        }
        if (!widget.activeDisables.empty()) {
            widget.options->disabled = true;
            for (auto option : widget.activeDisables) {
                disabledTempTooltip += std::string("- ") + disabledMap.at(option).reason + std::string("\n");
            }
            widget.options->disabledTooltip = disabledTempTooltip.c_str();
        }
    }

    if (widget.sameLine) {
        ImGui::SameLine();
    }

    try {
        switch (widget.type) {
            case WIDGET_CHECKBOX: {
                bool* pointer = std::get<bool*>(widget.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("Checkbox Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                auto options = std::static_pointer_cast<UIWidgets::CheckboxOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::Checkbox(UIWidgets::WrappedText(widget.name.c_str(), width).c_str(), pointer,
                                        *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_CVAR_CHECKBOX: {
                auto options = std::static_pointer_cast<UIWidgets::CheckboxOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::CVarCheckbox(UIWidgets::WrappedText(widget.name.c_str(), width).c_str(), widget.cVar,
                                            *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                };
            } break;
            case WIDGET_AUDIO_BACKEND: {
                auto currentAudioBackend = Ship::Context::GetRawInstance()->GetAudio()->GetCurrentAudioBackend();
                UIWidgets::ComboboxOptions options = {};
                options.color = menuThemeIndex;
                options.tooltip = "Sets the audio API used by the game. Requires a relaunch to take effect.";
                options.disabled =
                    Ship::Context::GetRawInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1;
                options.disabledTooltip = "Only one audio API is available on this platform.";
                if (UIWidgets::Combobox("Audio API", &currentAudioBackend, &audioBackendsMap, options)) {
                    Ship::Context::GetRawInstance()->GetAudio()->SetCurrentAudioBackend(currentAudioBackend);
                }
            } break;
            case WIDGET_VIDEO_BACKEND: {
                UIWidgets::ComboboxOptions options = {};
                options.color = menuThemeIndex;
                options.tooltip = "Sets the renderer API used by the game.";
                options.disabled = availableWindowBackends->size() <= 1;
                options.disabledTooltip = "Only one renderer API is available on this platform.";
                if (UIWidgets::Combobox("Renderer API (Needs reload)", &configWindowBackend,
                                        &availableWindowBackendsMap, options)) {
                    Ship::Context::GetRawInstance()->GetConfig()->SetInt("Window.Backend.Id", configWindowBackend);
                    Ship::Context::GetRawInstance()->GetConfig()->SetString("Window.Backend.Name",
                                                                            windowBackendsMap.at(configWindowBackend));
                    Ship::Context::GetRawInstance()->GetConfig()->Save();
                    UpdateWindowBackendObjects();
                }
            } break;
            case WIDGET_SEPARATOR: {
                ImGui::Separator();
            } break;
            case WIDGET_SEPARATOR_TEXT: {
                NewDawn::Theme::SectionHeader(widget.name.c_str());
            } break;
            case WIDGET_TEXT: {
                if (widget.options->color != UIWidgets::Colors::NoColor) {
                    ImGui::PushStyleColor(ImGuiCol_Text, UIWidgets::ColorValues.at(widget.options->color));
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", widget.name.c_str());
                if (widget.options->color != UIWidgets::Colors::NoColor) {
                    ImGui::PopStyleColor();
                }
            } break;
            case WIDGET_COMBOBOX: {
                int32_t* pointer = std::get<int32_t*>(widget.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("Combobox Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                auto options = std::static_pointer_cast<UIWidgets::ComboboxOptions>(widget.options);
                options->color = menuThemeIndex;
                bool result = false;
                if (std::holds_alternative<UIWidgets::ComboVec_t>(options->comboVariant)) {
                    result = UIWidgets::Combobox(widget.name.c_str(), pointer,
                                                 *std::get<UIWidgets::ComboVec_t>(options->comboVariant), *options);
                } else if (std::holds_alternative<UIWidgets::ComboMap_t>(options->comboVariant)) {
                    result = UIWidgets::Combobox(widget.name.c_str(), pointer,
                                                 std::get<UIWidgets::ComboMap_t>(options->comboVariant), *options);
                }
                if (result) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                };
            } break;
            case WIDGET_CVAR_COMBOBOX: {
                auto options = std::static_pointer_cast<UIWidgets::ComboboxOptions>(widget.options);
                options->color = menuThemeIndex;
                bool result = false;
                if (std::holds_alternative<UIWidgets::ComboVec_t>(options->comboVariant)) {
                    result = UIWidgets::CVarCombobox(widget.name.c_str(), widget.cVar,
                                                     *std::get<UIWidgets::ComboVec_t>(options->comboVariant), *options);
                } else if (std::holds_alternative<UIWidgets::ComboMap_t>(options->comboVariant)) {
                    result = UIWidgets::CVarCombobox(widget.name.c_str(), widget.cVar,
                                                     std::get<UIWidgets::ComboMap_t>(options->comboVariant), *options);
                }
                if (result) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_SLIDER_INT: {
                int32_t* pointer = std::get<int32_t*>(widget.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("int32 Slider Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                auto options = std::static_pointer_cast<UIWidgets::IntSliderOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::SliderInt(widget.name.c_str(), pointer, *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                };
            } break;
            case WIDGET_CVAR_SLIDER_INT: {
                auto options = std::static_pointer_cast<UIWidgets::IntSliderOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::CVarSliderInt(widget.name.c_str(), widget.cVar, *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                };
            } break;
            case WIDGET_SLIDER_FLOAT: {
                float* pointer = std::get<float*>(widget.valuePointer);

                if (pointer == nullptr) {
                    SPDLOG_ERROR("float Slider Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                auto options = std::static_pointer_cast<UIWidgets::FloatSliderOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::SliderFloat(widget.name.c_str(), pointer, *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_CVAR_SLIDER_FLOAT: {
                auto options = std::static_pointer_cast<UIWidgets::FloatSliderOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::CVarSliderFloat(widget.name.c_str(), widget.cVar, *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_CVAR_BTN_SELECTOR: {
                auto options = std::static_pointer_cast<UIWidgets::BtnSelectorOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::CVarBtnSelector(widget.name.c_str(), widget.cVar, *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_BUTTON: {
                auto options = std::static_pointer_cast<UIWidgets::ButtonOptions>(widget.options);
                options->color = menuThemeIndex;
                if (UIWidgets::Button(widget.name.c_str(), *options)) {
                    if (widget.callback != nullptr) {
                        widget.callback(widget);
                    }
                }
            } break;
            case WIDGET_CUSTOM: {
                if (widget.customFunction != nullptr) {
                    widget.customFunction(widget);
                }
            } break;
            case WIDGET_WINDOW_BUTTON: {
                if (widget.windowName == nullptr || widget.windowName[0] == '\0') {
                    std::string msg =
                        fmt::format("Error drawing window contents for {}: windowName not defined", widget.name);
                    SPDLOG_ERROR(msg.c_str());
                    break;
                }
                auto window = Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow(widget.windowName);
                if (!window) {
                    std::string msg =
                        fmt::format("Error drawing window contents: windowName {} does not exist", widget.windowName);
                    SPDLOG_ERROR(msg.c_str());
                    break;
                }
                auto options = std::static_pointer_cast<UIWidgets::WindowButtonOptions>(widget.options);
                options->color = menuThemeIndex;
                UIWidgets::WindowButton(widget.name.c_str(), widget.cVar, window, *options);
                if (!window->IsVisible()) {
                    window->DrawElement();
                }
            } break;
            case WIDGET_SEARCH: {
                UIWidgets::PushStyleButton(menuThemeIndex);
                if (ImGui::Button("Clear")) {
                    menuSearch.Clear();
                }
                ImGui::SameLine();
                if (CVarGetInteger("gSettings.Menu.SearchAutofocus", 0) &&
                    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
                    !ImGui::IsMouseClicked(0)) {
                    ImGui::SetKeyboardFocusHere(0);
                }
                UIWidgets::PushStyleCombobox(menuThemeIndex);
                ImGui::PushStyleColor(ImGuiCol_Border, UIWidgets::ColorValues.at(menuThemeIndex));
                menuSearch.Draw();
                ImGui::PopStyleColor();
                UIWidgets::PopStyleCombobox();
                UIWidgets::PopStyleButton();
                std::string menuSearchText(menuSearch.InputBuf);

                if (menuSearchText == "") {
                    ImGui::Text("Start typing to see results.");
                    return;
                }
                DrawSearchResults(menuSearchText);
                ImGui::EndChild();
            } break;
            default:
                break;
        }
        if (widget.postFunc != nullptr) {
            widget.postFunc(widget);
        }
    } catch (const std::bad_variant_access& e) {
        SPDLOG_ERROR("Failed to draw menu item \"{}\" due to: {}", widget.name, e.what());
        assert(false);
    }
}

void Menu::Draw() {
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    // Sync up the IsVisible flag if it was changed by ImGui
    SyncVisibilityConsoleVariable();
}

// Draws one column's widgets. Unless the sidebar is `plain`, every run of widgets that starts with a section
// separator becomes a titled card.
static void DrawWidgetList(Menu& menu, std::vector<WidgetInfo*>& widgets, bool plain, UIWidgets::Colors accent,
                           const char* idPrefix) {
    using namespace NewDawn::Theme;
    bool cardOpen = false;
    int cardIndex = 0;
    auto endSectionCard = [&]() {
        if (cardOpen) {
            EndSectionCard();
            ImGui::Dummy(ImVec2(0, 4.0f * Unit()));
            cardOpen = false;
        }
    };
    for (WidgetInfo* w : widgets) {
        if (!plain) {
            if (w->type == WIDGET_SEPARATOR_TEXT) {
                endSectionCard();
                BeginSectionCard(fmt::format("{}card{}", idPrefix, cardIndex++).c_str());
                cardOpen = true;
                SectionHeader(w->name.c_str());
                continue;
            }
            if (!cardOpen) {
                BeginSectionCard(fmt::format("{}card{}", idPrefix, cardIndex++).c_str());
                cardOpen = true;
            }
        }
        menu.MenuDrawItem(*w, WrapChars(), accent);
    }
    endSectionCard();
}

void Menu::DrawWidgetColumns(SidebarEntry& sidebar, UIWidgets::Colors accent) {
    using namespace NewDawn::Theme;
    const float u = Unit();
    const float avail = ImGui::GetContentRegionAvail().x;
    const int displayCols = (sidebar.columnWidgets.size() > 1 && avail >= 1020.0f * u) ? 2 : 1;
    // Widget column j goes to display column j % displayCols; narrow windows stack everything.
    std::vector<std::vector<WidgetInfo*>> perColumn(displayCols);
    for (size_t j = 0; j < sidebar.columnWidgets.size(); j++) {
        for (auto& w : sidebar.columnWidgets[j]) {
            perColumn[j % displayCols].push_back(&w);
        }
    }
    if (displayCols == 1) {
        DrawWidgetList(*this, perColumn[0], sidebar.plain, accent, "c0");
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float colW = (avail - spacing) / displayCols;
    for (int d = 0; d < displayCols; d++) {
        if (d > 0) {
            ImGui::SameLine();
        }
        ImGui::BeginChild(fmt::format("dcol{}", d).c_str(), ImVec2(colW, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawWidgetList(*this, perColumn[d], sidebar.plain, accent, fmt::format("c{}", d).c_str());
        ImGui::EndChild();
    }
}

// Widgets grouped by WidgetInfo::group under collapsing headers (the Graphics "Advanced" tab).
void Menu::DrawGroupedWidgets(SidebarEntry& sidebar, UIWidgets::Colors accent) {
    using namespace NewDawn::Theme;
    const float u = Unit();
    std::vector<std::string> order = sidebar.groupOrder;
    std::unordered_map<std::string, std::vector<WidgetInfo*>> groups;
    for (auto& column : sidebar.columnWidgets) {
        for (auto& w : column) {
            if (std::find(order.begin(), order.end(), w.group) == order.end()) {
                order.push_back(w.group);
            }
            groups[w.group].push_back(&w);
        }
    }
    for (auto& name : order) {
        auto it = groups.find(name);
        if (it == groups.end()) {
            continue;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f * u, 12.0f * u));
        ImGui::PushStyleColor(ImGuiCol_Header, WithAlpha(Dusk, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, WithAlpha(Dusk, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, WithAlpha(Gold, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_Text, GoldHot);
        PushHeading(24);
        bool open = ImGui::CollapsingHeader(fmt::format("{}##grp", name).c_str());
        PopHeading();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        if (open) {
            BeginSectionCard(fmt::format("grp_{}", name).c_str());
            for (WidgetInfo* w : it->second) {
                if (w->type == WIDGET_SEPARATOR_TEXT) {
                    SectionHeader(w->name.c_str());
                    continue;
                }
                MenuDrawItem(*w, WrapChars(), accent);
            }
            EndSectionCard();
        }
        ImGui::Dummy(ImVec2(0, 2.0f * u));
    }
}

void Menu::DrawTabContent(const MenuTab& tab, UIWidgets::Colors accent) {
    auto entryIt = menuEntries.find(tab.entry);
    if (entryIt == menuEntries.end() || !entryIt->second.sidebars.contains(tab.sidebar)) {
        ImGui::TextDisabled("This section is not available.");
        return;
    }
    auto& updates = MenuInit::GetUpdateFuncs();
    if (updates.contains(tab.entry) && updates[tab.entry].contains(tab.sidebar)) {
        for (auto& updateFunc : updates[tab.entry][tab.sidebar]) {
            updateFunc();
        }
    }
    SidebarEntry& sidebar = entryIt->second.sidebars.at(tab.sidebar);
    if (sidebar.grouped) {
        DrawGroupedWidgets(sidebar, accent);
    } else {
        DrawWidgetColumns(sidebar, accent);
    }
}

static void CloseMenuAndRefreshNav(Menu* menu) {
    menu->ToggleVisibility();
    // Update gamepad navigation after close based on if other menus are still visible
    auto io = &ImGui::GetIO();
    if (CVarGetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0) &&
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetMenuOrMenubarVisible()) {
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    } else {
        io->ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    }
}

void Menu::DrawElement() {
    using namespace NewDawn::Theme;
    if (OTRGlobals::Instance->fontStandardLargest == nullptr) {
        return;
    }
    if (gGameState == nullptr) {
        return;
    }
    for (auto& [reason, info] : disabledMap) {
        info.active = info.evaluation(info);
    }
    if (menuPages.empty()) {
        return;
    }
    auto accent = static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", defaultThemeIndex));
    const bool developer = NewDawn::IsDeveloperMode();

    // Resolve the active page (hidden developer pages fall back to the first one).
    static std::string activePage = CVarGetString("gSettings.Menu.ActivePage", menuPages.front().id.c_str());
    static std::unordered_map<std::string, int> activeTab;
    auto pageVisible = [&](const MenuPage& p) { return !p.devOnly || developer; };
    int pageIndex = -1;
    for (size_t i = 0; i < menuPages.size(); i++) {
        if (menuPages[i].id == activePage && pageVisible(menuPages[i])) {
            pageIndex = static_cast<int>(i);
        }
    }
    if (pageIndex < 0) {
        pageIndex = 0;
        activePage = menuPages.front().id;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    bool open = ImGui::Begin("Main Menu", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    if (!open) {
        ImGui::End();
        return;
    }

    NewDawn::Theme::ReplayDebugClicks();
    ImGui::PushFont(OTRGlobals::Instance->fontStandardLarger);
    const float u = Unit();
    DrawBackdrop(CVarGetFloat("gSettings.Menu.Dim", 0.55f));

    // Panel geometry: 92% of the screen, capped so it stays a window rather than a wall on large displays.
    ImVec2 panelSize = { std::min(vp->WorkSize.x * 0.94f, 1560.0f * u), std::min(vp->WorkSize.y * 0.92f, 980.0f * u) };
    if (vp->WorkSize.x < 1100.0f * u) {
        panelSize.x = vp->WorkSize.x - 20.0f * u;
    }
    if (vp->WorkSize.y < 700.0f * u) {
        panelSize.y = vp->WorkSize.y - 16.0f * u;
    }
    const ImVec2 panelPos = vp->WorkPos + (vp->WorkSize - panelSize) * 0.5f;
    const bool compact = panelSize.x < 1000.0f * u;
    const float railW = (compact ? 80.0f : 288.0f) * u;

    ImGui::SetCursorScreenPos(panelPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(Panel, CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.95f)));
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(GoldDim, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 18.0f * u);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("ND Panel", panelSize, ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    {   // decoration: rail tint, moonlit glow in the corner, hairline between rail and content
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = panelPos, b = panelPos + panelSize;
        dl->AddRectFilled(a, ImVec2(a.x + railW, b.y), ImGui::GetColorU32(WithAlpha(Ink, 0.60f)), 18.0f * u,
                          ImDrawFlags_RoundCornersLeft);
        ImU32 glow = ImGui::GetColorU32(WithAlpha(Dusk, 0.16f));
        ImU32 none = ImGui::GetColorU32(WithAlpha(Dusk, 0.0f));
        dl->AddRectFilledMultiColor(ImVec2(a.x + railW, a.y), ImVec2(b.x, a.y + 220.0f * u), none, glow, none, none);
        dl->AddLine(ImVec2(a.x + railW, a.y + 18.0f * u), ImVec2(a.x + railW, b.y - 18.0f * u),
                    ImGui::GetColorU32(WithAlpha(Outline, 0.6f)), 1.0f);
    }

    // ---- Left rail: wordmark, pages, utility buttons -------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * u, 16.0f * u));
    ImGui::BeginChild("ND Rail", ImVec2(railW, panelSize.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    const float innerRailW = railW - 28.0f * u;
    if (!compact) {
        // short windows get a smaller wordmark so every page fits in the rail
        const float markW = panelSize.y < 800.0f * u ? innerRailW * 0.72f : innerRailW;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innerRailW - markW) * 0.5f);
        float h = DrawWordmark(markW);
        if (h == 0.0f) {
            PushHeading(36);
            ImGui::TextColored(Gold, "New Dawn");
            PopHeading();
        }
        ImGui::Dummy(ImVec2(0, 6.0f * u));
    } else {
        ImGui::Dummy(ImVec2(0, 20.0f * u));
    }
    const float footerH = (compact ? 100.0f : 112.0f) * u;
    int visiblePages = 0, dividers = 0;
    for (const MenuPage& p : menuPages) {
        visiblePages += pageVisible(p) ? 1 : 0;
        dividers += (pageVisible(p) && p.dividerBefore) ? 1 : 0;
    }
    // Shrink the rows on short windows so every page stays visible without scrolling the rail.
    const float pagesHeight = ImGui::GetContentRegionAvail().y - footerH - dividers * 26.0f * u;
    const float railItemH = std::clamp(pagesHeight / u / std::max(1, visiblePages) - 4.0f, 26.0f, 46.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4.0f * u));
    ImGui::BeginChild("ND Pages", ImVec2(0, ImGui::GetContentRegionAvail().y - footerH),
                      ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    for (size_t i = 0; i < menuPages.size(); i++) {
        const MenuPage& page = menuPages[i];
        if (!pageVisible(page)) {
            continue;
        }
        if (page.dividerBefore) {
            ImGui::Dummy(ImVec2(0, 6.0f * u));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + innerRailW, p.y), ImGui::GetColorU32(WithAlpha(Outline, 0.6f)));
            ImGui::Dummy(ImVec2(0, 8.0f * u));
        }
        if (RailItem(("rail_" + page.id).c_str(), page.icon, compact ? "" : page.label.c_str(),
                     static_cast<int>(i) == pageIndex, innerRailW, railItemH)) {
            if (static_cast<int>(i) != pageIndex) {
                activePage = page.id;
                CVarSetString("gSettings.Menu.ActivePage", page.id.c_str());
                CVarSave();
                menuSearch.Clear();
            }
        }
        if (compact && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", page.label.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // footer: version + reset/quit
    ImGui::SetCursorPosY(panelSize.y - footerH + 4.0f * u);
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    if (gGitCommitTag[0] == 0) {
        ImGui::Text("%s", compact ? "" : (const char*)gBuildVersion);
    } else if (!compact) {
        ImGui::Text("%s", (const char*)gBuildVersion);
    }
    ImGui::PopStyleColor();
    {
        UIWidgets::ButtonOptions resetOpts = {};
        resetOpts.size = UIWidgets::Sizes::Inline;
        resetOpts.color = UIWidgets::Colors::NewDawn;
        resetOpts.tooltip = "Reset the game"
#ifdef __APPLE__
                            " (Command-R)"
#elif !defined(__SWITCH__) && !defined(__WIIU__)
                            " (Ctrl+R)"
#endif
            ;
        if (UIWidgets::Button(compact ? ICON_FA_UNDO : ICON_FA_UNDO " Reset", resetOpts)) {
            std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
                ->Dispatch("reset");
        }
        if (!compact) {
            ImGui::SameLine();
        }
        UIWidgets::ButtonOptions quitOpts = {};
        quitOpts.size = UIWidgets::Sizes::Inline;
        quitOpts.color = UIWidgets::Colors::Red;
        quitOpts.tooltip = "Quit New Dawn";
        if (UIWidgets::Button(compact ? ICON_FA_POWER_OFF : ICON_FA_POWER_OFF " Quit", quitOpts)) {
            BenGui::mModalWindow->RegisterPopup(
                "Quit New Dawn", "Are you sure you want to quit New Dawn?", "Quit", "Cancel",
                []() {
                    std::shared_ptr<Menu> menu =
                        static_pointer_cast<Menu>(Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetMenu());
                    menu->ToggleVisibility();
                    Ship::Context::GetRawInstance()->GetWindow()->Close();
                },
                nullptr);
        }
    }
    ImGui::EndChild(); // rail

    // ---- Content --------------------------------------------------------------------------------------------
    ImGui::SetCursorPos(ImVec2(railW, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.0f * u, 22.0f * u));
    ImGui::BeginChild("ND Content", ImVec2(panelSize.x - railW, panelSize.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    const MenuPage& page = menuPages[pageIndex];
    const float contentW = ImGui::GetContentRegionAvail().x;

    // header: page title on the left, search and close on the right
    const float searchW = 300.0f * u;
    const float closeW = 46.0f * u;
    static bool searchSeeded = false; // capture aid: gSettings.Menu.DebugSearch pre-fills the search box once
    if (!searchSeeded && CVarGetString("gSettings.Menu.DebugSearch", "")[0] != '\0') {
        snprintf(menuSearch.InputBuf, sizeof(menuSearch.InputBuf), "%s", CVarGetString("gSettings.Menu.DebugSearch", ""));
        searchSeeded = true;
    }
    std::string searchText = menuSearch.InputBuf;
    searchText.erase(std::remove(searchText.begin(), searchText.end(), ' '), searchText.end());
    const bool searching = !searchText.empty();
    ImVec2 titleStart = ImGui::GetCursorPos();
    PushHeading(36);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Parchment, "%s", searching ? "Search" : page.label.c_str());
    PopHeading();
    ImGui::SetCursorPos(ImVec2(titleStart.x + contentW - searchW - closeW - ImGui::GetStyle().ItemSpacing.x, titleStart.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.0f * u);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(40.0f * u, 10.0f * u));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, WithAlpha(Ink, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(Outline, 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    menuSearch.Draw("##ndsearch", searchW);
    const ImVec2 sMin = ImGui::GetItemRectMin(), sMax = ImGui::GetItemRectMax();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(sMin.x + 15.0f * u, (sMin.y + sMax.y) * 0.5f - ImGui::GetFontSize() * 0.5f),
                                        ImGui::GetColorU32(TextDim), ICON_FA_SEARCH);
    if (!searching && !ImGui::IsItemActive()) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(sMin.x + 40.0f * u, (sMin.y + sMax.y) * 0.5f - ImGui::GetFontSize() * 0.5f),
                                            ImGui::GetColorU32(WithAlpha(TextDim, 0.8f)), "Search settings");
    }
    ImGui::SameLine();
    {
        UIWidgets::ButtonOptions closeOpts = {};
        closeOpts.size = UIWidgets::Sizes::Inline;
        closeOpts.color = UIWidgets::Colors::NewDawn;
        closeOpts.tooltip = "Close menu (Esc)";
        if (UIWidgets::Button(ICON_FA_TIMES, closeOpts)) {
            CloseMenuAndRefreshNav(this);
        }
    }

    // tabs
    const std::string tabCvar = "gSettings.Menu.Tab." + page.id;
    if (!activeTab.contains(page.id)) {
        activeTab[page.id] = CVarGetInteger(tabCvar.c_str(), 0);
    }
    int& tabIndex = activeTab[page.id];
    tabIndex = std::clamp(tabIndex, 0, static_cast<int>(page.tabs.size()) - 1);
    if (!searching && page.tabs.size() > 1) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * u, 0));
        const float tabsLeft = ImGui::GetCursorScreenPos().x;
        for (size_t t = 0; t < page.tabs.size(); t++) {
            // wrap to a second row instead of running off the panel (the Developer page has 11 tabs)
            const float tabW = ImGui::CalcTextSize(page.tabs[t].label.c_str()).x + 28.0f * u;
            if (t > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + tabW <= tabsLeft + contentW) {
                ImGui::SameLine();
            }
            if (TabItem(fmt::format("tab_{}_{}", page.id, t).c_str(), page.tabs[t].label.c_str(),
                        static_cast<int>(t) == tabIndex)) {
                tabIndex = static_cast<int>(t);
                CVarSetInteger(tabCvar.c_str(), tabIndex);
                CVarSave();
            }
        }
        ImGui::PopStyleVar();
    }
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 2.0f * u), ImVec2(p.x + contentW, p.y + 2.0f * u),
                                            ImGui::GetColorU32(WithAlpha(Outline, 0.7f)));
        ImGui::Dummy(ImVec2(0, 10.0f * u));
    }

    // body
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * u, 6.0f * u));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f * u);
    ImGui::BeginChild("ND Body", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    if (searching) {
        std::string text = menuSearch.InputBuf;
        uint32_t count = DrawSearchResults(text);
        if (count == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
            ImGui::TextWrapped("Nothing matches \"%s\".", menuSearch.InputBuf);
            ImGui::PopStyleColor();
        }
    } else {
        DrawTabContent(page.tabs[tabIndex], accent);
    }
    ImGui::EndChild(); // body
    ImGui::EndChild(); // content
    ImGui::EndChild(); // panel
    ImGui::PopFont();
    ImGui::End();
}
} // namespace Ship
