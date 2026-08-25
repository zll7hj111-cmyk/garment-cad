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
/// Design language ("Endfield field engineering", 2026-09): 蓝图灰纸面
/// (industrial concrete) + 碳黑墨字 + 信号黄 #FFDE00 唯一受控强调色。
/// 信号黄只作「图形信号」(选中/激活条/焦点/一个主动作), 绝不作正文或
/// 大面积填充 (黄字白底不可读)。几何 = 默认直角, 功能圆角 2–4px, 1px 细线。
/// 深度 = complex (系统级重建), 克制在「工具仍快、密、可用」边界内。
/// 其余纪律 (cyclorama/variable-font/cassette-j-card/alphabet-storm) 不变。
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
    QColor accent;        ///< 信号黄 #FFDE00: selection, focus, active tool (图形信号, 非文字色).
    QColor accentStrong;  ///< Pressed / darker step.
    QColor accentTint;    ///< Light wash background for active states.
    QColor onAccent;      ///< Text on accent fills (碳黑 #0D1117 on yellow, 两模式同值).

    // ── Piece palette (entity identity ONLY, never status) ──
    QColor piece1;  ///< 碳黑灰 — Block pieces, variable-type values.
    QColor piece2;  ///< 深青 — formula-type values.
    QColor piece3;  ///< 陶土橙 — measure-type values.
    QColor piece4;  ///< 钴蓝 — linked-type values.

    // ── Semantic (meaning only, never decoration) ──
    QColor success;  ///< Green: snap points, aux geometry, OK states.
    QColor warning;  ///< Amber: protected connections, non-fatal alerts.
    QColor danger;   ///< Red: errors, diagnostics.
    QColor teal;     ///< Attachment / connection rings.

    // ── Row alternation (card lists; 2026-08 用户拍板 — fixed in both modes) ──
    QColor rowEven;  ///< 偶数行竖线蓝 #2F6FED (亮/暗同值; 终末地下待用户重拍板)
    QColor rowOdd;   ///< 奇数行竖线橙 #F59E0B

    // ── Tooltips (inverted surfaces) ──
    QColor tooltipBg;
    QColor tooltipFg;

    // ── Type scale (variable-font discipline: hierarchy by size) ──
    static constexpr int FontXs   = 10;  ///< meta labels, tags, badges
    static constexpr int FontSm   = 11;  ///< captions, source info
    static constexpr int FontMd   = 12;  ///< cards, inputs, secondary text
    static constexpr int FontBase = 13;  ///< body (app default)
    static constexpr int FontLg   = 14;  ///< emphasized values

    // ── Radius scale (Endfield: 默认 0, 功能圆角 2–4px, 禁胶囊) ──
    static constexpr int RadiusXs   = 0;   ///< chips, inline marks (squared)
    static constexpr int RadiusSm   = 2;   ///< inputs, tags, scrollbars
    static constexpr int RadiusMd   = 2;   ///< buttons, menu items, rows
    static constexpr int RadiusLg   = 4;   ///< cards, group boxes
    static constexpr int RadiusPill = 4;   ///< dock, badges (no pills)

    // ── Spacing scale ──
    static constexpr int SpaceXs  = 2;   ///< icon gaps
    static constexpr int SpaceSm  = 4;   ///< tight control padding
    static constexpr int SpaceMd  = 6;   ///< chip gaps
    static constexpr int SpaceBase = 8;  ///< standard gutter
    static constexpr int SpaceLg  = 12;  ///< card inner padding
    static constexpr int SpaceXl  = 16;  ///< panel margins

    // ── Reusable stylesheet fragments (monospace discipline) ──
    /// CAD readouts: monospace digits so drag values never jitter.
    /// Replaces the copy-pasted literal across MainWindow/SegmentEditBar/
    /// ConditionDialog/FormulaCard/VariableCard/etc.
    static constexpr const char* kMonospaceFamily =
        "font-family: 'Consolas','Courier New',monospace;";
    /// Monospace + 12px, for value inputs/readouts.
    static constexpr const char* kMonospaceMd =
        "font-family: 'Consolas','Courier New',monospace; font-size: 12px;";
    /// 11px caption on transparent background (source info, hints).
    static constexpr const char* kCaptionSm =
        "font-size: 11px; background: transparent;";

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

    // ── Badge / dim-text stylesheets (theme-token driven, light/dark aware) ──
    /// Status-badge pill: colored text on a tinted translucent background.
    /// @p fg is the text color (e.g. teal/warning), the wash is 12% alpha.
    [[nodiscard]] static QString badgeStyle(const QColor& fg);
    /// Badge style wrapped in a Qt selector (e.g. "QLabel { … }"), for widgets
    /// whose instance QSS must override the global stylesheet.
    [[nodiscard]] static QString badgeStyle(const QColor& fg, const char* selector);
    /// Teal badge — attachment/slide-mode status (teal token family).
    [[nodiscard]] static QString tealBadgeStyle();
    /// Purple badge — cross-layer connection (piece-family purple, fixed hue).
    [[nodiscard]] static QString purpleBadgeStyle();
    /// Dim secondary value (tertiary text family, e.g. placeholder readouts).
    [[nodiscard]] static QString dimValueStyle();

private:
    static ThemeTokens s_tokens;
    static ThemeMode   s_mode;
};

} // namespace cad::ui
