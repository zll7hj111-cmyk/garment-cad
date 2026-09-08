#pragma once

#include <QColor>
#include <QString>

namespace cad::ui {

enum class ThemeMode { Light, Dark };

/// Central design-token table for the whole application chrome.
/// Single source of truth for the UI look: the QSS generator and the
/// application palette are both derived from these tokens. Canvas-side
/// tokens live in CanvasStyle and are kept in sync with these by hand
/// (same accent / semantic families) — that contract is enforced by
/// tests/test_theme_sync.cpp (2026-12 审计 P0-3 / G3); 改这里的令牌必须同步
/// src/canvas/CanvasStyle.cpp，否则守卫测试红。
///
/// Design language: Anthropic / Claude "Warm Editorial Minimalism" (数字手艺人工作台):
/// 温润象牙制版图纸面 (#FAF9F5 / #141413) + 炭墨深字 (#141413 / #ECE9E2) +
/// 陶土珊瑚色 (#CC785C / #D97757) 唯一受控主强调色。
/// 强调色用于选中、激活工具、焦点边框与主确认操作；文字在纯色强调色上统一为纯白 (#FFFFFF)。
/// 颜色三分层: 强调色(陶土珊瑚) → 类型色(piece 家族, 标识数据类别) → 语义色(莫斯绿/琥珀/砖红/青, 标识状态)。
/// 几何 = 1px 发丝线，功能圆角 2–4px，胶囊形数值输入。
struct ThemeTokens
{
    // ── Surfaces ──
    QColor canvasBg;      ///< Behind the graphics view (warm ivory paper / night slate paper).
    QColor surface;       ///< Panels, dialogs, toolbar, menus.
    QColor surface2;      ///< Recessed areas (card list background, hover).
    QColor surface3;      ///< Alternate card stripe (must differ from surface2).
    QColor border;        ///< Hairline borders / dividers (1px #E5E2DA).
    QColor borderStrong;  ///< Input borders, stronger separators (#D5D0C5).
    QColor chipBorder;    ///< 中灰微徽标/胶囊描边 (CopyChip, CompoundChip).

    // ── Text ──
    QColor text1;  ///< Primary text (near black ink #141413 / soft white #ECE9E2).
    QColor text2;  ///< Secondary text (warm gray #5C5850 / warm light gray #A39E93).
    QColor text3;  ///< Tertiary / placeholder / disabled (WCAG AA ≥ 4.5:1).

    // ── Accent (the ONLY decorative hue) ──
    QColor accent;        ///< 陶土珊瑚色 #CC785C / #D97757: selection, focus, active tool.
    QColor accentStrong;  ///< Pressed / darker step (#B8674D / #C46849).
    QColor accentTint;    ///< Light wash background for active states (12% / 18% alpha).
    QColor onAccent;      ///< Text on accent fills (纯白 #FFFFFF).

    // ── Piece palette (entity identity ONLY, never status) ──
    // ui-redesign-2026-08 §2.5 方案 A（用户拍板）: 卡片左竖线 = 卡片类型色
    // （变量=piece1 / 公式=piece2 / 测量=piece3 / 关联=piece4），替代旧
    // 蓝/橙行交替。竖线用量 4px，禁止整卡铺色。
    QColor piece1;  ///< 碳黑灰 — Block pieces, variable-type values.
    QColor piece2;  ///< 深青 — formula-type values.
    QColor piece3;  ///< 陶土橙 — measure-type values.
    QColor piece4;  ///< 钴蓝 — linked-type values.

    // ── Semantic (meaning only, never decoration) ──
    QColor success;  ///< Green: snap points, aux geometry, OK states.
    QColor warning;  ///< Amber: protected connections, non-fatal alerts.
    QColor danger;   ///< Red: errors, diagnostics.
    QColor teal;     ///< Attachment / connection rings.

    // ── Tooltips (inverted surfaces) ──
    QColor tooltipBg;
    QColor tooltipFg;
    QColor tooltipBorder;

    // ── Type scale (variable-font discipline: hierarchy by size) ──
    static constexpr int FontXs   = 10;  ///< meta labels, tags, badges
    static constexpr int FontSm   = 11;  ///< captions, source info
    static constexpr int FontMd   = 12;  ///< cards, inputs, secondary text
    static constexpr int FontBase = 13;  ///< body (app default)
    static constexpr int FontLg   = 15;  ///< 卡片数值读数 (15px Semibold+Mono, 卡片第一视觉焦点)
    static constexpr int FontXl   = 18;  ///< 对话框标题、空状态主文案 (Semibold)

    // ── Radius scale (Endfield: 默认 0, 功能圆角 2–4px; 输入框统一纯胶囊) ──
    static constexpr int RadiusXs      = 0;    ///< chips, inline marks (squared)
    static constexpr int RadiusSm      = 2;    ///< tags, scrollbars
    static constexpr int RadiusMd      = 2;    ///< buttons, menu items, rows
    static constexpr int RadiusLg      = 4;    ///< cards, group boxes (功能圆角上限)
    static constexpr int RadiusBadge   = 4;    ///< dock, badges (原 RadiusPill 更名,
                                               ///< ui-redesign §07: 值恒 4px,「胶囊」
                                               ///< 命名与禁胶囊纪律自相矛盾)
    static constexpr int RadiusCapsule = 999;  ///< capsule inputs/chips (height/2 pill)

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

    /// QSS rgba() string from a theme token + alpha fraction (审计 P0-1:
    /// 消灭散落的 `rgba(220,38,38,32)` 类字面量，alpha 0.125 → 32)。
    [[nodiscard]] static QString rgbaCss(const QColor& c, double alpha);

private:
    static ThemeTokens s_tokens;
    static ThemeMode   s_mode;
};

} // namespace cad::ui
