# GUI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the GoySDK GUI from a 3-tab red-accent layout to a sidebar-navigated dashboard with multi-accent color-coded sections.

**Architecture:** Single-file rewrite of `Components/Components/GUI.cpp`. Replace the Theme namespace, all draw functions, and widget helpers. Preserve everything below the rendering layer: DX11 hooking, WndProc, ConsoleBuffer, font loading, and all BotMod/OverlayMod API calls. `GUI.hpp` is unchanged.

**Tech Stack:** C++, Dear ImGui, Direct3D 11, Kiero hooking

**Spec:** `docs/superpowers/specs/2026-04-11-gui-redesign-design.md`

---

## File Structure

All changes are in a single file:

- **Modify:** `Components/Components/GUI.cpp` (full rewrite of lines 79, 199-898)
- **Unchanged:** `Components/Components/GUI.hpp`

The file is organized in this order after the rewrite:
1. Includes + ConsoleBuffer + TextColorToImVec4 (lines 1-64, unchanged)
2. DX11 hooking globals + hkPresent + MainThread (lines 67-133, unchanged)
3. GUIComponent lifecycle + InitImGui + WndProc (lines 135-196, unchanged)
4. **NEW** Theme namespace (replaces lines 199-223)
5. **NEW** ApplyTheme (replaces lines 266-325)
6. **NEW** Widget helpers: ToggleSwitch, SubtleButton, AccentSliderFloat, AccentSliderInt, Badge (replaces lines 328-459)
7. **NEW** DrawSidebar (replaces DrawTopTabBar + DrawCustomTitleBar)
8. **NEW** DrawStatusBar (new component)
9. **NEW** DrawControlsPage (replaces first half of DrawMainTab)
10. **NEW** DrawTuningPage (replaces second half of DrawMainTab)
11. **NEW** DrawOverlaysPage (replaces first half of DrawMoreTab)
12. **NEW** DrawAutomationPage (replaces second half of DrawMoreTab)
13. **NEW** DrawConsolePage (replaces DrawConsoleTab)
14. **MODIFIED** Render() (replaces lines 848-895)

---

### Task 1: Theme Namespace + ApplyTheme

**Files:**
- Modify: `Components/Components/GUI.cpp:79,199-325`

- [ ] **Step 1: Replace `g_SelectedTab` with `g_SelectedPage`**

At line 79, change:

```cpp
static int g_SelectedTab = 0;
```

to:

```cpp
static int g_SelectedPage = 0;
```

- [ ] **Step 2: Replace the Theme namespace (lines 199-223)**

Delete the old `Theme` namespace and all glass/section helpers (lines 199-264). Replace with:

```cpp
// ─── Dashboard theme: multi-accent, dark base ──
namespace Theme {
    // Background tiers
    static const ImVec4 Base       = ImVec4(0.039f, 0.039f, 0.043f, 1.00f); // #0A0A0B
    static const ImVec4 Surface    = ImVec4(0.067f, 0.067f, 0.075f, 1.00f); // #111113
    static const ImVec4 Elevated   = ImVec4(0.094f, 0.094f, 0.106f, 1.00f); // #18181B
    static const ImVec4 Hover      = ImVec4(0.122f, 0.122f, 0.137f, 1.00f); // #1F1F23

    // Borders
    static const ImVec4 Border       = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    static const ImVec4 BorderSubtle = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Text
    static const ImVec4 Text       = ImVec4(0.878f, 0.878f, 0.894f, 1.00f); // #E0E0E4
    static const ImVec4 TextDim    = ImVec4(0.541f, 0.541f, 0.573f, 1.00f); // #8A8A92
    static const ImVec4 TextMuted  = ImVec4(0.333f, 0.333f, 0.361f, 1.00f); // #55555C

    // Accent colors
    static const ImVec4 Green      = ImVec4(0.133f, 0.773f, 0.369f, 1.00f); // #22C55E
    static const ImVec4 GreenDim   = ImVec4(0.133f, 0.773f, 0.369f, 0.12f);
    static const ImVec4 Amber      = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #F59E0B
    static const ImVec4 AmberDim   = ImVec4(0.961f, 0.620f, 0.043f, 0.12f);
    static const ImVec4 Red        = ImVec4(0.937f, 0.267f, 0.267f, 1.00f); // #EF4444
    static const ImVec4 RedDim     = ImVec4(0.937f, 0.267f, 0.267f, 0.12f);
    static const ImVec4 Blue       = ImVec4(0.231f, 0.510f, 0.965f, 1.00f); // #3B82F6
    static const ImVec4 BlueDim    = ImVec4(0.231f, 0.510f, 0.965f, 0.12f);
    static const ImVec4 Purple     = ImVec4(0.659f, 0.333f, 0.969f, 1.00f); // #A855F7
    static const ImVec4 PurpleDim  = ImVec4(0.659f, 0.333f, 0.969f, 0.12f);
    static const ImVec4 Cyan       = ImVec4(0.024f, 0.714f, 0.831f, 1.00f); // #06B6D4
    static const ImVec4 CyanDim    = ImVec4(0.024f, 0.714f, 0.831f, 0.12f);

    static const ImVec4 ConsoleBg  = ImVec4(0.020f, 0.020f, 0.027f, 1.00f); // #050507
    static const ImVec4 SliderTrack = ImVec4(0.165f, 0.165f, 0.188f, 1.00f); // #2A2A30
}
```

- [ ] **Step 3: Replace ApplyTheme() (lines 266-325)**

