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
    t.canvasBg     = QColor("#FAF9F5");   // Warm ivory drafting paper
    t.surface      = QColor("#FFFFFF");   // Pure paper
    t.surface2     = QColor("#F5F3EB");   // Warm sand (recessed terminal / card background)
    t.surface3     = QColor("#EFECE1");   // Deep sand
    t.border       = QColor("#E5E2DA");   // 1px hairline border
    t.borderStrong = QColor("#D5D0C5");   // Input & active borders
    t.chipBorder   = QColor("#C2BCB0");   // Medium warm gray chip outline

    t.text1 = QColor("#141413");   // Near black ink
    t.text2 = QColor("#5C5850");   // Warm gray secondary
    t.text3 = QColor("#8C877D");   // Muted tertiary / disabled

    t.accent       = QColor("#CC785C");   // Terracotta Coral
    t.accentStrong = QColor("#B8674D");   // Pressed terracotta step
    t.accentTint   = QColor("#F9EFEB");   // Pre-blended 12% terracotta wash on warm ivory paper
    t.onAccent     = QColor("#FFFFFF");   // Pure white text on solid terracotta

    // Piece palette — fabric-block hues for entity identity only.
    t.piece1 = QColor("#383530");   // Charcoal slate (variable-type values)
    t.piece2 = QColor("#2E6B65");   // Muted pine / sage teal (formula-type values)
    t.piece3 = QColor("#C46849");   // Terracotta clay (measure-type values)
    t.piece4 = QColor("#3D5A80");   // Muted indigo (linked-type values)

    // Semantic hues
    t.success = QColor("#3E8966");  // Craftsman moss green
    t.warning = QColor("#D48B38");  // Amber ochre
    t.danger  = QColor("#C94A4A");  // Brick red
    t.teal    = QColor("#2A7B88");  // Cyan connection / attachment rings

    // Tooltips (温润象牙图纸面规范，高对比度炭黑墨字与淡暖发丝边框)
    t.tooltipBg     = QColor("#FAF9F5");
    t.tooltipFg     = QColor("#141413");
    t.tooltipBorder = QColor("#D5D0C5");
    return t;
}

ThemeTokens ThemeTokens::dark()
{
    ThemeTokens t;
    t.canvasBg     = QColor("#141413");   // Carbon slate night paper
    t.surface      = QColor("#1F1E1D");   // Deep charcoal
    t.surface2     = QColor("#262422");   // Muted ink
    t.surface3     = QColor("#2E2B28");   // Charcoal layer
    t.border       = QColor("#383531");   // 1px subdued
    t.borderStrong = QColor("#4D4943");   // Stronger separator
    t.chipBorder   = QColor("#5A554E");

    t.text1 = QColor("#ECE9E2");   // Soft white
    t.text2 = QColor("#A39E93");   // Warm light gray
    t.text3 = QColor("#706C63");   // Muted

    t.accent       = QColor("#D97757");   // Terracotta Coral in dark
    t.accentStrong = QColor("#C46849");
    t.accentTint   = QColor("#382721");   // Pre-blended 18% terracotta wash on carbon slate night paper
    t.onAccent     = QColor("#FFFFFF");

    // Piece palette in dark
    t.piece1 = QColor("#A8A29E");
    t.piece2 = QColor("#4D9088");
    t.piece3 = QColor("#E08466");
    t.piece4 = QColor("#6E88A8");

    t.success = QColor("#52AB7F");
    t.warning = QColor("#E2A04A");
    t.danger  = QColor("#E06060");
    t.teal    = QColor("#3BA0B0");

    // Tooltips (暗色主题下的炭黑墨面)
    t.tooltipBg     = QColor("#1F1E1D");
    t.tooltipFg     = QColor("#ECE9E2");
    t.tooltipBorder = QColor("#4D4943");
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

QString Theme::rgbaCss(const QColor& c, double alpha)
{
    const int a = static_cast<int>(alpha * 255.0 + 0.5);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(qBound(0, a, 255));
}

QString Theme::badgeStyle(const QColor& fg)
{
    QColor wash = fg;
    wash.setAlphaF(0.12f);
    QColor border = fg;
    border.setAlphaF(0.28f);
    // background-color (not the shorthand) so the same string also works as
    // inline CSS inside QTextDocument rich text (SegmentAuxTab HTML badges).
    return QStringLiteral(
               "color:%1; background-color:rgba(%2,%3,%4,%5); border:1px solid rgba(%2,%3,%4,%6);"
               " border-radius:2px; padding:1px 5px; font-size:10px; font-weight:600;")
        .arg(fg.name(),
             QString::number(wash.red()), QString::number(wash.green()),
             QString::number(wash.blue()),
             QString::number(wash.alpha()),
             QString::number(border.alpha()));
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
    // Cross-layer badge hue — muted slate violet, aligned with warm editorial palette.
    return badgeStyle(QColor(QStringLiteral("#6B5B88")));
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

    // Base font: Editorial clean sans + CJK fallback (safe stack).
    QFont f = QApplication::font();
    f.setFamilies({QStringLiteral("Segoe UI Variable Text"),
                   QStringLiteral("Segoe UI"),
                   QStringLiteral("Inter"),
                   QStringLiteral("Noto Sans SC"),
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
    pal.setColor(QPalette::HighlightedText, t.onAccent);  // 陶土珊瑚配纯白字
    pal.setColor(QPalette::Link,            t.accent);    // 陶土珊瑚链接色
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
