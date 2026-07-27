#include "VariableCard.h"

#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>

#include "CopyChip.h"
#include "geometry/Units.h"

namespace {

class CompactSpinBox : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double value) const override
    {
        QString s = QString::number(value, 'f', decimals());
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
        return s;
    }
};

QString fmtCm(double mm)
{
    const double cm = cad::geo::Units::mmToCm(mm);
    QString s = QString::number(cm, 'f', 2);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

} // namespace

VariableCard::VariableCard(const cad::param::Variable& var, bool alternate,
                           QWidget* parent)
    : QWidget(parent)
    , m_id(var.id)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(var, alternate);
}

cad::param::Variable VariableCard::variable() const
{
    cad::param::Variable v;
    v.id = m_id;
    v.name = m_nameChip->text().trimmed();
    v.refName = m_refChip->text().trimmed();
    v.value = cad::geo::Units::cmToMm(m_valueSpin->value());
    v.comment = m_commentEdit->text().trimmed();
    return v;
}

void VariableCard::focusName()
{
    m_nameChip->focusEdit();
}

void VariableCard::setExpanded(bool on)
{
    m_expanded = on;
    m_detail->setVisible(on);
    m_arrow->setText(on ? QStringLiteral("\u25BE") : QStringLiteral("\u25B8"));
}

void VariableCard::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void VariableCard::updateValueLabel()
{
    m_valueLabel->setText(QStringLiteral("%1 cm").arg(fmtCm(variable().value)));
}

void VariableCard::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

bool VariableCard::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_arrow && event->type() == QEvent::MouseButtonPress) {
        toggleExpanded();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void VariableCard::setupUi(const cad::param::Variable& var, bool alternate)
{
    setObjectName(QStringLiteral("VariableCard"));
    const QString bg = alternate ? QStringLiteral("#F4F6F7") : QStringLiteral("#FFFFFF");
    setStyleSheet(QStringLiteral(
        "QWidget#VariableCard {"
        "  background-color: %1;"
        "  border: 1px solid #E5E8E8;"
        "  border-radius: 4px;"
        "}"
        "QWidget#VariableCard:hover { border: 1px solid #3498DB; }"
    ).arg(bg));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(0);

    // === Header row (always visible) ===
    auto* header = new QHBoxLayout();
    header->setSpacing(4);

    m_arrow = new QLabel(QStringLiteral("\u25B8"), this);  // ▸
    m_arrow->setFixedWidth(12);
    m_arrow->setCursor(Qt::PointingHandCursor);
    m_arrow->setStyleSheet("font-size: 10px; color: #85929E; background: transparent;");
    m_arrow->setAlignment(Qt::AlignCenter);
    header->addWidget(m_arrow, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(var.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setStyleSheet(
        "font-size: 11px; font-weight: bold; color: #21618C; background: transparent;");
    header->addWidget(m_valueLabel, 0);

    m_deleteBtn = new QToolButton(this);
    m_deleteBtn->setText(QStringLiteral("\u2715"));
    m_deleteBtn->setToolTip(QStringLiteral("删除变量"));
    m_deleteBtn->setFixedSize(14, 14);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setStyleSheet(
        "QToolButton { font-size: 9px; color: #B0B0B0;"
        "  background: transparent; border: none; border-radius: 7px; }"
        "QToolButton:hover { color: #FFFFFF; background: #E74C3C; }");
    header->addWidget(m_deleteBtn, 0);

    mainLayout->addLayout(header);

    // === Detail row (collapsible) ===
    m_detail = new QWidget(this);
    m_detail->setVisible(false);
    auto* detailLayout = new QHBoxLayout(m_detail);
    detailLayout->setContentsMargins(16, 1, 0, 1);
    detailLayout->setSpacing(4);

    m_refChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Ref, m_detail);
    m_refChip->setPlaceholderText(QStringLiteral("引用名"));
    m_refChip->setText(var.refName);
    m_refChip->setFixedWidth(56);
    detailLayout->addWidget(m_refChip, 0);

    m_valueSpin = new CompactSpinBox(m_detail);
    m_valueSpin->setRange(-99999.0, 99999.0);
    m_valueSpin->setDecimals(2);
    m_valueSpin->setSuffix(QStringLiteral(" cm"));
    m_valueSpin->setSingleStep(0.5);
    m_valueSpin->setValue(cad::geo::Units::mmToCm(var.value));
    m_valueSpin->setFixedWidth(76);
    m_valueSpin->setFixedHeight(16);
    m_valueSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_valueSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueSpin->setStyleSheet(
        "QDoubleSpinBox { font-size: 11px; color: #21618C;"
        "  border: 1px solid #D5DBDB; border-radius: 3px;"
        "  padding: 0 3px; background: #FBFCFC; }"
        "QDoubleSpinBox:focus { border: 1px solid #3498DB; background: #FFF; }");
    detailLayout->addWidget(m_valueSpin, 0);

    m_commentEdit = new QLineEdit(var.comment, m_detail);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(16);
    m_commentEdit->setStyleSheet(
        "QLineEdit { font-size: 10px; font-style: italic; color: #85929E;"
        "  background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 0 3px; }"
        "QLineEdit:hover { border: 1px solid #B0BEC5; background: #FFF; }"
        "QLineEdit:focus { border: 1px solid #3498DB; background: #FFF; }");
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Init display ===
    updateValueLabel();

    // === Connections ===
    m_arrow->installEventFilter(this);

    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { updateValueLabel(); emit edited(variable()); });
    connect(m_refChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(variable()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(variable()); });
    connect(m_valueSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { updateValueLabel(); emit edited(variable()); });
}
