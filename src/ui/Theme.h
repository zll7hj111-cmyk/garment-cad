#pragma once

#include <QColor>
#include <QString>

namespace cad::ui {

enum class ThemeMode { Light, Dark };

/// Central design-token table for the whole application chrome.
/// Single source of truth for the UI look: the QSS generator and the
/// application palette are both derived from these tokens. Canvas-side
/// tokens live in CanvasStyle and are kept in sync with these by hand
/// (same accent / semantic families).
///
/// Design language ("Pattern Workbench"): warm paper-white canvas ground
/// (the drafting table), a piece-palette of four fabric-block hues used ONLY
/// for entity identity (blocks, card type colors), ONE accent (blue) for all
/// selection / focus / active states, and four muted semantic colors
/// (success / warning / danger / teal) reserved for meaning.
/// Raised disciplines (impeccable direction round, 2026-08-09):
/// - cyclorama: states are discrete phases, each carries a text label;
///   color never carries a signal alone.
/// - variable-font: hierarchy is carried by the type-scale steps, not by
///   color or decoration.
/// - cassette-j-card: working/aux layer is an A/B flip with a perceptible
///   physical split, not a subtle tint.
/// - alphabet-storm: annotation text is visual material; density comes
///   from typography and the grid, not from decoration.
struct ThemeTokens
{
    // ── Surfaces ──
    QColor canvasBg;      ///< Behind the graphics view (paper white / night paper).
    QColor surface;       ///< Panels, dialogs, toolbar, menus.
    QColor surface2;      ///< Recessed areas (card list background, hover).
    QColor surface3;      ///< Alternate card stripe (must differ from surface2).
    QColor border;        ///< Hairline borders / dividers.
    QColor borderStrong;  ///< Input borders, stronger separators.

    // ── Text ──
    QColor text1;  ///< Primary text.
    QColor text2;  ///< Secondary text (labels).
    QColor text3;  ///< Tertiary / placeholder / disabled (WCAG AA ≥ 4.5:1).

    // ── Accent (the ONLY decorative hue) ──
    QColor accent;        ///< Brand blue: selection, focus, active tool.
    QColor accentStrong;  ///< Pressed / darker step.
    QColor accentTint;    ///< Light wash background for active states.
    QColor onAccent;      ///< Text on accent fills (light: white, dark: deep ink).

    // ── Piece palette (entity identity ONLY, never status) ──
    QColor piece1;  ///< Deep navy — Block pieces, variable-type values.
    QColor piece2;  ///< Slate blue — formula-type values.
    QColor piece3;  ///< Terracotta — measure-type values.
    QColor piece4;  ///< Muted olive — linked-type values.

    // ── Semantic (meaning only, never decoration) ──
    QColor success;  ///< Green: snap points, aux geometry, OK states.
    QColor warning;  ///< Amber: protected connections, non-fatal alerts.
    QColor danger;   ///< Red: errors, diagnostics.
    QColor teal;     ///< Attachment / connection rings.

    // ── Tooltips (inverted surfaces) ──
    QColor tooltipBg;
    QColor tooltipFg;

    // ── Type scale (variable-font discipline: hierarchy by size) ──
    static constexpr int FontXs   = 10;  ///< meta labels, tags, badges
    static constexpr int FontSm   = 11;  ///< captions, source info
    static constexpr int FontMd   = 12;  ///< cards, inputs, secondary text
    static constexpr int FontBase = 13;  ///< body (app default)
    static constexpr int FontLg   = 14;  ///< emphasized values

    // ── Radius scale (one system, no stray values) ──
    static constexpr int RadiusXs  = 2;   ///< chips, inline marks
    static constexpr int RadiusSm  = 4;   ///< inputs, tags, scrollbars
    static constexpr int RadiusMd  = 6;   ///< buttons, menu items, rows
    static constexpr int RadiusLg  = 8;   ///< cards, group boxes
    static constexpr int RadiusPill = 10; ///< pill dock, badges, delete buttons

    // ── Spacing scale ──
    static constexpr int SpaceXs  = 2;   ///< icon gaps
    static constexpr int SpaceSm  = 4;   ///< tight control padding
    static constexpr int SpaceMd  = 6;   ///< chip gaps
    static constexpr int SpaceBase = 8;  ///< standard gutter
    static constexpr int SpaceLg  = 12;  ///< card inner padding
    static constexpr int SpaceXl  = 16;  ///< panel margins

    static ThemeTokens light();
    static ThemeTokens dark();
};

class Theme
{
public:
    /// Install Fusion style + palette + generated QSS application-wide.
    /// Call once in main() before creating any widget.
    static void apply(ThemeMode mode = ThemeMode::Light);

    /// Tokens of the currently applied theme.
    [[nodiscard]] static const ThemeTokens& tokens();

    /// Theme mode of the currently applied theme.
    [[nodiscard]] static ThemeMode mode();

    /// Generate the full global stylesheet from a token set.
    [[nodiscard]] static QString buildStylesheet(const ThemeTokens& t);

private:
    static ThemeTokens s_tokens;
    static ThemeMode   s_mode;
};

} // namespace cad::ui
