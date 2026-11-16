#include "NewDawnLook.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

extern "C" {
#include "global.h"
}

namespace fs = std::filesystem;
using nlohmann::json;
using nlohmann::ordered_json;

namespace NewDawn::Look {
namespace {

std::vector<Profile> sProfiles;
bool sLoaded = false;
Inputs sLastInputs;

const Fast::NewDawnLookField& Info(int id) {
    return Fast::NewDawnLookFieldInfo(id);
}

int FieldId(const std::string& name) {
    for (int id = 0; id < NEWDAWN_LOOK_COUNT; id++) {
        if (name == Info(id).name) {
            return id;
        }
    }
    return -1;
}

// Scenes are named in the files by their enum name without "SCENE_" (e.g. "00KEIKOKU").
const std::pair<const char*, int> kScenes[] = {
#undef DEFINE_SCENE
#undef DEFINE_SCENE_UNSET
#define DEFINE_SCENE(name, enumValue, ...) { #enumValue, enumValue },
#define DEFINE_SCENE_UNSET(enumValue) { #enumValue, enumValue },
#include "tables/scene_table.h"
#undef DEFINE_SCENE
#undef DEFINE_SCENE_UNSET
};

int SceneId(const std::string& name) {
    for (const auto& [enumName, id] : kScenes) {
        if (name == enumName + 6) {
            return id;
        }
    }
    return -1;
}

const char* SceneEnum(int id) {
    for (const auto& [enumName, sceneId] : kScenes) {
        if (sceneId == id) {
            return enumName + 6;
        }
    }
    return "?";
}

// {"light": {"keyLevel": 1}} and {"light.keyLevel": 1} both state the field "light.keyLevel".
void ReadFields(const json& j, const std::string& prefix, Fields& out, const std::string& where) {
    if (!j.is_object()) {
        return;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string name = prefix.empty() ? it.key() : prefix + "." + it.key();
        if (it->is_object()) {
            ReadFields(*it, name, out, where);
            continue;
        }
        const int id = FieldId(name);
        if (id < 0 || !it->is_number()) {
            SPDLOG_WARN("NewDawnLook: {}: \"{}\" is not a look field", where, name);
            continue;
        }
        out[id] = std::clamp(it->get<float>(), Info(id).lo, Info(id).hi);
    }
}

ordered_json WriteFields(const Fields& fields) {
    ordered_json j = ordered_json::object();
    for (int id = 0; id < NEWDAWN_LOOK_COUNT; id++) { // registry order, so saved files are stable
        const auto it = fields.find(id);
        if (it == fields.end()) {
            continue;
        }
        const std::string name = Info(id).name;
        const size_t dot = name.find('.');
        j[name.substr(0, dot)][name.substr(dot + 1)] = std::round((double)it->second * 1e6) / 1e6;
    }
    return j;
}

bool FromJson(const json& j, Profile& p, const std::string& where) {
    if (!j.is_object()) {
        SPDLOG_WARN("NewDawnLook: {}: not a profile", where);
        return false;
    }
    p = {};
    p.name = j.value("name", fs::path(where).stem().string());
    p.inherits = j.value("inherits", "");
    if (j.contains("scenes") && j["scenes"].is_array()) {
        for (const json& s : j["scenes"]) {
            Profile::Scene entry;
            const std::string name = s.is_string() ? s.get<std::string>() : s.is_object() ? s.value("scene", "") : "";
            entry.id = SceneId(name);
            entry.when = s.is_object() ? s.value("when", "") : "";
            if (entry.id < 0) {
                SPDLOG_WARN("NewDawnLook: {}: unknown scene \"{}\"", where, name);
                continue;
            }
            p.scenes.push_back(entry);
        }
    }
    if (j.contains("base")) {
        ReadFields(j["base"], "", p.base, where);
    }
    for (const char* block : { "clock", "lights" }) {
        if (!j.contains(block) || !j[block].is_object()) {
            continue;
        }
        for (auto it = j[block].begin(); it != j[block].end(); ++it) {
            try {
                if (block[0] == 'c') {
                    const float hour = std::fmod(std::fmod(std::stof(it.key()), 24.0f) + 24.0f, 24.0f);
                    ReadFields(*it, "", p.clock[hour], where);
                } else {
                    ReadFields(*it, "", p.lights[std::stoi(it.key())], where);
                }
            } catch (const std::exception&) {
                SPDLOG_WARN("NewDawnLook: {}: \"{}\" is not a number in \"{}\"", where, it.key(), block);
            }
        }
    }
    if (j.contains("overcast")) {
        ReadFields(j["overcast"], "", p.overcast, where);
    }
    if (j.contains("dread")) {
        ReadFields(j["dread"], "", p.dread, where);
    }
    return true;
}

ordered_json ToJson(const Profile& p) {
    ordered_json j;
    j["name"] = p.name;
    if (!p.inherits.empty()) {
        j["inherits"] = p.inherits;
    }
    if (!p.scenes.empty()) {
        j["scenes"] = ordered_json::array();
        for (const auto& s : p.scenes) {
            if (s.when.empty()) {
                j["scenes"].push_back(SceneEnum(s.id));
            } else {
                j["scenes"].push_back({ { "scene", SceneEnum(s.id) }, { "when", s.when } });
            }
        }
    }
    if (!p.base.empty()) {
        j["base"] = WriteFields(p.base);
    }
    char key[16];
    for (const auto& [hour, fields] : p.clock) {
        if (fields.empty()) {
            continue; // a key the editor emptied
        }
        snprintf(key, sizeof(key), "%g", hour);
        j["clock"][key] = WriteFields(fields);
    }
    for (const auto& [index, fields] : p.lights) {
        if (fields.empty()) {
            continue;
        }
        j["lights"][std::to_string(index)] = WriteFields(fields);
    }
    if (!p.overcast.empty()) {
        j["overcast"] = WriteFields(p.overcast);
    }
    if (!p.dread.empty()) {
        j["dread"] = WriteFields(p.dread);
    }
    return j;
}

fs::path SaveDir() {
    return CVarGetString("gNewDawn.Look.SavePath", "newdawn/looks");
}

// The shipped profiles, then the player's own folder: a profile there replaces the shipped one of the same name.
void Load() {
    sProfiles.clear();
    sLoaded = true;
    const fs::path shipped = fs::path(CVarGetString("gNewDawn.Assets.Path", "assets/newdawn")) / "looks";
    for (const fs::path& dir : { shipped, SaveDir() }) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            std::ifstream in(file);
            Profile p;
            if (!FromJson(json::parse(in, nullptr, false, true), p, file.string())) {
                continue;
            }
            const auto old =
                std::find_if(sProfiles.begin(), sProfiles.end(), [&](const Profile& q) { return q.name == p.name; });
            if (old != sProfiles.end()) {
                *old = std::move(p);
            } else {
                sProfiles.push_back(std::move(p));
            }
        }
    }
    SPDLOG_INFO("NewDawnLook: {} profiles from {} and {}", sProfiles.size(), shipped.string(), SaveDir().string());
}

