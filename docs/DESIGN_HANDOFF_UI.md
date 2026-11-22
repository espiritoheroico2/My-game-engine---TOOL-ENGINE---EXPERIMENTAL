# Editor UI: design evaluation and handoff

Branch: `design/ui-avaliacao` (base `da41dfc`). Scope: `apps/editor` and `assets/lang` only. Nothing in `engine/` was changed.

## 1. Overview

The editor draws its windows with two chromes that do not share a visual system:

- **Dear ImGui windows** (Hierarchy, Inspector, Console, Profiler, Assets, Scene/Game/Coding tabs, toolbar). Colours come from `apps/editor/theme.hpp`, which derives ImGui colours from the palette.
- **SpriteUI windows** (Scenes, Settings, Script, Editor info, Start screen). Colours come from `apps/editor/sui_palette.hpp` through `window_palette.hpp` and `sui_panel.hpp`, but several windows also keep their own hardcoded `Srgb(...)` colour sets (Scenes `Colors`, Profiler, Inspector, Start screen).

Palette presets: Dark blue (default), Graphite, Light, Night. The active one is chosen in Settings > Appearance and read every frame.

This document records what was checked, what was changed on this branch, and what is left open.

## 2. Evaluation

Severity: **High** blocks use or breaks the palette promise; **Medium** hurts legibility or consistency; **Low** cosmetic.

| # | Sev. | Finding | Evidence | Status |
|---|------|---------|----------|--------|
| E1 | High | The Scenes window body was grey `#656972` in every palette, ignoring the palette. | Pixel sample `(60,488)` = `#656972` on Dark blue, Graphite, Light and Night. Cause: `WindowFrameColors::body` default `(0.13, 0.14, 0.17)` linear (SpriteUI `WindowFramePaint.hpp`), baked into the panel visual in `buildTree()` before the palette was assigned. | **Fixed** (`6c0f27a`). After: `#0d0d32` Dark blue, `#f0f0f2` Light. |
| E2 | Medium | Button label on hover fails 4.5:1 in SpriteUI Scenes. | Text on `mix(surface, accent, 0.8)`: Night 3.52, Graphite 3.74 (passes on the other two). | **Fixed** (`723c252`): rest 0.5, hover 0.65. Hover now 4.72 to 6.66. |
| E3 | Medium | ImGui button hover fails 4.5:1. | Text on `mix(surface, accent, 0.75)`: Night 3.88, Graphite 4.06. | **Fixed** (`712a4bd`): hover 0.65. Now 4.72 to 6.66. |
| E4 | Medium | Label on the selected accent row (Settings > Appearance theme list) fails. | White-ish `text` on accent: Dark blue 4.08, Graphite 2.76, Light 3.79, Night 2.46. | **Fixed** (`a6a2117`): `LabelOn()` picks white or black, the higher contrast. Now 4.66, 6.32, 4.67, 6.79. |
| E5 | Medium | ImGui active or toggled buttons (Grid, Gizmos, Move (W), Console Auto-scroll) use accent fill with palette text. | `ImGuiCol_ButtonActive = accent`, text `p.text`: Dark blue 4.08, Graphite 2.76, Light 3.79, Night 2.46. | **Open** (section 8). |
| E6 | Low | The brief expected `textDim` to fail. It does not: textDim on panel or surface is 5.04 to 7.82 in every preset. | Computed WCAG 2.1 ratios, section 6. | No token change needed. The palette test now requires 4.5 (was 3.0). |
| E7 | Medium | Two chromes and several colour sources. Some windows hardcode colours; `ui_tokens.hpp` is not wired to the UI. | `scenes_spriteui.hpp` `Colors`, `profiler_panel.hpp`, `inspector_panel.hpp`, `start_screen_spriteui.hpp`; `ui_tokens.hpp` header says the Settings panel is not connected to it. | **Open** (section 8). |
| E8 | Medium | Narrow windows break layout. At 640x480 the Scenes window clips its content and is overlapped by Console and Hierarchy. Hierarchy title and buttons are cut ("HI", "Du"). Settings values are cut ("Appea...", "API/Gr..."). | Captures at 900x600 and 640x480 (`--size`, see section 5). | **Open** (section 8). |
| E9 | Low | At the default 1600x900, Settings hint and dropdown text is cut with "...": "Below 1.0 renders fewer pixels (faster); above 1.0 renders m...", "Bicubic (Catmull-R...". | Baseline capture. | **Open**. |
| E10 | Low | Scenes list empty state was blank navy rows. | Code: `rows_` empty leaves six empty field rows. | **Fixed** (`af73819`), text added in en/pt-BR/es. Not captured (section 5). |
| E11 | Low | Keyboard: the Scenes view handles mouse clicks, typing and Backspace in the name field only. No focus ring on buttons. | `scenes_spriteui.hpp` and `scenes_panel.hpp` key handling. | **Open** (section 7). |
| E12 | Low | Settings sliders (button radius, window radius, text size). | Capture: values read 3 px, 6 px, 100%. Writes go `settings_changes` -> `main.cpp` `set*` -> `ui_style.hpp`. | **Works, unchanged.** |

