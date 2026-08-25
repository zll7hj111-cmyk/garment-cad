#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QWidget>

#include "ElaTheme.h"

namespace cad::ui {

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

    // Row alternation: fixed by user decision (蓝/橙交替), same in both modes.
    t.rowEven = QColor("#2F6FED");
    t.rowOdd  = QColor("#F59E0B");

    t.tooltipBg = QColor("#0D1117");
    t.tooltipFg = QColor("#F8FAFC");
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

    t.text1 = QColor("#E8EAED");
    t.text2 = QColor("#9AA3AD");
    t.text3 = QColor("#8A94A0");   // WCAG AA on surface2 (4.7:1), was 2.9-3.9:1

    t.accent       = QColor("#FFE600");   // Endfield Action Lemon Yellow in dark
    t.accentStrong = QColor("#FFF04D");
    t.accentTint   = QColor("#2D2B10");
    t.onAccent     = QColor("#0D1117");   // deep ink on bright yellow

    // Piece palette — lighter fabric-block hues readable on dark surfaces.
    t.piece1 = QColor("#7E9CC0");
    t.piece2 = QColor("#93B3D9");
    t.piece3 = QColor("#E08F73");
    t.piece4 = QColor("#8FB58A");

    t.success = QColor("#34C77B");
    t.warning = QColor("#F0A94B");
    t.danger  = QColor("#F0655A");
    t.teal    = QColor("#2BB3A3");

    // Row alternation: fixed by user decision (蓝/橙交替), same in both modes.
    t.rowEven = QColor("#2F6FED");
    t.rowOdd  = QColor("#F59E0B");

    t.tooltipBg = QColor("#E8EAED");
    t.tooltipFg = QColor("#1D2126");
    return t;
}

// ---------------------------------------------------------------------------
// Stylesheet generation (ElaTheme-driven tail)
// ---------------------------------------------------------------------------
//
// ElaWidgetTools paints its own widgets from ElaTheme colors, so the global
// stylesheet is reduced to a small tail for the few plain-Qt widgets that
// remain in the app (QListWidget pickers, dividers, tooltips) plus the
// semantic-text object-name rules that ElaText's transparent instance style
// does not override (QSS color wins over ElaText's forced palette).

QString Theme::buildStylesheet(const ThemeTokens& t)
{
    QString s = QStringLiteral(R"QSS(
/* ============================================================
   WildWind Pattern theme tail - Endfield Industrial CAD Style
   ============================================================ */

QWidget {
    font-family: "Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI";
    font-size: 13px;
    color: @text1;
}

/* ── Frames & chrome ─────────────────────────────────────── */

QFrame#divider { background: @border; border: none; max-height: 1px; min-height: 1px; }
QFrame#accentBar { background: @accent; border: none; }

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

/* ── Tooltip ─────────────────────────────────────────────── */

QToolTip {
    background: @tooltipBg; color: @tooltipFg;
    border: 1px solid @borderStrong; border-radius: 2px;
    padding: 4px 8px; font-size: 12px;
    font-family: 'Consolas','Courier New',monospace;
}
)QSS");

    s.replace(QStringLiteral("@surface2"),     t.surface2.name());  // before @surface
    s.replace(QStringLiteral("@surface"),      t.surface.name());
    s.replace(QStringLiteral("@borderStrong"), t.borderStrong.name());  // before @border
    s.replace(QStringLiteral("@border"),       t.border.name());
    s.replace(QStringLiteral("@text1"),        t.text1.name());
    s.replace(QStringLiteral("@text2"),        t.text2.name());
    s.replace(QStringLiteral("@text3"),        t.text3.name());
    s.replace(QStringLiteral("@accentStrong"), t.accentStrong.name());  // before @accent
    s.replace(QStringLiteral("@accentTint"),   t.accentTint.name());    // before @accent
    s.replace(QStringLiteral("@accent"),       t.accent.name());
    s.replace(QStringLiteral("@onAccent"),     t.onAccent.name());
    s.replace(QStringLiteral("@danger"),       t.danger.name());
    s.replace(QStringLiteral("@tooltipBg"),    t.tooltipBg.name());
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
    // Secondary readout: tertiary text family on transparent background.
    return QStringLiteral("color:%1; font-size:11px; background:transparent;")
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
        pal.setColor(g, QPalette::WindowText, t.text3);
        pal.setColor(g, QPalette::Text,       t.text3);
        pal.setColor(g, QPalette::ButtonText, t.text3);
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

    // setStyleSheet is a QWidget member — call through the app instance.
    if (auto* app = qobject_cast<QWidget*>(QApplication::instance()))
        app->setStyleSheet(buildStylesheet(s_tokens));
}

} // namespace cad::ui