int Find(const std::string& name) {
    for (size_t i = 0; i < sProfiles.size(); i++) {
        if (sProfiles[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

bool FlagHolds(const Inputs& in, const std::string& when) {
    if (when.empty()) {
        return true;
    }
    const bool negate = when[0] == '!';
    const bool set = in.flag && in.flag(negate ? when.substr(1) : when);
    return set != negate;
}

// Value of a keyframe track at an hour: smoothstep between the keys either side, around the clock.
float Track(const std::map<float, float>& keys, float hour) {
    if (keys.size() == 1) {
        return keys.begin()->second;
    }
    auto next = keys.upper_bound(hour);
    const auto prev = next == keys.begin() ? std::prev(keys.end()) : std::prev(next);
    if (next == keys.end()) {
        next = keys.begin();
    }
    float span = next->first - prev->first, t = hour - prev->first;
    if (span <= 0.0f) {
        span += 24.0f; // the pair straddles midnight
    }
    if (t < 0.0f) {
        t += 24.0f;
    }
    t = std::clamp(t / span, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    return prev->second + (next->second - prev->second) * t;
}

bool Flag(const std::string& name) {
    if (name == "woodfall_cleared") {
        return CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE);
    }
    if (name == "snowhead_cleared") {
        return CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_SNOWHEAD_TEMPLE);
    }
    if (name == "greatbay_cleared") {
        return CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_GREAT_BAY_TEMPLE);
    }
    if (name == "stonetower_cleared") {
        return CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_STONE_TOWER_TEMPLE);
    }
    return false;
}

} // namespace

