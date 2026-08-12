# DESIGN — Pattern Workbench（打版工作台）

<!-- impeccable:design-schema 1 -->

## World

Pattern Workbench: the pattern-maker's drafting table as an interface. Canvas ground is warm pattern paper (paper-white `#F6F3EC` light / night paper `#14181E` dark) with a hairline coordinate grid; garment geometry renders as precise ink construction lines; block pieces and card type identities share the **piece palette** — four fabric-block hues (deep navy `#2F4259`, slate blue `#3E5C76`, terracotta `#C85A3E`, muted olive `#6A8D5F`) that mean *identity*, never status. One accent blue drives all selection/focus/active states. Semantic colors (success/warning/danger/teal) are reserved exclusively for meaning.

Direction provenance: impeccable concept-seed direction round, 2026-08-09, key b946a5ee. Assigned direction was Marker Making (排料图), user-corrected to 2D garment CAD pattern-drafting semantics (no fabric-utilization concept; status bar reads coordinates/length/angle/solve state). User confirmed: professional pattern makers + learners, full-scope rework, framework-first execution.

## Design Tokens

Central table: `src/ui/Theme.h` `ThemeTokens` (light + dark factories in `Theme.cpp`). QSS generated at runtime by `Theme::buildStylesheet` (all values substituted from tokens; no literals).

- **Surfaces**: canvasBg (paper), surface, surface2, surface3, border, borderStrong
- **Text**: text1/text2/text3 — text3 passes WCAG AA 4.5:1 on surface2 in both themes (light `#667085` 4.7:1, dark `#8A94A0` 4.7:1)
- **Accent**: accent/accentStrong/accentTint + **onAccent** (text on accent fills; dark = deep ink `#0A1420` fixing the 3.2:1 white-on-blue failure)
- **Piece palette**: piece1..piece4 (entity identity; canvas blocks and card values share the family)
- **Semantic**: success/warning/danger/teal — light-theme values deepened to pass AA as foreground (success `#15803D`, warning `#B45309`, teal `#0F766E`)
- **Type scale**: FontXs 10 / FontSm 11 / FontMd 12 / FontBase 13 / FontLg 14 — hierarchy by size, not color
- **Radius scale**: RadiusXs 2 / RadiusSm 4 / RadiusMd 6 / RadiusLg 8 / RadiusPill 10
- **Spacing scale**: SpaceXs 2 / SpaceSm 4 / SpaceMd 6 / SpaceBase 8 / SpaceLg 12 / SpaceXl 16

Canvas tokens: `src/canvas/CanvasStyle.h/.cpp` kept in sync with ThemeTokens by hand (same accent/semantic/piece families). `canvasBackground` is now consumed: `CanvasScene::setStyle` pushes it to every attached view (authoritative theme path), and `CanvasView` seeds its background brush from the scene style at construction (fixes the dead-token P0).

**Dark-mode line adaptation**: `CanvasStyle::displayColor(role, dataColor)` lifts dark data colors (the default near-black ink `(30,30,30)`) to the role's light-on-dark family (outline `#F4F6F8`, internal `#C6CDD5`, auxiliary `#606872`) so lines stay legible on night paper; user-chosen bright colors pass through untouched (luma threshold 0.5). Applied at paint time in `BlockItem` (lines) and `CurveItem` (curves) — light theme and print are identity.

## Rules

1. **Type hue ≠ semantic hue.** Card type identity comes from the piece palette (variable=piece1, formula=piece2, measure/angle=piece3, linked=piece4). Semantic colors appear only where they mean status (snap=success, protected=warning, error=danger, attachment=teal). The old formula-green/measure-amber/linked-teal type colors are gone.
2. **No hardcoded colors outside token tables.** All card accent bars, layer bars, HUD overlays, badge fills, dialog inline styles and menu icons read from `Theme::tokens()` or `CanvasStyle` — verified by audit: non-theme hex count dropped from 125 to near zero in the refactored files.
3. **Dark mode is first-class.** Default theme is Dark; every restyled surface was checked for the light-island failure (FormulaCard, ConditionDialog, AngleHud, GroupBadge, LayerPanel bars).
4. **Hierarchy by scale.** Type scale steps carry emphasis; color never does the job of size.
5. **State phases carry text labels.** Any state conveyed by color also carries a label or glyph (cyclorama discipline).

## Surfaces

- **Canvas**: pattern-paper ground, ink construction lines, piece-hued block fills, hairline grid, snap indicators in success green, protected connections in amber, attachments in teal. Length labels in Consolas monospace 10px.
- **Chrome**: surface-toned panels, one accent for active/checked/focus, hairline borders, pill tool dock (radius 10).
- **Cards** (variable/formula/measure/linked/angle): surface card + 3px piece-hued left bar + piece-hued value text; hover border in the piece soft tint; dangling values in danger.
- **Dialogs** (condition etc.): token-derived inline styles only; warning-toned callouts with translucent washes built from rgba(warning, 30/110) so dark mode stays coherent.
- **HUD** (AngleHud): theme-aware floating overlay — reads background/text/valid/invalid colors from the owning scene's CanvasStyle instead of hardcoded dark.

## Status

- 2026-08-09: framework layer complete (tokens, stylesheet engine, canvas sync, card/dialog/HUD/badge/icons token migration) + dark-mode line adaptation (`displayColor`); 19/19 tests green. Pending: pixel-level review vs the approved comp, remaining hardcoded tool-layer colors (MarqueeGesture/ToolMeasure/ToolCurveEdit preview hues) if the review requires.
- Direction contract review: `.impeccable/critique/src-ui-2026-08-09.md` (baseline 29/40), comp: `.impeccable/mocks/decision/pattern-workbench-v3` (approved direction).
