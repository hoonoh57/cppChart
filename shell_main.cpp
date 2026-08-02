// ============================================================================
//  Trading Shell — UI 골격 (엔진 스텁 / 목데이터)
//  · Dear ImGui(docking) + D3D11, 단일 디바이스
//  · 차트 = 오프스크린 RT, dirty 시에만 재렌더 → 티어별 갱신주기 설계와 일치
//  · 모든 조작 = CommandBus 경유, 모든 결함 = 중앙 정책표 경유
//  build: build.bat   (소스는 UTF-8 저장, /utf-8 필수)
// ============================================================================
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// ─────────────────────────────── 공용 상태 ──────────────────────────────────
static ID3D11Device*           g_dev  = nullptr;
static ID3D11DeviceContext*    g_ctx  = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_mainRTV = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;
static int  g_wakeFrames = 60;          // 입력/데이터 변화 시 60프레임 활성

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ─────────────────────────────── 로그 (고정 링버퍼) ─────────────────────────
struct LogLine { char cat[12]; char msg[220]; };
struct LogRing {
    std::mutex mtx; std::vector<LogLine> buf; size_t head = 0, count = 0;
    LogRing() { buf.resize(4000); }
    void Add(const char* cat, const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(mtx);
        LogLine& l = buf[head];
        snprintf(l.cat, sizeof(l.cat), "%s", cat);
        va_list ap; va_start(ap, fmt);
        vsnprintf(l.msg, sizeof(l.msg), fmt, ap);
        va_end(ap);
        head = (head + 1) % buf.size();
        if (count < buf.size()) ++count;
    }
    size_t Size() { return count; }
    LogLine Get(size_t i) {  // 0 = 가장 오래된 것
        std::lock_guard<std::mutex> lk(mtx);
        size_t start = (head + buf.size() - count) % buf.size();
        return buf[(start + i) % buf.size()];
    }
};
static LogRing g_log, g_signalLog, g_orderLog;

// ─────────────────────────────── 결함 정책표 ────────────────────────────────
enum class Fault { DeviceLost, RenderStall, WsDisconnected, WsStale,
                   HttpRateLimited, TokenExpired, BarGap, DataCorrupt,
                   OrderRejected, PositionMismatch, COUNT };
enum class Action { Ignore, SoftReset, FeedReset, HardRestart, Observe };

static const char* kFaultName[] = { "DeviceLost","RenderStall","WsDisconnected","WsStale",
    "HttpRateLimited","TokenExpired","BarGap","DataCorrupt","OrderRejected","PositionMismatch" };
static const char* kActionName[] = { "Ignore","SoftReset","FeedReset","HardRestart","Observe" };

struct Policy { Action first; int window_s; int threshold; Action escalated; };
static const Policy kPolicy[(int)Fault::COUNT] = {
    { Action::SoftReset,   60,  3, Action::HardRestart }, // DeviceLost
    { Action::SoftReset,   60,  5, Action::HardRestart }, // RenderStall
    { Action::FeedReset,  300,  5, Action::HardRestart }, // WsDisconnected
    { Action::FeedReset,  300,  3, Action::HardRestart }, // WsStale
    { Action::Ignore,      60, 20, Action::FeedReset   }, // HttpRateLimited
    { Action::FeedReset,  600,  3, Action::Observe     }, // TokenExpired
    { Action::FeedReset,  300, 10, Action::HardRestart }, // BarGap
    { Action::FeedReset,   60,  3, Action::HardRestart }, // DataCorrupt
    { Action::Ignore,      60,  3, Action::Observe     }, // OrderRejected
    { Action::Observe,      0,  0, Action::Observe     }, // PositionMismatch
};
struct FaultStat { int total = 0; int recent = 0; double windowStart = 0; Action last = Action::Ignore; };
static FaultStat g_faultStat[(int)Fault::COUNT];
static std::atomic<bool> g_observeMode{ false };

static double NowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

// 호출 지점은 이것만 부르고 즉시 리턴한다. 판단은 전부 여기서.
static void RaiseFault(Fault f, const char* ctx) {
    int i = (int)f; const Policy& p = kPolicy[i]; FaultStat& s = g_faultStat[i];
    double now = NowSec();
    if (p.window_s > 0 && now - s.windowStart > p.window_s) { s.windowStart = now; s.recent = 0; }
    ++s.total; ++s.recent;
    Action a = (p.threshold > 0 && s.recent >= p.threshold) ? p.escalated : p.first;
    s.last = a;
    if (a == Action::Observe) g_observeMode = true;
    g_log.Add("FAULT", "%s (%s) x%d → %s", kFaultName[i], ctx, s.recent, kActionName[(int)a]);
    g_wakeFrames = 60;
}

// ─────────────────────────────── 커맨드 버스 ────────────────────────────────
enum class Cmd { LoadSymbol, Backfill, PromoteTarget, DemoteTarget,
                 LiquidateAll, LiquidateSelected, ArmStrategy, DisarmStrategy,
                 OpenMultiChart, ResetSoft, ResetFeed, ResetHard };
struct Command { Cmd type; std::string arg; int i0 = 0; };

struct CommandBus {
    std::mutex mtx; std::deque<Command> q;
    void Push(Cmd c, std::string arg = "", int i0 = 0) {
        std::lock_guard<std::mutex> lk(mtx); q.push_back({ c, std::move(arg), i0 }); g_wakeFrames = 60;
    }
    bool Pop(Command& out) {
        std::lock_guard<std::mutex> lk(mtx);
        if (q.empty()) return false; out = q.front(); q.pop_front(); return true;
    }
};
static CommandBus g_bus;

