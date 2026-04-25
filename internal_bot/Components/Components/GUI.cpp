#include "GUI.hpp"
#include "../Includes.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <deque>
#include <cmath>
#include <algorithm>
#include <functional>
#include <mutex>
#include <shlobj.h>
#include <thread>

#include "../ImGui/Kiero/kiero.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"
#include "../ImGui/imgui_stdlib.h"
#include "Main.hpp"
#include "../../OverlayRenderer.hpp"
#include "../../GoySDK/BotModule.hpp"
#include "../../GoySDK/ActionMask.hpp"


struct ConsoleBuffer {
    struct LogEntry {
        std::string text;
        ImVec4 color;
    };
    std::deque<LogEntry> entries;
    std::mutex mtx;
    size_t maxEntries = 1000;
    bool autoScroll = true;

    void Add(const std::string& text, ImVec4 color = ImVec4(1,1,1,1)) {
        std::lock_guard<std::mutex> lock(mtx);
        entries.push_back({text, color});
        if (entries.size() > maxEntries) entries.pop_front();
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        entries.clear();
    }
};
static ConsoleBuffer g_ConsoleBuffer;

static ImVec4 TextColorToImVec4(TextColors c) {
    switch (c) {
        case TextColors::Red:         return ImVec4(1.00f, 0.30f, 0.30f, 1.0f);
        case TextColors::LightRed:    return ImVec4(1.00f, 0.50f, 0.50f, 1.0f);
        case TextColors::Yellow:      return ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
        case TextColors::LightYellow: return ImVec4(1.00f, 0.95f, 0.55f, 1.0f);
        case TextColors::Green:       return ImVec4(0.30f, 0.85f, 0.30f, 1.0f);
        case TextColors::LightGreen:  return ImVec4(0.45f, 1.00f, 0.45f, 1.0f);
        case TextColors::Blue:        return ImVec4(0.30f, 0.50f, 1.00f, 1.0f);
        case TextColors::LightBlue:   return ImVec4(0.45f, 0.70f, 1.00f, 1.0f);
        case TextColors::Aqua:        return ImVec4(0.30f, 0.85f, 0.85f, 1.0f);
        case TextColors::LightAqua:   return ImVec4(0.55f, 1.00f, 1.00f, 1.0f);
        case TextColors::Purple:      return ImVec4(0.70f, 0.40f, 1.00f, 1.0f);
        case TextColors::LightPurple: return ImVec4(0.85f, 0.60f, 1.00f, 1.0f);
        case TextColors::Grey:        return ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
        case TextColors::Black:       return ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        default:                      return ImVec4(0.85f, 0.87f, 0.92f, 1.0f);
    }
}


typedef HRESULT(__stdcall* Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

static int g_SelectedPage = 0;
static ImFont* g_FontMain = nullptr;
static ImFont* g_FontBold = nullptr;
static ImFont* g_FontMono = nullptr;
static bool g_FontsLoaded = false;

static constexpr float kPageGutter = 16.0f;
static constexpr float kColumnGap = 14.0f;
static constexpr float kPanelPadding = 16.0f;
static constexpr float kHeaderBodyGap = 12.0f;
static constexpr float kRowMinHeight = 44.0f;
static constexpr float kRowSpacing = 8.0f;
static constexpr float kSeparatorGap = 8.0f;
static constexpr float kSidebarWidth = 220.0f;
static constexpr float kSidebarItemWidth = kSidebarWidth - 16.0f;
static constexpr float kToggleW = 36.0f;
static constexpr float kToggleH = 20.0f;
static constexpr float kSliderW = 200.0f;
static constexpr float kTopbarH  = 52.0f;
static constexpr float kBottomBarH = 36.0f;
static constexpr float kConsoleToolbarH = 40.0f;
static constexpr float kWindowW = 1120.0f;
static constexpr float kWindowH = 720.0f;


GUIComponent::GUIComponent() : Component("UserInterface", "Displays an interface") { OnCreate(); }
GUIComponent::~GUIComponent() { OnDestroy(); }

bool init = false;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!init)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
        {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            window = sd.OutputWindow;
            ID3D11Texture2D* pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();
            oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)GUIComponent::WndProc);
            GUIComponent::InitImGui();
            init = true;
        }
        else
            return oPresent(pSwapChain, SyncInterval, Flags);
    }
    GUIComponent::Render();
    pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread()
{
    bool init_hook = false;
    int attempts = 0;
    do
    {
        attempts++;
        if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
        {
            kiero::bind(8, (void**)&oPresent, hkPresent);
            init_hook = true;
        }
    } while (!init_hook);
    return TRUE;
}

void GUIComponent::OnCreate() {}
void GUIComponent::OnDestroy() {}

void GUIComponent::Unload()
{
    if (init)
    {
        kiero::shutdown();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }
    CloseHandle(InterfaceThread);
}

HANDLE GUIComponent::InterfaceThread = NULL;
bool GUIComponent::IsOpen = true;

void GUIComponent::Initialize() {
    Console.SetGuiCallback([](const std::string& text, TextColors color) {
        g_ConsoleBuffer.Add(text, TextColorToImVec4(color));
    });
    InterfaceThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), nullptr, 0, nullptr);
}

void GUIComponent::InitImGui()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);

    if (!g_FontsLoaded) {
        char fontsDir[MAX_PATH] = "C:\\Windows\\Fonts";
        SHGetFolderPathA(NULL, CSIDL_FONTS, NULL, 0, fontsDir);
        std::string fontsDirStr(fontsDir);

        g_FontMain = io.Fonts->AddFontFromFileTTF((fontsDirStr + "\\segoeui.ttf").c_str(), 15.0f);
        if (!g_FontMain) g_FontMain = io.Fonts->AddFontDefault();

        g_FontBold = io.Fonts->AddFontFromFileTTF((fontsDirStr + "\\segoeuib.ttf").c_str(), 15.0f);
        if (!g_FontBold) g_FontBold = g_FontMain;

        g_FontMono = io.Fonts->AddFontFromFileTTF((fontsDirStr + "\\consola.ttf").c_str(), 13.0f);
        if (!g_FontMono) g_FontMono = io.Fonts->AddFontDefault();

        g_FontsLoaded = true;
    }
}

LRESULT __stdcall GUIComponent::WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    if (io.WantCaptureMouse && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP || uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEMOVE))
        return TRUE;
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}


// ─── Dark overlay UI ──
// Palette aligned with the Claude Design "GoySDK Internal" handoff:
// deeper bases, brighter per-page accents, and dim/glow variants for
// pill backgrounds and toggle aura.
namespace Theme {
    static const ImVec4 Page        = ImVec4(0.027f, 0.027f, 0.039f, 1.00f); // #07070a
    static const ImVec4 Base        = ImVec4(0.043f, 0.043f, 0.063f, 1.00f); // #0b0b10
    static const ImVec4 Surface     = ImVec4(0.063f, 0.063f, 0.086f, 1.00f); // #101016
    static const ImVec4 Elevated    = ImVec4(0.086f, 0.086f, 0.118f, 1.00f); // #16161e
    static const ImVec4 Hover       = ImVec4(0.114f, 0.114f, 0.153f, 1.00f); // #1d1d27
    static const ImVec4 Input       = ImVec4(0.051f, 0.051f, 0.071f, 1.00f); // #0d0d12

    static const ImVec4 Border       = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    static const ImVec4 BorderStrong = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    static const ImVec4 BorderSubtle = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);

    static const ImVec4 Text        = ImVec4(0.902f, 0.902f, 0.925f, 1.00f); // #e6e6ec
    static const ImVec4 TextDim     = ImVec4(0.541f, 0.541f, 0.592f, 1.00f); // #8a8a97
    static const ImVec4 TextMuted   = ImVec4(0.329f, 0.329f, 0.373f, 1.00f); // #54545f

    static const ImVec4 Green       = ImVec4(0.133f, 0.773f, 0.369f, 1.00f); // #22c55e
    static const ImVec4 GreenDim    = ImVec4(0.133f, 0.773f, 0.369f, 0.14f);
    static const ImVec4 GreenGlow   = ImVec4(0.133f, 0.773f, 0.369f, 0.35f);
    static const ImVec4 Amber       = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #f59e0b
    static const ImVec4 AmberDim    = ImVec4(0.961f, 0.620f, 0.043f, 0.14f);
    static const ImVec4 AmberGlow   = ImVec4(0.961f, 0.620f, 0.043f, 0.35f);
    static const ImVec4 Red         = ImVec4(0.937f, 0.267f, 0.267f, 1.00f); // #ef4444
    static const ImVec4 RedDim      = ImVec4(0.937f, 0.267f, 0.267f, 0.14f);
    static const ImVec4 Blue        = ImVec4(0.231f, 0.510f, 0.965f, 1.00f); // #3b82f6
    static const ImVec4 BlueDim     = ImVec4(0.231f, 0.510f, 0.965f, 0.14f);
    static const ImVec4 BlueGlow    = ImVec4(0.231f, 0.510f, 0.965f, 0.35f);
    static const ImVec4 Purple      = ImVec4(0.659f, 0.333f, 0.969f, 1.00f); // #a855f7
    static const ImVec4 PurpleDim   = ImVec4(0.659f, 0.333f, 0.969f, 0.14f);
    static const ImVec4 PurpleGlow  = ImVec4(0.659f, 0.333f, 0.969f, 0.35f);
    static const ImVec4 Cyan        = ImVec4(0.024f, 0.714f, 0.831f, 1.00f); // #06b6d4
    static const ImVec4 CyanDim     = ImVec4(0.024f, 0.714f, 0.831f, 0.14f);
    static const ImVec4 CyanGlow    = ImVec4(0.024f, 0.714f, 0.831f, 0.35f);

    static const ImVec4 ConsoleBg   = ImVec4(0.020f, 0.020f, 0.027f, 1.00f); // #050507
    static const ImVec4 SliderTrack = ImVec4(0.082f, 0.082f, 0.114f, 1.00f); // #15151d
}

