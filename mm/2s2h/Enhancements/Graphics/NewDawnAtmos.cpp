#include "NewDawnAtmos.h"
#include <algorithm>
#include <libultraship/bridge/consolevariablebridge.h>
extern "C" {
#include "global.h"
}

// Read-only scene/light bridge. No actor, environment, clock or gameplay RNG writes.
void NewDawnAtmos_Fill(const PlayState* play, NewDawnPostFxParams* params) {
    const bool particles =
        CVarGetInteger("gNewDawn.Atmos.Particles", 0) && CVarGetFloat("gNewDawn.Atmos.Strength", 1.0f) > 0;
    const bool weather = CVarGetInteger("gNewDawn.Atmos.Weather", 0);
    if ((!particles && !weather) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        (GET_ACTIVE_CAM(play)->stateFlags & CAM_STATE_UNDERWATER)) {
        return;
    }
    // 1: courtyard dust, 2: grass/pollen, 3: woodland, 4: interior light-local dust.
    switch (play->sceneId) {
        case SCENE_CLOCKTOWER:
        case SCENE_TOWN:
        case SCENE_ICHIBA:
        case SCENE_BACKTOWN:
        case SCENE_30GYOSON:
        case SCENE_IKANA:
            params->atmosBiome = 1;
            break;
        case SCENE_00KEIKOKU:
            params->atmosBiome = 2;
            break;
        case SCENE_20SICHITAI:
        case SCENE_20SICHITAI2:
        case SCENE_21MITURINMAE:
        case SCENE_26SARUNOMORI:
        case SCENE_LOST_WOODS:
            params->atmosBiome = 3;
            break;
        case SCENE_10YUKIYAMANOMURA:
            params->atmosBiome = 5;
            break;
        case SCENE_YADOYA:
            params->atmosBiome = 4;
            break;
        default:
            return;
    }
    params->atmosScene = play->sceneId;
    params->atmosParticles = particles && params->atmosBiome != 5;
    params->atmosWeather = weather && params->atmosBiome != 4;
    params->atmosSnow = std::clamp(play->envCtx.precipitation[PRECIP_SNOW_CUR] / 128.0f, 0.0f, 1.0f);
    // Smooth, read-only day/night weights independent of the sun shadow toggle.
    const float hour = gSaveContext.save.time * (24.0f / 65536.0f);
    params->atmosNight = std::clamp(std::max(6.0f - hour, hour - 18.0f), 0.0f, 1.0f);
    params->atmosRain = play->envCtx.precipitation[PRECIP_RAIN_CUR] / 60.0f;
    params->atmosEye[0] = play->view.eye.x;
    params->atmosEye[1] = play->view.eye.y;
    params->atmosEye[2] = play->view.eye.z;
    const auto& sun = play->envCtx.dirLight1.params.dir;
    for (int i = 0; i < 3; ++i) {
        params->atmosSunColor[i] = sun.color[i] / 255.0f;
        params->atmosSkyColor[i] = play->lightCtx.fogColor[i] / 255.0f;
    }
    // Scene lights, including torches: bounded nearest selection so linked-list ordering cannot evict a near light.
    for (const LightNode* node = play->lightCtx.listHead; node; node = node->next) {
        if (!node->info || node->info->type == LIGHT_DIRECTIONAL)
            continue;
        const auto& light = node->info->params.point;
        if (light.radius <= 0)
            continue;
        const float pos[3] = { float(light.x), float(light.y), float(light.z) };
        float distance = 0;
        for (int i = 0; i < 3; ++i)
            distance += SQ(pos[i] - params->atmosEye[i]);
        int slot = params->atmosLightCount;
        if (slot == NEWDAWN_ATMOS_LIGHTS) {
            float farthest = -1;
            for (int j = 0; j < NEWDAWN_ATMOS_LIGHTS; ++j) {
                float d = 0;
                for (int i = 0; i < 3; ++i)
                    d += SQ(params->atmosLights[j][i] - params->atmosEye[i]);
                if (d > farthest) {
                    farthest = d;
                    slot = j;
                }
            }
            if (distance >= farthest)
                continue;
        } else {
            ++params->atmosLightCount;
        }
        for (int i = 0; i < 3; ++i) {
            params->atmosLights[slot][i] = pos[i];
            params->atmosLightColors[slot][i] = light.color[i] / 255.0f;
        }
        params->atmosLights[slot][3] = light.radius;
    }
}
