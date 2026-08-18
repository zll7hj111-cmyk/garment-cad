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
    t.canvasBg     = QColor("#F6F3EC");   // pattern-paper white (drafting table)
    t.surface      = QColor("#FFFFFF");
    t.surface2     = QColor("#F3F4F6");
    t.surface3     = QColor("#F7F9FA");
    t.border       = QColor("#E4E7EC");
    t.borderStrong = QColor("#D0D5DD");

    t.text1 = QColor("#1D2129");
    t.text2 = QColor("#4D5766");   // darker step keeps text3 ≥ 4.5:1 on surface2
    t.text3 = QColor("#667085");   // WCAG AA on white (5.2:1) and surface2 (4.7:1)

    t.accent       = QColor("#2F6FED");
    t.accentStrong = QColor("#1E5FD6");
    t.accentTint   = QColor("#EAF2FE");
    t.onAccent     = QColor("#FFFFFF");

    // Piece palette — fabric-block hues for entity identity only.
    t.piece1 = QColor("#2F4259");   // deep navy (blocks, variables)
    t.piece2 = QColor("#3E5C76");   // slate blue (formulas)
    t.piece3 = QColor("#C85A3E");   // terracotta (measures)
    t.piece4 = QColor("#6A8D5F");   // muted olive (linked)

    // Semantic hues deepened so they pass WCAG AA as foreground on white.
    t.success = QColor("#15803D");  // 4.95:1 on white (was 3.3:1)
    t.warning = QColor("#B45309");  // 5.8:1  on white (was 3.2:1)
    t.danger  = QColor("#DC2626");  // 4.83:1 on white
    t.teal    = QColor("#0F766E");  // 4.9:1  on white (was 3.8:1)

    t.tooltipBg = QColor("#1D2129");
    t.tooltipFg = QColor("#F3F4F6");
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

    t.accent       = QColor("#4C8DFF");
    t.accentStrong = QColor("#6EA3FF");
    t.accentTint   = QColor("#1E2B42");
    t.onAccent     = QColor("#0A1420");   // deep ink on bright blue (5.2:1), was white 3.2:1

    // Piece palette — lighter fabric-block hues readable on dark surfaces.
    t.piece1 = QColor("#7E9CC0");
    t.piece2 = QColor("#93B3D9");
    t.piece3 = QColor("#E08F73");
    t.piece4 = QColor("#8FB58A");

    t.success = QColor("#34C77B");
    t.warning = QColor("#F0A94B");
    t.danger  = QColor("#F0655A");
    t.teal    = QColor("#2BB3A3");

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
   WildWind Pattern theme tail - ElaWidgetTools paints its own chrome,
   this file only styles the remaining plain-Qt widgets.
   ============================================================ */

QWidget {
    font-family: "Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI";
    font-size: 13px;
    color: @text1;
}

/* ── Frames & chrome ─────────────────────────────────────── */

QFrame#divider { background: @border; border: none; max-height: 1px; min-height: 1px; }
QFrame#accentBar { background: @accent; border: none; }

/* ── 分组头 / 组卡片: 悬停 + 拖放目标高亮 ───────────────
   拖拽公式卡悬停在分组头上时, [dropping] 属性置位由
   FormulaGroupHeader::setDropHighlight / GroupCard::setDropHighlight
   触发 (style()->unpolish/polish). 缺这两条规则 = 高亮完全不可见. */

QWidget#FormulaGroupHeader { background: transparent; border-radius: 4px; }
QWidget#FormulaGroupHeader:hover { background: @surface2; }
QWidget#FormulaGroupHeader[dropping="true"] {
    background: @accentTint; border: 1px solid @accent;
}

QFrame#GroupCard { background: transparent; border-radius: 4px; }
QFrame#GroupCard:hover { background: @surface2; }
QFrame#GroupCard[dropping="true"] {
    background: @accentTint; border: 1px solid @accent;
}

/* ── Semantic text on ElaText (QSS color beats forced palette) ── */

QLabel#mutedText   { color: @text2; }
QLabel#dimText     { color: @text3; }
QLabel#accentText  { color: @accent; font-weight: 600; padding-left: 8px; }
QLabel#dangerText  { color: @danger; font-weight: 500; }
QLabel#warningText { color: @warning; font-weight: 500; }
QLabel#successText { color: @success; }

QLabel#cardValue {
    font-family: 'Consolas','Courier New',monospace;
    font-size: 12px; font-weight: bold; background: transparent;
}
/* Type identity comes from the piece palette (fabric-block hues);
   semantic colors are reserved for status only (pattern-workbench rule). */
QLabel#cardValue[dangling="true"] { color: @danger; font-size: 11px; }
QLabel#cardIndex { font-size: 10px; font-weight: bold; color: @text3; }

QLabel#chipLabel {
    font-size: 12px; font-weight: 600; color: @text1;
    background: transparent; padding: 0 4px;
}
QLabel#chipLabel[placeholder="true"] {
    font-size: 11px; color: @text3;
}
QLabel#chipLabel[variant="ref"] {
    font-family: 'Consolas','Courier New',monospace; font-size: 11px;
    color: @accent;
}

/* ── Plain pickers (no Ela equivalent) ───────────────────── */

QListWidget {
    background: @surface; color: @text1;
    border: 1px solid @borderStrong; border-radius: 6px;
    outline: none;
}
QListWidget::item { padding: 4px 8px; border-radius: 6px; }
QListWidget::item:hover { background: @surface2; }
QListWidget::item:selected {
    background: @accentTint; color: @accentStrong;
}

/* ── Scrollbars (Ela scroll areas use ElaScrollBar; plain Qt leftovers) ── */

QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical {
    background: @borderStrong; border-radius: 4px; min-height: 24px;
}
QScrollBar::handle:vertical:hover { background: @text3; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle:horizontal {
    background: @borderStrong; border-radius: 4px; min-width: 24px;
}
QScrollBar::handle:horizontal:hover { background: @text3; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ── Tooltip ─────────────────────────────────────────────── */

QToolTip {
    background: @tooltipBg; color: @tooltipFg;
    border: none; border-radius: 4px;
    padding: 4px 8px; font-size: 12px;
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
    s.replace(QStringLiteral("@danger"),       t.danger.name());
    s.replace(QStringLiteral("@tooltipBg"),    t.tooltipBg.name());
    s.replace(QStringLiteral("@tooltipFg"),    t.tooltipFg.name());
    return s;
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

    // Base font: modern Windows UI face with CJK fallback.
    QFont f = QApplication::font();
    f.setFamilies({QStringLiteral("Segoe UI Variable Text"),
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
    pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    pal.setColor(QPalette::Link,            t.accent);
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