static void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(0, 0);
    s.WindowRounding    = 16.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildRounding     = 8.0f;
    s.ChildBorderSize   = 0.0f;
    s.FramePadding      = ImVec2(10, 6);
    s.FrameRounding     = 5.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(8, 8);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.GrabMinSize       = 14.0f;
    s.GrabRounding      = 7.0f;
    s.ScrollbarRounding = 3.0f;
    s.ScrollbarSize     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabRounding       = 6.0f;
    s.Alpha             = 1.0f;

    auto& c = s.Colors;
    c[ImGuiCol_Text]                 = Theme::Text;
    c[ImGuiCol_TextDisabled]         = Theme::TextDim;
    c[ImGuiCol_WindowBg]             = Theme::Base;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = Theme::Elevated;
    c[ImGuiCol_Border]               = Theme::Border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = Theme::Elevated;
    c[ImGuiCol_FrameBgHovered]       = Theme::Hover;
    c[ImGuiCol_FrameBgActive]        = Theme::Hover;
    c[ImGuiCol_TitleBg]              = Theme::Base;
    c[ImGuiCol_TitleBgActive]        = Theme::Base;
    c[ImGuiCol_TitleBgCollapsed]     = Theme::Base;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = Theme::Hover;
    c[ImGuiCol_ScrollbarGrabHovered] = Theme::TextMuted;
    c[ImGuiCol_ScrollbarGrabActive]  = Theme::TextDim;
    c[ImGuiCol_CheckMark]            = Theme::Green;
    c[ImGuiCol_SliderGrab]           = Theme::Amber;
    c[ImGuiCol_SliderGrabActive]     = Theme::Amber;
    c[ImGuiCol_Button]               = Theme::Elevated;
    c[ImGuiCol_ButtonHovered]        = Theme::Hover;
    c[ImGuiCol_ButtonActive]         = Theme::Hover;
    c[ImGuiCol_Header]               = Theme::Elevated;
    c[ImGuiCol_HeaderHovered]        = Theme::Hover;
    c[ImGuiCol_HeaderActive]         = Theme::Hover;
    c[ImGuiCol_Separator]            = Theme::Border;
    c[ImGuiCol_SeparatorHovered]     = Theme::Border;
    c[ImGuiCol_SeparatorActive]      = Theme::Border;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab]                  = Theme::Surface;
    c[ImGuiCol_TabHovered]           = Theme::Hover;
    c[ImGuiCol_TabActive]            = Theme::Elevated;
}


// ────────────────────────────────────────────────────────────────────
// Hex Coin logo — implements the "02 — Hex Coin" mark from the design
// handoff (claude.ai/design KWzMJ_BUimDtZpKMYnFmtw).
// Six-vertex hexagon (top-up), accent rim + inset rim + embossed "$".
// Drawn via the window draw-list at the requested center/size.
// ────────────────────────────────────────────────────────────────────
static void DrawHexCoin(ImDrawList* dl, ImVec2 center, float size, const ImVec4& accent)
{
    const float r = size * 0.5f;
    ImVec2 outerPts[6];
    ImVec2 innerPts[6];
    const float innerScale = 0.86f;
    for (int i = 0; i < 6; ++i) {
        const float a = (3.14159265f / 3.0f) * static_cast<float>(i) - 3.14159265f * 0.5f;
        outerPts[i] = ImVec2(center.x + r * cosf(a),               center.y + r * sinf(a));
        innerPts[i] = ImVec2(center.x + r * innerScale * cosf(a),  center.y + r * innerScale * sinf(a));
    }
    const ImU32 face = ImGui::ColorConvertFloat4ToU32(ImVec4(0.039f, 0.039f, 0.051f, 1.0f));
    const ImU32 rim  = ImGui::ColorConvertFloat4ToU32(accent);
    const ImU32 dim  = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.25f));

    dl->AddConvexPolyFilled(outerPts, 6, face);
    dl->AddPolyline(outerPts, 6, rim, ImDrawFlags_Closed, std::max(1.0f, size * 0.025f));
    dl->AddPolyline(innerPts, 6, dim, ImDrawFlags_Closed, std::max(1.0f, size * 0.06f));

    // Embossed "$". font size scales with mark size; we stick with the
    // current font so the glyph blends with the rest of the UI.
    const char* dollar = "$";
    const float charScale = size / 96.0f;
    ImFont* font = ImGui::GetFont();
    const float baseSize = ImGui::GetFontSize();
    const float drawSize = std::max(8.0f, baseSize * (1.6f + 0.6f * charScale));
    ImVec2 ts = font->CalcTextSizeA(drawSize, FLT_MAX, 0.0f, dollar);
    dl->AddText(font, drawSize,
                ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.55f),
                rim, dollar);
}

static bool ToggleSwitch(const char* str_id, bool* v, const ImVec4& accent = Theme::Green)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = kToggleH * 0.5f;

    ImGui::InvisibleButton(str_id, ImVec2(kToggleW, kToggleH));
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
    }

    // Soft accent glow when on (matches design's box-shadow on .switch.on).
    if (*v) {
        for (int i = 3; i >= 1; --i) {
            const float spread = static_cast<float>(i) * 2.0f;
            ImU32 glow = ImGui::ColorConvertFloat4ToU32(
                ImVec4(accent.x, accent.y, accent.z, 0.10f / static_cast<float>(i)));
            dl->AddRectFilled(ImVec2(p.x - spread, p.y - spread),
                              ImVec2(p.x + kToggleW + spread, p.y + kToggleH + spread),
                              glow, radius + spread);
        }
    }

    ImVec4 track = *v ? accent : Theme::SliderTrack;
    if (hovered && !*v) track = Theme::Hover;
    dl->AddRectFilled(p, ImVec2(p.x + kToggleW, p.y + kToggleH),
                      ImGui::ColorConvertFloat4ToU32(track), radius);
    if (!*v) {
        dl->AddRect(p, ImVec2(p.x + kToggleW, p.y + kToggleH),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), radius, 0, 1.0f);
    }

    const float knobR = radius - 3.0f;
    const float knobX = p.x + radius + (*v ? 1.0f : 0.0f) * (kToggleW - kToggleH);
    const ImVec2 center(knobX, p.y + radius);
    dl->AddCircleFilled(ImVec2(center.x + 0.5f, center.y + 0.5f), knobR + 0.5f, IM_COL32(0, 0, 0, 80));
    dl->AddCircleFilled(center, knobR, IM_COL32(243, 243, 247, 255));
    return changed;
}

static float RightAlignedControlX(float controlWidth)
{
    return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - controlWidth;
}

static float ToggleTrailingX()
{
    return RightAlignedControlX(kToggleW);
}

static bool SubtleButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImVec2 sz = size;
    if (sz.y <= 0) sz.y = 30.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Elevated);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Hover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border);
    const bool pressed = ImGui::Button(label, sz);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return pressed;
}

static void Badge(const char* text, const ImVec4& color, const ImVec4& bg)
{
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padX = 6.0f;
    const float padY = 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + padX * 2.0f, p.y + ts.y + padY * 2.0f),
                      ImGui::ColorConvertFloat4ToU32(bg), 3.0f);
    dl->AddText(ImVec2(p.x + padX, p.y + padY), ImGui::ColorConvertFloat4ToU32(color), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2.0f, ts.y + padY * 2.0f));
}

static bool AccentSliderFloat(const char* label, const char* id, float* v, float mn, float mx,
                              const char* fmt, const ImVec4& accent)
{
    ImGui::SetCursorPosX(kPanelPadding);
    ImGui::TextColored(Theme::TextDim, "%s", label);
    ImGui::SameLine(RightAlignedControlX(kSliderW));
    ImGui::SetNextItemWidth(kSliderW);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::SliderTrack);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover);
    const bool ch = ImGui::SliderFloat(id, v, mn, mx, fmt);
    ImGui::PopStyleColor(4);
    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), fmt, *v);
    ImGui::SameLine();
    ImGui::TextColored(accent, "%s", valBuf);
    return ch;
}