std::vector<Profile>& Profiles() {
    if (!sLoaded) {
        Load();
    }
    return sProfiles;
}

void Reload() {
    Load();
}

const Inputs& LastInputs() {
    return sLastInputs;
}

const char* SceneName(int id) {
    return SceneEnum(id);
}

std::vector<int> Chain(const Inputs& in) {
    const int outdoor = Find("outdoor"), interior = Find("interior");
    std::vector<int> best; // leaf first
    for (size_t i = 0; i < sProfiles.size(); i++) {
        const auto& scenes = sProfiles[i].scenes;
        if (std::none_of(scenes.begin(), scenes.end(),
                         [&](const Profile::Scene& s) { return s.id == in.scene && FlagHolds(in, s.when); })) {
            continue;
        }
        std::vector<int> lineage;
        for (int p = (int)i; p >= 0 && lineage.size() < 8; p = Find(sProfiles[p].inherits)) {
            lineage.push_back(p);
            if (sProfiles[p].inherits.empty()) {
                break;
            }
        }
        if (lineage.size() >= best.size()) { // the deeper profile is the more specific one; a later file wins a tie
            best = lineage;
        }
    }
    const int root = in.clockLit ? outdoor : interior;
    if (root >= 0 && (best.empty() || (best.back() != outdoor && best.back() != interior))) {
        best.push_back(root);
    }
    std::reverse(best.begin(), best.end());
    return best;
}

void Evaluate(const Inputs& in, float out[NEWDAWN_LOOK_COUNT]) {
    const std::vector<int> chain = Chain(in);
    for (int id = 0; id < NEWDAWN_LOOK_COUNT; id++) {
        float v = Info(id).def;
        for (int p : chain) {
            if (const auto it = sProfiles[p].base.find(id); it != sProfiles[p].base.end()) {
                v = it->second;
            }
        }
        if (in.clockLit) {
            std::map<float, float> keys; // the chain's keys for this field, a child's hour replacing its parent's
            for (int p : chain) {
                for (const auto& [hour, fields] : sProfiles[p].clock) {
                    if (const auto it = fields.find(id); it != fields.end()) {
                        keys[hour] = it->second;
                    }
                }
            }
            if (!keys.empty()) {
                v = Track(keys, in.hour);
            }
        } else {
            float a = v, b = v;
            for (int p : chain) {
                const auto& lights = sProfiles[p].lights;
                if (const auto l = lights.find(in.lightA); l != lights.end() && l->second.count(id)) {
                    a = l->second.at(id);
                }
                if (const auto l = lights.find(in.lightB); l != lights.end() && l->second.count(id)) {
                    b = l->second.at(id);
                }
            }
            v = a + (b - a) * std::clamp(in.lightBlend, 0.0f, 1.0f);
        }
        out[id] = v;
    }
    const auto pull = [&](Fields Profile::* block, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        for (int id = 0; id < NEWDAWN_LOOK_COUNT && amount > 0.0f; id++) {
            for (auto p = chain.rbegin(); p != chain.rend(); ++p) {
                const Fields& fields = sProfiles[*p].*block;
                if (const auto it = fields.find(id); it != fields.end()) {
                    out[id] += (it->second - out[id]) * amount;
                    break;
                }
            }
        }
    };
    pull(&Profile::overcast, in.overcast);
    pull(&Profile::dread, in.dread);
}