// ─────────────────────────────── 파라미터 레지스트리 ────────────────────────
enum class PType { Int, Float, Bool, Color };
struct Param {
    const char* group; const char* name; PType type; void* p;
    float lo = 0, hi = 0; const char* tip = nullptr;
};
static std::vector<Param> g_params;
static void Reg(const char* g, const char* n, PType t, void* p, float lo = 0, float hi = 0, const char* tip = nullptr) {
    g_params.push_back({ g, n, t, p, lo, hi, tip });
}

// 실제 파라미터 값들 (엔진이 그대로 읽어 쓰게 될 대상)
static int   P_visibleBars = 220;
static bool  P_showVolume = true;
static bool  P_showIndex = true;
static float P_upColor[4] = { 0.90f, 0.22f, 0.22f, 1.f };
static float P_dnColor[4] = { 0.25f, 0.50f, 0.95f, 1.f };
static int   P_jmaFast = 5, P_jmaMid = 20, P_confirmBars = 1, P_maxPositions = 5;
static float P_minBeta = 1.0f, P_minCorr = 0.6f;
static int   P_minLagMin = 1;
static float P_minTurnover = 5.0f;   // 억원
static int   P_universeRefreshSec = 30;
static bool  g_paramsDirty = false;

static void RegisterParams() {
    Reg("차트", "표시 봉 수", PType::Int, &P_visibleBars, 30, 2000);
    Reg("차트", "거래량 표시", PType::Bool, &P_showVolume);
    Reg("차트", "지수 오버레이", PType::Bool, &P_showIndex);
    Reg("차트", "상승 색", PType::Color, P_upColor);
    Reg("차트", "하락 색", PType::Color, P_dnColor);
    Reg("전략", "JMA 단기", PType::Int, &P_jmaFast, 2, 60);
    Reg("전략", "JMA 중기", PType::Int, &P_jmaMid, 5, 200);
    Reg("전략", "확정 대기 봉", PType::Int, &P_confirmBars, 0, 10);
    Reg("전략", "최대 보유 종목", PType::Int, &P_maxPositions, 1, 20);
    Reg("선별", "최소 일간 베타", PType::Float, &P_minBeta, 0.0f, 3.0f);
    Reg("선별", "최소 장중 상관", PType::Float, &P_minCorr, 0.0f, 1.0f);
    Reg("선별", "최소 후행 시차(분)", PType::Int, &P_minLagMin, 0, 10);
    Reg("선별", "최소 거래대금(억)", PType::Float, &P_minTurnover, 0.f, 500.f);
    Reg("선별", "전종목 갱신주기(초)", PType::Int, &P_universeRefreshSec, 10, 300);
}

// ─────────────────────────────── 목 데이터 ──────────────────────────────────
struct Bar { float o, h, l, c, vol; };
struct Series {
    std::string code, name;
    std::vector<Bar> bars;
    float beta = 1.0f, corr = 0.5f, turnover = 10.f; int lag = 1; float score = 0;
};
struct Position { std::string code, name; int qty; float avg, cur; bool sel = false; };

static std::vector<Series>  g_series;      // [0] = 지수
static std::vector<Position> g_positions;
static std::mutex g_dataMtx;

struct Health {
    std::atomic<bool> wsUp{ true };
    std::atomic<int>  latencyMs{ 12 };
    std::atomic<int>  rateUsed{ 0 };
    std::atomic<int>  rateCap{ 60 };
    std::atomic<int>  subCount{ 0 };
    std::atomic<double> bootMs{ 0 };
    std::atomic<double> frameMs{ 0 };
};
static Health g_health;

static void MakeMockData() {
    static const char* names[][2] = {
        {"KOSPI","종합(KOSPI)"},{"097230","효성중공업"},{"090710","제주반도체"},
        {"403870","HPSP"},{"117730","삼양엔씨켐"},{"240810","원익IPS"},
        {"000660","SK하이닉스"},{"005930","삼성전자"},{"042700","한미반도체"},
        {"095340","ISC"},{"166090","하나머티리얼즈"},{"036930","주성엔지니어링"}
    };
    std::mt19937 rng(20260802);
    std::normal_distribution<float> nd(0.f, 1.f);
    for (auto& n : names) {
        Series s; s.code = n[0]; s.name = n[1];
        float px = 10000.f + (rng() % 90000);
        s.bars.reserve(3000);
        for (int i = 0; i < 3000; ++i) {
            float drift = std::sin(i * 0.004f) * 0.0012f;
            float o = px;
            px *= (1.f + drift + nd(rng) * 0.0016f);
            float c = px;
            float hi = (std::max)(o, c) * (1.f + std::fabs(nd(rng)) * 0.0008f);
            float lo = (std::min)(o, c) * (1.f - std::fabs(nd(rng)) * 0.0008f);
            s.bars.push_back({ o, hi, lo, c, 1000.f + std::fabs(nd(rng)) * 4000.f });
        }
        s.beta = 0.7f + (rng() % 160) / 100.0f;
        s.corr = 0.35f + (rng() % 60) / 100.0f;
        s.lag = (int)(rng() % 4);
        s.turnover = 3.f + (rng() % 400);
        s.score = s.beta * s.corr * 100.f;
        g_series.push_back(std::move(s));
    }
    g_positions = {
        {"097230","효성중공업",  10, 2418000.f, 2431000.f},
        {"090710","제주반도체", 300,   62800.f,   65100.f},
        {"403870","HPSP",       120,   34900.f,   34600.f},
    };
}

