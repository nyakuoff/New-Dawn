#include "2s2h/BenGui/UIWidgets.hpp"
#include <ship/config/Config.h>
#include <ship/window/gui/IconsFontAwesome4.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/DeveloperTools/DeveloperTools.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/BenGui/BenGui.hpp"
#include "BenJsonConversions.hpp"

extern "C" {
#include <libultraship/bridge/consolevariablebridge.h>
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "overlays/gamestates/ovl_select/z_select.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
extern GameState* gGameState;
}

typedef struct WarpPoint {
    s32 entranceId;
    s8 roomNum;
    Vec3f pos;
    s16 rotY;
    bool bootToPoint;
} WarpPoint;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WarpPoint, entranceId, roomNum, pos, rotY, bootToPoint)
std::map<std::string, WarpPoint> warpPoints;

#define CVAR_BOOT_TO_FILE_SELECT_NAME "gEnhancements.Cutscenes.SkipToFileSelect"
#define CVAR_BOOT_TO_FILE_SELECT ((bool)CVarGetInteger(CVAR_BOOT_TO_FILE_SELECT_NAME, 0))

void LoadConfig() {
    auto allConfig = Ship::Context::GetRawInstance()->GetConfig()->GetNestedJson();
    if (allConfig.find("WarpPoints") == allConfig.end() || !allConfig["WarpPoints"].is_object()) {
        allConfig["WarpPoints"] = nlohmann::json::object();
    }
    warpPoints = allConfig["WarpPoints"];
}

void SaveConfig() {
    auto allConfig = Ship::Context::GetRawInstance()->GetConfig()->GetNestedJson();
    allConfig["WarpPoints"] = warpPoints;
    Ship::Context::GetRawInstance()->GetConfig()->SetBlock("WarpPoints", warpPoints);
    Ship::Context::GetRawInstance()->GetConfig()->Save();
}

void Warp(WarpPoint& warpPoint) {
    if (gPlayState == NULL) {
        // If gPlayState is NULL, it means the the user opted into BootToWarpPoint and the game is starting up.
        gSaveContext.gameMode = GAMEMODE_NORMAL;
        Sram_InitNewSave();
        gSaveContext.sceneLayer = 0;
        gSaveContext.save.time = CLOCK_TIME(8, 0);
        gSaveContext.save.day = 1;
        // Capture/debug only: boot at another hour (and optionally with the Woodfall temple cleared).
        float bootHour = CVarGetFloat("gNewDawn.Debug.BootHour", -1.0f);
        if (bootHour >= 0.0f) {
            gSaveContext.save.time = (u16)(bootHour * (0x10000 / 24.0f));
        }
        gSaveContext.save.cutsceneIndex = 0;
        gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
        gSaveContext.save.linkAge = 0;

        if (CVarGetInteger("gNewDawn.Debug.BootWoodfallCleared", 0)) {
            SET_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE);
        }

        // Need to unset flag values that are outside of `.save`
        // Normally would happened in the MapSelect_LoadGame, but is skipped because of the dummy file num below
        // This is mostly copied from Sram_OpenSave.
        for (size_t i = 0; i < ARRAY_COUNT(gSaveContext.eventInf); i++) {
            gSaveContext.eventInf[i] = 0;
        }
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.cycleSceneFlags); i++) {
            gSaveContext.cycleSceneFlags[i].chest = gSaveContext.save.saveInfo.permanentSceneFlags[i].chest;
            gSaveContext.cycleSceneFlags[i].switch0 = gSaveContext.save.saveInfo.permanentSceneFlags[i].switch0;
            gSaveContext.cycleSceneFlags[i].switch1 = gSaveContext.save.saveInfo.permanentSceneFlags[i].switch1;
            gSaveContext.cycleSceneFlags[i].clearedRoom = gSaveContext.save.saveInfo.permanentSceneFlags[i].clearedRoom;
            gSaveContext.cycleSceneFlags[i].collectible = gSaveContext.save.saveInfo.permanentSceneFlags[i].collectible;
        }

        // Using dummy file num to bypass debug save setup in map select and manually execute save init/load hooks after
        gSaveContext.fileNum = 0xFE;
        MapSelect_LoadGame((MapSelectState*)gGameState, warpPoint.entranceId, 0);
        // Then back to debug file num
        gSaveContext.fileNum = 0xFF;
        // These two lines allow randomizer to be used with BootToWarpPoint. Not sure how reliable this is, might remove
        GameInteractor_ExecuteOnSaveInit(gSaveContext.fileNum);
        GameInteractor_ExecuteOnSaveLoad(gSaveContext.fileNum);
        gSaveContext.save.entrance = warpPoint.entranceId;
    } else {
        // The else case, and the rest of this function is primarily relevant code copied from Play_SetRespawnData and
        // func_80169EFC, minus the parts that copy scene flags to scene we are warping to (this is obviously
        // undesirable)
        gPlayState->nextEntrance = Entrance_Create(warpPoint.entranceId >> 9, 0, warpPoint.entranceId & 0xF);
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_INSTANT;
    }
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entrance =
        Entrance_Create(warpPoint.entranceId >> 9, 0, warpPoint.entranceId & 0xF);
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = warpPoint.roomNum;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = warpPoint.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = warpPoint.rotY;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = PLAYER_PARAMS(0xFF, PLAYER_START_MODE_D);
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.respawnFlag = -8;
}