static bool AccentSliderInt(const char* label, const char* id, int* v, int mn, int mx,
                            const ImVec4& accent)
{
    ImGui::SetCursorPosX(kPanelPadding);
    ImGui::TextColored(Theme::TextDim, "%s", label);
    ImGui::SameLine(RightAlignedControlX(kSliderW));
    ImGui::SetNextItemWidth(kSliderW);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::SliderTrack);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover);
    const bool ch = ImGui::SliderInt(id, v, mn, mx, "%d");
    ImGui::PopStyleColor(4);
    char valBuf[16];
    snprintf(valBuf, sizeof(valBuf), "%d", *v);
    ImGui::SameLine();
    ImGui::TextColored(accent, "%s", valBuf);
    return ch;
}

static void PanelBegin() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kRowSpacing, kRowSpacing));
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border);
}

static void DrawSubtleSeparator() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp = ImGui::GetCursorScreenPos();
    dl->AddLine(sp, ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
    ImGui::Dummy(ImVec2(0, kSeparatorGap));
}

static void PanelHeader(const char* title, const ImVec4& accent, const char* badgeText = nullptr,
                        const ImVec4* badgeColor = nullptr, const ImVec4* badgeBg = nullptr)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    // Glowing accent bar (3×14) with a soft halo to match the design's
    // .accent-bar box-shadow.
    for (int i = 3; i >= 1; --i) {
        const float spread = static_cast<float>(i) * 1.5f;
        ImU32 glow = ImGui::ColorConvertFloat4ToU32(
            ImVec4(accent.x, accent.y, accent.z, 0.18f / static_cast<float>(i)));
        dl->AddRectFilled(ImVec2(p.x - spread, p.y - spread),
                          ImVec2(p.x + 3.0f + spread, p.y + 14.0f + spread),
                          glow, 2.0f);
    }
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 3.0f, p.y + 14.0f),
                      ImGui::ColorConvertFloat4ToU32(accent), 2.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
    if (g_FontBold) ImGui::PushFont(g_FontBold);
    ImGui::TextColored(Theme::Text, "%s", title);
    if (g_FontBold) ImGui::PopFont();

    if (badgeText && badgeColor && badgeBg) {
        float badgeWidth = ImGui::CalcTextSize(badgeText).x + 12.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - badgeWidth - kPanelPadding);
        Badge(badgeText, *badgeColor, *badgeBg);
    }

    ImVec2 sepMin = ImGui::GetCursorScreenPos();
    const float sepW = ImGui::GetContentRegionAvail().x;
    dl->AddLine(sepMin, ImVec2(sepMin.x + sepW, sepMin.y),
                ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
    ImGui::Dummy(ImVec2(0, kHeaderBodyGap));
}