// ─────────────────────────────── 차트 렌더러 ────────────────────────────────
struct Vtx { float x, y, r, g, b, a; };

static ID3D11VertexShader*   g_vs = nullptr;
static ID3D11PixelShader*    g_ps = nullptr;
static ID3D11InputLayout*    g_il = nullptr;
static ID3D11RasterizerState* g_rs = nullptr;
static ID3D11Buffer*         g_vb = nullptr;
static UINT                  g_vbCap = 0;
static std::vector<Vtx>      g_cpu;

static const char* kShader =
"struct VS_IN{float2 p:POSITION;float4 c:COLOR;};"
"struct PS_IN{float4 p:SV_POSITION;float4 c:COLOR;};"
"PS_IN VS(VS_IN i){PS_IN o;o.p=float4(i.p,0,1);o.c=i.c;return o;}"
"float4 PS(PS_IN i):SV_TARGET{return i.c;}";

static bool InitChartGfx() {
    ID3DBlob* vsb = nullptr, * psb = nullptr, * err = nullptr;
    D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &err);
    D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psb, &err);
    if (err) err->Release();
    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0} };
    g_dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_il);
    vsb->Release(); psb->Release();
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    g_dev->CreateRasterizerState(&rd, &g_rs);

    return true;
}

static inline void Quad(std::vector<Vtx>& v, float l, float t, float r, float b, const float c[4]) {
    Vtx a{ l,t,c[0],c[1],c[2],c[3] }, bb{ r,t,c[0],c[1],c[2],c[3] },
        cc{ l,b,c[0],c[1],c[2],c[3] }, d{ r,b,c[0],c[1],c[2],c[3] };
    v.push_back(a); v.push_back(bb); v.push_back(cc);
    v.push_back(cc); v.push_back(bb); v.push_back(d);
}

struct View { int offset = -1; int visible = 0; };   // offset<0 = 최신 고정

// 오프스크린 캔버스: dirty일 때만 다시 그린다.
struct Canvas {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0; bool dirty = true;

    void Release() {
        if (srv) { srv->Release(); srv = nullptr; }
        if (rtv) { rtv->Release(); rtv = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
        w = h = 0;
    }
    void Ensure(int W, int H) {
        W = (std::max)(16, W); H = (std::max)(16, H);
        if (tex && W == w && H == h) return;
        Release(); w = W; h = H; dirty = true;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &tex))) { RaiseFault(Fault::DeviceLost, "CreateTexture2D"); return; }
        g_dev->CreateRenderTargetView(tex, nullptr, &rtv);
        g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    }
};

static void UploadVB() {
    if (g_cpu.empty()) return;
    if (!g_vb || g_cpu.size() > g_vbCap) {
        if (g_vb) { g_vb->Release(); g_vb = nullptr; }
        g_vbCap = (UINT)(g_cpu.size() * 3 / 2 + 4096);
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = sizeof(Vtx) * g_vbCap;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_vb))) { g_vbCap = 0; return; }
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(g_ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, g_cpu.data(), sizeof(Vtx) * g_cpu.size());
        g_ctx->Unmap(g_vb, 0);
    }
}

