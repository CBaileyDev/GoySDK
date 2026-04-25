# GoySDK GUI Redesign: Dashboard/Pro with Multi-Accent

**Date:** 2026-04-11
**Status:** Approved (mockup review)
**File:** `Components/Components/GUI.cpp` (898 lines, full rewrite of rendering logic)

## Overview

Full redesign of the GoySDK ImGui overlay from a 3-tab layout with red accent to a sidebar-navigated dashboard with color-coded sections. The window size (928x592), DX11 hooking, font loading, input handling, and console buffer remain unchanged. Only the theme, layout, and rendering functions change.

## Architecture

### Layout Structure

```
+--[ Sidebar 180px ]--+--[ Main Content Area ]------------------+
|  GoySDK             |  [ Status Bar - 36px tall ]              |
|  Dashboard          |  Bot state | Model | Tick | Device | Toggle |
|                     |---------------------------------------------|
|  CONTROL            |  [ Content Area - scrollable ]              |
|    > Controls       |                                             |
|    > Tuning         |    +-- Panel --+    +-- Panel --+           |
|                     |    |           |    |           |           |
|  CONFIGURE          |    +-----------+    +-----------+           |
|    > Overlays       |                                             |
|    > Automation     |                                             |
|                     |                                             |
|  SYSTEM             |                                             |
|    > Console        |                                             |
+---------------------+---------------------------------------------+
```

### Navigation Model

Replace `g_SelectedTab` (int 0-2) with `g_SelectedPage` (int 0-4):

| Index | Page       | Sidebar Group | Accent Color |
|-------|------------|---------------|--------------|
| 0     | Controls   | Control       | Green `#22C55E` |
| 1     | Tuning     | Control       | Amber `#F59E0B` |
| 2     | Overlays   | Configure     | Blue `#3B82F6` |
| 3     | Automation | Configure     | Purple `#A855F7` |
| 4     | Console    | System        | Cyan `#06B6D4` |

Red (`#EF4444`) is reserved for errors/alerts only — no longer used as the primary accent.

## Theme

### Color Palette

Replace the existing `Theme` namespace entirely:

```
Background tiers:
  Base:      #0A0A0B  (body background)
  Surface:   #111113  (sidebar, status bar, panels)
  Elevated:  #18181B  (dropdown, slot cards, console toolbar)
  Hover:     #1F1F23  (hover states)

Borders:
  Default:   rgba(255,255,255, 0.06)
  Subtle:    rgba(255,255,255, 0.03)

Text:
  Primary:   #E0E0E4
  Secondary: #8A8A92
  Muted:     #55555C

Accent colors (each with a -dim variant at 12% opacity for badges/backgrounds):
  Green:     #22C55E
  Amber:     #F59E0B
  Red:       #EF4444
  Blue:      #3B82F6
  Purple:    #A855F7
  Cyan:      #06B6D4
```

### Style Properties

```
WindowPadding:    0, 0  (sidebar handles its own padding)
WindowRounding:   12
ChildRounding:    8
FramePadding:     10, 6
FrameRounding:    6
ItemSpacing:      8, 6
GrabRounding:     6
ScrollbarSize:    6
ScrollbarRounding: 3
```

### Fonts

No changes. Keep the same 4 fonts:
- `g_FontMain` (Segoe UI 15pt) — body text
- `g_FontBold` (Segoe UI Bold 15pt) — panel headers, sidebar brand
- `g_FontTitle` (Segoe UI Bold 20pt) — unused after redesign (no more large title bar)
- `g_FontMono` (Consolas 13pt) — console output

Note: `g_FontTitle` can be repurposed for sidebar brand "GoySDK" at 16pt, or removed in a future cleanup. For now, use `g_FontBold` for the sidebar brand.

## Components

### 1. Sidebar (`DrawSidebar()`)

**Width:** 180px fixed, full window height
**Background:** `Surface` color
**Right border:** 1px `Border` color

Sections:
- **Brand block** (top): "GoySDK" in `g_FontBold`, "Dashboard" subtitle in `TextMuted` 10px uppercase. Bottom border separator.
- **Nav groups**: Each has an uppercase 10px label ("CONTROL", "CONFIGURE", "SYSTEM") followed by nav items.
- **Nav items**: 7px vertical padding, 10px horizontal. Left-aligned with a 6px colored dot, then label text. Active item gets `Elevated` background + `TextPrimary` color. Inactive items are `TextSecondary` with hover -> `BgHover`.

### 2. Status Bar (`DrawStatusBar()`)

**Height:** 36px, spans full width of main content area
**Background:** `Surface` color
**Bottom border:** 1px `Border` color

Items (left to right, separated by 1px vertical borders):
1. **Bot state**: 7px dot (green glow if active, red glow if inactive) + "ACTIVE"/"INACTIVE" text
2. **Model**: "Model" label + model name + badge (e.g., "3v3" in amber)
3. **Tick**: "Tick" label + current tick rate value
4. **Device**: "Device" label + "CPU" or "CUDA"
5. **Spacer** (flex)
6. **Bot toggle**: Pill button — "BOT ON" (green-dim bg, green text) or "BOT OFF" (red-dim bg, red text). Clickable, calls `BotMod.ToggleBot()`.

### 3. Panels (`DrawPanel()`)

Each content page uses a 2-column grid of panels.

**Panel structure:**
- Background: `Surface`
- Border: 1px `Border`, 8px radius
- Padding: 14px
- Header: 3px tall colored indicator bar (left edge) + bold title + optional right-side badge/toggle
- Content: `control-row` items with label + control, separated by subtle borders