static void PanelEnd()
{
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

static const char* GetModelNameForIndex(int modelIdx)
{
    if (modelIdx < 0 || modelIdx >= static_cast<int>(GoySDK::kModelProfiles.size()))
        return "Unknown";
    return GoySDK::kModelProfiles[modelIdx].name;
}

static float EstimatePanelHeight(float bodyHeight)
{
    return (kPanelPadding * 2.0f) + 14.0f + kHeaderBodyGap + bodyHeight;
}

static float EstimateStackedRowsHeight(int rows, float rowHeight = kRowMinHeight, int separatorCount = -1)
{
    if (rows < 0) rows = 0;
    if (separatorCount < 0) separatorCount = (rows > 0) ? rows - 1 : 0;
    return rows * rowHeight + separatorCount * kSeparatorGap;
}

static void DrawBodyRowSpacer()
{
    ImGui::Dummy(ImVec2(0, kRowSpacing));
}


/// Title + helper on the left; toggles align to the right when splitTrailing is true. Full-width controls (combos) use splitTrailing false.
static bool DrawSettingRow(const char* title, const char* helper, const std::function<void()>& drawControl,
                           bool withSeparator = true, bool splitTrailing = true)
{
    ImGui::SetCursorPosX(kPanelPadding);
    const float yStart = ImGui::GetCursorPosY();
    bool hovered = false;

    if (!splitTrailing) {
        ImGui::BeginGroup();
        ImGui::TextColored(Theme::TextDim, "%s", title);
        if (helper && helper[0])
            ImGui::TextColored(Theme::TextMuted, "%s", helper);
        drawControl();
        ImGui::EndGroup();
        hovered = ImGui::IsItemHovered();
    } else {
        ImGui::PushID(title);
        const float yRow = ImGui::GetCursorPosY();
        const float leftW = std::max(140.0f, ImGui::GetContentRegionAvail().x - kToggleW - kColumnGap);

        ImGui::BeginChild("srL", ImVec2(leftW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImGui::TextColored(Theme::TextDim, "%s", title);
        if (helper && helper[0])
            ImGui::TextColored(Theme::TextMuted, "%s", helper);
        ImGui::EndChild();

        const float rowH = ImGui::GetItemRectSize().y;

        ImGui::SameLine(0, kColumnGap);
        ImGui::SetCursorPosY(yRow + std::max(0.0f, (rowH - kToggleH) * 0.5f));
        drawControl();
        hovered = ImGui::IsItemHovered();
        ImGui::SetCursorPosY(yRow + std::max(rowH, kToggleH));
        ImGui::PopID();
    }

    const float yEnd = ImGui::GetCursorPosY();
    if (yEnd - yStart < kRowMinHeight)
        ImGui::Dummy(ImVec2(0, kRowMinHeight - (yEnd - yStart)));

    if (withSeparator)
        DrawSubtleSeparator();
    else
        DrawBodyRowSpacer();
    return hovered;
}

static void DrawSimpleRowHeader(const char* title, const char* helper = nullptr)
{
    ImGui::SetCursorPosX(kPanelPadding);
    ImGui::TextColored(Theme::TextDim, "%s", title);
    if (helper && helper[0]) {
        ImGui::SetCursorPosX(kPanelPadding);
        ImGui::TextColored(Theme::TextMuted, "%s", helper);
    }
}

// ────────────────────────────────────────────────────────────────────
// Tiny SVG-style nav glyphs drawn through ImDrawList. One per page so
// the sidebar matches the design's icon-bearing nav rows. 14×14 logical
// box centered on the supplied origin; stroke colour is from the
// active/inactive accent.
// ────────────────────────────────────────────────────────────────────
static void DrawNavIcon(ImDrawList* dl, ImVec2 o, int kind, ImU32 col, float thick = 1.6f)
{
    auto P = [&](float x, float y) { return ImVec2(o.x + x, o.y + y); };
    switch (kind) {
        case 0: { // Controls — sun/gear: dot + radial spokes
            dl->AddCircle(P(7, 7), 3.0f, col, 0, thick);
            dl->AddLine(P(7, 1), P(7, 3), col, thick);   dl->AddLine(P(7, 11), P(7, 13), col, thick);
            dl->AddLine(P(1, 7), P(3, 7), col, thick);   dl->AddLine(P(11, 7), P(13, 7), col, thick);
            dl->AddLine(P(2.5f, 2.5f), P(4.0f, 4.0f), col, thick);
            dl->AddLine(P(10.0f, 10.0f), P(11.5f, 11.5f), col, thick);
            dl->AddLine(P(2.5f, 11.5f), P(4.0f, 10.0f), col, thick);
            dl->AddLine(P(10.0f, 4.0f), P(11.5f, 2.5f), col, thick);
            break;
        }
        case 1: { // Tuning — three rising bars (sliders)
            dl->AddLine(P(2.5f, 13), P(2.5f, 8.5f), col, thick);
            dl->AddLine(P(7,    13), P(7,    5.5f), col, thick);
            dl->AddLine(P(11.5f, 13), P(11.5f, 2.5f), col, thick);
            dl->AddCircleFilled(P(2.5f, 8.5f), 1.6f, col);
            dl->AddCircleFilled(P(7, 5.5f), 1.6f, col);
            dl->AddCircleFilled(P(11.5f, 2.5f), 1.6f, col);
            break;
        }
        case 2: { // Overlays — 4-pane grid
            dl->AddRect(P(1.5f, 1.5f), P(6.5f, 6.5f), col, 1.0f, 0, thick);
            dl->AddRect(P(7.5f, 1.5f), P(12.5f, 6.5f), col, 1.0f, 0, thick);
            dl->AddRect(P(1.5f, 7.5f), P(6.5f, 12.5f), col, 1.0f, 0, thick);
            dl->AddRect(P(7.5f, 7.5f), P(12.5f, 12.5f), col, 1.0f, 0, thick);
            break;
        }
        case 3: { // Automation — circle + crosshair spokes (gear-like)
            dl->AddCircle(P(7, 7), 2.6f, col, 0, thick);
            dl->AddLine(P(7, 1), P(7, 3),  col, thick);  dl->AddLine(P(7, 11), P(7, 13), col, thick);
            dl->AddLine(P(1, 7), P(3, 7),  col, thick);  dl->AddLine(P(11, 7), P(13, 7), col, thick);
            dl->AddLine(P(2.5f, 2.5f), P(4.0f, 4.0f), col, thick);
            dl->AddLine(P(10.0f, 10.0f), P(11.5f, 11.5f), col, thick);
            break;
        }
        case 4: { // Console — terminal chevron + underline
            dl->AddLine(P(2, 4),  P(5, 7),  col, thick);
            dl->AddLine(P(5, 7),  P(2, 10), col, thick);
            dl->AddLine(P(7, 11), P(13, 11), col, thick);
            break;
        }
    }
}

static void DrawSidebar()
{
    // Slight vertical gradient on the sidebar to match the design's
    // linear-gradient(180deg, #0d0d13, #08080d).
    ImVec2 sbPos = ImGui::GetCursorScreenPos();
    ImVec2 sbMax = ImVec2(sbPos.x + kSidebarWidth, sbPos.y + ImGui::GetContentRegionAvail().y);
    ImDrawList* outer = ImGui::GetWindowDrawList();
    const ImU32 sbTop = ImGui::ColorConvertFloat4ToU32(ImVec4(0.051f, 0.051f, 0.075f, 1.0f));
    const ImU32 sbBot = ImGui::ColorConvertFloat4ToU32(ImVec4(0.031f, 0.031f, 0.051f, 1.0f));
    outer->AddRectFilledMultiColor(sbPos, sbMax, sbTop, sbTop, sbBot, sbBot);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##Sidebar", ImVec2(kSidebarWidth, -1), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Brand row: hex coin + name + sub ──
    ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
    ImVec2 brand = ImGui::GetCursorScreenPos();
    DrawHexCoin(dl, ImVec2(brand.x + 14.0f, brand.y + 14.0f), 28.0f, Theme::Amber);
    if (g_FontBold) ImGui::PushFont(g_FontBold);
    dl->AddText(ImVec2(brand.x + 38.0f, brand.y + 1.0f),
                ImGui::ColorConvertFloat4ToU32(Theme::Text), "GoySDK");
    if (g_FontBold) ImGui::PopFont();
    dl->AddText(ImVec2(brand.x + 38.0f, brand.y + 18.0f),
                ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), "INTERNAL · v0.9");
    ImGui::Dummy(ImVec2(0, 36.0f));

    // ── Nav rows ──
    struct NavEntry { const char* label; int pageIdx; ImVec4 accent; int icon; const char* kbd; };
    auto DrawNavItem = [&](const NavEntry& e) {
        const bool sel = (g_SelectedPage == e.pageIdx);
        ImGui::SetCursorPosX(8.0f);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float h = 30.0f;
        if (ImGui::InvisibleButton(e.label, ImVec2(kSidebarItemWidth, h))) g_SelectedPage = e.pageIdx;
        const bool hov = ImGui::IsItemHovered();
        if (sel) {
            dl->AddRectFilled(p0, ImVec2(p0.x + kSidebarItemWidth, p0.y + h),
                              ImGui::ColorConvertFloat4ToU32(Theme::Elevated), 7.0f);
            dl->AddRect(p0, ImVec2(p0.x + kSidebarItemWidth, p0.y + h),
                        ImGui::ColorConvertFloat4ToU32(Theme::Border), 7.0f, 0, 1.0f);
        } else if (hov) {
            dl->AddRectFilled(p0, ImVec2(p0.x + kSidebarItemWidth, p0.y + h),
                              ImGui::ColorConvertFloat4ToU32(Theme::Elevated), 7.0f);
        }
        const ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(
            sel ? e.accent : Theme::TextMuted);
        DrawNavIcon(dl, ImVec2(p0.x + 11.0f, p0.y + (h - 14.0f) * 0.5f), e.icon, iconCol);

        ImVec4 tc = sel ? Theme::Text : (hov ? Theme::Text : Theme::TextDim);
        ImVec2 ts = ImGui::CalcTextSize(e.label);
        dl->AddText(ImVec2(p0.x + 33.0f, p0.y + (h - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(tc), e.label);

        // KBD hint pill on the right
        if (e.kbd && e.kbd[0]) {
            ImVec2 ks = ImGui::CalcTextSize(e.kbd);
            float kbdX = p0.x + kSidebarItemWidth - ks.x - 14.0f;
            float kbdY = p0.y + (h - ks.y) * 0.5f;
            dl->AddRectFilled(ImVec2(kbdX - 4.0f, kbdY - 1.0f),
                              ImVec2(kbdX + ks.x + 4.0f, kbdY + ks.y + 1.0f),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.03f)), 3.0f);
            dl->AddRect(ImVec2(kbdX - 4.0f, kbdY - 1.0f),
                        ImVec2(kbdX + ks.x + 4.0f, kbdY + ks.y + 1.0f),
                        ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 3.0f, 0, 1.0f);
            dl->AddText(ImVec2(kbdX, kbdY),
                        ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), e.kbd);
        }
    };

    auto DrawGroupLabel = [&](const char* text) {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, ImGui::GetCursorPosY() + 14.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextMuted);
        ImGui::SetWindowFontScale(0.72f);
        ImGui::Text("%s", text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
    };

    DrawGroupLabel("CONTROL");
    DrawNavItem({"Controls",   0, Theme::Green,  0, "1"});
    DrawNavItem({"Tuning",     1, Theme::Amber,  1, "2"});

    DrawGroupLabel("CONFIGURE");
    DrawNavItem({"Overlays",   2, Theme::Blue,   2, "3"});
    DrawNavItem({"Automation", 3, Theme::Purple, 3, "4"});

    DrawGroupLabel("SYSTEM");
    DrawNavItem({"Console",    4, Theme::Cyan,   4, "5"});

    // ── Sidebar foot — FPS / Inference / Build meters ──
    const float footH = 80.0f;
    const float availH = ImGui::GetContentRegionAvail().y;
    if (availH > footH) ImGui::Dummy(ImVec2(0, availH - footH));
    ImGui::SetCursorPosX(kPanelPadding);
    ImVec2 footStart = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(footStart.x - kPanelPadding, footStart.y),
                ImVec2(footStart.x + kSidebarWidth - kPanelPadding, footStart.y),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
    ImGui::Dummy(ImVec2(0, 12.0f));

    auto DrawMeter = [&](const char* label, const char* value, const ImVec4& valColor) {
        ImGui::SetCursorPosX(kPanelPadding);
        ImVec2 row = ImGui::GetCursorScreenPos();
        const float rowW = kSidebarWidth - kPanelPadding * 2.0f;
        dl->AddText(row,
                    ImGui::ColorConvertFloat4ToU32(Theme::TextDim), label);
        ImVec2 vs = ImGui::CalcTextSize(value);
        if (g_FontMono) ImGui::PushFont(g_FontMono);
        dl->AddText(g_FontMono ? g_FontMono : ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(row.x + rowW - vs.x, row.y),
                    ImGui::ColorConvertFloat4ToU32(valColor), value);
        if (g_FontMono) ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 18.0f));
    };

    char fpsBuf[16];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", ImGui::GetIO().Framerate);
    DrawMeter("FPS", fpsBuf, Theme::Green);
    DrawMeter("Inference", "--", Theme::Text);
    DrawMeter("Build", "12cfd6b", Theme::TextDim);

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Right-edge separator between sidebar and main column.
    outer->AddLine(ImVec2(sbMax.x, sbPos.y),
                   ImVec2(sbMax.x, sbMax.y),
                   ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
}