float Dread(int day, float hour) {
    const float sinceDawn = std::fmod(hour - 6.0f + 24.0f, 24.0f); // the game's day starts at 06:00
    const bool night = sinceDawn >= 12.0f;
    if (day <= 1) {
        return night ? 0.1f : 0.0f;
    }
    if (day == 2) {
        return night ? 0.4f : 0.3f;
    }
    if (sinceDawn < 12.0f) {
        return 0.5f + 0.2f * sinceDawn / 12.0f;
    }
    if (sinceDawn < 18.0f) {
        return 0.7f + 0.15f * (sinceDawn - 12.0f) / 6.0f;
    }
    return 0.85f + 0.15f * (sinceDawn - 18.0f) / 6.0f;
}

const Fast::NewDawnLookFrame& Resolve(PlayState* play) {
    static Fast::NewDawnLookFrame frame;
    static bool valid = false;
    static u32 lastFrame = 0;
    static s16 lastScene = -1;

    if (!CVarGetInteger("gNewDawn.Look.Profiles", 0) || !CVarGetInteger("gNewDawn.HDR.Enabled", 0) ||
        Profiles().empty()) {
        frame.on = false;
        valid = false;
        return frame;
    }
    if (valid && lastFrame == play->gameplayFrames && lastScene == play->sceneId) {
        return frame;
    }

    Inputs in;
    in.scene = play->sceneId;
    in.clockLit = play->envCtx.lightMode == LIGHT_MODE_TIME;
    in.hour = gSaveContext.save.time * (24.0f / 65536.0f);
    in.lightA = play->envCtx.prevLightSetting;
    in.lightB = play->envCtx.lightSetting;
    in.lightBlend = play->envCtx.lightBlendEnabled ? play->envCtx.lightBlend : 1.0f;
    // Developer previews (the look editor): only the look reads them.
    const float previewOvercast = CVarGetFloat("gNewDawn.Look.PreviewOvercast", -1.0f);
    const float previewDread = CVarGetFloat("gNewDawn.Look.PreviewDread", -1.0f);
    in.overcast = previewOvercast >= 0.0f ? previewOvercast
                                          : std::clamp(play->envCtx.precipitation[PRECIP_RAIN_CUR] / 40.0f +
                                                           play->envCtx.precipitation[PRECIP_SNOW_CUR] / 128.0f,
                                                       0.0f, 1.0f);
    in.dread = previewDread >= 0.0f ? previewDread : Dread(gSaveContext.save.day, in.hour);
    in.flag = Flag;

    float target[NEWDAWN_LOOK_COUNT];
    Evaluate(in, target);
    // A scene change passes through a fade, so it starts on its look; everything else eases (about a second at 20 Hz).
    const bool snap = !valid || lastScene != play->sceneId || play->gameplayFrames < lastFrame;
    for (int id = 0; id < NEWDAWN_LOOK_COUNT; id++) {
        frame.v[id] = snap ? target[id] : frame.v[id] + (target[id] - frame.v[id]) * 0.06f;
    }
    frame.on = true;
    valid = true;
    lastFrame = play->gameplayFrames;
    lastScene = play->sceneId;
    sLastInputs = in;
    return frame;
}

bool Save(Profile& profile, std::string* error) {
    std::error_code ec;
    fs::create_directories(SaveDir(), ec);
    const fs::path file = SaveDir() / (profile.name + ".json"), tmp = SaveDir() / (profile.name + ".json.tmp");
    {
        std::ofstream out(tmp);
        out << ToJson(profile).dump(2) << "\n";
        if (!out) {
            if (error) {
                *error = "cannot write " + tmp.string();
            }
            return false;
        }
    }
    fs::rename(tmp, file, ec); // replace in one step, so a failed save never leaves half a file
    if (ec) {
        if (error) {
            *error = ec.message();
        }
        return false;
    }
    profile.dirty = false;
    return true;
}