## 3. Design tokens used

Palette tokens are in `apps/editor/sui_palette.hpp` (sRGB). Dark blue values, with the other presets in that file.

| Token | Dark blue value | Usage |
|-------|-----------------|-------|
| `panel` | `#0a0f33` | Window body (SpriteUI), ImGui `WindowBg` |
| `surface` | `#141f57` | Fields, cards, track, title bar active |
| `text` | `#edf0f7` | Main text, button labels |
| `textDim` | `#99a8cc` | Labels, hints, disabled button text |
| `accent` | `#0078c7` | Marked buttons, selected rows (mixed), help icon |
| `scroll` | `#00e6cc` | Scroll thumb |
| `border` | `#293a7a` | Outline, separators |
| `error` | `#ff7366` | Errors and warnings; danger button mix |

Derived colours (computed in code, sRGB mix, `t` is the weight of the accent or error):

| Derived | Formula | Usage |
|---------|---------|-------|
| Button rest | `mix(surface, accent, 0.5)` | Scenes buttons (was 0.6) |
| Button hover | `mix(surface, accent, 0.65)` | Scenes buttons (was 0.8) |
| ImGui button hover | `mix(surface, accent, 0.65)` | `theme.hpp` (was 0.75) |
| Row selected | `mix(surface, accent, 0.45)` | Scenes selected row |
| Button danger | `mix(surface, error, 0.6)` | Delete and danger buttons |
| Button disabled | `mix(panel, surface, 0.5)` | Disabled buttons |
| Label on accent | `LabelOn(accent)`: white or black | Selected preset row |

Shape tokens (`apps/editor/ui_style.hpp`, persisted in `editor_settings.json`):

| Token | Default | Range | Usage |
|-------|---------|-------|-------|
| Button radius | 3 px | 0 to 12 | Buttons, preset rows |
| Window radius | 6 px | 0 to 16 | Window frames, panels |
| Text scale | 100% | 80% to 160% | `uiFont()` for SpriteUI text |
| Interface scale | theme | 0.7 to 2.0 | ImGui metrics (`theme.scale`) |

Layout constants (Scenes): padding 24, gap 6, label 18, field 30, button 34 (150 wide, max 220), row 26, header 30.

## 4. Components and states

### Button (SpriteUI, Scenes)

| State | Fill | Label | Notes |
|-------|------|-------|-------|
| Default | `mix(surface, accent, 0.5)` | `text` | Contrast 6.18 to 8.40 |
| Hover | `mix(surface, accent, 0.65)` | `text` | 4.72 to 6.66 (fixed, E2) |
| Active (pressed) | none defined | none | No pressed style exists in the Scenes view. Open (section 8). |
| Disabled | `mix(panel, surface, 0.5)` | `textDim` | Not clickable (`interactable = false`). 5.33 to 7.20 |
| Danger | `mix(surface, error, 0.6)` | `text` | Delete and confirm-delete; 4.53 to 5.95 |
| Focus | none | none | Open (section 7). |