// Chip helper — draws a pill-shaped status chip ("KEY value") in mono font
// at draw-list level. Returns the rendered width so callers can lay out the
// next chip horizontally.
static float DrawChip(ImDrawList* dl, ImVec2 origin, const char* key, const char* value,
                      const ImVec4& bg, const ImVec4& border, const ImVec4& keyCol, const ImVec4& valCol)
{
    const float padX = 10.0f;
    const float h = 26.0f;
    ImFont* mono = g_FontMono ? g_FontMono : ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    ImVec2 ks = (key && key[0]) ? mono->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, key) : ImVec2(0, 0);
    ImVec2 vs = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, value);
    const float gap = (key && key[0]) ? 6.0f : 0.0f;
    const float w = padX * 2.0f + ks.x + gap + vs.x;

    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h),
                      ImGui::ColorConvertFloat4ToU32(bg), h * 0.5f);
    dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h),
                ImGui::ColorConvertFloat4ToU32(border), h * 0.5f, 0, 1.0f);

    float cx = origin.x + padX;
    if (key && key[0]) {
        dl->AddText(mono, fs * 0.85f,
                    ImVec2(cx, origin.y + (h - ks.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(keyCol), key);
        cx += ks.x + gap;
    }
    dl->AddText(mono, fs,
                ImVec2(cx, origin.y + (h - vs.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(valCol), value);
    return w;
}

static void DrawStatusBar()
{
    auto& cfg = BotMod.GetConfig();
    const bool active = BotMod.IsActive();
    const int currentModel = BotMod.GetCurrentModelIdx();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Base);
    ImGui::BeginChild("##StatusBar", ImVec2(-1, kTopbarH), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();

    // Subtle bottom border
    dl->AddLine(ImVec2(wpos.x, wpos.y + kTopbarH - 0.5f),
                ImVec2(wpos.x + wsz.x, wpos.y + kTopbarH - 0.5f),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);

    // ── Crumbs (left) — "GoySDK / <Page>"
    static const char* pageNames[] = {"Controls", "Tuning", "Overlays", "Automation", "Console"};
    const float crumbY = (kTopbarH - ImGui::GetFontSize()) * 0.5f;
    float cx = kPanelPadding;
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + crumbY),
                ImGui::ColorConvertFloat4ToU32(Theme::TextDim), "GoySDK");
    cx += ImGui::CalcTextSize("GoySDK").x + 8.0f;
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + crumbY),
                ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), "/");
    cx += ImGui::CalcTextSize("/").x + 8.0f;
    const char* nowLabel = (g_SelectedPage >= 0 && g_SelectedPage <= 4)
        ? pageNames[g_SelectedPage] : "—";
    if (g_FontBold) {
        dl->AddText(g_FontBold, ImGui::GetFontSize(),
                    ImVec2(wpos.x + cx, wpos.y + crumbY),
                    ImGui::ColorConvertFloat4ToU32(Theme::Text), nowLabel);
    } else {
        dl->AddText(ImVec2(wpos.x + cx, wpos.y + crumbY),
                    ImGui::ColorConvertFloat4ToU32(Theme::Text), nowLabel);
    }

    // ── Chip strip + bot pill (right) — laid out from the right edge inward.
    const char* modelName = GetModelNameForIndex(currentModel);
    const char* mode = (currentModel >= 0 && currentModel < static_cast<int>(GoySDK::kModelProfiles.size()))
        ? GoySDK::kModelProfiles[currentModel].supportedModes : "—";
    char tickBuf[8];
    snprintf(tickBuf, sizeof(tickBuf), "%d", cfg.tickSkip);
    const char* devName = (cfg.inferenceBackend == GoySDK::InferenceBackend::Cuda0) ? "CUDA" : "CPU";

    // Bot pill spec
    const char* botLabel = active ? "BOT ON" : "BOT OFF";
    const float pillH = 28.0f;
    ImVec2 bls;
    if (g_FontBold) bls = g_FontBold->CalcTextSizeA(ImGui::GetFontSize() * 0.85f, FLT_MAX, 0.0f, botLabel);
    else bls = ImGui::CalcTextSize(botLabel);
    const float pillW = bls.x + 38.0f;

    // Measure each chip so we can right-align.
    const float chipH = 26.0f;
    const float chipY = wpos.y + (kTopbarH - chipH) * 0.5f;
    const float pillY = wpos.y + (kTopbarH - pillH) * 0.5f;
    const float gap = 8.0f;

    ImFont* mono = g_FontMono ? g_FontMono : ImGui::GetFont();
    const float fs = ImGui::GetFontSize();

    auto chipMeasure = [&](const char* k, const char* v) {
        ImVec2 ks = (k && k[0]) ? mono->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, k) : ImVec2(0, 0);
        ImVec2 vs = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, v);
        const float g = (k && k[0]) ? 6.0f : 0.0f;
        return 20.0f + ks.x + g + vs.x; // 10px pad on each side
    };

    const float wPill   = pillW;
    const float wDevice = chipMeasure("Device", devName);
    const float wTick   = chipMeasure("Tick", tickBuf);
    const float wMode   = chipMeasure(nullptr, mode);
    const float wModel  = chipMeasure("Model", modelName);

    float rx = wpos.x + wsz.x - kPanelPadding - wPill;
    // Bot pill at the rightmost slot
    {
        ImVec4 bg = active ? Theme::GreenDim : Theme::RedDim;
        ImVec4 fg = active ? Theme::Green   : Theme::Red;
        ImVec2 p0(rx, pillY);
        ImVec2 p1(rx + wPill, pillY + pillH);
        // Optional outer glow when on
        if (active) {
            for (int i = 3; i >= 1; --i) {
                const float spread = static_cast<float>(i) * 2.0f;
                dl->AddRectFilled(ImVec2(p0.x - spread, p0.y - spread),
                                  ImVec2(p1.x + spread, p1.y + spread),
                                  ImGui::ColorConvertFloat4ToU32(
                                      ImVec4(fg.x, fg.y, fg.z, 0.10f / static_cast<float>(i))),
                                  pillH * 0.5f + spread);
            }
        }
        dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(bg), pillH * 0.5f);
        dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(
                        ImVec4(fg.x, fg.y, fg.z, 0.30f)), pillH * 0.5f, 0, 1.0f);

        // Pulsing dot. ImGui::GetTime() drives a sine-wave alpha to mimic
        // the @keyframes pulse animation in the design.
        const float t = static_cast<float>(ImGui::GetTime());
        const float pulse = active ? (0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 4.0f))) : 1.0f;
        const float dotR = 3.5f;
        ImVec2 dotC(p0.x + 14.0f, p0.y + pillH * 0.5f);
        dl->AddCircleFilled(dotC, dotR + 2.0f,
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(fg.x, fg.y, fg.z, 0.18f * pulse)));
        dl->AddCircleFilled(dotC, dotR,
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(fg.x, fg.y, fg.z, pulse)));

        // Bold uppercase label.
        ImFont* bf = g_FontBold ? g_FontBold : ImGui::GetFont();
        dl->AddText(bf, fs * 0.85f,
                    ImVec2(p0.x + 14.0f + dotR * 2.0f + 6.0f, p0.y + (pillH - bls.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(fg), botLabel);

        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton("##BotPill", ImVec2(wPill, pillH)))
            Main.Execute([]() { BotMod.ToggleBot(); });
    }

    rx -= gap + wDevice;
    DrawChip(dl, ImVec2(rx, chipY), "Device", devName,
             Theme::Elevated, Theme::Border, Theme::TextMuted, Theme::Text);
    rx -= gap + wTick;
    DrawChip(dl, ImVec2(rx, chipY), "Tick", tickBuf,
             Theme::Elevated, Theme::Border, Theme::TextMuted, Theme::Text);
    rx -= gap + wMode;
    // Mode chip uses an amber treatment to match the design's .chip-mode.
    DrawChip(dl, ImVec2(rx, chipY), nullptr, mode,
             Theme::AmberDim,
             ImVec4(Theme::Amber.x, Theme::Amber.y, Theme::Amber.z, 0.25f),
             Theme::Amber, Theme::Amber);
    rx -= gap + wModel;
    DrawChip(dl, ImVec2(rx, chipY), "Model", modelName,
             Theme::Elevated, Theme::Border, Theme::TextMuted, Theme::Text);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawBottomBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Base);
    ImGui::BeginChild("##BottomBar", ImVec2(-1, kBottomBarH), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();

    // Top border
    dl->AddLine(ImVec2(wpos.x, wpos.y + 0.5f),
                ImVec2(wpos.x + wsz.x, wpos.y + 0.5f),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);

    ImFont* mono = g_FontMono ? g_FontMono : ImGui::GetFont();
    const float fs = std::max(11.0f, ImGui::GetFontSize() - 2.0f);
    const float ty = wpos.y + (kBottomBarH - fs) * 0.5f;

    // ── Left cluster: HOOKED LED · game · pid ──
    float lx = wpos.x + kPanelPadding;
    const float ledR = 3.0f;
    const ImU32 led = ImGui::ColorConvertFloat4ToU32(Theme::Green);
    dl->AddCircleFilled(ImVec2(lx + ledR, wpos.y + kBottomBarH * 0.5f), ledR + 2.5f,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(Theme::Green.x, Theme::Green.y, Theme::Green.z, 0.25f)));
    dl->AddCircleFilled(ImVec2(lx + ledR, wpos.y + kBottomBarH * 0.5f), ledR, led);
    lx += ledR * 2.0f + 8.0f;
    dl->AddText(mono, fs, ImVec2(lx, ty),
                ImGui::ColorConvertFloat4ToU32(Theme::Text), "HOOKED");
    lx += mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, "HOOKED").x + 14.0f;
    const char* host = "Rocket League";
    dl->AddText(mono, fs, ImVec2(lx, ty),
                ImGui::ColorConvertFloat4ToU32(Theme::TextDim), host);

    // ── Right cluster: fps · infer · INSERT hint ──
    char fpsBuf[24];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f fps", ImGui::GetIO().Framerate);
    const char* hint = "INSERT to toggle UI";
    ImVec2 fpsSz = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, fpsBuf);
    ImVec2 hintSz = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, hint);
    float rx = wpos.x + wsz.x - kPanelPadding - hintSz.x;
    dl->AddText(mono, fs, ImVec2(rx, ty),
                ImGui::ColorConvertFloat4ToU32(Theme::TextDim), hint);
    rx -= 14.0f + fpsSz.x;
    dl->AddText(mono, fs, ImVec2(rx, ty),
                ImGui::ColorConvertFloat4ToU32(Theme::TextDim), fpsBuf);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}