### 4. Custom Widgets

#### Toggle Switch (updated)
- **Dimensions:** 36x20px (smaller than current 52x24)
- **Colors:** Track uses section-appropriate accent when ON (green for Controls, amber for Tuning, blue for Overlays, purple for Automation). OFF state uses `#2A2A30`.
- **Knob:** 14px diameter, white, 3px shadow

The toggle function signature changes to accept an accent color:
```cpp
static bool ToggleSwitch(const char* id, bool* v, const ImVec4& accentColor = Theme::Green);
```

#### Slider (updated)
- **Track:** 4px tall, `#2A2A30` background
- **Fill:** Section accent color
- **Thumb:** 12px diameter, section accent color, 2px white border
- **Value display:** Right-aligned, section accent color, tabular numerals

#### Dropdown (unchanged logic)
- Background: `Elevated`
- Border: 1px `Border`, 6px radius
- Arrow: `TextMuted` colored dropdown indicator

#### Primary Button -> Subtle Button
- No more full-red primary buttons. "Add Slot" uses `Elevated` bg with `Border`, `TextSecondary` text. Hover brightens.

#### Badges
- Small pills (10px font, 2px/6px padding, 3px radius)
- Color-coded: green/amber/red/blue with dim background variants

### 5. Page Content

#### Controls Page (index 0, green)
Left panel: **Bot Controls**
- Model dropdown (Slot 0)
- CUDA inference toggle
- ViGEm controller toggle

Right panel: **Splitscreen**
- Slot cards showing index, model name, mode, obs count
- "Primary" badge on slot 0
- "+ Add Slot" button at bottom
- "Remove" button (when >1 slot)

When splitscreen has 2+ players, the left panel shows per-slot model combos (same logic as current `DrawMainTab` splitscreen section).

#### Tuning Page (index 1, amber)
Left panel: **Humanizer**
- Enable toggle in panel header (right side)
- Noise / Smoothing / Deadzone sliders (only visible when enabled)

Right panel: **Performance**
- Tick Rate slider
- Kickoff Tick Rate slider

#### Overlays Page (index 2, blue)
Left panel: **Boost Indicators**
- My Boost toggle
- Enemy Boost toggle

Right panel: **Ball Tracking**
- Ball Center toggle

#### Automation Page (index 3, purple)
Left panel: **Match Automation**
- Skip Replays toggle (with tooltip)
- Auto Forfeit toggle (with tooltip)
- Score gap slider + Min time slider (conditional, shown when auto forfeit is on)

Right panel: Empty/placeholder for future features (dashed border, "+" icon, muted text)

#### Console Page (index 4, cyan)
- Toolbar: Clear button, scroll toggle (mini 28x14 toggle), spacer, line count
- Body: Same `ConsoleBuffer` rendering with `ImGuiListClipper`, monospace font, colored entries. Background uses very dark `#050507`.

## What Changes vs. What Stays

### Changes (rendering only)
- `Theme` namespace: Full replacement with new color palette
- `ApplyTheme()`: Updated style values
- `DrawTopTabBar()`: Deleted, replaced by `DrawSidebar()`
- `DrawCustomTitleBar()`: Deleted (brand moves to sidebar)
- `DrawWindowGlassFrame()`: Simplified (no more glass morphism, just border)
- `DrawMainTab()`: Split into `DrawControlsPage()` and `DrawTuningPage()`
- `DrawMoreTab()`: Split into `DrawOverlaysPage()` and `DrawAutomationPage()`
- `DrawConsoleTab()`: Renamed to `DrawConsolePage()`, toolbar redesigned
- `OriginalSectionTitle()` / `GlassSectionLabel()`: Replaced by panel header with colored indicator
- `PaintGlassChildBg()` / `DrawGlassPanelRect()`: Simplified to flat panels with borders
- `ToggleSwitch()`: Smaller (36x20), accepts accent color parameter
- `PrimaryButton()` / `SecondaryButton()`: Replaced by single subtle button style
- `RowSliderFloat/Int`: Replaced by inline slider with accent-colored fill+thumb
- `g_SelectedTab`: Renamed to `g_SelectedPage`, range 0-4

### No Changes
- `ConsoleBuffer` struct and thread-safe logging
- `TextColorToImVec4()` color mapping
- DX11 hooking (`hkPresent`, `MainThread`, kiero)
- `WndProc` input handling
- `InitImGui()` font loading
- `GUIComponent` class interface (`GUI.hpp` unchanged)
- Window size: 928x592
- All `BotMod.*` and `OverlayMod.*` API calls
- `Render()` overall structure (NewFrame -> ApplyTheme -> content -> Render)

## Render Flow (Updated)

```cpp
void GUIComponent::Render() {
    // ... existing NewFrame, ApplyTheme ...
    
    if (IsOpen) {
        ImGui::Begin("###AbuseMain", ...);
        
        // Sidebar (left 180px)
        DrawSidebar();
        ImGui::SameLine(0, 0);
        
        // Main content area
        ImGui::BeginChild("##MainArea", ...);
          DrawStatusBar();
          ImGui::BeginChild("##Content", ...);
            switch (g_SelectedPage) {
                case 0: DrawControlsPage(); break;
                case 1: DrawTuningPage();   break;
                case 2: DrawOverlaysPage(); break;
                case 3: DrawAutomationPage(); break;
                case 4: DrawConsolePage();  break;
            }
          ImGui::EndChild();
        ImGui::EndChild();
        
        ImGui::End();
    }
    ImGui::Render();
}
```
