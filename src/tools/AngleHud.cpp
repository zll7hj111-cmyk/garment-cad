#include "AngleHud.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QEvent>

namespace cad::tools {

AngleHud::AngleHud(QWidget* viewport)
    : QWidget(viewport)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    // Mode toggle button (compact, acts as caption + switch).
    m_btnToggle = new QPushButton(this);
    m_btnToggle->setCursor(Qt::PointingHandCursor);
    m_btnToggle->setFixedHeight(22);
    m_btnToggle->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(255,255,255,20);color:#80cbc4;"
        "border:1px solid rgba(255,255,255,40);border-radius:4px;"
        "font-size:11px;padding:2px 6px;}"
        "QPushButton:hover{background:rgba(255,255,255,35);}"));

    m_lblCaption = new QLabel(this);
    m_edit = new QLineEdit(this);
    m_edit->setFixedWidth(150);
    m_lblUnit = new QLabel(this);

    layout->addWidget(m_btnToggle);
    layout->addWidget(m_lblCaption);
    layout->addWidget(m_edit);
    layout->addWidget(m_lblUnit);

    // Dark floating-panel look (AutoCAD dynamic-input style).
    setStyleSheet(QStringLiteral(
        "AngleHud{background:rgba(30,38,46,235);"
        "border:1px solid rgba(255,255,255,35);border-radius:8px;}"
        "QLabel{color:#b0bec5;font-size:12px;}"));
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
    m_edit->setStyleSheet(ok
        ? QStringLiteral("QLineEdit{border:1px solid #4db6ac;border-radius:4px;"
                         "padding:3px 6px;background:rgba(255,255,255,245);"
                         "color:#263238;selection-background-color:#80cbc4;}")
        : QStringLiteral("QLineEdit{border:1px solid #ef5350;border-radius:4px;"
                         "padding:3px 6px;background:#fff5f5;color:#b71c1c;}"));
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