static void DrawControlsPage()
{
    auto& cfg = BotMod.GetConfig();
    int currentModel = BotMod.GetCurrentModelIdx();
    const int profileCount = static_cast<int>(GoySDK::kModelProfiles.size());
    int numSlots = BotMod.GetLocalPlayerCount();
    if (numSlots < 1) numSlots = 1;

    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - kColumnGap) * 0.5f;
    const int extraSlots = std::max(0, numSlots - 1);
    const float leftHeight = EstimatePanelHeight(EstimateStackedRowsHeight(3 + extraSlots));
    const float rightHeight = EstimatePanelHeight(
        EstimateStackedRowsHeight(numSlots, 50.0f) +
        30.0f +
        ((numSlots > 1) ? (30.0f + kRowSpacing) : 0.0f)
    );
    const float rowH = std::max(leftHeight, rightHeight);

    PanelBegin();
    ImGui::BeginChild("##PanelBotCtrl", ImVec2(colW, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        PanelHeader("Bot Controls", Theme::Green);

        DrawSettingRow("Model", "(Slot 0)", [&]() {
            const bool loading = BotMod.IsModelLoading();
            if (loading) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::BeginCombo("##Model", loading ? "Loading..." : GetModelNameForIndex(currentModel))) {
                for (int i = 0; i < profileCount; ++i) {
                    const bool selected = (currentModel == i);
                    if (ImGui::Selectable(GoySDK::kModelProfiles[i].name, selected)) {
                        currentModel = i;
                        if (currentModel != BotMod.GetCurrentModelIdx()) {
                            Main.Execute([currentModel]() { BotMod.SwitchModel(currentModel); });
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (loading) ImGui::EndDisabled();
        }, true, false);

        const bool cudaAvail = BotMod.IsCudaInferenceAvailable();
        bool useCuda = cfg.inferenceBackend == GoySDK::InferenceBackend::Cuda0;
        DrawSettingRow("CUDA Inference", "GPU acceleration", [&]() {
            ImGui::BeginDisabled(!cudaAvail);
            if (ToggleSwitch("##CudaToggle", &useCuda, Theme::Green)) {
                const auto backend = useCuda ? GoySDK::InferenceBackend::Cuda0 : GoySDK::InferenceBackend::CPU;
                Main.Execute([backend]() { GoySDK::BotModule::SetInferenceBackend(backend); });
            }
            ImGui::EndDisabled();
        });

        const bool vigemAvail = BotMod.IsViGEmInputAvailable();
        bool useViGem = vigemAvail && (BotMod.GetInputMethod() == GoySDK::InputMethod::ViGem);
        DrawSettingRow("ViGEm Controller", "Virtual Xbox 360", [&]() {
            ImGui::BeginDisabled(!vigemAvail);
            if (ToggleSwitch("##ViGEmToggle", &useViGem, Theme::Green)) {
                const auto method = useViGem ? GoySDK::InputMethod::ViGem : GoySDK::InputMethod::Internal;
                Main.Execute([method]() { BotMod.SetInputMethod(method); });
            }
            ImGui::EndDisabled();
        }, numSlots >= 2);

        if (numSlots >= 2) {
            ImGui::SetCursorPosX(kPanelPadding);
            for (int s = 1; s < numSlots; ++s) {
                char slotLabel[24];
                snprintf(slotLabel, sizeof(slotLabel), "Slot %d", s);
                auto& slot = BotMod.GetSlot(s);
                const char* preview = (slot.modelIdx >= 0) ? GetModelNameForIndex(slot.modelIdx) : "Human";

                DrawSettingRow(slotLabel, nullptr, [&]() {
                    ImGui::PushID(s);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::BeginCombo("##SlotModel", preview)) {
                        if (ImGui::Selectable("Human", slot.modelIdx < 0)) {
                            const int si = s;
                            Main.Execute([si]() { BotMod.AssignModel(si, -1); });
                        }
                        for (int mi = 0; mi < profileCount; ++mi) {
                            const auto& p = GoySDK::kModelProfiles[mi];
                            std::string label = std::string(p.name) + " [" + p.supportedModes + "]";
                            if (ImGui::Selectable(label.c_str(), slot.modelIdx == mi)) {
                                const int si = s;
                                const int sm = mi;
                                Main.Execute([si, sm]() { BotMod.AssignModel(si, sm); });
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }, s + 1 < numSlots, false);
            }
        }
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, kColumnGap);

    PanelBegin();
    char slotBadge[16];
    snprintf(slotBadge, sizeof(slotBadge), "%d slot%s", numSlots, numSlots > 1 ? "s" : "");
    ImGui::BeginChild("##PanelSplit", ImVec2(0, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        PanelHeader("Splitscreen", Theme::Green, slotBadge, &Theme::Green, &Theme::GreenDim);

        for (int s = 0; s < numSlots; s++) {
            ImGui::SetCursorPosX(kPanelPadding);
            auto& slot = BotMod.GetSlot(s);
            const int modelIdx = (s == 0) ? currentModel : slot.modelIdx;
            const bool isHuman = (modelIdx < 0);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Elevated);
            char childId[32];
            snprintf(childId, sizeof(childId), "##Slot%d", s);
            ImGui::BeginChild(childId, ImVec2(ImGui::GetContentRegionAvail().x - kPanelPadding, 50), true,
                              ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* childDl = ImGui::GetWindowDrawList();
                ImVec2 origin = ImGui::GetCursorScreenPos();

                // Slot number badge — gradient based on bot/human, with rim
                // glow that matches the design's .slot-card.bot / .human swatches.
                const float badgeSize = 28.0f;
                ImVec2 b0(origin.x + 4.0f, origin.y + 6.0f);
                ImVec2 b1(b0.x + badgeSize, b0.y + badgeSize);
                ImVec4 badgeTop, badgeBot, badgeBorder, badgeText;
                if (isHuman) {
                    badgeTop = ImVec4(Theme::Purple.x, Theme::Purple.y, Theme::Purple.z, 0.18f);
                    badgeBot = ImVec4(Theme::Purple.x, Theme::Purple.y, Theme::Purple.z, 0.06f);
                    badgeBorder = ImVec4(Theme::Purple.x, Theme::Purple.y, Theme::Purple.z, 0.20f);
                    badgeText = Theme::Purple;
                } else {
                    badgeTop = ImVec4(Theme::Green.x, Theme::Green.y, Theme::Green.z, 0.18f);
                    badgeBot = ImVec4(Theme::Green.x, Theme::Green.y, Theme::Green.z, 0.06f);
                    badgeBorder = ImVec4(Theme::Green.x, Theme::Green.y, Theme::Green.z, 0.20f);
                    badgeText = Theme::Green;
                }
                childDl->AddRectFilledMultiColor(b0, b1,
                    ImGui::ColorConvertFloat4ToU32(badgeTop),
                    ImGui::ColorConvertFloat4ToU32(badgeTop),
                    ImGui::ColorConvertFloat4ToU32(badgeBot),
                    ImGui::ColorConvertFloat4ToU32(badgeBot));
                childDl->AddRect(b0, b1,
                    ImGui::ColorConvertFloat4ToU32(badgeBorder), 6.0f, 0, 1.0f);
                char idxBuf[4];
                snprintf(idxBuf, sizeof(idxBuf), "%d", s);
                ImFont* mono = g_FontMono ? g_FontMono : ImGui::GetFont();
                ImVec2 idxSz = mono->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, idxBuf);
                childDl->AddText(mono, ImGui::GetFontSize(),
                                 ImVec2(b0.x + (badgeSize - idxSz.x) * 0.5f,
                                        b0.y + (badgeSize - idxSz.y) * 0.5f),
                                 ImGui::ColorConvertFloat4ToU32(badgeText), idxBuf);

                // Name + sub
                const char* name = isHuman ? "Human" : GetModelNameForIndex(modelIdx);
                if (g_FontBold) {
                    childDl->AddText(g_FontBold, ImGui::GetFontSize(),
                                     ImVec2(origin.x + 42.0f, origin.y + 6.0f),
                                     ImGui::ColorConvertFloat4ToU32(Theme::Text), name);
                } else {
                    childDl->AddText(ImVec2(origin.x + 42.0f, origin.y + 6.0f),
                                     ImGui::ColorConvertFloat4ToU32(Theme::Text), name);
                }
                char sub[64];
                if (isHuman) {
                    snprintf(sub, sizeof(sub), "controller input · passthrough");
                } else if (modelIdx >= 0 && modelIdx < profileCount) {
                    const auto& prof = GoySDK::kModelProfiles[modelIdx];
                    snprintf(sub, sizeof(sub), "%s · %d obs", prof.supportedModes, cfg.GetExpectedObsCount());
                } else {
                    sub[0] = '\0';
                }
                childDl->AddText(mono, ImGui::GetFontSize() - 2.0f,
                                 ImVec2(origin.x + 42.0f, origin.y + 24.0f),
                                 ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), sub);

                // Right-side badge: Primary (slot 0) / Bot / Human
                const char* badgeText2 = (s == 0) ? "Primary" : (isHuman ? "Human" : "Bot");
                ImVec4 badgeFg, badgeBg;
                if (s == 0)        { badgeFg = Theme::Amber;  badgeBg = Theme::AmberDim;  }
                else if (isHuman)  { badgeFg = Theme::Purple; badgeBg = Theme::PurpleDim; }
                else               { badgeFg = Theme::Green;  badgeBg = Theme::GreenDim;  }
                ImVec2 bs = ImGui::CalcTextSize(badgeText2);
                const float padX = 8.0f, padY = 3.0f;
                float bx = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - bs.x - padX * 2.0f - kPanelPadding;
                float by = origin.y + (50 - (bs.y + padY * 2.0f)) * 0.5f;
                childDl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bs.x + padX * 2.0f, by + bs.y + padY * 2.0f),
                                       ImGui::ColorConvertFloat4ToU32(badgeBg), 4.0f);
                childDl->AddText(ImVec2(bx + padX, by + padY),
                                 ImGui::ColorConvertFloat4ToU32(badgeFg), badgeText2);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            DrawBodyRowSpacer();
        }

        ImGui::SetCursorPosX(kPanelPadding);
        const bool inGame = BotMod.IsInGame();
        const bool canAdd = (numSlots < BotMod.kMaxSlots) && !inGame;
        if (!canAdd) ImGui::BeginDisabled();
        if (SubtleButton("+ Add Slot", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            Main.Execute([]() { BotMod.AddSplitscreen(); });
        }
        if (!canAdd) ImGui::EndDisabled();

        const bool canRemove = (numSlots > 1) && !inGame;
        if (canRemove) {
            ImGui::SetCursorPosX(kPanelPadding);
            if (SubtleButton("Remove Slot", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                Main.Execute([]() { BotMod.RemoveSplitscreen(); });
            }
        }
    }
    ImGui::EndChild();
    PanelEnd();
}

static void DrawTuningPage()
{
    auto& cfg = BotMod.GetConfig();
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - kColumnGap) * 0.5f;
    const float leftBody = cfg.humanize
        ? (3.0f * kRowMinHeight) + (2.0f * kSeparatorGap) + kRowSpacing
        : 28.0f;
    const float leftHeight = EstimatePanelHeight(leftBody);
    const float rightHeight = EstimatePanelHeight((2.0f * kRowMinHeight) + kSeparatorGap);
    const float rowH = std::max(leftHeight, rightHeight);

    PanelBegin();
    ImGui::BeginChild("##PanelHum", ImVec2(colW, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 3.0f, p.y + 14.0f),
                          ImGui::ColorConvertFloat4ToU32(Theme::Amber), 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kColumnGap);
        if (g_FontBold) ImGui::PushFont(g_FontBold);
        ImGui::TextColored(Theme::Text, "Humanizer");
        if (g_FontBold) ImGui::PopFont();
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##HumToggle", &cfg.humanize, Theme::Amber);

        DrawSubtleSeparator();

        if (cfg.humanize) {
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderFloat("Noise", "##Noise", &cfg.jitterAmount, 0.0f, 0.02f, "%.3f", Theme::Amber);
            DrawBodyRowSpacer();
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderFloat("Smoothing", "##Smooth", &cfg.smoothFactor, 0.0f, 0.95f, "%.2f", Theme::Amber);
            DrawBodyRowSpacer();
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderFloat("Deadzone", "##Dead", &cfg.deadzone, 0.0f, 0.1f, "%.3f", Theme::Amber);
        } else {
            ImGui::SetCursorPosX(kPanelPadding);
            ImGui::TextColored(Theme::TextMuted, "Enable to configure parameters");
        }
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, kColumnGap);

    PanelBegin();
    ImGui::BeginChild("##PanelPerf", ImVec2(0, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        PanelHeader("Performance", Theme::Amber);

        ImGui::SetCursorPosX(kPanelPadding);
        int ts = cfg.tickSkip;
        AccentSliderInt("Tick Rate", "##TickSkip", &ts, 1, 16, Theme::Amber);
        cfg.tickSkip = ts;
        DrawBodyRowSpacer();

        ImGui::SetCursorPosX(kPanelPadding);
        int kts = cfg.kickoffTickSkip;
        AccentSliderInt("Kickoff Tick Rate", "##KickTS", &kts, 1, 16, Theme::Amber);
        cfg.kickoffTickSkip = kts;
    }
    ImGui::EndChild();
    PanelEnd();
}
static void DrawConsolePage()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Elevated);
    ImGui::BeginChild("##ConsoleToolbar", ImVec2(-1, kConsoleToolbarH), false, ImGuiWindowFlags_NoScrollbar);
    {
        const float toolbarPadY = std::max(0.0f, (kConsoleToolbarH - 22.0f) * 0.5f);
        ImGui::SetCursorPos(ImVec2(kPanelPadding - 4.0f, toolbarPadY));
        if (SubtleButton("Clear", ImVec2(56, 22)))
            g_ConsoleBuffer.Clear();

        ImGui::SameLine(0, kRowSpacing);
        ImGui::SetCursorPosY(toolbarPadY + 2.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 tp = ImGui::GetCursorScreenPos();
        const float miniW = 28.0f;
        const float miniH = 14.0f;
        const float miniR = 7.0f;
        ImVec4 miniCol = g_ConsoleBuffer.autoScroll ? Theme::Cyan : Theme::SliderTrack;
        if (ImGui::InvisibleButton("##MiniScroll", ImVec2(miniW, miniH)))
            g_ConsoleBuffer.autoScroll = !g_ConsoleBuffer.autoScroll;
        dl->AddRectFilled(tp, ImVec2(tp.x + miniW, tp.y + miniH),
                          ImGui::ColorConvertFloat4ToU32(miniCol), miniR);
        float knobX = g_ConsoleBuffer.autoScroll ? tp.x + miniW - miniR : tp.x + miniR;
        dl->AddCircleFilled(ImVec2(knobX, tp.y + miniR), 5.0f, IM_COL32(255, 255, 255, 255));

        ImGui::SameLine(0, kRowSpacing - 2.0f);
        ImGui::SetCursorPosY(toolbarPadY + 4.0f);
        ImGui::TextColored(Theme::TextDim, "Scroll");

        std::lock_guard<std::mutex> lock(g_ConsoleBuffer.mtx);
        char lineBuf[32];
        snprintf(lineBuf, sizeof(lineBuf), "%zu lines", g_ConsoleBuffer.entries.size());
        float tw = ImGui::CalcTextSize(lineBuf).x;
        ImGui::SameLine(RightAlignedControlX(tw));
        ImGui::SetCursorPosY(toolbarPadY + 4.0f);
        ImGui::TextColored(Theme::TextMuted, "%s", lineBuf);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ConsoleBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("##ConsoleBody", ImVec2(-1, -1), true, ImGuiWindowFlags_None);
    {
        if (g_FontMono) ImGui::PushFont(g_FontMono);

        std::lock_guard<std::mutex> lock(g_ConsoleBuffer.mtx);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_ConsoleBuffer.entries.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const auto& entry = g_ConsoleBuffer.entries[i];
                ImGui::PushStyleColor(ImGuiCol_Text, entry.color);
                ImGui::TextUnformatted(entry.text.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (g_ConsoleBuffer.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        if (g_FontMono) ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void DrawOverlaysPage()
{
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - kColumnGap) * 0.5f;
    const float leftHeight = EstimatePanelHeight(EstimateStackedRowsHeight(5));
    const float rightHeight = EstimatePanelHeight(OverlayMod.drawBallPrediction ? 238.0f : 86.0f);
    const float rowH = std::max(leftHeight, rightHeight);

    // Left column: Boost + Ball
    PanelBegin();
    ImGui::BeginChild("##PanelBoost", ImVec2(colW, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        PanelHeader("Boost Indicators", Theme::Blue);

        DrawSettingRow("My Boost", "Show your boost amount", [&]() {
            ToggleSwitch("##ShowMyBoost", &OverlayMod.showMyBoost, Theme::Blue);
        });

        DrawSettingRow("Enemy Boost", "Show opponent boost", [&]() {
            ToggleSwitch("##ShowEnemyBoost", &OverlayMod.showEnemyBoost, Theme::Blue);
        });

        DrawSettingRow("Boost Timers", "Respawn countdown badges only (no marker when ready)", [&]() {
            ToggleSwitch("##BoostTimers", &OverlayMod.drawBoostTimers, Theme::Blue);
        });

        DrawSettingRow("Ball Center", "Mark ball center point", [&]() {
            ToggleSwitch("##DrawBallCenter", &OverlayMod.drawBallCenter, Theme::Blue);
        });

        DrawSettingRow("Hitbox", "Show car hitbox outline", [&]() {
            ToggleSwitch("##DrawHitbox", &OverlayMod.drawHitbox, Theme::Blue);
        }, false);
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, kColumnGap);

    // Right column: Ball Prediction with full config
    PanelBegin();
    ImGui::BeginChild("##PanelPred", ImVec2(0, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));

        // Header with toggle on the right
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 3.0f, p.y + 14.0f),
                          ImGui::ColorConvertFloat4ToU32(Theme::Blue), 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kColumnGap);
        if (g_FontBold) ImGui::PushFont(g_FontBold);
        ImGui::TextColored(Theme::Text, "Ball Prediction");
        if (g_FontBold) ImGui::PopFont();
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##BallPred", &OverlayMod.drawBallPrediction, Theme::Blue);

        DrawSubtleSeparator();
        ImGui::SetCursorPosX(kPanelPadding);
        ImGui::TextColored(Theme::TextMuted, "Uses game trajectory data: arena-accurate bounces (walls, ceiling, floor)");
        DrawBodyRowSpacer();

        if (OverlayMod.drawBallPrediction) {
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderFloat("Line Thickness", "##PredThick", &OverlayMod.predLineThickness,
                              1.0f, 5.0f, "%.1f", Theme::Blue);
            DrawBodyRowSpacer();

            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderFloat("Prediction window (s)", "##PredTime", &OverlayMod.predTimeSeconds,
                              0.5f, 5.0f, "%.1f", Theme::Blue);
            DrawBodyRowSpacer();

            DrawSubtleSeparator();

            // Color preset selector
            ImGui::SetCursorPosX(kPanelPadding);
            ImGui::TextColored(Theme::TextDim, "Line Color");
            DrawBodyRowSpacer();

            static const char* colorNames[] = {"Cyan", "Green", "Yellow", "Red", "White"};
            static const ImVec4 colorSwatches[] = {
                ImVec4(0.0f, 0.86f, 1.0f, 1.0f),
                ImVec4(0.31f, 1.0f, 0.31f, 1.0f),
                ImVec4(1.0f, 0.86f, 0.0f, 1.0f),
                ImVec4(1.0f, 0.31f, 0.31f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
            };

            for (int i = 0; i < 5; i++) {
                ImGui::SetCursorPosX(kPanelPadding);
                ImGui::PushID(i);
                const bool sel = (OverlayMod.predColorPreset == i);

                ImVec2 cp = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##color", ImVec2(ImGui::GetContentRegionAvail().x - kPanelPadding, 22)))
                    OverlayMod.predColorPreset = i;

                if (sel || ImGui::IsItemHovered()) {
                    dl->AddRectFilled(cp, ImVec2(cp.x + ImGui::GetItemRectSize().x, cp.y + 22),
                                      ImGui::ColorConvertFloat4ToU32(sel ? Theme::Elevated : Theme::Hover), 4.0f);
                }

                // Color swatch
                dl->AddCircleFilled(ImVec2(cp.x + 12, cp.y + 11), 5.0f,
                                    ImGui::ColorConvertFloat4ToU32(colorSwatches[i]));

                // Label
                ImVec4 tc = sel ? Theme::Text : Theme::TextDim;
                dl->AddText(ImVec2(cp.x + 24, cp.y + 4),
                            ImGui::ColorConvertFloat4ToU32(tc), colorNames[i]);

                // Checkmark
                if (sel) {
                    ImVec2 chk = ImVec2(cp.x + ImGui::GetItemRectSize().x - 18, cp.y + 4);
                    dl->AddText(chk, ImGui::ColorConvertFloat4ToU32(Theme::Blue), "✓");
                }

                ImGui::PopID();
            }
        } else {
            ImGui::SetCursorPosX(kPanelPadding);
            ImGui::TextColored(Theme::TextMuted, "Enable to configure the arena-accurate path");
        }
    }
    ImGui::EndChild();
    PanelEnd();
}

static void DrawAutomationPage()
{
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - kColumnGap) * 0.5f;
    const float leftHeight = EstimatePanelHeight(
        (2.0f * kRowMinHeight) +
        (BotMod.AutoForfeit() ? ((2.0f * kRowMinHeight) + kSeparatorGap) : 28.0f)
    );
    const float rightHeight = EstimatePanelHeight(120.0f);
    const float rowH = std::max(leftHeight, rightHeight);

    PanelBegin();
    ImGui::BeginChild("##PanelAuto", ImVec2(colW, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        PanelHeader("Match Automation", Theme::Purple);

        const bool skipHovered = DrawSettingRow("Skip Replays", "Auto-skip goal replays", [&]() {
            ToggleSwitch("##AutoSkipReplay", &BotMod.AutoSkipReplay(), Theme::Purple);
        }, true);
        if (skipHovered)
            ImGui::SetTooltip("Sends jump to skip goal replays.");

        const bool forfeitHovered = DrawSettingRow("Auto Forfeit", "Forfeit when losing by threshold", [&]() {
            ToggleSwitch("##AutoForfeit", &BotMod.AutoForfeit(), Theme::Purple);
        }, !BotMod.AutoForfeit());
        if (forfeitHovered)
            ImGui::SetTooltip("Vote to forfeit when down by the score gap with less than the time left.");

        if (BotMod.AutoForfeit()) {
            DrawBodyRowSpacer();
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderInt("Score gap", "##ForfeitDiff", &BotMod.AutoForfeitScoreDiff(), 1, 10, Theme::Purple);
            DrawBodyRowSpacer();
            ImGui::SetCursorPosX(kPanelPadding);
            AccentSliderInt("Min. time (s)", "##ForfeitTime", &BotMod.AutoForfeitTimeSec(), 0, 300, Theme::Purple);
        }
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, kColumnGap);

    PanelBegin();
    ImGui::BeginChild("##PanelFuture", ImVec2(0, rowH), true);
    {
        ImGui::SetCursorPos(ImVec2(kPanelPadding, kPanelPadding));
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 dashColor = ImGui::ColorConvertFloat4ToU32(ImVec4(Theme::Purple.x, Theme::Purple.y, Theme::Purple.z, 0.18f));
        auto DrawDashedLine = [&](ImVec2 a, ImVec2 b) {
            const float dashLen = 8.0f;
            const float gapLen = 6.0f;
            const ImVec2 delta(b.x - a.x, b.y - a.y);
            const float len = sqrtf(delta.x * delta.x + delta.y * delta.y);
            if (len <= 0.0f) return;
            const ImVec2 dir(delta.x / len, delta.y / len);
            for (float t = 0.0f; t < len; t += dashLen + gapLen) {
                float tEnd = (t + dashLen < len) ? (t + dashLen) : len;
                dl->AddLine(ImVec2(a.x + dir.x * t, a.y + dir.y * t),
                            ImVec2(a.x + dir.x * tEnd, a.y + dir.y * tEnd),
                            dashColor, 1.0f);
            }
        };
        const ImVec2 end(start.x + avail.x, start.y + avail.y);
        DrawDashedLine(start, ImVec2(end.x, start.y));
        DrawDashedLine(ImVec2(end.x, start.y), end);
        DrawDashedLine(end, ImVec2(start.x, end.y));
        DrawDashedLine(ImVec2(start.x, end.y), start);

        const char* plus = "+";
        const char* text = "More automation features coming soon";
        ImVec2 plusSz = ImGui::CalcTextSize(plus);
        ImVec2 textSz = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - plusSz.x) * 0.5f, avail.y * 0.40f));
        ImGui::TextColored(ImVec4(Theme::TextMuted.x, Theme::TextMuted.y, Theme::TextMuted.z, 0.4f), "%s", plus);
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - textSz.x) * 0.5f, avail.y * 0.48f));
        ImGui::TextColored(Theme::TextMuted, "%s", text);
    }
    ImGui::EndChild();
    PanelEnd();
}
void GUIComponent::Render()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ApplyTheme();

    ImGuiIO& IO = ImGui::GetIO();
    GUI.DisplayX = IO.DisplaySize.x;
    GUI.DisplayY = IO.DisplaySize.y;

    // P1/07: OnRender now does its own locked state check; no need to gate here.
    OverlayMod.OnRender();

    BotMod.TickJoinCountdowns();

    IO.MouseDrawCursor = IsOpen;

    // 1–5 keyboard shortcuts mirror the design's nav .kbd hints.
    if (IsOpen) {
        for (int i = 0; i < 5; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false))
                g_SelectedPage = i;
        }
    }

    if (IsOpen) {
        ImGui::SetNextWindowSize(ImVec2(kWindowW, kWindowH), ImGuiCond_Always);
        ImGui::Begin("###AbuseMain", &IsOpen,
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar);

        DrawSidebar();
        ImGui::SameLine(0, 0);

        ImGui::BeginChild("##MainArea", ImVec2(0, -1), false, ImGuiWindowFlags_NoScrollbar);
        {
            DrawStatusBar();

            // Page area — leave room for the bottom bar.
            const float contentH = ImGui::GetContentRegionAvail().y - kBottomBarH;
            ImGui::BeginChild("##Content", ImVec2(-1, contentH), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(kPageGutter, kPanelPadding));

            // Page title is gone — the topbar's crumbs cover that.
            if (g_SelectedPage < 0 || g_SelectedPage > 4) g_SelectedPage = 0;
            switch (g_SelectedPage) {
                case 0: DrawControlsPage(); break;
                case 1: DrawTuningPage(); break;
                case 2: DrawOverlaysPage(); break;
                case 3: DrawAutomationPage(); break;
                case 4: DrawConsolePage(); break;
            }
            ImGui::EndChild();

            DrawBottomBar();
        }
        ImGui::EndChild();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wa = ImGui::GetWindowPos();
        ImVec2 wsz = ImGui::GetWindowSize();
        dl->AddRect(wa, ImVec2(wa.x + wsz.x, wa.y + wsz.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderStrong), 16.0f, 0, 1.0f);

        ImGui::End();
    }
    ImGui::Render();
}
void Initialize() {}
class GUIComponent GUI;



