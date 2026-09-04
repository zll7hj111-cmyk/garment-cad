#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QWidget>
#include <QToolTip>
#include <QHelpEvent>

#include "ElaTheme.h"

namespace cad::ui {

namespace {

/// 全局 ToolTip 防护过滤器：
/// Qt 默认的 QToolTip::showText(..., this, rect) 会将触发控件作为 QTipLabel 的父级，
/// 导致该控件及其祖先树上的任何无选择器裸 setStyleSheet（如含有 background: transparent）
/// 级联污染 QTipLabel，使其在 Windows DWM 下回退合成出纯黑底色大框。
/// 本过滤器拦截全部 QEvent::ToolTip，通过将关联上下文转为全局屏幕坐标 + nullptr 宿主，
/// 阻断子树样式的向下渗透，确保 QTipLabel 纯净继承全局 QToolTip 纸黄色技术样式。
class ToolTipGuard : public QObject
{
public:
    static void install()
    {
        static ToolTipGuard* s_guard = nullptr;
        if (!s_guard && QApplication::instance()) {
            s_guard = new ToolTipGuard(QApplication::instance());
            QApplication::instance()->installEventFilter(s_guard);
        }
    }

private:
    explicit ToolTipGuard(QObject* parent) : QObject(parent) {}

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(event);
            auto* w = qobject_cast<QWidget*>(watched);
            if (w && !w->toolTip().isEmpty()) {
                const QRect globalRect(w->mapToGlobal(QPoint(0, 0)), w->size());
                QToolTip::showText(he->globalPos(), w->toolTip(), nullptr, globalRect);
                event->accept();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

ThemeTokens Theme::s_tokens = ThemeTokens::light();
ThemeMode   Theme::s_mode   = ThemeMode::Light;

// ---------------------------------------------------------------------------
// Token sets
// ---------------------------------------------------------------------------

ThemeTokens ThemeTokens::light()
{
    ThemeTokens t;
    t.canvasBg     = QColor("#ECEFF2");   // Endfield blueprint / industrial light concrete
    t.surface      = QColor("#FFFFFF");
    t.surface2     = QColor("#E2E6EC");   // Recessed terminal surfaces
    t.surface3     = QColor("#EAEDF2");
    t.border       = QColor("#CBD2DC");   // Clean technical hairline
    t.borderStrong = QColor("#1A202C");   // High-contrast carbon border
    t.chipBorder   = QColor("#9AA4B2");   // Medium gray chip outline (visible on paper)

    t.text1 = QColor("#0D1117");   // Deep carbon black
    t.text2 = QColor("#4A5568");   // Industrial slate secondary
    t.text3 = QColor("#718096");   // Technical metadata

    t.accent       = QColor("#FFDE00");   // Endfield Action Lemon Yellow
    t.accentStrong = QColor("#E6C600");
    t.accentTint   = QColor("#FFFAD1");
    t.onAccent     = QColor("#0D1117");   // Solid black text on yellow

    // Piece palette — fabric-block hues for entity identity only.
    t.piece1 = QColor("#1E293B");   // deep carbon slate
    t.piece2 = QColor("#0F766E");   // deep cyan / teal
    t.piece3 = QColor("#C85A3E");   // terracotta / orange
    t.piece4 = QColor("#2563EB");   // cobalt / linked

    // Semantic hues deepened so they pass WCAG AA as foreground on white.
    t.success = QColor("#15803D");  // 4.95:1 on white (was 3.3:1)
    t.warning = QColor("#B45309");  // 5.8:1  on white (was 3.2:1)
    t.danger  = QColor("#DC2626");  // 4.83:1 on white
    t.teal    = QColor("#0284C7");  // cyan connection

    // Row alternation bars removed (ui-redesign-2026-08 §2.5 方案 A):
    // card accent bars now carry the piece type color — see CardBase.

    // Tooltips (纸黄色工程图纸面规范，高对比度深碳黑文字与暖琥珀细边框)
    t.tooltipBg     = QColor("#FFFAD1");
    t.tooltipFg     = QColor("#1A202C");
    t.tooltipBorder = QColor("#D8CC80");
    return t;
}

ThemeTokens ThemeTokens::dark()
{
    ThemeTokens t;
    t.canvasBg     = QColor("#14181E");   // night paper, not dead black
    t.surface      = QColor("#1D2126");
    t.surface2     = QColor("#23282E");
    t.surface3     = QColor("#272D34");
    t.border       = QColor("#333A42");
    t.borderStrong = QColor("#444D57");
    t.chipBorder   = QColor("#4E5866");   // Medium gray chip outline (one step brighter than dark surface)

    t.text1 = QColor("#E8EAED");
    t.text2 = QColor("#9AA3AD");
    t.text3 = QColor("#8A94A0");   // WCAG AA on surface2 (4.7:1), was 2.9-3.9:1

    t.accent       = QColor("#FFE600");   // Endfield Action Lemon Yellow in dark
    t.accentStrong = QColor("#FFF04D");
    t.accentTint   = QColor("#2D2B10");
    t.onAccent     = QColor("#0D1117");   // deep ink on bright yellow

    // Piece palette — same hue as light(), only raised lightness (§2.3/§2.6):
    // 深青 #0F766E → #2DD4BF, 钴蓝 #2563EB → #60A5FA, 碳灰 #1E293B → #94A3B8,
    // 陶土保持同相 #E08F73。「公式 = 青」跨模式记忆一致, 不再换组色相。
    t.piece1 = QColor("#94A3B8");
    t.piece2 = QColor("#2DD4BF");
    t.piece3 = QColor("#E08F73");
    t.piece4 = QColor("#60A5FA");

    t.success = QColor("#34C77B");
    t.warning = QColor("#F0A94B");
    t.danger  = QColor("#F0655A");
    t.teal    = QColor("#2BB3A3");

    // Tooltips (暗色主题下的暖调纸面)
    t.tooltipBg     = QColor("#28251C");
    t.tooltipFg     = QColor("#FFF8DB");
    t.tooltipBorder = QColor("#6B5E38");
    return t;
}

// ---------------------------------------------------------------------------
// Stylesheet generation (global design tail)
// ---------------------------------------------------------------------------
//
// 2026-08 教训：这份"全局样式表"从写下第一天起就因 Theme::apply 里的
// qobject_cast<QWidget*> 失败而从未安装（见 TROUBLESHOOTING「全局样式表从未
// 被安装」条目）——全程序的视觉实际是各控件的实例级 setStyleSheet 调出来的。
// 修复安装后全部规则"苏醒"：程序整体走全局设计样式（用户拍板），但
// **ContextStrip 编辑条带例外** —— 它的控件曾按"全局规则不存在"精调
// （11px 实例字号），苏醒的 QWidget 兜底字号把标签放大变丑。豁免方式 =
// 条带内部 objectName 改用 strip 前缀（stripSerial/stripField/stripNote，
// 不命中全局 ID 规则）+ 无实例样式的标签补 font-size:11px 实例钉死 +
// editBand 改名（黄底规则失配）。全局规则今后新增时注意勿命中 strip 前缀。

QString Theme::buildStylesheet(const ThemeTokens& t)
{
    QString s = QStringLiteral(R"QSS(
/* ============================================================
   WildWind Pattern theme tail - Endfield Industrial CAD Style
   （条带 ContextStrip 已豁免：其内部 objectName 均为 strip* 前缀，
     不会命中下方任何 ID/类规则；QWidget 兜底字号被各控件实例钉回）
   ============================================================ */

QWidget {
    font-family: "Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI";
    font-size: 13px;
    color: @text1;
}

/* ── Frames & chrome ─────────────────────────────────────── */

QFrame#divider { background: @border; border: none; max-height: 1px; min-height: 1px; }
QFrame#accentBar { background: @accent; border: none; }

/* ── 线条属性面板端点徽章 (PANEL_REDESIGN §10.5): P#/P# 墨底黄字, 串号徽章同语言 ── */
ElaText#endpointBadge {
    background-color: @text1; color: @accent; font-weight: 600; padding: 1px 6px;
}

/* ── 端点组朝向轴: 1px 虚线竖线, 连接 P1/P2 徽章 (纯装饰, QSS 统一管理主题色) ── */
QFrame#endpointAxis {
    background: transparent; border: none;
    border-left: 1px dashed @borderStrong;
}

/* ── 编辑条带 / 串号徽章: 死规则已删 (2026-08-31) ──────────
   曾为 editBand / serialBadge 留档的全局规则已删除: 条带对象
   已改 stripBand / stripSerial 豁免, 规则永不命中 = 死代码。
   豁免约定见上方头注释; 新增全局规则勿命中 strip* 前缀。 */

/* ── 上下文属性条连接角度会话 (二期): 公式解析失败 → 角度框红边提示 ── */
QLineEdit#angleEdit[angleInvalid="true"] {
    border: 1px solid @danger;
}

/* ── 分组头: 悬停 + 拖放目标高亮 ─────────────── */
QWidget#FormulaGroupHeader { background: transparent; border-radius: 2px; }
QWidget#FormulaGroupHeader:hover { background: @surface2; }
QWidget#FormulaGroupHeader[dropping="true"] {
    background: @accentTint; border: 1px solid @accent;
}

/* ── Semantic text on ElaText (QSS color beats forced palette) ── */

QLabel#mutedText   { color: @text2; font-family: 'Consolas','Courier New',monospace; }
QLabel#dimText     { color: @text3; }
QLabel#accentText  { color: @text1; font-weight: 600; padding-left: 8px; font-family: 'Consolas','Courier New',monospace; }
QLabel#dangerText  { color: @danger; font-weight: 500; }
QLabel#warningText { color: @warning; font-weight: 500; }
QLabel#successText { color: @success; }

QLabel#cardValue {
    font-family: 'Consolas','Courier New',monospace;
    font-size: 12px; font-weight: bold; background: transparent;
}
QLabel#cardValue[dangling="true"] { color: @danger; font-size: 11px; }
QLabel#cardIndex { font-size: 10px; font-weight: bold; color: @text3; font-family: 'Consolas','Courier New',monospace; }

QLabel#chipLabel {
    font-size: 12px; font-weight: 600; color: @text1;
    background: transparent; padding: 0 4px;
}
QLabel#chipLabel[placeholder="true"] {
    font-size: 11px; color: @text3;
}
QLabel#chipLabel[variant="ref"] {
    font-family: 'Consolas','Courier New',monospace; font-size: 11px;
    color: @text1;
}

