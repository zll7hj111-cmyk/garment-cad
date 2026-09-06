---
name: qt-anthropic-design
description: >-
  Applies Anthropic/Claude's warm-minimalist, editorial design system to C++23/26 Qt6 desktop UI refactoring and component design. Use when designing, styling, or refactoring Qt widgets, dialogs, property bars, cards, or QSS to achieve an elegant, non-generic, craftsman-grade CAD interface adhering to Theme tokens, FormScaffold, and Qt performance rules.
---

# Qt6 Anthropic / Claude UI Design Skill (WildWind Pattern Edition)

This skill guides the design, restyling, and implementation of UI components in C++23/26 & Qt6 following Anthropic's signature aesthetic (**Warm Editorial Minimalism**), tailored specifically for the WildWind Pattern (野风帖) parametric CAD architecture.

---

## 1. Design Philosophy: The Digital Craftsman Atelier

Traditional CAD interfaces often suffer from two extremes:
- **90s Industrial Clutter**: Dense, cold-gray grids with raw Win32 buttons and high visual fatigue.
- **Generic AI Slop / Cyberpunk SaaS**: Gratuitous purple-to-blue neon gradients, excessive floating shadows, and bubbly border radiuses inappropriate for professional tools.

**The Anthropic / Claude Aesthetic represents an "Academic & Editorial" paradigm:**
1. **Paper & Ink Metaphor**: The interface evokes a master tailor's workbench—warm ivory drafting paper (`#FAF9F5`), deep slate-charcoal ink typography (`#141413`), and hairline dividers (`#E5E2DA`).
2. **Subdued Earth Accents**: Replace harsh digital blues and fluorescent greens with **Terracotta Coral (`#CC785C` / `#D97757`)** as the primary action/focus accent, accompanied by muted sage green, clay amber, and indigo slate.
3. **Typography Authority**: Editorial serif headers (evoking technical monographs and pattern design books) paired with crisp sans-serif controls and monospaced numeric readouts.
4. **Restraint & Flat Precision**: 1px sharp hairline borders, subtle tint washes instead of heavy drop shadows, and disciplined corner radiuses (<= 4px).

---

## 2. Token System & `Theme.h` Mapping

All visual styling **must** map to `cad::ui::ThemeTokens` in `src/ui/Theme.h`. Never hardcode hex values directly in QSS or `.cpp` (strictly enforced by `check_hardcoded_colors.py`).

### 2.1 Palette Tokens

| Semantic Role | Token in `ThemeTokens` | Claude Light Tone | Claude Dark Tone | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Canvas Background** | `canvasBg` | `#FAF9F5` (Warm Ivory) | `#141413` (Carbon Slate) | Drawing surface / main drafting paper |
| **Main Surface** | `surface` | `#FFFFFF` (Pure Paper) | `#1F1E1D` (Deep Charcoal) | Sidebars, property panels, dialogs |
| **Recessed Surface** | `surface2` | `#F5F3EB` (Warm Sand) | `#262422` (Muted Ink) | Card lists, item hover background |
| **Alternate Stripe** | `surface3` | `#EFECE1` (Deep Sand) | `#2E2B28` (Charcoal Layer) | Table alternating rows, button bars |
| **Hairline Border** | `border` | `#E5E2DA` (1px Hairline) | `#383531` (1px Subdued) | Card outlines, structural dividers |
| **Strong Border** | `borderStrong` | `#D5D0C5` | `#4D4943` | Input borders, active separators |
| **Primary Text** | `text1` | `#141413` (Near Black) | `#ECE9E2` (Soft White) | Main titles, values, prominent text |
| **Secondary Text** | `text2` | `#5C5850` (Warm Gray) | `#A39E93` (Warm Light Gray) | Field labels, captions, metadata |
| **Tertiary Text** | `text3` | `#8C877D` (Muted) | `#706C63` (Muted) | Placeholders, disabled states |
| **Primary Accent** | `accent` | `#CC785C` (Terracotta) | `#D97757` (Terracotta Coral) | Active tools, focus ring, primary buttons |
| **Accent Wash** | `accentTint` | `rgba(204,120,92,0.12)` | `rgba(217,119,87,0.18)` | Selected item backgrounds, hover tint |
| **Text on Accent** | `onAccent` | `#FFFFFF` | `#FFFFFF` | Text on solid accent buttons |