bool SelfCheck() {
    std::vector<Profile> saved;
    saved.swap(sProfiles);
    const bool wasLoaded = sLoaded;
    sLoaded = true;
    const int gain = NEWDAWN_LOOK_GRADE_GAIN, key = NEWDAWN_LOOK_KEY_LEVEL, darken = NEWDAWN_LOOK_DARKEN;
    const auto parse = [](const char* text) {
        Profile p;
        FromJson(json::parse(text), p, "self-check");
        return p;
    };
    sProfiles.push_back(parse(R"({"name":"outdoor","base":{"light":{"keyLevel":2}},
        "clock":{"6":{"grade":{"gain":1}},"18":{"grade.gain":1.5}},"overcast":{"light.keyLevel":0}})"));
    sProfiles.push_back(parse(R"({"name":"interior","lights":{"1":{"light.darken":0.5},"2":{"light.darken":1}}})"));
    sProfiles.push_back(parse(R"({"name":"a","scenes":["20SICHITAI2"],"clock":{"12":{"grade.gain":1.4}}})"));
    sProfiles.push_back(parse(R"({"name":"b","inherits":"a","scenes":[{"scene":"20SICHITAI2","when":"f"}],
        "base":{"light.keyLevel":3}})"));

    bool ok = true;
    const auto expect = [&](const char* what, Inputs in, int id, float want) {
        float out[NEWDAWN_LOOK_COUNT];
        Evaluate(in, out);
        if (std::fabs(out[id] - want) > 1e-4f) {
            SPDLOG_ERROR("NewDawnLook self-check: {}: got {}, expected {}", what, out[id], want);
            ok = false;
        }
    };
    Inputs in;
    in.scene = SCENE_20SICHITAI2;
    in.hour = 6.0f;
    expect("a key holds at its hour", in, gain, 1.0f);
    in.hour = 9.0f;
    expect("smoothstep half way between a parent's key and a child's", in, gain, 1.2f);
    in.hour = 12.0f;
    expect("the child's key", in, gain, 1.4f);
    in.hour = 0.0f;
    expect("a track wraps past midnight", in, gain, 1.25f);
    expect("a base value is inherited", in, key, 2.0f);
    in.flag = [](const std::string& name) { return name == "f"; };
    expect("the deeper profile wins when its condition holds", in, key, 3.0f);
    in.flag = nullptr;
    in.overcast = 0.5f;
    expect("a modifier pulls half way", in, key, 1.0f);
    in = {};
    in.scene = SCENE_20SICHITAI;
    in.hour = 12.0f;
    expect("an unlisted scene gets its root only", in, gain, 1.25f);
    in.clockLit = false;
    in.lightA = 1;
    in.lightB = 2;
    in.lightBlend = 0.5f;
    expect("light settings blend", in, darken, 0.75f);
    expect("a fixed-light scene ignores the clock", in, gain, Info(gain).def);

    Profile round;
    FromJson(json::parse(ToJson(sProfiles[3]).dump()), round, "self-check");
    if (round.name != "b" || round.inherits != "a" || round.scenes.size() != 1 || round.scenes[0].when != "f" ||
        round.base != sProfiles[3].base) {
        SPDLOG_ERROR("NewDawnLook self-check: a saved profile does not read back the same");
        ok = false;
    }
    if (Dread(1, 12.0f) != 0.0f || std::fabs(Dread(3, 6.0f) - 0.5f) > 1e-4f ||
        std::fabs(Dread(3, 5.99f) - 1.0f) > 1e-2f) {
        SPDLOG_ERROR("NewDawnLook self-check: dread curve");
        ok = false;
    }
    sProfiles.swap(saved);
    sLoaded = wasLoaded;
    return ok;
}

} // namespace NewDawn::Look