// 하나의 캔버스에 캔들 + (옵션)거래량 + (옵션)지수 오버레이를 그린다.
static void RenderChart(Canvas& cv, const Series& s, View& view, bool volumePane, bool indexOverlay) {
    if (!cv.rtv) return;
    const float clear[4] = { 0.07f, 0.07f, 0.09f, 1.f };
    g_ctx->ClearRenderTargetView(cv.rtv, clear);

    int n = (int)s.bars.size(); if (n < 2) return;
    int vis = view.visible > 0 ? view.visible : P_visibleBars;
    vis = std::clamp(vis, 10, n);
    int off = view.offset < 0 ? n - vis : std::clamp(view.offset, 0, n - vis);

    float lo = 1e30f, hi = -1e30f, vmax = 1.f;
    for (int i = off; i < off + vis; ++i) {
        lo = (std::min)(lo, s.bars[i].l); hi = (std::max)(hi, s.bars[i].h);
        vmax = (std::max)(vmax, s.bars[i].vol);
    }
    float pad = (hi - lo) * 0.05f; if (pad <= 0) pad = 1.f; lo -= pad; hi += pad;
    float rng = hi - lo;

    const float L = -0.985f, R = 0.985f;
    const float pTop = 0.96f, pBot = volumePane ? -0.55f : -0.96f;
    const float vTop = -0.66f, vBot = -0.96f;
    const float pxY = 2.f / cv.h, pxX = 2.f / cv.w;
    auto Y = [&](float p) { return pBot + (p - lo) / rng * (pTop - pBot); };

    g_cpu.clear();
    const float grid[4] = { 0.16f, 0.16f, 0.19f, 1.f };
    for (int i = 0; i <= 4; ++i) {
        float y = pBot + (pTop - pBot) * i / 4.f;
        Quad(g_cpu, L, y + pxY * .5f, R, y - pxY * .5f, grid);
    }

    float stepX = (R - L) / vis;
    float halfW = (std::max)(pxX * .5f, stepX * 0.36f);
    float wickW = (std::max)(pxX * .5f, stepX * 0.07f);
    bool  thin = (stepX / pxX) < 2.5f;

    g_cpu.reserve(vis * 18 + 200);
    for (int i = 0; i < vis; ++i) {
        const Bar& b = s.bars[off + i];
        float x = L + (i + .5f) * stepX;
        bool up = b.c >= b.o;
        const float* col = up ? P_upColor : P_dnColor;
        Quad(g_cpu, x - wickW, Y(b.h), x + wickW, Y(b.l), col);
        if (!thin) {
            float t = (std::max)(Y(b.o), Y(b.c)), bt = (std::min)(Y(b.o), Y(b.c));
            if (t - bt < pxY) { t = (t + bt) * .5f + pxY * .5f; bt = t - pxY; }
            Quad(g_cpu, x - halfW, t, x + halfW, bt, col);
        }
        if (volumePane) {
            float vc[4] = { col[0] * .6f, col[1] * .6f, col[2] * .6f, 1.f };
            float yv = vBot + (b.vol / vmax) * (vTop - vBot);
            Quad(g_cpu, x - halfW, yv, x + halfW, vBot, vc);
        }
    }

    // 지수 오버레이: 표시 구간 시작점 기준 정규화 라인
    if (indexOverlay && !g_series.empty() && &s != &g_series[0]) {
        const Series& ix = g_series[0];
        int m = (int)ix.bars.size();
        const float oc[4] = { 0.95f, 0.85f, 0.35f, 1.f };
        float base = ix.bars[std::clamp(off, 0, m - 1)].c;
        float sbase = s.bars[off].c;
        float prevX = 0, prevY = 0;
        for (int i = 0; i < vis; ++i) {
            int k = std::clamp(off + i, 0, m - 1);
            float mapped = sbase * (ix.bars[k].c / base);
            float x = L + (i + .5f) * stepX, y = Y(mapped);
            if (i > 0) {
                float dx = x - prevX, dy = y - prevY, len = std::sqrt(dx * dx + dy * dy);
                if (len > 1e-6f) {
                    float nx = -dy / len * pxY, ny = dx / len * pxY;
                    Vtx a{ prevX + nx,prevY + ny,oc[0],oc[1],oc[2],1 }, b2{ x + nx,y + ny,oc[0],oc[1],oc[2],1 },
                        c2{ prevX - nx,prevY - ny,oc[0],oc[1],oc[2],1 }, d2{ x - nx,y - ny,oc[0],oc[1],oc[2],1 };
                    g_cpu.push_back(a); g_cpu.push_back(b2); g_cpu.push_back(c2);
                    g_cpu.push_back(c2); g_cpu.push_back(b2); g_cpu.push_back(d2);
                }
            }
            prevX = x; prevY = y;
        }
    }

    UploadVB();
    D3D11_VIEWPORT vp{ 0,0,(float)cv.w,(float)cv.h,0,1 };
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetRenderTargets(1, &cv.rtv, nullptr);
    g_ctx->RSSetState(g_rs);
    UINT stride = sizeof(Vtx), offb = 0;
    g_ctx->IASetInputLayout(g_il);
    g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offb);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(g_ps, nullptr, 0);
    g_ctx->Draw((UINT)g_cpu.size(), 0);
    cv.dirty = false;
}

// ─────────────────────────────── 문서(패널) 상태 ────────────────────────────
static Canvas g_mainCanvas;  static View g_mainView;  static int g_mainSel = 1;
static Canvas g_multi[6];    static View g_multiView[6]; static int g_multiSel[6] = { 1,2,3,4,5,6 };
static bool  g_showMulti = true;
static char  g_symbolInput[32] = "097230";
static int   g_tfIndex = 0;
static std::vector<int> g_targets = { 1,2,3 };

// 캔버스를 ImGui 이미지로 배치 + 휠/드래그 상호작용
static void ChartWidget(Canvas& cv, Series& s, View& view, ImVec2 size, bool volumePane, bool overlay) {
    cv.Ensure((int)size.x, (int)size.y);
    if (cv.dirty) RenderChart(cv, s, view, volumePane, overlay);
    ImGui::Image((ImTextureID)(intptr_t)cv.srv, size);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        int n = (int)s.bars.size();
        int vis = view.visible > 0 ? view.visible : P_visibleBars;
        if (io.MouseWheel != 0.f) {
            int nv = (int)(vis * (io.MouseWheel > 0 ? 0.87f : 1.15f));
            view.visible = std::clamp(nv == vis ? vis - (int)io.MouseWheel * 5 : nv, 10, n);
            cv.dirty = true; g_wakeFrames = 30;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float perBar = size.x / (float)(view.visible > 0 ? view.visible : P_visibleBars);
            int d = (int)(io.MouseDelta.x / (std::max)(1.f, perBar));
            if (d != 0) {
                int cur = view.offset < 0 ? n - (view.visible > 0 ? view.visible : P_visibleBars) : view.offset;
                view.offset = std::clamp(cur - d, 0, (std::max)(0, n - 10));
                cv.dirty = true; g_wakeFrames = 30;
            }
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { view.offset = -1; view.visible = 0; cv.dirty = true; }
    }
}