### Button (ImGui, toolbar and panels)

| State | Fill | Label | Notes |
|-------|------|-------|-------|
| Default | `mix(surface, accent, 0.45)` | `text` | 6.74 to 9.05 |
| Hover | `mix(surface, accent, 0.65)` | `text` | 4.72 to 6.66 (fixed, E3) |
| Active or toggled on | `accent` | `text` | 2.46 to 4.08. Fails (E5, open). |

### Slider (Settings > Appearance)

- Track: grey rail with blue fill to the value; thumb white circle; value text at the right in `textDim` (`3 px`, `6 px`, `100%`).
- Range: button radius 0 to 12 px, step 1, integer. Window radius 0 to 16 px, step 1, integer. Text size 80% to 160%, step 0.05.
- Drag works through `appearanceSliderPointer`. Writes are clamped (`Clamp*` in `settings_model.hpp`) and saved.
- Verified: render with values, and the value display. Dragging was not captured (no pointer automation).

### List row (Scenes list, 6 visible rows)

| State | Fill | Label |
|-------|------|-------|
| Normal | `surface` | `text` |
| Selected | `mix(surface, accent, 0.45)` | `text` |
| Current (open) | as normal, suffix " (open)" | `text` |
| Empty slot | `surface` | none |
| Empty list | `surface` | first row shows the empty-state text in `textDim` (E10) |

### Tab and section header (Settings)

- Settings uses collapsible section headers, not a tab bar: 30 px header on `surface`, with a `+` or `-` glyph at the right. The `API/Graphics` section opens by default.
- The top-level Scene, Game and Coding tabs are ImGui tabs. Their active tab is not separately styled in this pass.

## 5. Responsive behaviour

Captured with `--size` (added on `e5ad5d9`).

- **900x600**: Hierarchy names truncate with "..." (ImGui). The Scenes window is about 150 px wide and its buttons fill it. Settings value column truncates ("Soft...", "Matc..."). Usable, but cramped.
- **640x480**: not usable.
  - Scenes content is clipped at the window edge, and its right side is covered by the Scene view.
  - Hierarchy title and "Duplicate" are cut.
  - Settings section labels are cut ("Appea...").
  - Scene toolbar hint is cut ("right: orbit ...").
- **Minimum size**: no minimum width is set for docked SpriteUI panels. The Scenes panel width is `min(680, window - 32)` when floating, and the dock width when docked.

Verified by capture: 900x600 and 640x480 (screenshots in the session scratchpad, not committed). Not verified: any size between 640 and 900, and the minimum-size rule (none exists to test).

## 6. Edge cases

| Case | Behaviour | Status |
|------|-----------|--------|
| No scenes | First row shows "No scenes yet. Press New scene... to create one." | Code-verified (builds, tests pass). Not captured: the editor always creates `Main` on start, and there is no headless way to delete it. |
| Long scene name | Single-line label, clipped to the row | Not checked |
| Long text in Settings hints | Cut with "..." | Seen at 1600x900 (E9), open |
| No file open (no object selected) | Inspector shows "no object selected" | Seen in baseline |
| Narrow window | See section 5 | Open |
| Palette change at runtime | Scenes body, rows and buttons update on the next frame | Captured (Dark blue, Graphite, Light, Night) |

Palette contrast (WCAG 2.1, sRGB linearised, `(L1+0.05)/(L2+0.05)`), computed from `sui_palette.hpp`:

| Pair | Dark blue | Graphite | Light | Night |
|------|-----------|----------|-------|-------|
| text on panel | 16.25 | 15.45 | 14.95 | 16.17 |
| text on surface | 13.53 | 13.78 | 16.71 | 15.36 |
| textDim on panel | 7.82 | 5.86 | 5.04 | 6.69 |
| textDim on surface | 6.51 | 5.23 | 5.63 | 6.36 |
| error on panel | 6.97 | 6.98 | 4.75 | 7.62 |
| text on button (rest, 0.5) | 7.59 | 6.18 | 8.40 | 6.43 |
| text on button hover (0.65) | 6.27 | 4.78 | 6.66 | 4.72 |
| text on selected row (0.45) | 8.09 | 6.74 | 9.05 | 7.14 |
| text on danger (0.6) | 4.75 | 4.53 | 5.95 | 4.73 |
| label on accent (`LabelOn`) | 4.66 | 6.32 | 4.67 | 6.79 |
| ImGui button rest (0.45) | 8.09 | 6.74 | 9.05 | 7.14 |
| ImGui button hover, before (0.75) | 5.53 | 4.06 | 5.68 | 3.88 |
| text on accent, ImGui active | 4.08 | 2.76 | 3.79 | 2.46 |

Before, the Scenes hover values were 5.20, 3.74, 5.24 and 3.52 (Dark blue, Graphite, Light, Night). Those four are the ones that failed. The text-on-accent row is still open (E5).

Test: `Editor.SuiPalette` asserts text and textDim at 4.5:1 on panel and surface, and `LabelOn(accent)` at 4.5:1, for all presets. Passes.

## 7. Accessibility notes

- **Contrast**: all primary text and textDim labels pass 4.5:1 on panel and surface in all presets. Fixed: Scenes and ImGui hover labels (E2, E3) and the selected preset label (E4). Open: ImGui active or toggled buttons (E5), 2.46 to 4.08.
- **Keyboard**: the Scenes panel handles key events only for Backspace in the name field, when it is focused (`scenes_panel.hpp`). Its buttons have no keyboard activation and there is no tab order. Open.
- **Focus indicator**: the name field changes fill when focused (`mix(surface, text, 0.08)`), which is a subtle cue. Buttons show no focus ring. Open.
- **Colour only**: selected rows and the active preset are shown by fill colour. There is no border or check mark, so the selection relies on colour. Acceptable for now; a border or check mark would be stronger.
- **Text size**: Settings > Appearance can raise text size to 160%. Fixed-width rows may clip larger text. Not tested above 100% in this pass.

## 8. Open items not fixed, and why

1. **E5, ImGui accent-filled labels (contrast 2.46 to 4.08).** Fix is a per-button text colour (`LabelOn`) in the toolbar and the Console toggles, or a lighter active fill. Left open: it touches many ImGui call sites and needs a decision on the look of active toggles.
2. **E7, two chromes and scattered colours.** Scenes, Profiler, Inspector and Start screen keep their own `Srgb` sets, and `ui_tokens.hpp` is not wired. Unifying them is a refactor across the editor, out of scope for a UI pass. Decision for the user: which source of truth to keep (`sui_palette.hpp` is the likely one).
3. **E8, narrow windows.** Needs a minimum width for docked panels and a rule for how the dock behaves when space runs out. Design decision: minimum sizes per window, or allow overlap.
4. **E9, Settings long text.** Could wrap hints onto two lines or move them into a tooltip. Left as is: the wording is for the user to choose.
5. **Pressed (active) state for SpriteUI buttons.** Not defined; needs a colour choice.
6. **Focus ring and keyboard navigation for SpriteUI windows.** Needs an input design (which keys, and a focus order). Not started.
7. **Empty-state capture.** The empty Scenes list is compile-verified only. A capture needs either a way to delete `Main` headlessly, or a test hook.
8. **Hover states captured.** Hover colours are verified by calculation only; pointer hover was not captured.
9. **Sliders while dragging.** Render verified; drag not verified (no pointer automation).
10. **Scenes button fill changed.** Rest mix moved from 0.6 to 0.5, so buttons are slightly less saturated than before. This is a visual choice to confirm with the user.

## Verification summary

- Build: `cmake --preset editor`, then `cmake --build build/editor -j8` (full build, exit 0).
- Tests: `ctest -R "Editor|Palette|Settings|Coding|Script|Scenes|Sui" -j4` in `build/editor`: 41 of 41 passed.
- Captures: `"Projeto Final/ToolEditor" --frames 40 --screenshot <file>.ppm --lang en [--palette N] [--tab 0] [--size WxH]`, run from `Projeto Final/`. Converted with PIL.