### 2.2 Piece Identity & Semantic Colors
- `piece1` (Variables): `#383530` (Charcoal Slate)
- `piece2` (Formulas): `#2E6B65` (Muted Pine / Sage Teal)
- `piece3` (Measures): `#C46849` (Terracotta Clay)
- `piece4` (Linked Objects): `#3D5A80` (Muted Indigo)
- `success`: `#3E8966` (Craftsman Moss Green)
- `warning`: `#D48B38` (Amber Ochre)
- `danger`: `#C94A4A` (Brick Red)
- `teal`: `#2A7B88` (Attachment Rings)

### 2.3 Typography & Readout Discipline
- **Font Hierarchy**:
  - `FontXl` (18px): Dialog titles, major section headers (Prefers Serif: `Lora`, `Charter`, `Georgia`, `Noto Serif SC`, Semibold).
  - `FontLg` (15px): Primary numeric readout in cards (`kMonospaceMd`, Semibold).
  - `FontBase` (13px): Standard body text, main labels (`Inter`, system UI font).
  - `FontMd` (12px): Secondary text, input box values.
  - `FontSm` (11px): Captions, units, formula references (`kCaptionSm`).
  - `FontXs` (10px): Badges, category uppercase tags, micro chips.
- **Monospace Rule**: Any live CAD coordinate, angle, length, or formula input **must** use `kMonospaceFamily` ("Consolas", "Courier New", monospace) to prevent jitter when values scrub during drag.

### 2.4 Corner Radius & Spacing Rules
- **Radius Discipline (Strictly <= 4px)**:
  - `RadiusXs = 0` (Squared CAD marks)
  - `RadiusSm = 2px` (Scrollbars, tags)
  - `RadiusMd = 2px` (Standard buttons, menu rows)
  - `RadiusLg = 4px` (Cards, group frames, floating panels)
  - `RadiusBadge = 4px` (Status badges)
  - `RadiusCapsule = 999` (Search inputs & value capsules via ElaWidgetTools patch)
- **Spacing Steps**: `SpaceXs=2`, `SpaceSm=4`, `SpaceMd=6`, `SpaceBase=8`, `SpaceLg=12`, `SpaceXl=16`.

---

## 3. UI Component Construction Checklist

### 3.1 Dialogs & Tool Forms (Use `FormScaffold.h`)
Instead of assembling bare `QDialog` layouts manually:
1. **Title Bar**: Use `cad::ui::makeFormTitleBar("CODE", "标题", parent)`:
   - Deep slate ink background with terracotta code badge + crisp Chinese title.
2. **Section Headers**: Use `cad::ui::makeFormGroupHeader("SECTION", "分组名称")`:
   - 10px uppercase letter-spaced code + 1px hairline rule beneath.
3. **Form Grid**: Use `cad::ui::applyFormGrid(formLayout)`:
   - Fixed 88px right-aligned labels, 10px row spacing.
4. **Button Bar**: Use `cad::ui::makeFormButtonBar({btnCancel, btnApply}, parent)`:
   - Secondary button has subtle 1px border; primary confirm button is solid Terracotta `accent`.

### 3.2 Property Panels & Cards (Inherit `CardBase`)
1. **Left Accent Stripe**: 4px vertical bar (`setAccentRole`) colored by entity type (`piece1..piece4`).
2. **Surface & Border**: `surface` background with 1px `border` outline, `RadiusLg` (4px).
3. **Hover & Selection**: On hover, background shifts softly to `surface2`; selected cards gain `accentTint` background wash and 1px `accent` border.
4. **Contract Preservation**: Always preserve testing contracts (`cardIndex`, `varIndex`, `measureIndex`, `angleIndex` objectNames).

### 3.3 Flat Chip Buttons
Use `cad::ui::chipButtonStyle()` for compact toggle buttons (degrees/arc, segment flip, modes):
```cpp
// 1px border, 2px radius, transparent background, terracotta wash when checked
btn->setStyleSheet(cad::ui::chipButtonStyle());
```

### 3.4 Floating Context Strip & HUD Keycaps (ContextStrip & HudItem)
1. **Floating Capsule Container**:
   - `background: surface; border: 1px solid border; border-radius: RadiusCapsule;` (or `RadiusLg=4px` for docked variants).
   - Minimal ambient drop shadow: `QGraphicsDropShadowEffect` with blur radius 10px, offset (0, 3px), color `rgba(0,0,0,0.06)`.