/* ── Plain pickers (no Ela equivalent) ───────────────────── */

QListWidget {
    background: @surface; color: @text1;
    border: 1px solid @borderStrong; border-radius: 2px;
    outline: none;
}
QListWidget::item { padding: 4px 8px; border-radius: 2px; }
QListWidget::item:hover { background: @surface2; }
QListWidget::item:selected {
    background: @accent; color: @onAccent; font-weight: bold;
}

/* ── Scrollbars ─────────────────────────────────────────── */

QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }
QScrollBar::handle:vertical {
    background: @borderStrong; border-radius: 2px; min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: @text2; }
QScrollBar:horizontal { background: transparent; height: 6px; margin: 0; }
QScrollBar::handle:horizontal {
    background: @borderStrong; border-radius: 2px; min-width: 24px;
}
QScrollBar::handle:horizontal:hover { background: @text2; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ── SpinBoxes (统一移除上下步进箭头) ─────────────────────── */
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    width: 0px; height: 0px; border: none;
}

/* ── Tooltip (Endfield 2.0 统一反转技术墨面) ────────────────
   颜色约定：统一深色反转技术面，圆角 4px，技术描边，文字 11px */

QToolTip {
    background-color: @tooltipBg;
    background: @tooltipBg;
    color: @tooltipFg;
    border: 1px solid @tooltipBorder;
    border-radius: 4px;
    padding: 5px 9px;
    font-size: 11px;
}
QLabel#qtooltip_label {
    background-color: @tooltipBg;
    background: @tooltipBg;
    color: @tooltipFg;
    border: 1px solid @tooltipBorder;
    border-radius: 4px;
}
)QSS");

    // 坑: @tooltipFg 曾漏替换 → QSS 颜色声明非法, 文字取回调色板深色,
    // 叠加 @tooltipBg 黑底 → "提示纯黑、字完全看不清" (用户 2026-12 反馈)。
    s.replace(QStringLiteral("@tooltipBorder"), t.tooltipBorder.name());
    s.replace(QStringLiteral("@tooltipFg"),     t.tooltipFg.name());
    s.replace(QStringLiteral("@tooltipBg"),     t.tooltipBg.name());
    s.replace(QStringLiteral("@surface2"),     t.surface2.name());  // before @surface
    s.replace(QStringLiteral("@surface"),      t.surface.name());
    s.replace(QStringLiteral("@borderStrong"), t.borderStrong.name());  // before @border
    s.replace(QStringLiteral("@border"),       t.border.name());
    s.replace(QStringLiteral("@text1"),        t.text1.name());
    s.replace(QStringLiteral("@text2"),        t.text2.name());
    s.replace(QStringLiteral("@text3"),        t.text3.name());
    s.replace(QStringLiteral("@accentTint"),   t.accentTint.name());    // before @accent
    s.replace(QStringLiteral("@accent"),       t.accent.name());
    s.replace(QStringLiteral("@onAccent"),     t.onAccent.name());
    s.replace(QStringLiteral("@danger"),       t.danger.name());
    return s;
}

