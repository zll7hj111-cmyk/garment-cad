# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop — Qt6 (Widgets + Graphics View + OpenGL), Windows 25H2 primary target. Not a web surface; QSS is a CSS-2.1 subset, no flex/grid/backdrop-filter.

## Stack

C++23 / Qt6 (Widgets, Svg, Test) / MSVC 2022 / CMake. QSS generated from a central ThemeTokens table at runtime (Theme::buildStylesheet); canvas rendering is custom QGraphicsItem paintEvent + CanvasStyle token table kept in sync by hand.

## Users

Primary: professional garment pattern makers (打版师) — full-time users, efficiency-first, fluent in pattern-making vocabulary (智能笔/打断/交点/烘焙/辅助层), heavy keyboard use, often working with 200+ variables on real garment drafts.

Secondary (confirmed by user): learners and students — need guidance, discoverability, visible affordances, and gentler error recovery; the UI must carry both registers without slowing the expert down.

## Product Purpose

Parametric garment CAD: pattern drafting through parametric modeling. The designer draws constrained segments on canvas; formula variables, measurements, conditions, and groups drive automatic re-solving and linked updates. Success = an expert can draft and adjust a full garment pattern without ever breaking flow, and a learner can understand what each tool does without a manual.

## Positioning

The canvas is the authority and the panels are live views of the same parametric model: click a measure card and the canvas flashes the two source points; a group badge selects the whole group; edits re-solve instantly (200ms debounce). This canvas↔panel↔status-bar three-channel identity is the mechanism no generic CAD clone copies.

## Operating Context

- All-day sessions; docked panels on the right (variables/measures/formulas/layers/groups), floating pill tool dock top-center, canvas dominant.
- Pattern-maker vocabulary is the language: cm/° units, L3-style serial labels, 前片/后片 blocks, 辅助层 vs 工作层, 桥接线 with follow/absolute angle semantics.
- Existing shortcuts are product facts: 8 single-letter tool keys, W toggle, Shift 45°/15° snap, H aux-layer round-trip, Ctrl+D theme swap, Esc cancel-everything.
- Default theme is Dark (main.cpp), light mode available; native Windows title bar (DWM).

## Capabilities and Constraints

- Capabilities: smart pen creation with inline edit strip, segment break/intersection/rotate/measure/angle-measure tools, formula variables with Kahn-topology single-pass solving, conditions dialog, layers (aux/working), groups (lightweight, zero-restriction philosophy), copy chips, delete-impact confirmation with 8 consequence classes, undo/redo, save/load with graceful degradation warnings.
- Constraints: Qt style engine limits (no CSS flex/grid, no backdrop-filter, QSS box-shadow is simulated outline); QSS must be built at runtime from tokens (MSVC C2026 string-literal limits); canvas and UI token tables are separate and must stay in sync; UI text is Chinese; internal units cm, display mm.
- Terminology is domain-fact, not negotiable: 智能笔, 打断, 交点, 烘焙, 辅助层, 桥接线, 组, 引用名 (uppercase), etc.

## Brand Commitments

- Product name: 服装 CAD (garment-cad repo). No logo or external brand system exists; visual identity is entirely internal.
- User confirmed: visual baseline decision is delegated to the design-process competition (concept-seed direction round); no incumbent palette is binding.
- Chinese UI language is binding.
- Accessibility is not yet a stated brand commitment, but the redesign scope includes keyboard reachability and contrast (user approved full-scope rework).

## Evidence on Hand

- Real UI evidence: Theme.cpp token table (42 colors, light+dark), Theme::buildStylesheet QSS generator, CanvasStyle.cpp canvas tokens, 19 Phosphor-style SVGs, MainWindow layout (pill dock / side panel / status bar), five card families (Variable/Formula/Measure/Linked/AngleMeasure), SegmentEditBar inline edit strip, DeleteImpactConfirm 8-class dialog.
- Audit evidence (impeccable critique 2026-08-09, .impeccable/critique/src-ui-2026-08-09.md): 29/40 heuristics; P0 canvas-background dead token; P0 hardcoded light colors bypassing tokens + type colors colliding with semantic hues; P1 keyboard dead ends (Tab swallowed, hover-only delete); P1 inconsistent feedback channels; WCAG failures (dark primary-button white text 3.2:1, text3 all-backgrounds <4.5:1); no loading states; no spacing/radius/type-scale token system.
- Must not fabricate: no testimonials, no customers, no pricing, no market claims.

## Product Principles

1. Efficiency is respect: expert workflows (shortcuts, batch, instant re-solve) must never be slowed to accommodate learners; guidance lives in progressive disclosure, never in blocking ceremony.
2. Canvas is the authority, panels are views: every panel-to-canvas mapping must stay one glance (flash, badge, highlight), because that mapping is the product's unique mechanism.
3. One accent, semantic color means meaning only: decorative hue ≠ status hue; a color that says "success" must never also say "formula type".
4. State is never silent: every action either confirms (toast/inline) or fails loudly with attribution (which connection, which formula); no dead-ends, no tooltip-only errors.
5. Tokens before pixels: no hardcoded color/radius/spacing/font outside the central tables; dark/light are first-class, not afterthoughts.

## Accessibility & Inclusion

Learner audience implies visible affordances and forgiving error recovery. Redesign scope (user-approved) includes: keyboard reachability (Tab flow, focus-visible delete, F2/Enter chip edit), WCAG AA contrast (4.5:1 body / 3:1 large), states not conveyed by color alone, system font scaling respect. No screen-reader-specific requirement has been established beyond these.