// ─────────────────────────────── 패널 그리기 ────────────────────────────────
static void DrawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##sym", g_symbolInput, sizeof(g_symbolInput));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const char* tfs[] = { "1분","3분","5분","10분","30분","일" };
    ImGui::Combo("##tf", &g_tfIndex, tfs, IM_ARRAYSIZE(tfs));
    ImGui::SameLine();
    if (ImGui::Button("조회")) g_bus.Push(Cmd::LoadSymbol, g_symbolInput, g_tfIndex);
    ImGui::SameLine();
    if (ImGui::Button("매매 멀티차트")) g_bus.Push(Cmd::OpenMultiChart);
    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

    bool ws = g_health.wsUp.load();
    ImGui::TextColored(ws ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.3f, 0.3f, 1), ws ? "WS●" : "WS○");
    ImGui::SameLine();
    ImGui::Text("지연 %dms   유량 %d/%d   구독 %d   부팅 %.0fms   %.1ffps",
        g_health.latencyMs.load(), g_health.rateUsed.load(), g_health.rateCap.load(),
        g_health.subCount.load(), g_health.bootMs.load(), ImGui::GetIO().Framerate);

    if (g_observeMode.load()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.75f, 0.2f, 1), "  [관망 모드]");
    }

    // 우측 고정: 전량청산 (항상 보이는 위치, 탭 안에 두지 않는다)
    float btnW = 130.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - btnW - 16.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.12f, 0.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.18f, 0.18f, 1));
    bool panic = ImGui::Button("전량청산", ImVec2(btnW, 0));
    ImGui::PopStyleColor(2);
    if (panic || (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)))
        ImGui::OpenPopup("confirm_liq_all");
    ImGui::PopStyleVar();

    if (ImGui::BeginPopupModal("confirm_liq_all", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("보유 전 종목을 시장가로 청산합니다. 진행할까요?");
        ImGui::Separator();
        if (ImGui::Button("청산 실행", ImVec2(120, 0))) { g_bus.Push(Cmd::LiquidateAll); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("취소", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void DrawSymbolPool() {
    ImGui::Begin("종목풀");
    if (ImGui::TreeNodeEx("관심종목", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 1; i < (int)g_series.size(); ++i) {
            bool sel = (i == g_mainSel);
            char lbl[96]; snprintf(lbl, sizeof(lbl), "%s  %s", g_series[i].code.c_str(), g_series[i].name.c_str());
            if (ImGui::Selectable(lbl, sel)) { g_mainSel = i; g_mainCanvas.dirty = true; }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("매매대상으로 승격")) g_bus.Push(Cmd::PromoteTarget, g_series[i].code, i);
                ImGui::EndPopup();
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("매매대상", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int idx : g_targets)
            ImGui::BulletText("%s %s", g_series[idx].code.c_str(), g_series[idx].name.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("조건식")) { ImGui::BulletText("(엔진 연결 후 표시)"); ImGui::TreePop(); }
    if (ImGui::TreeNode("전종목")) { ImGui::BulletText("(엔진 연결 후 표시)"); ImGui::TreePop(); }
    ImGui::End();
}

static void DrawMainChart() {
    ImGui::Begin("주력 차트");
    Series& s = g_series[std::clamp(g_mainSel, 0, (int)g_series.size() - 1)];
    ImGui::Text("%s  %s   |  베타 %.2f  상관 %.2f  시차 %d분", s.code.c_str(), s.name.c_str(), s.beta, s.corr, s.lag);
    ImGui::SameLine(); ImGui::TextDisabled("(휠=확대, 드래그=이동, 더블클릭=최신)");
    ChartWidget(g_mainCanvas, s, g_mainView, ImGui::GetContentRegionAvail(), P_showVolume, P_showIndex);
    ImGui::End();
}

static void DrawMultiChart() {
    if (!g_showMulti) return;
    ImGui::Begin("매매 멀티차트", &g_showMulti);
    ImVec2 area = ImGui::GetContentRegionAvail();
    const int cols = 2, rows = 3;
    ImVec2 cell((area.x - 8) / cols, (area.y - 8) / rows);
    int k = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c, ++k) {
            if (c) ImGui::SameLine();
            ImGui::BeginChild(ImGui::GetID(k + 1000), cell, true);
            int si = std::clamp(g_multiSel[k], 1, (int)g_series.size() - 1);
            ImGui::TextUnformatted(g_series[si].name.c_str());
            ChartWidget(g_multi[k], g_series[si], g_multiView[k], ImGui::GetContentRegionAvail(), false, P_showIndex);
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

static void DrawScanner() {
    ImGui::Begin("스캐너");
    ImGui::Text("선별 조건: 베타≥%.2f  상관≥%.2f  시차≥%d분  대금≥%.0f억", P_minBeta, P_minCorr, P_minLagMin, P_minTurnover);
    ImGui::Separator();
    if (ImGui::BeginTable("scan", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const char* hdr[] = { "종목","점수","베타","상관","시차","대금(억)","" };
        for (auto h : hdr) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 1; i < (int)g_series.size(); ++i) {
            Series& s = g_series[i];
            bool pass = s.beta >= P_minBeta && s.corr >= P_minCorr && s.lag >= P_minLagMin && s.turnover >= P_minTurnover;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(s.name.c_str(), i == g_mainSel, ImGuiSelectableFlags_SpanAllColumns))
            { g_mainSel = i; g_mainCanvas.dirty = true; }
            ImGui::TableNextColumn(); ImGui::TextColored(pass ? ImVec4(0.4f, 1, 0.5f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%.0f", s.score);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", s.beta);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", s.corr);
            ImGui::TableNextColumn(); ImGui::Text("%d", s.lag);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", s.turnover);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("승격")) g_bus.Push(Cmd::PromoteTarget, s.code, i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void DrawProperty() {
    ImGui::Begin("프로퍼티");
    if (ImGui::BeginTabBar("ptabs")) {
        const char* groups[] = { "차트","전략","선별" };
        for (const char* grp : groups) {
            if (ImGui::BeginTabItem(grp)) {
                if (ImGui::BeginTable("pg", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
                    for (auto& p : g_params) {
                        if (strcmp(p.group, grp) != 0) continue;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name);
                        ImGui::TableNextColumn();
                        ImGui::PushID(p.name);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        bool ch = false;
                        switch (p.type) {
                        case PType::Int:   ch = ImGui::SliderInt("##v", (int*)p.p, (int)p.lo, (int)p.hi); break;
                        case PType::Float: ch = ImGui::SliderFloat("##v", (float*)p.p, p.lo, p.hi, "%.2f"); break;
                        case PType::Bool:  ch = ImGui::Checkbox("##v", (bool*)p.p); break;
                        case PType::Color: ch = ImGui::ColorEdit4("##v", (float*)p.p, ImGuiColorEditFlags_NoInputs); break;
                        }
                        ImGui::PopID();
                        if (ch) { g_paramsDirty = true; g_mainCanvas.dirty = true; for (auto& c : g_multi) c.dirty = true; }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void DrawDashboard() {
    ImGui::Begin("대시보드");
    std::lock_guard<std::mutex> lk(g_dataMtx);
    double totBuy = 0, totEval = 0;
    for (auto& p : g_positions) { totBuy += (double)p.avg * p.qty; totEval += (double)p.cur * p.qty; }
    double pl = totEval - totBuy;
    double rate = totBuy > 0 ? pl / totBuy * 100.0 : 0.0;

    ImGui::Text("매입 %.0f원   평가 %.0f원", totBuy, totEval);
    ImGui::SameLine();
    ImGui::TextColored(pl >= 0 ? ImVec4(0.95f, 0.35f, 0.35f, 1) : ImVec4(0.35f, 0.6f, 1, 1),
        "   손익 %+.0f원 (%+.2f%%)", pl, rate);
    ImGui::SameLine();
    if (ImGui::Button("선택 청산")) g_bus.Push(Cmd::LiquidateSelected);
    ImGui::SameLine();
    if (g_observeMode.load()) { if (ImGui::Button("전략 가동")) g_bus.Push(Cmd::ArmStrategy); }
    else { if (ImGui::Button("관망 전환")) g_bus.Push(Cmd::DisarmStrategy); }

    if (ImGui::BeginTable("pos", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        const char* hdr[] = { "선택","종목","수량","평단","현재가","평가손익","수익률" };
        for (auto h : hdr) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (auto& p : g_positions) {
            double ppl = (double)(p.cur - p.avg) * p.qty;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::PushID(p.code.c_str()); ImGui::Checkbox("##s", &p.sel); ImGui::PopID();
            ImGui::TableNextColumn(); ImGui::Text("%s %s", p.code.c_str(), p.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", p.qty);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", p.avg);
            ImGui::TableNextColumn(); ImGui::Text("%.0f", p.cur);
            ImGui::TableNextColumn();
            ImGui::TextColored(ppl >= 0 ? ImVec4(0.95f, 0.35f, 0.35f, 1) : ImVec4(0.35f, 0.6f, 1, 1), "%+.0f", ppl);
            ImGui::TableNextColumn(); ImGui::Text("%+.2f%%", p.avg > 0 ? (p.cur - p.avg) / p.avg * 100.f : 0.f);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void DrawLogWindow(const char* title, LogRing& ring) {
    ImGui::Begin(title);
    if (ImGui::Button("지우기")) { std::lock_guard<std::mutex> lk(ring.mtx); ring.count = 0; ring.head = 0; }
    ImGui::SameLine(); ImGui::TextDisabled("%zu 줄", ring.Size());
    ImGui::Separator();
    ImGui::BeginChild("body", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    size_t n = ring.Size();
    ImGuiListClipper clip; clip.Begin((int)n);
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            LogLine l = ring.Get((size_t)i);
            ImGui::TextDisabled("[%s]", l.cat); ImGui::SameLine(); ImGui::TextUnformatted(l.msg);
        }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    ImGui::End();
}

static void DrawFaultWindow() {
    ImGui::Begin("결함");
    ImGui::TextDisabled("중앙 정책표 — 호출 지점은 신고만 하고 판단하지 않는다");
    if (ImGui::BeginTable("ft", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        const char* hdr[] = { "결함","기본조치","임계","누적","최근조치" };
        for (auto h : hdr) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)Fault::COUNT; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kFaultName[i]);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kActionName[(int)kPolicy[i].first]);
            ImGui::TableNextColumn(); ImGui::Text("%d/%ds", kPolicy[i].threshold, kPolicy[i].window_s);
            ImGui::TableNextColumn(); ImGui::Text("%d", g_faultStat[i].total);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g_faultStat[i].total ? kActionName[(int)g_faultStat[i].last] : "-");
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    if (ImGui::Button("WS 끊김 시뮬레이션")) RaiseFault(Fault::WsDisconnected, "simulate");
    ImGui::SameLine();
    if (ImGui::Button("유량 초과 시뮬레이션")) RaiseFault(Fault::HttpRateLimited, "simulate");
    ImGui::SameLine();
    if (ImGui::Button("포지션 불일치")) RaiseFault(Fault::PositionMismatch, "simulate");
    ImGui::SameLine();
    if (ImGui::Button("관망 해제")) { g_observeMode = false; g_log.Add("SYS", "관망 모드 해제 (수동)"); }
    ImGui::End();
}

// ─────────────────────────────── 엔진 스텁 ──────────────────────────────────
static void DrainCommands() {
    Command c;
    while (g_bus.Pop(c)) {
        switch (c.type) {
        case Cmd::LoadSymbol:
            g_log.Add("CMD", "종목 조회 요청: %s (tf=%d)  ← 엔진 연결 지점", c.arg.c_str(), c.i0);
            for (int i = 1; i < (int)g_series.size(); ++i)
                if (g_series[i].code == c.arg) { g_mainSel = i; g_mainCanvas.dirty = true; }
            break;
        case Cmd::PromoteTarget:
            if (std::find(g_targets.begin(), g_targets.end(), c.i0) == g_targets.end()) {
                g_targets.push_back(c.i0);
                if (g_targets.size() <= 6) g_multiSel[g_targets.size() - 1] = c.i0;
                for (auto& cv : g_multi) cv.dirty = true;
            }
            g_log.Add("CMD", "매매대상 승격: %s", c.arg.c_str());
            break;
        case Cmd::OpenMultiChart: g_showMulti = true; g_log.Add("CMD", "매매 멀티차트 열기"); break;
        case Cmd::LiquidateAll:
            g_orderLog.Add("ORDER", "전량청산 커맨드 수신 — %zu종목 시장가 매도 (스텁)", g_positions.size());
            g_log.Add("CMD", "전량청산 실행");
            break;
        case Cmd::LiquidateSelected: {
            int n = 0; for (auto& p : g_positions) if (p.sel) ++n;
            g_orderLog.Add("ORDER", "선택청산 커맨드 수신 — %d종목 (스텁)", n);
            break;
        }
        case Cmd::ArmStrategy:    g_observeMode = false; g_log.Add("CMD", "전략 가동"); break;
        case Cmd::DisarmStrategy: g_observeMode = true;  g_log.Add("CMD", "관망 전환"); break;
        default: g_log.Add("CMD", "미구현 커맨드"); break;
        }
    }
}

static std::atomic<bool> g_feedRun{ true };
static void MockFeedThread() {
    std::mt19937 rng(1234);
    std::normal_distribution<float> nd(0.f, 1.f);
    int tick = 0;
    while (g_feedRun.load()) {
        {
            std::lock_guard<std::mutex> lk(g_dataMtx);
            for (auto& s : g_series) {                     // 마지막 봉만 갱신
                Bar& b = s.bars.back();
                b.c *= (1.f + nd(rng) * 0.0006f);
                b.h = (std::max)(b.h, b.c); b.l = (std::min)(b.l, b.c);
                b.vol += std::fabs(nd(rng)) * 40.f;
            }
            for (auto& p : g_positions)
                for (auto& s : g_series)
                    if (s.code == p.code) p.cur = s.bars.back().c;
        }
        g_mainCanvas.dirty = true;
        for (auto& c : g_multi) c.dirty = true;
        g_health.latencyMs = 8 + (int)(rng() % 20);
        g_health.rateUsed = (int)(rng() % 45);
        g_health.subCount = (int)g_targets.size() + 1;
        if (++tick % 12 == 0) g_signalLog.Add("SIG", "JMA(%d/%d) 교차 후보 감지 — 스텁", P_jmaFast, P_jmaMid);
        g_wakeFrames = (std::max)(g_wakeFrames, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

// ─────────────────────────────── 레이아웃 ───────────────────────────────────
static void BuildDefaultLayout(ImGuiID root) {
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = root;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.15f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.38f, nullptr, &center);
    ImGuiID bottom2 = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Down, 0.5f, nullptr, &bottom);

    ImGui::DockBuilderDockWindow("종목풀", left);
    ImGui::DockBuilderDockWindow("주력 차트", center);
    ImGui::DockBuilderDockWindow("매매 멀티차트", center);
    ImGui::DockBuilderDockWindow("스캐너", center);
    ImGui::DockBuilderDockWindow("프로퍼티", right);
    ImGui::DockBuilderDockWindow("대시보드", bottom);
    ImGui::DockBuilderDockWindow("로그", bottom2);
    ImGui::DockBuilderDockWindow("신호", bottom2);
    ImGui::DockBuilderDockWindow("주문/체결", bottom2);
    ImGui::DockBuilderDockWindow("결함", bottom2);
    ImGui::DockBuilderFinish(root);
}

// ─────────────────────────────── D3D / Win32 ────────────────────────────────
static void CreateMainRTV() {
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) {
        g_dev->CreateRenderTargetView(bb, nullptr, &g_mainRTV);
        bb->Release();
    }
}
static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        want, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx))) return false;
    CreateMainRTV();
    return true;
}
static void CleanupDeviceD3D() {
    if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wP, LPARAM lP) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wP, lP)) { g_wakeFrames = 60; return true; }
    switch (msg) {
    case WM_SIZE:
        if (wP != SIZE_MINIMIZED) { g_resizeW = LOWORD(lP); g_resizeH = HIWORD(lP); g_wakeFrames = 60; }
        return 0;
    case WM_MOUSEMOVE: case WM_KEYDOWN: case WM_LBUTTONDOWN: case WM_MOUSEWHEEL:
        g_wakeFrames = 60; break;
    case WM_SYSCOMMAND: if ((wP & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wP, lP);
}

// ─────────────────────────────── main ───────────────────────────────────────

// ==== [PATCH] 한글 글리프 범위 (완성형 전 영역) ====
static const ImWchar* GetKoreanRanges(ImGuiIO& io)
{
    static ImVector<ImWchar> s_kr;
    if (s_kr.Size == 0) {
        ImFontGlyphRangesBuilder gb;
        gb.AddRanges(io.Fonts->GetGlyphRangesDefault());
        static const ImWchar kKR[] = { 0x3131,0x318E, 0xAC00,0xD7A3, 0x2010,0x2027, 0x3000,0x303F, 0xFF01,0xFF60, 0 };
        gb.AddRanges(kKR);
        gb.BuildRanges(&s_kr);
    }
    return s_kr.Data;
}
// ==== [/PATCH] ====
// ==== [PATCH] 디바이스 리소스 일괄 생성/해제 ====
static void ReleaseChartGfx()
{
    if (g_vs) { g_vs->Release(); g_vs = nullptr; }
    if (g_ps) { g_ps->Release(); g_ps = nullptr; }
    if (g_il) { g_il->Release(); g_il = nullptr; }
    if (g_rs) { g_rs->Release(); g_rs = nullptr; }
    if (g_vb) { g_vb->Release(); g_vb = nullptr; }
}
static bool CreateDeviceObjects()
{
    if (!InitChartGfx()) return false;
    ImGui_ImplDX11_CreateDeviceObjects();
    return true;
}
static void ReleaseDeviceObjects()
{
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ReleaseChartGfx();
}
// ==== [/PATCH] ====
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    double t0 = NowSec();
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr,
                       LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"TradingShell", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Trading Shell — UI 골격",
        WS_OVERLAPPEDWINDOW, 60, 40, 1600, 950, nullptr, nullptr, hInst, nullptr);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, hInst); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "shell_layout.ini";           // 레이아웃 자동 저장/복원
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.f; st.FrameRounding = 2.f; st.WindowPadding = ImVec2(6, 6);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);


    // 한글 폰트 (없으면 기본 폰트 → 한글 네모로 표시됨)
    {
        ImFontConfig cfg; cfg.OversampleH = 2; cfg.OversampleV = 1;
        const char* cands[] = { "C:\\Windows\\Fonts\\malgun.ttf", "C:\\Windows\\Fonts\\gulim.ttc" };
        bool ok = false;
        for (const char* f : cands) {
            if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) continue;
#if IMGUI_VERSION_NUM < 19200
            ok = io.Fonts->AddFontFromFileTTF(f, 16.f, &cfg, GetKoreanRanges(io)) != nullptr;
#else
            ok = io.Fonts->AddFontFromFileTTF(f, 16.f, &cfg) != nullptr;   // 1.92+ 동적 로드
#endif
            if (ok) break;
        }
        if (!ok) g_log.Add("SYS", "한글 폰트 로드 실패 — 기본 폰트 사용");
    }

    // 폰트 등록이 끝난 뒤 ImGui 폰트 텍스처와 차트 셰이더 생성
    if (!CreateDeviceObjects()) {
        MessageBoxW(
            nullptr,
            L"차트 셰이더 초기화 실패",
            L"오류",
            MB_ICONERROR
        );

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInst);
        return 2;
    }
    RegisterParams();
    MakeMockData();
    g_health.bootMs = (NowSec() - t0) * 1000.0;
    g_log.Add("SYS", "셸 기동 완료 (%.0fms). 데이터는 목(mock)이며 엔진은 스텁입니다.", g_health.bootMs.load());

    std::thread feed(MockFeedThread);
    bool firstLayout = true;
    bool running = true;

    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_resizeW && g_resizeH) {
            if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; }
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateMainRTV();
        }

        double fstart = NowSec();
        DrainCommands();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ── 호스트 창: 툴바(고정) + 도크스페이스
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        ImGui::Begin("##host", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("파일")) {
                if (ImGui::MenuItem("레이아웃 초기화")) firstLayout = true;
                if (ImGui::MenuItem("종료")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("보기")) {
                ImGui::MenuItem("매매 멀티차트", nullptr, &g_showMulti);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("리셋")) {
                if (ImGui::MenuItem("소프트(렌더러)")) g_bus.Push(Cmd::ResetSoft);
                if (ImGui::MenuItem("피드"))          g_bus.Push(Cmd::ResetFeed);
                if (ImGui::MenuItem("하드(재시작)"))   g_bus.Push(Cmd::ResetHard);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        DrawToolbar();
        ImGui::Separator();
        ImGuiID root = ImGui::GetID("MainDock");
        if (firstLayout) { BuildDefaultLayout(root); firstLayout = false; }
        ImGui::DockSpace(root, ImVec2(0, 0), ImGuiDockNodeFlags_None);
        ImGui::End();

        // ── 패널들
        DrawSymbolPool();
        DrawMainChart();
        DrawMultiChart();
        DrawScanner();
        DrawProperty();
        DrawDashboard();
        DrawLogWindow("로그", g_log);
        DrawLogWindow("신호", g_signalLog);
        DrawLogWindow("주문/체결", g_orderLog);
        DrawFaultWindow();

        // ── 화면 출력
        ImGui::Render();
        const float clear[4] = { 0.06f, 0.06f, 0.07f, 1.f };
        g_ctx->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_ctx->ClearRenderTargetView(g_mainRTV, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        HRESULT hr = g_swap->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            RaiseFault(Fault::DeviceLost, "Present");

        g_health.frameMs = (NowSec() - fstart) * 1000.0;

        // ── 유휴 절전: 입력/데이터 변화 없으면 15fps로 낮춰 대기
        if (g_wakeFrames > 0) --g_wakeFrames;
        else MsgWaitForMultipleObjectsEx(0, nullptr, 60, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    g_feedRun = false; feed.join();
    g_mainCanvas.Release(); for (auto& c : g_multi) c.Release();
    ReleaseDeviceObjects();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, hInst);
    return 0;
}