// ---------------------------------------------------------------------------
// Badge / dim-text stylesheets
// ---------------------------------------------------------------------------
//
// Badge pills: colored text on a 12% alpha tinted wash of the same hue, so
// the pill reads correctly in both light and dark modes without hardcoded
// hex pairs. Replaces the copy-pasted #0F766E/#E6F4F2 and #8e44ad/#f3e8ff
// literals in SegmentConnectionCardBuild / SegmentAuxTab.

QString Theme::badgeStyle(const QColor& fg)
{
    QColor wash = fg;
    wash.setAlphaF(0.12f);
    // background-color (not the shorthand) so the same string also works as
    // inline CSS inside QTextDocument rich text (SegmentAuxTab HTML badges).
    return QStringLiteral(
               "color:%1; background-color:rgba(%2,%3,%4,%5); border-radius:3px;"
               "padding:0 4px; font-size:11px;")
        .arg(fg.name(),
             QString::number(wash.red()), QString::number(wash.green()),
             QString::number(wash.blue()),
             QString::number(wash.alpha()));
}

QString Theme::badgeStyle(const QColor& fg, const char* selector)
{
    return QStringLiteral("%1 { %2 }")
        .arg(QString::fromUtf8(selector), badgeStyle(fg));
}

QString Theme::tealBadgeStyle()
{
    return badgeStyle(tokens().teal);
}

