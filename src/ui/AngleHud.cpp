#include "AngleHud.h"

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"

#include <QGraphicsView>
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QKeyEvent>
#include <QEvent>

namespace cad::tools {

AngleHud::AngleHud(QWidget* viewport)
    : QWidget(viewport)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    // Palette comes from the owning canvas scene's CanvasStyle so the HUD
    // follows the active theme (pattern-workbench rule: no hardcoded colors).
    QColor hudBg = QColor(30, 38, 46, 235);
    QColor hudFg = QColor(154, 163, 173);
    QColor hudBorder = QColor(255, 255, 255, 35);
    QColor validCol = QColor(43, 179, 163);   // teal family
    QColor invalidCol = QColor(240, 101, 90); // danger family
    if (auto* view = qobject_cast<QGraphicsView*>(viewport ? viewport->parentWidget() : nullptr)) {
        if (auto* cs = qobject_cast<CanvasScene*>(view->scene())) {
            hudBg = cs->style()->hudBackground;
            hudFg = cs->style()->hudText;
            hudBorder = cs->style()->crosshairColor;
            validCol = cs->style()->snapPointColor;
            invalidCol = cs->style()->snapIndicatorColor;
        }
    }

    // Mode toggle button (compact, acts as caption + switch).
    m_btnToggle = new ElaPushButton(this);
    m_btnToggle->setCursor(Qt::PointingHandCursor);
    m_btnToggle->setFixedHeight(22);
    m_btnToggle->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;"
        "border:1px solid %3;border-radius:4px;"
        "font-size:11px;padding:2px 6px;}")
        .arg(hudBg.name(QColor::HexArgb), validCol.name(), hudBorder.name(QColor::HexArgb)));

    m_lblCaption = new ElaText(QString(), 13, this);
    m_edit = new ElaLineEdit(this);
    m_edit->setFixedWidth(150);
    m_lblUnit = new ElaText(QString(), 13, this);

    layout->addWidget(m_btnToggle);
    layout->addWidget(m_lblCaption);
    layout->addWidget(m_edit);
    layout->addWidget(m_lblUnit);

    // Floating overlay look (AutoCAD dynamic-input style) — theme-aware.
    setStyleSheet(QStringLiteral(
        "AngleHud{background:%1;"
        "border:1px solid %2;border-radius:8px;}"
        "QLabel{color:%3;font-size:12px;}")
        .arg(hudBg.name(QColor::HexArgb), hudBorder.name(QColor::HexArgb),
             hudFg.name()));
    setValid(true);
    applyModeVisuals();

    // Toggle mode on button click.
    connect(m_btnToggle, &QPushButton::clicked, [this] {
        m_mode = (m_mode == cad::param::RotationMode::Angle)
                     ? cad::param::RotationMode::ArcLength
                     : cad::param::RotationMode::Angle;
        applyModeVisuals();
        if (onModeChanged) onModeChanged(m_mode);
    });

    // Esc must work no matter which child holds keyboard focus.
    m_edit->installEventFilter(this);
    installEventFilter(this);
    connect(m_edit, &QLineEdit::returnPressed,
            [this] { if (onCommit) onCommit(); });
    connect(m_edit, &QLineEdit::textChanged,
            [this](const QString& t) { if (onTextChanged) onTextChanged(t); });
}

void AngleHud::setMode(cad::param::RotationMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    applyModeVisuals();
}

void AngleHud::setCaption(const QString& text)
{
    if (m_captionOverride == text) return;
    m_captionOverride = text;
    applyModeVisuals();
}

void AngleHud::applyModeVisuals()
{
    if (m_mode == cad::param::RotationMode::Angle) {
        m_btnToggle->setText(QStringLiteral("\xe2\x88\xa0"));  // ∠
        // 默认标签 = 跟随角度（跟随线相对基准线的夹角）；自由线场景由
        // ToolRotate 通过 setCaption 覆盖为“绝对角度”/“相对角度”。
        m_lblCaption->setText(m_captionOverride.isEmpty()
            ? QString::fromUtf8("跟随角度") : m_captionOverride);
        m_lblUnit->setText(QStringLiteral("\xc2\xb0"));  // °
        m_edit->setPlaceholderText(QStringLiteral(
            "\xe5\xba\xa6\xe6\x95\xb0\xe6\x88\x96\xe5\x85\xac\xe5\xbc\x8f\xef\xbc\x8c\xe5\xa6\x82 b/4+5"));  // 度数或公式，如 b/4+5
    } else {
        m_btnToggle->setText(QStringLiteral("\xe2\x8c\x92"));  // ⌒
        m_lblCaption->setText(QStringLiteral("\xe5\xbc\xa7\xe9\x95\xbf"));  // 弧长
        m_lblUnit->setText(QStringLiteral("cm"));
        m_edit->setPlaceholderText(QStringLiteral(
            "\xe9\x95\xbf\xe5\xba\xa6\xe6\x88\x96\xe5\x85\xac\xe5\xbc\x8f\xef\xbc\x8c\xe5\xa6\x82 sleeve/2"));  // 长度或公式，如 sleeve/2
    }
}

void AngleHud::setValid(bool ok)
{
    // Theme-aware valid/invalid colors (from the owning canvas scene).
    QColor validCol = QColor(43, 179, 163);
    QColor invalidCol = QColor(240, 101, 90);
    QColor editBg = QColor(255, 255, 255, 245);
    QColor editFg = QColor(232, 234, 237);
    // NOTE: this widget's parent IS the viewport (a plain QWidget), so the
    // scene must be reached via viewport->parentWidget() — casting the parent
    // directly never matches, silently falling back to the hardcoded light
    // gray text that was unreadable in BOTH themes (用户报告 2026-08).
    if (auto* view = qobject_cast<QGraphicsView*>(
            parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
        if (auto* cs = qobject_cast<CanvasScene*>(view->scene())) {
            validCol = cs->style()->snapPointColor;
            invalidCol = cs->style()->snapIndicatorColor;
            editBg = cs->style()->hudBackground.lighter(115);
            editFg = cs->style()->hudText;
        }
    }
    m_edit->setStyleSheet(ok
        ? QStringLiteral("QLineEdit{border:1px solid %1;border-radius:4px;"
                         "padding:3px 6px;background:%2;"
                         "color:%3;selection-background-color:%1;}")
              .arg(validCol.name(), editBg.name(QColor::HexArgb), editFg.name())
        : QStringLiteral("QLineEdit{border:1px solid %1;border-radius:4px;"
                         "padding:3px 6px;background:%2;color:%1;}")
              .arg(invalidCol.name(), errorWash(invalidCol)));
}

// Danger wash used by the invalid-state edit background. Same 12% alpha
// tinting rule as Theme::badgeStyle, kept here because the hue itself comes
// from CanvasStyle (canvas-side tokens), not the UI token table.
QString AngleHud::errorWash(const QColor& fg)
{
    QColor wash = fg;
    wash.setAlphaF(0.12f);
    return wash.name(QColor::HexArgb);
}

bool AngleHud::eventFilter(QObject* o, QEvent* e)
{
    if ((o == m_edit || o == this) && e->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            if (onCancel) onCancel();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace cad::tools
