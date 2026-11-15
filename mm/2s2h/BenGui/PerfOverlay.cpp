#include "PerfOverlay.h"
#include "NewDawnTheme.h"
#include "NewDawnPresets.h"
#include <libultraship/libultraship.h>
#include <fast/newdawn_perf.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
extern GameState* gGameState;
const char* Ship_GetSceneName(s16 sceneId);
}

using namespace NewDawn::Theme;

namespace {
constexpr int kSpark = 120;   // sparkline frames
constexpr int kHistory = 600; // frames used for the 1% low
constexpr double kTextPeriod = 0.25;

struct Line {
    std::string label, value;
};

float sFrames[kHistory] = {};
int sFrameHead = 0, sFrameCount = 0;
double sLastFrameT = 0, sCpuMsAccum = 0, sLastThreadCpu = 0;
int sDrawsSinceText = 0;
double sLastTextT = 0, sLastProcCpu = 0;
u32 sLastLogicFrames = 0;
std::vector<Line> sLines;     // cached text, rebuilt 4x/s
std::vector<Line> sStages;
float sCpuMs = 0, sGpuMs = 0;

double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double CpuSeconds(clockid_t id) {
#ifdef __linux__
    timespec ts;
    clock_gettime(id, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#else
    return 0.0;
#endif
}

// Resident set size in MB, 0 when unavailable.
double RssMB() {
#ifdef __linux__
    FILE* f = fopen("/proc/self/statm", "r");
    long pages = 0, rss = 0;
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &rss) != 2) {
            rss = 0;
        }
        fclose(f);
    }
    return rss * (sysconf(_SC_PAGESIZE) / 1048576.0);
#else
    return 0.0;
#endif
}

std::string Fmt(const char* fmt, double a, double b = 0, double c = 0) {
    char buf[96];
    snprintf(buf, sizeof(buf), fmt, a, b, c);
    return buf;
}

void RebuildText(double now, bool detailed, float avgMs, int draws) {
    const double dt = now - sLastTextT;
    std::vector<float> sorted(sFrames, sFrames + sFrameCount);
    std::sort(sorted.begin(), sorted.end());
    const float p99 = sorted.empty() ? 0.f : sorted[std::min<size_t>(sorted.size() - 1, sorted.size() * 99 / 100)];
    sLines.clear();
    sLines.push_back({ "FPS", Fmt("%.0f   1%% low %.0f", avgMs > 0 ? 1000.0 / avgMs : 0, p99 > 0 ? 1000.0 / p99 : 0) });
    sLines.push_back({ "Frame", Fmt("%.2f ms", avgMs) });
    sLines.push_back({ "CPU / GPU", sGpuMs > 0 ? Fmt("%.2f / %.2f ms", sCpuMs, sGpuMs) : Fmt("%.2f / -- ms", sCpuMs) });
    const int tier = NewDawn::DetectTier();
    sLines.push_back({ "Look", tier >= 0 ? NewDawn::TierName(static_cast<NewDawn::Tier>(tier)) : "Custom" });

    sStages.clear();
    if (!detailed) {
        sLastTextT = now;
        return;
    }
    NewDawnPerfInfo info;
    Fast::NewDawnPerfGet(&info);
    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < NEWDAWN_PERF_STAGES; i++) {
        if (info.stageMs[i] >= 0.005f) {
            order.push_back({ info.stageMs[i], i });
        }
    }
    std::sort(order.rbegin(), order.rend());
    for (size_t i = 0; i < order.size() && i < 6; i++) {
        sStages.push_back({ Fast::NewDawnPerfStageName(order[i].second), Fmt("%.2f ms", order[i].first) });
    }

    auto* vp = ImGui::GetMainViewport();
    const float mult = CVarGetFloat("gSettings.InternalResolution", 1.0f);
    const int msaa = CVarGetInteger("gSettings.MSAAValue", 1);
    sLines.push_back({ "Resolution", Fmt("%.0fx%.0f", vp->Size.x * mult, vp->Size.y * mult) +
                                         (msaa > 1 ? "  " + std::to_string(msaa) + "x MSAA" : "  no MSAA") });
    if (info.vramTotalMB > 0 && info.vramFreeMB >= 0) {
        sLines.push_back({ "VRAM", Fmt("%.0f / %.0f MB", info.vramTotalMB - info.vramFreeMB, info.vramTotalMB) });
    } else if (info.vramFreeMB >= 0) {
        sLines.push_back({ "VRAM free", Fmt("%.0f MB", info.vramFreeMB) });
    }
    if (double rss = RssMB(); rss > 0) {
        sLines.push_back({ "RAM", Fmt("%.0f MB", rss) });
        const double cpu = CpuSeconds(CLOCK_PROCESS_CPUTIME_ID);
        sLines.push_back({ "Process CPU", Fmt("%.0f%%", dt > 0 ? (cpu - sLastProcCpu) / dt * 100.0 : 0) });
        sLastProcCpu = cpu;
    }
    if (gGameState != nullptr) {
        const u32 logic = gGameState->frames;
        sLines.push_back({ "Logic / render", Fmt("%.0f / %.0f fps", dt > 0 ? (logic - sLastLogicFrames) / dt : 0,
                                                 dt > 0 ? draws / dt : 0) });
        sLastLogicFrames = logic;
    }
    if (gPlayState != nullptr) {
        sLines.push_back({ "Scene", Ship_GetSceneName(gPlayState->sceneId) });
        const int mins = static_cast<int>(gSaveContext.save.time * (24.0 * 60.0 / 0x10000));
        sLines.push_back({ "Time", Fmt("%02.0f:%02.0f", mins / 60, mins % 60) });
    }
    sLastTextT = now;
}