Delete the old `ApplyTheme`, `DrawGlassPanelRect`, `PaintGlassChildBg`, `PaintGlassChildBgCustom`, `OriginalSectionTitle`, and `GlassSectionLabel` functions (lines 225-265). Replace `ApplyTheme` with:

```cpp
static void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(0, 0);
    s.WindowRounding    = 12.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildRounding     = 8.0f;
    s.ChildBorderSize   = 0.0f;
    s.FramePadding      = ImVec2(10, 6);
    s.FrameRounding     = 6.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.GrabMinSize       = 12.0f;
    s.GrabRounding      = 6.0f;
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
```

- [ ] **Step 4: Compile to verify no syntax errors**

Run the project build. Expected: compiles (the old draw functions still exist but some reference deleted helpers — that's fine, they'll be replaced in subsequent tasks). If `OriginalSectionTitle` or `PaintGlassChildBg` cause compile errors because they're still referenced by `DrawMainTab` etc., temporarily stub them:

```cpp
static void PaintGlassChildBg() {}
static void PaintGlassChildBgCustom(const ImVec4&, float = 8.0f) {}
static void GlassSectionLabel(const char*) {}
static void OriginalSectionTitle(const char*) {}
```

- [ ] **Step 5: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "refactor(gui): replace theme with dashboard multi-accent palette"
```

---

### Task 2: Widget Helpers

**Files:**
- Modify: `Components/Components/GUI.cpp` (replace lines 328-459)

- [ ] **Step 1: Replace ToggleSwitch with accent-color version**

Delete the old `ToggleSwitch`, `ToggleSwitchTrailingX`, `PrimaryButton`, `SecondaryButton`, `LabelSliderFloat`, `LabelSliderInt`, `RowSliderFloat`, `RowSliderInt` (lines 328-459). Replace with all new widgets:

```cpp
// ─── Widget helpers ──

static constexpr float kToggleW = 36.0f;
static constexpr float kToggleH = 20.0f;

static bool ToggleSwitch(const char* str_id, bool* v, const ImVec4& accent = Theme::Green) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = kToggleH * 0.5f;

    ImGui::InvisibleButton(str_id, ImVec2(kToggleW, kToggleH));
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }

    // Track
    ImVec4 track = *v ? accent : Theme::SliderTrack;
    if (hovered && !*v) track = Theme::Hover;
    dl->AddRectFilled(p, ImVec2(p.x + kToggleW, p.y + kToggleH),
                      ImGui::ColorConvertFloat4ToU32(track), radius);

    // Knob
    const float knobR = radius - 3.0f;
    const float t = *v ? 1.0f : 0.0f;
    const float knobX = p.x + radius + t * (kToggleW - kToggleH);
    ImVec2 center(knobX, p.y + radius);
    dl->AddCircleFilled(ImVec2(center.x + 0.5f, center.y + 0.5f), knobR + 0.5f,
                        IM_COL32(0, 0, 0, 80)); // shadow
    dl->AddCircleFilled(center, knobR, IM_COL32(255, 255, 255, 255));
    return changed;
}

static float ToggleTrailingX() {
    return ImGui::GetContentRegionAvail().x - kToggleW + ImGui::GetCursorPosX();
}

static bool SubtleButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
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

static void Badge(const char* text, const ImVec4& color, const ImVec4& bg) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padX = 6.0f, padY = 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + padX * 2, p.y + ts.y + padY * 2),
                      ImGui::ColorConvertFloat4ToU32(bg), 3.0f);
    dl->AddText(ImVec2(p.x + padX, p.y + padY), ImGui::ColorConvertFloat4ToU32(color), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2, ts.y + padY * 2));
}

static bool AccentSliderFloat(const char* label, const char* id, float* v, float mn, float mx,
                              const char* fmt, const ImVec4& accent) {
    ImGui::TextColored(Theme::TextDim, "%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200 + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(160);
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
                            const ImVec4& accent) {
    ImGui::TextColored(Theme::TextDim, "%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200 + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(160);
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
```

- [ ] **Step 2: Add PanelBegin/PanelEnd helper**

Add below the widget code:

```cpp
static void PanelBegin(const char* title, const ImVec4& accent, const char* badgeText = nullptr,
                       const ImVec4* badgeColor = nullptr, const ImVec4* badgeBg = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Surface);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border);
}

static void PanelHeader(const char* title, const ImVec4& accent, const char* badgeText = nullptr,
                        const ImVec4* badgeColor = nullptr, const ImVec4* badgeBg = nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    // Colored indicator bar (3px wide, 14px tall)
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 3, p.y + 14),
                      ImGui::ColorConvertFloat4ToU32(accent), 2.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
    if (g_FontBold) ImGui::PushFont(g_FontBold);
    ImGui::TextColored(Theme::Text, "%s", title);
    if (g_FontBold) ImGui::PopFont();

    if (badgeText && badgeColor && badgeBg) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40 + ImGui::GetCursorPosX());
        Badge(badgeText, *badgeColor, *badgeBg);
    }

    // Separator line
    ImVec2 sepMin = ImGui::GetCursorScreenPos();
    float sepW = ImGui::GetContentRegionAvail().x;
    dl->AddLine(sepMin, ImVec2(sepMin.x + sepW, sepMin.y),
                ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
    ImGui::Dummy(ImVec2(0, 8));
}