2. **Keycap Badge Token (`<kbd>` Style)**:
   - For keyboard shortcut hints (e.g. `[W]`, `[Enter]`, `[D]`):
     - Background: `surface2` (`#F5F3EB`), Border: 1px `borderStrong` (`#D5D0C5`), Radius: 2px.
     - Font: 10px Monospace Semibold, Color: `text2` (`#5C5850`), Padding: `1px 4px`.
   - Never render shortcuts as plain unstyled text.

### 3.5 Humanist Empty States (Panel & List Guidance)
When card lists, layers, or variable tables are empty:
1. **Avoid "No Data" Clichés**: Never render raw cold strings like "暂无数据" or an empty gray box.
2. **Editorial Composition**:
   - Icon: Phosphor Icon (Regular 1.5px stroke, 32x32, color `text3`).
   - Title: 14px Semibold (Serif preferred), color `text1` (e.g., *"从第一块样板开始"*).
   - Body: 11px warm gray caption (`text2`), providing clear, constructive guidance (e.g., *"使用智能笔在画布上绘制衣片轮廓，或在此添加人体规格公式"*).
   - Margin & Padding: Center-aligned with generous vertical breathing room (`SpaceXl * 2`).

### 3.6 Graceful Error & Conflict Feedback
Parametric solvers encounter cycle dependencies, out-of-range formulas, and over-constrained points.
1. **No Jarring Red Walls**: Avoid harsh `#FF0000` text or intrusive modal alert dialogs.
2. **Warm Brick-Red Wash**:
   - Container background: `rgba(201, 74, 74, 0.08)` (subdued brick red wash).
   - Border: 1px solid `danger` (`#C94A4A`).
   - Icon: Phosphor `warning-circle` or `info` (16px, terracotta/brick color).
3. **Constructive Copy**: Explain the cause and suggest actionable remedy (e.g., *"公式中的变量 'B_bust' 未定义，点击此处快速创建"*).

---

## 4. Canvas & Drafting Paper Metaphor (CanvasView & Scene)

The drafting viewport is the core stage of the CAD software. It should feel like high-grade drafting paper under an architect's desk lamp:

1. **Paper Background**:
   - Fill with `canvasBg` (`#FAF9F5` in light mode, `#141413` in dark mode).
2. **Architectural Dot / Hairline Grid**:
   - **Main Grid**: 100mm / 50mm major lines using 1px hairline with `border` at 40% alpha (`rgba(229, 226, 218, 0.4)`).
   - **Sub Grid**: 10mm / 5mm grid points rendered as discrete 1px dots (`DotGrid`) rather than dense intersecting lines, avoiding visual noise.
3. **Precision Snapping Indicators**:
   - Snap rings / point markers: Clean geometric shapes (diamonds, circles) with 1.5px stroke in `success` (Moss Green) or `accent` (Terracotta).
   - Avoid chunky filled blobs or neon halos.

---

## 5. Iconography & Visual Weight Hierarchy

1. **Unified Weight (Phosphor Icons)**:
   - Stick strictly to **Regular (1.5px stroke)** across all tools, menus, and card icons.
   - Never mix filled icons with line icons unless an icon is specifically in its **active/toggled** state.
2. **Color Roles for Icons**:
   - **Resting**: `text2` (`#5C5850`) - calm, non-distracting.
   - **Hover**: `text1` (`#141413`) - subtle focus.
   - **Active Tool / Checked**: `accent` (`#CC785C`) or `onAccent` (`#FFFFFF`) if inside a solid button.
   - **Disabled**: `text3` (`#8C877D`).

---

## 6. Architectural & Performance Rules (Do Not Break)

1. **Layering Independence**:
   - UI widgets must live exclusively in `src/ui/`.
   - Tool gesture classes (`src/tools/`) must **never** include QWidget headers or create UI directly. Use `cad::ui::` forward declarations.
2. **Zero `setStyleSheet` in Critical Paths**:
   - **Never** call `setStyleSheet` inside `mouseMoveEvent`, rendering loops, or high-frequency solver signals (`currentLengthChanged`, canvas pan/zoom).
   - Dynamic states must be handled via `setProperty` + `style()->polish()`, custom `QPainter` drawing, or dedicated `QPalette` updates.
3. **Theme Baking**:
   - Dialogs bake tokens from `Theme::tokens()` during construction.
   - For long-lived panels, subscribe to theme change signals rather than polling.
4. **Guard Checks Validation**:
   - Run `python tools/check_hardcoded_colors.py` after writing new QSS or widgets.
   - Run `python tools/check_layering.py` to ensure zero upward dependencies.