QString Theme::purpleBadgeStyle()
{
    // Cross-layer badge hue — piece-family purple, fixed across themes.
    return badgeStyle(QColor(QStringLiteral("#8e44ad")));
}

QString Theme::dimValueStyle()
{
    // Secondary readout: tertiary text family (QLabel is naturally transparent).
    return QStringLiteral("color:%1; font-size:11px;")
        .arg(tokens().text3.name());
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

const ThemeTokens& Theme::tokens() { return s_tokens; }

ThemeMode Theme::mode() { return s_mode; }

void Theme::apply(ThemeMode mode)
{
    s_tokens = (mode == ThemeMode::Dark) ? ThemeTokens::dark()
                                         : ThemeTokens::light();
    s_mode   = mode;

    // Fusion gives a consistent cross-platform base that QSS can fully restyle.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Base font: Endfield technical Latin + CJK fallback (safe stack).
    QFont f = QApplication::font();
    f.setFamilies({QStringLiteral("IBM Plex Sans"),
                   QStringLiteral("Space Grotesk"),
                   QStringLiteral("Noto Sans SC"),
                   QStringLiteral("Segoe UI Variable Text"),
                   QStringLiteral("Segoe UI"),
                   QStringLiteral("Microsoft YaHei UI")});
    f.setPointSizeF(9.5);
    QApplication::setFont(f);

    // Palette covers the bits QSS does not (native popups, message boxes).
    const ThemeTokens& t = s_tokens;
    QPalette pal;
    pal.setColor(QPalette::Window,          t.surface);
    pal.setColor(QPalette::WindowText,      t.text1);
    pal.setColor(QPalette::Base,            t.surface);
    pal.setColor(QPalette::AlternateBase,   t.surface2);
    pal.setColor(QPalette::Text,            t.text1);
    pal.setColor(QPalette::Button,          t.surface);
    pal.setColor(QPalette::ButtonText,      t.text1);
    pal.setColor(QPalette::BrightText,      t.surface);
    pal.setColor(QPalette::Highlight,       t.accent);
    pal.setColor(QPalette::HighlightedText, t.onAccent);  // 黄底配碳黑字（信号黄不作文字底色）
    pal.setColor(QPalette::Link,            t.text1);  // 黄链接白底不可读 → 墨字
    pal.setColor(QPalette::PlaceholderText, t.text3);
    pal.setColor(QPalette::ToolTipBase,     t.tooltipBg);
    pal.setColor(QPalette::ToolTipText,     t.tooltipFg);
    pal.setColor(QPalette::Mid,             t.border);
    pal.setColor(QPalette::Light,           t.surface2);
    const QPalette::ColorGroup disabled[]{QPalette::Disabled, QPalette::Inactive};
    for (QPalette::ColorGroup g : disabled) {
        pal.setColor(g, QPalette::WindowText,  t.text3);
        pal.setColor(g, QPalette::Text,        t.text3);
        pal.setColor(g, QPalette::ButtonText,  t.text3);
        pal.setColor(g, QPalette::ToolTipBase, t.tooltipBg);
        pal.setColor(g, QPalette::ToolTipText, t.tooltipFg);
    }
    QApplication::setPalette(pal);

    // QApplication::setPalette() snapshots the new palette only into widgets
    // that re-polish afterwards; widgets polished under the previous palette
    // keep resolving the stale colors (QWidget::palette() caches on polish).
    // Propagate explicitly so every live widget (panels, plain pages) adopts
    // the new theme immediately.
    for (QWidget* w : QApplication::allWidgets())
        w->setPalette(pal);

    // Drive the ElaWidgetTools theme in lockstep so both systems stay in sync.
    if (auto* theme = ElaTheme::getInstance())
        theme->setThemeMode(mode == ThemeMode::Dark ? ElaThemeType::Dark
                                                    : ElaThemeType::Light);

    // App-level stylesheet: QApplication has its own setStyleSheet member
    // (qapplication.h) — NOT a QWidget cast! The old
    // qobject_cast<QWidget*>(QApplication::instance()) always returned null
    // (QApplication derives from QGuiApplication, not QWidget), so the global
    // stylesheet was NEVER installed: QToolTip/QListWidget/dimText rules were
    // silently dead. Tooltips then fell back to the palette/native path —
    // inside the translucent ElaDialog they rendered as an unreadable black
    // box (user report 2026-08). Cast via QApplication instead.
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance()))
        app->setStyleSheet(buildStylesheet(s_tokens));

    // 全局安装 ToolTip 过滤器，隔离所有局部控件裸样式对悬浮提示的穿透
    ToolTipGuard::install();
}

} // namespace cad::ui