static void PanelEnd() {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
```

- [ ] **Step 3: Compile to verify no syntax errors**

Build the project. The old draw functions still reference deleted helpers via the stubs from Task 1, so it should compile.

- [ ] **Step 4: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "refactor(gui): add dashboard widget helpers (toggle, slider, badge, panel)"
```

---

### Task 3: DrawSidebar + DrawStatusBar

**Files:**
- Modify: `Components/Components/GUI.cpp` (replace DrawTopTabBar + DrawCustomTitleBar + DrawWindowGlassFrame, lines 461-846)

- [ ] **Step 1: Delete old navigation/title functions**

Delete `DrawTopTabBar()` (lines 461-485), `DrawCustomTitleBar()` (lines 812-836), and `DrawWindowGlassFrame()` (lines 838-846). Also delete `GetModelNameForIndex()` — we'll move it above the new functions.

- [ ] **Step 2: Add GetModelNameForIndex + DrawSidebar**

Place after the widget helpers and before any page draw functions:

```cpp
static const char* GetModelNameForIndex(int modelIdx) {
    if (modelIdx < 0 || modelIdx >= static_cast<int>(GoySDK::kModelProfiles.size()))
        return "Unknown";
    return GoySDK::kModelProfiles[modelIdx].name;
}

static void DrawSidebar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Surface);
    ImGui::BeginChild("##Sidebar", ImVec2(180, -1), false, ImGuiWindowFlags_NoScrollbar);

    // Brand
    ImGui::SetCursorPos(ImVec2(16, 16));
    if (g_FontBold) ImGui::PushFont(g_FontBold);
    ImGui::TextColored(Theme::Text, "GoySDK");
    if (g_FontBold) ImGui::PopFont();
    ImGui::SetCursorPosX(16);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextMuted);
    ImGui::SetWindowFontScale(0.72f);
    ImGui::Text("DASHBOARD");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

    // Separator
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sp = ImGui::GetCursorScreenPos();
    dl->AddLine(sp, ImVec2(sp.x + 180, sp.y), ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
    ImGui::Dummy(ImVec2(0, 8));

    // Nav item lambda
    struct NavEntry { const char* label; int pageIdx; ImVec4 dotColor; };
    auto DrawNavItem = [&](const NavEntry& e) {
        const bool sel = (g_SelectedPage == e.pageIdx);
        ImGui::SetCursorPosX(8);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(e.label, ImVec2(164, 26))) g_SelectedPage = e.pageIdx;
        const bool hov = ImGui::IsItemHovered();
        if (sel)
            dl->AddRectFilled(p0, ImVec2(p0.x + 164, p0.y + 26),
                              ImGui::ColorConvertFloat4ToU32(Theme::Elevated), 6.0f);
        else if (hov)
            dl->AddRectFilled(p0, ImVec2(p0.x + 164, p0.y + 26),
                              ImGui::ColorConvertFloat4ToU32(Theme::Hover), 6.0f);

        // Dot
        dl->AddCircleFilled(ImVec2(p0.x + 14, p0.y + 13), 3.0f,
                            ImGui::ColorConvertFloat4ToU32(e.dotColor));
        // Label
        ImVec4 tc = sel ? Theme::Text : (hov ? Theme::Text : Theme::TextDim);
        ImVec2 ts = ImGui::CalcTextSize(e.label);
        dl->AddText(ImVec2(p0.x + 26, p0.y + (26 - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(tc), e.label);
    };

    auto DrawGroupLabel = [&](const char* text) {
        ImGui::SetCursorPos(ImVec2(16, ImGui::GetCursorPosY() + 6));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextMuted);
        ImGui::SetWindowFontScale(0.72f);
        ImGui::Text("%s", text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));
    };

    // Groups
    DrawGroupLabel("CONTROL");
    DrawNavItem({"Controls", 0, Theme::Green});
    DrawNavItem({"Tuning", 1, Theme::Amber});

    DrawGroupLabel("CONFIGURE");
    DrawNavItem({"Overlays", 2, Theme::Blue});
    DrawNavItem({"Automation", 3, Theme::Purple});

    DrawGroupLabel("SYSTEM");
    DrawNavItem({"Console", 4, Theme::Cyan});

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg

    // Right border
    ImVec2 sidebarMax = ImGui::GetItemRectMax();
    ImVec2 sidebarMin = ImGui::GetItemRectMin();
    dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(sidebarMax.x, sidebarMin.y), ImVec2(sidebarMax.x, sidebarMax.y),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
}
```

- [ ] **Step 3: Add DrawStatusBar**

```cpp
static void DrawStatusBar() {
    auto& cfg = BotMod.GetConfig();
    const bool active = BotMod.IsActive();
    const int currentModel = BotMod.GetCurrentModelIdx();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Surface);
    ImGui::BeginChild("##StatusBar", ImVec2(-1, 36), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();

    // Bottom border
    dl->AddLine(ImVec2(wpos.x, wpos.y + 35), ImVec2(wpos.x + wsz.x, wpos.y + 35),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);

    float cx = 12.0f;
    const float cy = 18.0f; // vertical center

    // 1) Bot state dot + text
    ImVec4 dotCol = active ? Theme::Green : Theme::Red;
    dl->AddCircleFilled(ImVec2(wpos.x + cx, wpos.y + cy), 3.5f,
                        ImGui::ColorConvertFloat4ToU32(dotCol));
    if (active)
        dl->AddCircle(ImVec2(wpos.x + cx, wpos.y + cy), 5.5f,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(dotCol.x, dotCol.y, dotCol.z, 0.3f)), 0, 1.5f);
    cx += 10;
    const char* stateText = active ? "ACTIVE" : "INACTIVE";
    ImVec4 stateColor = active ? Theme::Green : Theme::Text;
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6), ImGui::ColorConvertFloat4ToU32(stateColor), stateText);
    cx += ImGui::CalcTextSize(stateText).x + 12;

    // Separator
    dl->AddLine(ImVec2(wpos.x + cx, wpos.y + 8), ImVec2(wpos.x + cx, wpos.y + 28),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
    cx += 12;

    // 2) Model
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), "Model");
    cx += ImGui::CalcTextSize("Model").x + 6;
    const char* modelName = GetModelNameForIndex(currentModel);
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::Text), modelName);
    cx += ImGui::CalcTextSize(modelName).x + 6;

    // Mode badge
    if (currentModel >= 0 && currentModel < (int)GoySDK::kModelProfiles.size()) {
        const char* mode = GoySDK::kModelProfiles[currentModel].supportedModes;
        ImVec2 bts = ImGui::CalcTextSize(mode);
        float bx = wpos.x + cx, by = wpos.y + cy - 7;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bts.x + 10, by + bts.y + 3),
                          ImGui::ColorConvertFloat4ToU32(Theme::AmberDim), 3.0f);
        dl->AddText(ImVec2(bx + 5, by + 1), ImGui::ColorConvertFloat4ToU32(Theme::Amber), mode);
        cx += bts.x + 18;
    }

    // Separator
    dl->AddLine(ImVec2(wpos.x + cx, wpos.y + 8), ImVec2(wpos.x + cx, wpos.y + 28),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
    cx += 12;

    // 3) Tick
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), "Tick");
    cx += ImGui::CalcTextSize("Tick").x + 6;
    char tickBuf[8]; snprintf(tickBuf, sizeof(tickBuf), "%d", cfg.tickSkip);
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::Text), tickBuf);
    cx += ImGui::CalcTextSize(tickBuf).x + 12;

    // Separator
    dl->AddLine(ImVec2(wpos.x + cx, wpos.y + 8), ImVec2(wpos.x + cx, wpos.y + 28),
                ImGui::ColorConvertFloat4ToU32(Theme::Border), 1.0f);
    cx += 12;

    // 4) Device
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), "Device");
    cx += ImGui::CalcTextSize("Device").x + 6;
    const char* devName = (cfg.inferenceBackend == GoySDK::InferenceBackend::Cuda0) ? "CUDA" : "CPU";
    dl->AddText(ImVec2(wpos.x + cx, wpos.y + cy - 6),
                ImGui::ColorConvertFloat4ToU32(Theme::Text), devName);

    // 5) Bot toggle pill (right-aligned)
    const char* toggleLabel = active ? "BOT ON" : "BOT OFF";
    ImVec2 tls = ImGui::CalcTextSize(toggleLabel);
    float dotR = 2.5f;
    float pillW = tls.x + 22 + dotR * 2;
    float pillX = wpos.x + wsz.x - pillW - 12;
    float pillY = wpos.y + 8;
    float pillH = 20;
    ImVec4 pillBg = active ? Theme::GreenDim : Theme::RedDim;
    ImVec4 pillFg = active ? Theme::Green : Theme::Red;
    dl->AddRectFilled(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                      ImGui::ColorConvertFloat4ToU32(pillBg), 4.0f);
    dl->AddRect(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                ImGui::ColorConvertFloat4ToU32(ImVec4(pillFg.x, pillFg.y, pillFg.z, 0.2f)), 4.0f, 0, 1.0f);
    dl->AddCircleFilled(ImVec2(pillX + 8 + dotR, pillY + pillH * 0.5f), dotR,
                        ImGui::ColorConvertFloat4ToU32(pillFg));
    dl->AddText(ImVec2(pillX + 8 + dotR * 2 + 6, pillY + (pillH - tls.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(pillFg), toggleLabel);

    // Make pill clickable
    ImGui::SetCursorScreenPos(ImVec2(pillX, pillY));
    if (ImGui::InvisibleButton("##BotPill", ImVec2(pillW, pillH))) {
        Main.Execute([]() { BotMod.ToggleBot(); });
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}
```

- [ ] **Step 4: Compile**

Build the project to verify no syntax errors. Old page draw functions are still present but will be replaced next.

- [ ] **Step 5: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "feat(gui): add sidebar navigation and persistent status bar"
```

---

### Task 4: Controls Page + Tuning Page

**Files:**
- Modify: `Components/Components/GUI.cpp` (replace DrawMainTab, lines 495-706)

- [ ] **Step 1: Delete DrawMainTab and replace with DrawControlsPage**

Delete the entire `DrawMainTab()` function. Replace with:

```cpp
static void DrawControlsPage() {
    auto& cfg = BotMod.GetConfig();
    int currentModel = BotMod.GetCurrentModelIdx();
    const int profileCount = static_cast<int>(GoySDK::kModelProfiles.size());
    int numSlots = BotMod.GetLocalPlayerCount();
    if (numSlots < 1) numSlots = 1;

    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - 12) * 0.5f;

    // Left panel: Bot Controls
    PanelBegin("BotCtrl", Theme::Green);
    ImGui::BeginChild("##PanelBotCtrl", ImVec2(colW, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Bot Controls", Theme::Green);

        ImGui::SetCursorPosX(14);

        // Model dropdown
        ImGui::TextColored(Theme::TextDim, "Model");
        ImGui::SameLine();
        ImGui::TextColored(Theme::TextMuted, "(Slot 0)");
        const bool loading = BotMod.IsModelLoading();
        if (loading) ImGui::BeginDisabled();
        ImGui::SetCursorPosX(14);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 14);
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
        ImGui::Spacing();

        // Separator
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
        ImGui::Dummy(ImVec2(0, 6));

        // CUDA inference
        ImGui::SetCursorPosX(14);
        const bool cudaAvail = BotMod.IsCudaInferenceAvailable();
        bool useCuda = cfg.inferenceBackend == GoySDK::InferenceBackend::Cuda0;
        ImGui::TextColored(Theme::TextDim, "CUDA Inference");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "GPU acceleration");
        ImGui::SameLine(ToggleTrailingX());
        ImGui::BeginDisabled(!cudaAvail);
        if (ToggleSwitch("##CudaToggle", &useCuda, Theme::Green)) {
            const auto backend = useCuda ? GoySDK::InferenceBackend::Cuda0 : GoySDK::InferenceBackend::CPU;
            Main.Execute([backend]() { GoySDK::BotModule::SetInferenceBackend(backend); });
        }
        ImGui::EndDisabled();
        ImGui::Spacing();

        // Separator
        sp = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
        ImGui::Dummy(ImVec2(0, 6));

        // ViGEm controller
        ImGui::SetCursorPosX(14);
        const bool vigemAvail = BotMod.IsViGEmInputAvailable();
        bool useViGem = vigemAvail && (BotMod.GetInputMethod() == GoySDK::InputMethod::ViGem);
        ImGui::TextColored(Theme::TextDim, "ViGEm Controller");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Virtual Xbox 360");
        ImGui::SameLine(ToggleTrailingX());
        ImGui::BeginDisabled(!vigemAvail);
        if (ToggleSwitch("##ViGEmToggle", &useViGem, Theme::Green)) {
            const auto method = useViGem ? GoySDK::InputMethod::ViGem : GoySDK::InputMethod::Internal;
            Main.Execute([method]() { BotMod.SetInputMethod(method); });
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, 12);

    // Right panel: Splitscreen
    PanelBegin("Split", Theme::Green);
    char slotBadge[16]; snprintf(slotBadge, sizeof(slotBadge), "%d slot%s", numSlots, numSlots > 1 ? "s" : "");
    ImGui::BeginChild("##PanelSplit", ImVec2(0, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Splitscreen", Theme::Green, slotBadge, &Theme::Green, &Theme::GreenDim);

        // Slot cards
        for (int s = 0; s < numSlots; s++) {
            ImGui::SetCursorPosX(14);
            auto& slot = BotMod.GetSlot(s);
            const int modelIdx = (s == 0) ? currentModel : slot.modelIdx;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Elevated);
            char childId[32]; snprintf(childId, sizeof(childId), "##Slot%d", s);
            ImGui::BeginChild(childId, ImVec2(ImGui::GetContentRegionAvail().x - 14, 42), true,
                              ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::SetCursorPos(ImVec2(8, 10));

                // Slot index box
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 bp = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(bp, ImVec2(bp.x + 22, bp.y + 22),
                                  ImGui::ColorConvertFloat4ToU32(Theme::Hover), 4.0f);
                char idxBuf[4]; snprintf(idxBuf, sizeof(idxBuf), "%d", s);
                ImVec2 idxSz = ImGui::CalcTextSize(idxBuf);
                dl->AddText(ImVec2(bp.x + (22 - idxSz.x) * 0.5f, bp.y + (22 - idxSz.y) * 0.5f),
                            ImGui::ColorConvertFloat4ToU32(Theme::TextMuted), idxBuf);
                ImGui::SetCursorPos(ImVec2(38, 6));

                // Model name + info
                const char* name = (modelIdx >= 0) ? GetModelNameForIndex(modelIdx) : "Human";
                ImGui::TextColored(Theme::Text, "%s", name);
                ImGui::SetCursorPosX(38);
                if (modelIdx >= 0 && modelIdx < profileCount) {
                    const auto& prof = GoySDK::kModelProfiles[modelIdx];
                    ImGui::TextColored(Theme::TextMuted, "%s  %d obs", prof.supportedModes, cfg.GetExpectedObsCount());
                }

                // Badge on the right
                if (s == 0) {
                    float badgeW = ImGui::CalcTextSize("Primary").x + 12;
                    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - badgeW - 10, 12));
                    Badge("Primary", Theme::Amber, Theme::AmberDim);
                }

                // Per-slot model combo for splitscreen
                if (numSlots >= 2) {
                    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 120, 10));
                    ImGui::SetNextItemWidth(110);
                    const char* preview = (slot.modelIdx >= 0) ? GetModelNameForIndex(slot.modelIdx) : "Human";
                    ImGui::PushID(s);
                    if (ImGui::BeginCombo("##SlotModel", preview)) {
                        if (ImGui::Selectable("Human", slot.modelIdx < 0)) {
                            const int si = s;
                            Main.Execute([si]() { BotMod.AssignModel(si, -1); });
                        }
                        for (int mi = 0; mi < profileCount; ++mi) {
                            const auto& p = GoySDK::kModelProfiles[mi];
                            std::string label = std::string(p.name) + " [" + p.supportedModes + "]";
                            if (ImGui::Selectable(label.c_str(), slot.modelIdx == mi)) {
                                const int si = s, sm = mi;
                                Main.Execute([si, sm]() { BotMod.AssignModel(si, sm); });
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        // Add/Remove buttons
        ImGui::SetCursorPosX(14);
        const bool inGame = BotMod.IsInGame();
        const bool canAdd = (numSlots < BotMod.kMaxSlots) && !inGame;
        if (!canAdd) ImGui::BeginDisabled();
        if (SubtleButton("+ Add Slot", ImVec2(ImGui::GetContentRegionAvail().x - 14, 0))) {
            Main.Execute([]() { BotMod.AddSplitscreen(); });
        }
        if (!canAdd) ImGui::EndDisabled();

        const bool canRemove = (numSlots > 1) && !inGame;
        if (canRemove) {
            ImGui::SetCursorPosX(14);
            if (SubtleButton("Remove Slot", ImVec2(ImGui::GetContentRegionAvail().x - 14, 0))) {
                Main.Execute([]() { BotMod.RemoveSplitscreen(); });
            }
        }
    }
    ImGui::EndChild();
    PanelEnd();
}
```

- [ ] **Step 2: Add DrawTuningPage**

```cpp
static void DrawTuningPage() {
    auto& cfg = BotMod.GetConfig();
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - 12) * 0.5f;

    // Left panel: Humanizer
    PanelBegin("Hum", Theme::Amber);
    ImGui::BeginChild("##PanelHum", ImVec2(colW, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));

        // Header with toggle on the right
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + 3, p.y + 14),
                          ImGui::ColorConvertFloat4ToU32(Theme::Amber), 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
        if (g_FontBold) ImGui::PushFont(g_FontBold);
        ImGui::TextColored(Theme::Text, "Humanizer");
        if (g_FontBold) ImGui::PopFont();
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##HumToggle", &cfg.humanize, Theme::Amber);

        // Separator
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine(sp, ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
        ImGui::Dummy(ImVec2(0, 8));

        if (cfg.humanize) {
            ImGui::SetCursorPosX(14);
            AccentSliderFloat("Noise", "##Noise", &cfg.jitterAmount, 0.0f, 0.02f, "%.3f", Theme::Amber);
            ImGui::Spacing();
            ImGui::SetCursorPosX(14);
            AccentSliderFloat("Smoothing", "##Smooth", &cfg.smoothFactor, 0.0f, 0.95f, "%.2f", Theme::Amber);
            ImGui::Spacing();
            ImGui::SetCursorPosX(14);
            AccentSliderFloat("Deadzone", "##Dead", &cfg.deadzone, 0.0f, 0.1f, "%.3f", Theme::Amber);
        } else {
            ImGui::SetCursorPosX(14);
            ImGui::TextColored(Theme::TextMuted, "Enable to configure parameters");
        }
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, 12);

    // Right panel: Performance
    PanelBegin("Perf", Theme::Amber);
    ImGui::BeginChild("##PanelPerf", ImVec2(0, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Performance", Theme::Amber);

        ImGui::SetCursorPosX(14);
        int ts = cfg.tickSkip;
        AccentSliderInt("Tick Rate", "##TickSkip", &ts, 1, 16, Theme::Amber);
        cfg.tickSkip = ts;
        ImGui::Spacing();

        ImGui::SetCursorPosX(14);
        int kts = cfg.kickoffTickSkip;
        AccentSliderInt("Kickoff Tick Rate", "##KickTS", &kts, 1, 16, Theme::Amber);
        cfg.kickoffTickSkip = kts;
    }
    ImGui::EndChild();
    PanelEnd();
}
```

- [ ] **Step 3: Compile**

Build the project. `DrawControlsPage` and `DrawTuningPage` should compile — they use the same BotMod/OverlayMod APIs as the old code.

- [ ] **Step 4: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "feat(gui): add Controls and Tuning pages with new layout"
```

---

### Task 5: Overlays Page + Automation Page

**Files:**
- Modify: `Components/Components/GUI.cpp` (replace DrawMoreTab, lines 750-809)

- [ ] **Step 1: Delete DrawMoreTab and replace with DrawOverlaysPage**

Delete the entire `DrawMoreTab()` function. Replace with:

```cpp
static void DrawOverlaysPage() {
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - 12) * 0.5f;

    // Left panel: Boost Indicators
    PanelBegin("Boost", Theme::Blue);
    ImGui::BeginChild("##PanelBoost", ImVec2(colW, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Boost Indicators", Theme::Blue);

        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextDim, "My Boost");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Show your boost amount");
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##ShowMyBoost", &OverlayMod.showMyBoost, Theme::Blue);

        ImGui::Spacing();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine(sp, ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextDim, "Enemy Boost");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Show opponent boost");
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##ShowEnemyBoost", &OverlayMod.showEnemyBoost, Theme::Blue);
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, 12);

    // Right panel: Ball Tracking
    PanelBegin("Ball", Theme::Blue);
    ImGui::BeginChild("##PanelBall", ImVec2(0, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Ball Tracking", Theme::Blue);

        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextDim, "Ball Center");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Mark ball center point");
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##DrawBallCenter", &OverlayMod.drawBallCenter, Theme::Blue);
    }
    ImGui::EndChild();
    PanelEnd();
}
```

- [ ] **Step 2: Add DrawAutomationPage**

```cpp
static void DrawAutomationPage() {
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float colW = (contentW - 12) * 0.5f;

    // Left panel: Match Automation
    PanelBegin("Auto", Theme::Purple);
    ImGui::BeginChild("##PanelAuto", ImVec2(colW, -1), true);
    {
        ImGui::SetCursorPos(ImVec2(14, 14));
        PanelHeader("Match Automation", Theme::Purple);

        ImGui::SetCursorPosX(14);
        ImGui::BeginGroup();
        ImGui::TextColored(Theme::TextDim, "Skip Replays");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Auto-skip goal replays");
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##AutoSkipReplay", &BotMod.AutoSkipReplay(), Theme::Purple);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sends jump to skip goal replays.");

        ImGui::Spacing();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine(sp, ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::BorderSubtle), 1.0f);
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::SetCursorPosX(14);
        ImGui::BeginGroup();
        ImGui::TextColored(Theme::TextDim, "Auto Forfeit");
        ImGui::SetCursorPosX(14);
        ImGui::TextColored(Theme::TextMuted, "Forfeit when losing by threshold");
        ImGui::SameLine(ToggleTrailingX());
        ToggleSwitch("##AutoForfeit", &BotMod.AutoForfeit(), Theme::Purple);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vote to forfeit when down by the score gap with less than the time left.");

        if (BotMod.AutoForfeit()) {
            ImGui::Spacing();
            ImGui::SetCursorPosX(14);
            AccentSliderInt("Score gap", "##ForfeitDiff", &BotMod.AutoForfeitScoreDiff(), 1, 10, Theme::Purple);
            ImGui::Spacing();
            ImGui::SetCursorPosX(14);
            AccentSliderInt("Min. time (s)", "##ForfeitTime", &BotMod.AutoForfeitTimeSec(), 0, 300, Theme::Purple);
        }
    }
    ImGui::EndChild();
    PanelEnd();

    ImGui::SameLine(0, 12);

    // Right panel: placeholder for future features
    PanelBegin("Future", Theme::Purple);
    ImGui::BeginChild("##PanelFuture", ImVec2(0, -1), true);
    {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(Theme::Purple.x, Theme::Purple.y, Theme::Purple.z, 0.15f));
        float h = ImGui::GetContentRegionAvail().y;
        ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 10, h * 0.4f));
        ImGui::TextColored(ImVec4(Theme::TextMuted.x, Theme::TextMuted.y, Theme::TextMuted.z, 0.4f), "+");
        float tw = ImGui::CalcTextSize("More automation coming soon").x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f + 7);
        ImGui::TextColored(Theme::TextMuted, "More automation coming soon");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    PanelEnd();
}
```

- [ ] **Step 3: Compile**

Build the project.

- [ ] **Step 4: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "feat(gui): add Overlays and Automation pages"
```

---

### Task 6: Console Page

**Files:**
- Modify: `Components/Components/GUI.cpp` (replace DrawConsoleTab, lines 708-748)

- [ ] **Step 1: Delete DrawConsoleTab and replace with DrawConsolePage**

```cpp
static void DrawConsolePage() {
    // Toolbar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Elevated);
    ImGui::BeginChild("##ConsoleToolbar", ImVec2(-1, 34), false, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::SetCursorPos(ImVec2(10, 6));
        if (SubtleButton("Clear", ImVec2(56, 22)))
            g_ConsoleBuffer.Clear();

        ImGui::SameLine(0, 10);

        // Mini scroll toggle
        ImGui::SetCursorPosY(8);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 tp = ImGui::GetCursorScreenPos();
        const float miniW = 28, miniH = 14, miniR = 7;
        ImVec4 miniCol = g_ConsoleBuffer.autoScroll ? Theme::Cyan : Theme::SliderTrack;
        if (ImGui::InvisibleButton("##MiniScroll", ImVec2(miniW, miniH)))
            g_ConsoleBuffer.autoScroll = !g_ConsoleBuffer.autoScroll;
        dl->AddRectFilled(tp, ImVec2(tp.x + miniW, tp.y + miniH),
                          ImGui::ColorConvertFloat4ToU32(miniCol), miniR);
        float knobX = g_ConsoleBuffer.autoScroll ? tp.x + miniW - miniR : tp.x + miniR;
        dl->AddCircleFilled(ImVec2(knobX, tp.y + miniR), 5.0f, IM_COL32(255, 255, 255, 255));

        ImGui::SameLine(0, 6);
        ImGui::SetCursorPosY(10);
        ImGui::TextColored(Theme::TextDim, "Scroll");

        // Line count (right-aligned)
        {
            std::lock_guard<std::mutex> lock(g_ConsoleBuffer.mtx);
            char lineBuf[32];
            snprintf(lineBuf, sizeof(lineBuf), "%zu lines", g_ConsoleBuffer.entries.size());
            float tw = ImGui::CalcTextSize(lineBuf).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - tw - 14);
            ImGui::SetCursorPosY(10);
            ImGui::TextColored(Theme::TextMuted, "%s", lineBuf);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Console body
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ConsoleBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("##ConsoleBody", ImVec2(-1, -1), true, ImGuiWindowFlags_None);
    {
        if (g_FontMono) ImGui::PushFont(g_FontMono);

        std::lock_guard<std::mutex> lock(g_ConsoleBuffer.mtx);
        ImGuiListClipper clipper;
        clipper.Begin((int)g_ConsoleBuffer.entries.size());
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
```

- [ ] **Step 2: Compile**

Build the project.

- [ ] **Step 3: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "feat(gui): add Console page with redesigned toolbar"
```

---

### Task 7: Update Render() and Clean Up

**Files:**
- Modify: `Components/Components/GUI.cpp` (lines 848-898)

- [ ] **Step 1: Replace the Render function**

Delete the old `Render()` function (lines 848-895). Replace with:

```cpp
void GUIComponent::Render()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ApplyTheme();

    ImGuiIO& IO = ImGui::GetIO();
    GUI.DisplayX = IO.DisplaySize.x;
    GUI.DisplayY = IO.DisplaySize.y;

    if (OverlayMod.IsInGame) OverlayMod.OnRender();

    BotMod.TickJoinCountdowns();

    IO.MouseDrawCursor = IsOpen;

    if (IsOpen) {
        ImGui::SetNextWindowSize(ImVec2(928, 592), ImGuiCond_Always);
        ImGui::Begin("###AbuseMain", &IsOpen,
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar);

        // Sidebar
        DrawSidebar();
        ImGui::SameLine(0, 0);

        // Main content area
        ImGui::BeginChild("##MainArea", ImVec2(0, -1), false, ImGuiWindowFlags_NoScrollbar);
        {
            DrawStatusBar();

            // Content
            ImGui::BeginChild("##Content", ImVec2(-1, -1), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos(ImVec2(16, 12));

            // Page header
            const char* pageTitles[] = {"Controls", "Tuning", "Overlays", "Automation", "Console"};
            const char* pageSubs[] = {
                "Bot configuration and splitscreen management",
                "Humanizer and performance parameters",
                "In-game visual indicators",
                "Game flow automation settings",
                "System log output"
            };
            if (g_SelectedPage >= 0 && g_SelectedPage <= 4) {
                if (g_FontBold) ImGui::PushFont(g_FontBold);
                ImGui::TextColored(Theme::Text, "%s", pageTitles[g_SelectedPage]);
                if (g_FontBold) ImGui::PopFont();
                ImGui::SetCursorPosX(16);
                ImGui::TextColored(Theme::TextMuted, "%s", pageSubs[g_SelectedPage]);
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::SetCursorPosX(16);
            }

            // Page content
            if (g_SelectedPage < 0 || g_SelectedPage > 4) g_SelectedPage = 0;
            switch (g_SelectedPage) {
                case 0: DrawControlsPage(); break;
                case 1: DrawTuningPage(); break;
                case 2: DrawOverlaysPage(); break;
                case 3: DrawAutomationPage(); break;
                case 4: DrawConsolePage(); break;
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // Window border
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wa = ImGui::GetWindowPos();
        ImVec2 wsz = ImGui::GetWindowSize();
        dl->AddRect(wa, ImVec2(wa.x + wsz.x, wa.y + wsz.y),
                    ImGui::ColorConvertFloat4ToU32(Theme::Border), 12.0f, 0, 1.0f);

        ImGui::End();
    }
    ImGui::Render();
}
```

- [ ] **Step 2: Delete all remaining dead code**

Remove any leftover stubs or old functions that are no longer called:
- `OriginalSectionTitle` stub
- `GlassSectionLabel` stub
- `PaintGlassChildBg` stub
- `PaintGlassChildBgCustom` stub
- `DrawGlassPanelRect`
- `PrimaryButton`
- `SecondaryButton`
- `LabelSliderFloat` / `LabelSliderInt`
- `RowSliderFloat` / `RowSliderInt`
- `DrawTopTabBar`
- `DrawCustomTitleBar`
- `DrawWindowGlassFrame`
- `DrawMainTab`
- `DrawMoreTab`
- `DrawConsoleTab`
- Old `kToggleWidth` / `kToggleHeight` constants
- Old `kGlassRoundingChild` / `kGlassRoundingWin` constants

Also remove the `ToggleSwitchTrailingX` function (replaced by `ToggleTrailingX`).

- [ ] **Step 3: Verify no references to old g_SelectedTab**

Search for `g_SelectedTab` — should have zero references. All replaced by `g_SelectedPage`.

- [ ] **Step 4: Full build**

Build the entire project. Fix any compile errors.

- [ ] **Step 5: Commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "feat(gui): complete dashboard redesign with sidebar, status bar, multi-accent pages"
```

---

### Task 8: Visual Verification

**Files:**
- No file changes — runtime testing only

- [ ] **Step 1: Launch and verify Controls page**

Run the application. Open the GUI (press the toggle key). Verify:
- Sidebar appears on the left with "GoySDK" brand and nav groups
- Controls page is selected by default (green dot highlighted)
- Status bar shows bot state, model, tick, device
- Bot Controls panel has model dropdown, CUDA toggle, ViGEm toggle
- Splitscreen panel shows slot 0 with model info

- [ ] **Step 2: Test all sidebar navigation**

Click each sidebar item and verify:
- Tuning: Humanizer toggle + sliders, Performance sliders (amber accents)
- Overlays: Boost toggles, Ball center toggle (blue accents)
- Automation: Skip replays, Auto forfeit toggles (purple accents)
- Console: Log entries with clear button, scroll toggle, line count

- [ ] **Step 3: Test status bar interactivity**

Click the BOT ON/OFF pill in the status bar. Verify:
- Toggles bot state
- Dot changes from red to green
- Text changes from INACTIVE to ACTIVE
- Pill changes from red to green styling

- [ ] **Step 4: Test all controls function correctly**

- Toggle CUDA inference, ViGEm controller
- Change model via dropdown
- Toggle humanizer and adjust sliders
- Adjust tick rate sliders
- Toggle overlay settings
- Toggle automation settings, verify conditional sliders appear for auto forfeit
- Clear console, toggle auto-scroll

- [ ] **Step 5: Fix any visual issues**

Adjust spacing, alignment, colors as needed based on what you see at runtime. Common issues:
- Panel heights may need explicit sizing if content doesn't fill correctly
- Slider widths may need tuning based on actual label lengths
- Badge positioning may need adjustment

- [ ] **Step 6: Final commit**

```bash
git add Components/Components/GUI.cpp
git commit -m "fix(gui): visual polish and spacing adjustments"
```