void Row(const std::string& label, const std::string& value, float width) {
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(width - ImGui::CalcTextSize(value.c_str()).x);
    ImGui::PushStyleColor(ImGuiCol_Text, Parchment);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
}
} // namespace

void PerfOverlay::Draw() {
    auto* vp = ImGui::GetMainViewport();
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false) && !ImGui::GetIO().WantTextInput) {
        CVarSetInteger("gNewDawn.Perf.Overlay", !CVarGetInteger("gNewDawn.Perf.Overlay", 0));
        CVarSave();
    }
    if (!CVarGetInteger("gNewDawn.Perf.Overlay", 0)) {
        sLastTextT = 0; // restart the averaging windows on next enable
        return;
    }

    // Hidden while the menu or any popup/modal is open so it never covers them (the stats keep running).
    const bool hidden = CVarGetInteger("gWindows.Menu", 0) != 0 ||
                        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const double now = Now();
    const double threadCpu = CpuSeconds(CLOCK_THREAD_CPUTIME_ID);
    if (sLastFrameT > 0 && sLastTextT > 0) {
        sFrames[sFrameHead] = static_cast<float>((now - sLastFrameT) * 1000.0);
        sFrameHead = (sFrameHead + 1) % kHistory;
        sFrameCount = std::min(sFrameCount + 1, kHistory);
        sCpuMsAccum += (threadCpu - sLastThreadCpu) * 1000.0;
    }
    sLastFrameT = now;
    sLastThreadCpu = threadCpu;
    sDrawsSinceText++;

    const bool detailed = CVarGetInteger("gNewDawn.Perf.Detailed", 0) != 0;
    if (sLastTextT == 0 || now - sLastTextT >= kTextPeriod) {
        const int n = std::min(sFrameCount, static_cast<int>(sDrawsSinceText));
        float avg = 0;
        for (int i = 0; i < n; i++) {
            avg += sFrames[(sFrameHead - 1 - i + kHistory) % kHistory];
        }
        avg = n > 0 ? avg / n : 0;
        sCpuMs = n > 0 ? static_cast<float>(sCpuMsAccum / n) : 0;
        NewDawnPerfInfo info;
        Fast::NewDawnPerfGet(&info);
        sGpuMs = info.gpuFrameMs;
        RebuildText(now, detailed, avg, sDrawsSinceText);
        sCpuMsAccum = 0;
        sDrawsSinceText = 0;
    }
    if (sLines.empty() || hidden) {
        return;
    }

    const float u = Unit();
    const float width = (detailed ? 330.0f : 250.0f) * u;
    const float margin = 14.0f * u;
    const int corner = CVarGetInteger("gNewDawn.Perf.Corner", 1);
    const ImVec2 pos(vp->WorkPos.x + ((corner & 1) ? vp->WorkSize.x - margin : margin),
                     vp->WorkPos.y + ((corner & 2) ? vp->WorkSize.y - margin : margin));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * u);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * u, 10.0f * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * u, 3.0f * u));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Ink);
    ImGui::PushStyleColor(ImGuiCol_Border, Outline);
    ImGui::Begin("New Dawn Performance", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    const float inner = width - 24.0f * u;
    PushHeading(20);
    ImGui::PushStyleColor(ImGuiCol_Text, Gold);
    ImGui::TextUnformatted("PERFORMANCE");
    ImGui::PopStyleColor();
    PopHeading();

    for (size_t i = 0; i < 3 && i < sLines.size(); i++) {
        Row(sLines[i].label, sLines[i].value, inner);
        if (i == 1) { // sparkline under the frame time
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = 30.0f * u;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + inner, p.y + h), ImGui::GetColorU32(WithAlpha(PanelRaised, 0.7f)), 4.0f);
            float peak = 33.3f;
            for (int k = 0; k < std::min(kSpark, sFrameCount); k++) {
                peak = std::max(peak, sFrames[(sFrameHead - 1 - k + kHistory) % kHistory]);
            }
            std::vector<ImVec2> pts;
            for (int k = std::min(kSpark, sFrameCount) - 1, x = 0; k >= 0; k--, x++) {
                const float ms = sFrames[(sFrameHead - 1 - k + kHistory) % kHistory];
                pts.push_back(ImVec2(p.x + inner * x / (kSpark - 1), p.y + h - 2 - (h - 4) * std::min(1.0f, ms / peak)));
            }
            if (pts.size() > 1) {
                dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), ImGui::GetColorU32(Gold), 0, 1.5f);
            }
            ImGui::Dummy(ImVec2(inner, h));
        }
    }
    for (size_t i = 3; i < sLines.size(); i++) {
        Row(sLines[i].label, sLines[i].value, inner);
    }
    if (!sStages.empty()) {
        ImGui::Dummy(ImVec2(0, 2.0f * u));
        ImGui::PushStyleColor(ImGuiCol_Text, GoldDim);
        ImGui::TextUnformatted("GPU STAGES");
        ImGui::PopStyleColor();
        for (const Line& l : sStages) {
            Row(l.label, l.value, inner);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}