// Warp to a scene entrance with a quick fade. Only touches the transition state; no respawn/save/time changes.
static void WarpToEntrance(s32 entrance) {
    gPlayState->nextEntrance = entrance;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK_FAST;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
}

static std::string warpNameInput = "";

struct KeyLocation {
    const char* name;
    s32 entrance;
};
struct KeyRegion {
    const char* name;
    std::vector<KeyLocation> locations;
};

void RenderWarpMenu() {
    static const std::vector<KeyRegion> regions = {
        { "Opening",
          { { "Lost Woods", ENTRANCE(LOST_WOODS, 0) },
            { "Clock Tower Interior", ENTRANCE(CLOCK_TOWER_INTERIOR, 0) } } },
        { "Clock Town",
          { { "South", ENTRANCE(SOUTH_CLOCK_TOWN, 0) },
            { "North", ENTRANCE(NORTH_CLOCK_TOWN, 0) },
            { "East", ENTRANCE(EAST_CLOCK_TOWN, 0) },
            { "West", ENTRANCE(WEST_CLOCK_TOWN, 0) },
            { "Clock Tower Rooftop", ENTRANCE(CLOCK_TOWER_ROOFTOP, 0) },
            { "Stock Pot Inn", ENTRANCE(STOCK_POT_INN, 0) },
            { "Mayor's Residence", ENTRANCE(MAYORS_RESIDENCE, 0) },
            { "Milk Bar", ENTRANCE(MILK_BAR, 0) },
            { "Laundry Pool", ENTRANCE(LAUNDRY_POOL, 0) } } },
        { "Termina Field", { { "Termina Field", ENTRANCE(TERMINA_FIELD, 0) } } },
        { "South",
          { { "Southern Swamp", ENTRANCE(SOUTHERN_SWAMP_POISONED, 0) },
            { "Deku Palace", ENTRANCE(DEKU_PALACE, 0) },
            { "Woodfall", ENTRANCE(WOODFALL, 0) },
            { "Woodfall Temple", ENTRANCE(WOODFALL_TEMPLE, 0) } } },
        { "North",
          { { "Mountain Village", ENTRANCE(MOUNTAIN_VILLAGE_WINTER, 0) },
            { "Goron Village", ENTRANCE(GORON_VILLAGE_WINTER, 0) },
            { "Snowhead", ENTRANCE(SNOWHEAD, 0) },
            { "Snowhead Temple", ENTRANCE(SNOWHEAD_TEMPLE, 0) } } },
        { "West",
          { { "Great Bay Coast", ENTRANCE(GREAT_BAY_COAST, 0) },
            { "Zora Hall", ENTRANCE(ZORA_HALL, 0) },
            { "Pirates' Fortress", ENTRANCE(PIRATES_FORTRESS_EXTERIOR, 0) },
            { "Great Bay Temple", ENTRANCE(GREAT_BAY_TEMPLE, 0) } } },
        { "East",
          { { "Ikana Canyon", ENTRANCE(IKANA_CANYON, 0) },
            { "Ikana Graveyard", ENTRANCE(IKANA_GRAVEYARD, 0) },
            { "Stone Tower", ENTRANCE(STONE_TOWER, 0) },
            { "Stone Tower Temple", ENTRANCE(STONE_TOWER_TEMPLE, 0) } } },
        { "Ranch", { { "Romani Ranch", ENTRANCE(ROMANI_RANCH, 0) } } },
    };

    bool canWarp = gPlayState != NULL && gPlayState->csCtx.state == CS_STATE_IDLE &&
                   gPlayState->transitionTrigger == TRANS_TRIGGER_OFF && gPlayState->transitionMode == TRANS_MODE_OFF;
    if (!canWarp) {
        ImGui::TextDisabled("Warping is available in game, outside cutscenes and transitions.");
    }
    ImGui::BeginDisabled(!canWarp);
    for (auto& region : regions) {
        ImGui::SeparatorText(region.name);
        float maxX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < region.locations.size(); i++) {
            auto& loc = region.locations[i];
            ImGui::PushID(loc.name);
            if (i > 0) {
                float w = ImGui::CalcTextSize(loc.name).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < maxX) {
                    ImGui::SameLine();
                }
            }
            if (UIWidgets::Button(loc.name, { .size = UIWidgets::Sizes::Inline })) {
                WarpToEntrance(loc.entrance);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    RenderWarpPointSection(); // "Warp Points": the user's saved positions
}

void RenderWarpPointSection() {
    ImGui::SeparatorText("Warp Points");
    if (gPlayState != NULL && GET_PLAYER(gPlayState) != NULL) {
        UIWidgets::InputString("##WarpPointNameInput", &warpNameInput,
                               {
                                   .labelPosition = UIWidgets::LabelPosition::None,
                                   .size = ImVec2(ImGui::GetContentRegionAvail().x - 50.0f, 0.0f),
                                   .placeholder = "Enter warp point name...",
                               });

        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_PLUS)) {
            Player* player = GET_PLAYER(gPlayState);

            bool isEmpty = warpNameInput.empty();
            if (warpNameInput.empty()) {
                warpNameInput = Ship_GetSceneName(gPlayState->sceneId);
                if (gPlayState->roomCtx.curRoom.num != 0) {
                    warpNameInput += " (" + std::to_string(gPlayState->roomCtx.curRoom.num) + ")";
                }
            }

            warpPoints[warpNameInput] = WarpPoint{
                .entranceId = gSaveContext.save.entrance,
                .roomNum = gPlayState->roomCtx.curRoom.num,
                .pos = player->actor.world.pos,
                .rotY = player->actor.shape.rot.y,
            };
            SaveConfig();
            warpNameInput = "";
        }
    }
    // List of warp points, showing just their name, a button to warp and a button to delete
    for (auto it = warpPoints.begin(); it != warpPoints.end();) {
        ImGui::PushID(it->first.c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", it->first.c_str());
        if (it->second.bootToPoint) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.0f, 1.0f), "[Boot]");
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 115.0f);
        if (gPlayState == NULL)
            ImGui::BeginDisabled();
        if (UIWidgets::Button(ICON_FA_PLANE, { .size = UIWidgets::Sizes::Inline })) {
            // Warp to this point
            Warp(it->second);
        }
        if (gPlayState == NULL)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_REFRESH,
                              { .size = UIWidgets::Sizes::Inline, .color = UIWidgets::Colors::Orange })) {
            bool wasEnabled = it->second.bootToPoint;
            for (auto& wp : warpPoints) {
                wp.second.bootToPoint = false;
            }
            it->second.bootToPoint = !wasEnabled;
            SaveConfig();
        }
        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_TRASH, { .size = UIWidgets::Sizes::Inline, .color = UIWidgets::Colors::Red })) {
            it = warpPoints.erase(it);
            SaveConfig();
            ImGui::PopID();
            continue;
        }
        ImGui::PopID();

        ++it;
    }
}

void RegisterWarpPoints() {
    static bool loadedConfig = false;
    if (!loadedConfig) {
        LoadConfig();
        loadedConfig = true;
    }

    // Warp to point that has bootToWarp enabled. Do not run if Skip to File Select is enabled.
    COND_HOOK(OnConsoleLogoUpdate, !CVAR_BOOT_TO_FILE_SELECT, []() {
        // Normally called on console logo screen
        for (auto& wp : warpPoints) {
            if (wp.second.bootToPoint) {
                Warp(wp.second);
                break;
            }
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterWarpPoints, { CVAR_BOOT_TO_FILE_SELECT_NAME });
