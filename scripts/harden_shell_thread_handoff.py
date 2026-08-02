from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "shell_main.cpp"
text = PATH.read_text(encoding="utf-8-sig")

if "CPPCHART_UI_THREAD_DATA_HANDOFF" in text:
    print("shell thread handoff is already hardened")
    raise SystemExit(0)

if "CPPCHART_SHARED_RUNTIME_INTEGRATED" not in text:
    raise RuntimeError("shared runtime migration must run first")


def replace_exact(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    text = text.replace(old, new, 1)


replace_exact(
    "// CPPCHART_SHARED_RUNTIME_INTEGRATED\n",
    "// CPPCHART_SHARED_RUNTIME_INTEGRATED\n// CPPCHART_UI_THREAD_DATA_HANDOFF\n",
    "handoff marker",
)

replace_exact(
    '''static int g_mockOrderQty = 1;
static std::mutex g_dataMtx;

struct Health {
''',
    '''static int g_mockOrderQty = 1;
static std::mutex g_dataMtx;
static std::mutex g_paramMtx;
static std::atomic<bool> g_marketDataDirty{ true };

struct Health {
''',
    "thread handoff state",
)

replace_exact(
    '''static void ChartWidget(Canvas& cv, Series& s, View& view, ImVec2 size, bool volumePane, bool overlay) {
    cv.Ensure((int)size.x, (int)size.y);
    if (cv.dirty) RenderChart(cv, s, view, volumePane, overlay);
    ImGui::Image((ImTextureID)(intptr_t)cv.srv, size);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        int n = (int)s.bars.size();
''',
    '''static void ChartWidget(Canvas& cv, Series& s, View& view, ImVec2 size, bool volumePane, bool overlay) {
    cv.Ensure((int)size.x, (int)size.y);
    if (cv.dirty) {
        std::lock_guard<std::mutex> dataLock(g_dataMtx);
        RenderChart(cv, s, view, volumePane, overlay);
    }
    ImGui::Image((ImTextureID)(intptr_t)cv.srv, size);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        int n = 0;
        {
            std::lock_guard<std::mutex> dataLock(g_dataMtx);
            n = static_cast<int>(s.bars.size());
        }
''',
    "chart data lock",
)

replace_exact(
    '''static void DrawProperty() {
    ImGui::Begin("프로퍼티");
''',
    '''static void DrawProperty() {
    ImGui::Begin("프로퍼티");
    std::lock_guard<std::mutex> parameterLock(g_paramMtx);
''',
    "parameter UI lock",
)

replace_exact(
    '''            g_log.Add(
                "CMD",
                "매매대상 승격: %s",
                command.arg.c_str());
            break;
''',
    '''            g_health.subCount = static_cast<int>(g_targets.size()) + 1;
            g_log.Add(
                "CMD",
                "매매대상 승격: %s",
                command.arg.c_str());
            break;
''',
    "subscription count update",
)

replace_exact(
    '''        g_mainCanvas.dirty = true;
        for (Canvas& canvas : g_multi) canvas.dirty = true;
        g_health.latencyMs = 8 + static_cast<int>(rng() % 20);
        g_health.rateUsed = static_cast<int>(rng() % 45);
        g_health.subCount = static_cast<int>(g_targets.size()) + 1;
        if (++tick % 12 == 0) {
            g_signalLog.Add(
                "SIG",
                "JMA(%d/%d) 교차 후보 감지 — 스텁",
                P_jmaFast,
                P_jmaMid);
        }
''',
    '''        g_marketDataDirty.store(true, std::memory_order_release);
        g_health.latencyMs = 8 + static_cast<int>(rng() % 20);
        g_health.rateUsed = static_cast<int>(rng() % 45);
        if (++tick % 12 == 0) {
            int jmaFast = 0;
            int jmaMid = 0;
            {
                std::lock_guard<std::mutex> parameterLock(g_paramMtx);
                jmaFast = P_jmaFast;
                jmaMid = P_jmaMid;
            }
            g_signalLog.Add(
                "SIG",
                "JMA(%d/%d) 교차 후보 감지 — 스텁",
                jmaFast,
                jmaMid);
        }
''',
    "feed to UI handoff",
)

replace_exact(
    '''        double fstart = NowSec();
        DrainCommands();

        ImGui_ImplDX11_NewFrame();
''',
    '''        double fstart = NowSec();
        DrainCommands();

        if (g_marketDataDirty.exchange(false, std::memory_order_acq_rel)) {
            g_mainCanvas.dirty = true;
            for (Canvas& canvas : g_multi) canvas.dirty = true;
        }

        ImGui_ImplDX11_NewFrame();
''',
    "UI dirty consumption",
)

replace_exact(
    '''    g_health.wsUp = IsLocalMock();
    MakeMockData();
    g_health.bootMs = (NowSec() - t0) * 1000.0;
''',
    '''    g_health.wsUp = IsLocalMock();
    MakeMockData();
    g_health.subCount = static_cast<int>(g_targets.size()) + 1;
    g_marketDataDirty.store(true, std::memory_order_release);
    g_health.bootMs = (NowSec() - t0) * 1000.0;
''',
    "initial handoff state",
)

for forbidden in (
    "g_mainCanvas.dirty = true;\n        for (Canvas& canvas : g_multi) canvas.dirty = true;\n        g_health.latencyMs",
    "g_health.subCount = static_cast<int>(g_targets.size()) + 1;\n        if (++tick",
):
    if forbidden in text:
        raise RuntimeError(f"unsafe feed access remains: {forbidden}")

PATH.write_text("\ufeff" + text, encoding="utf-8")
print("Hardened shell feed/UI thread handoff")
