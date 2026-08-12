#include "VariableCard.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaDoubleSpinBox.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>

#include "CopyChip.h"
#include "IconHelper.h"
#include "geometry/Units.h"

namespace {

class CompactSpinBox : public ElaDoubleSpinBox
{
public:
    using ElaDoubleSpinBox::ElaDoubleSpinBox;
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

void VariableCard::syncFromModel(const cad::param::Variable& var)
{
    m_nameChip->setText(var.name);
    m_refChip->setText(var.refName);
    if (!m_valueSpin->hasFocus()) {
        m_valueSpin->blockSignals(true);
        m_valueSpin->setValue(cad::geo::Units::mmToCm(var.value));
        m_valueSpin->blockSignals(false);
    }
    m_commentEdit->blockSignals(true);
    m_commentEdit->setText(var.comment);
    m_commentEdit->blockSignals(false);
    updateValueLabel();
}

void VariableCard::updateValueLabel()
{
    m_valueLabel->setText(fmtCm(cad::geo::Units::cmToMm(m_valueSpin->value())));
}

void VariableCard::paintEvent(QPaintEvent* event)
{
        QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Left accent bar (type identity: blue for plain variables).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2E, 0x86, 0xC1));
    p.drawRoundedRect(0, 2, 3, height() - 4, 1.5, 1.5);
}

void VariableCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
}

void VariableCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
}

void VariableCard::setupUi(const cad::param::Variable& var, bool alternate)
{
    setObjectName(QStringLiteral("VariableCard"));
    (void)alternate;  // ElaScrollPageArea paints the card from the active theme.

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(var.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; font-weight: bold; background: transparent;");
    header->addWidget(m_valueLabel, 0);

    m_deleteBtn = new ElaToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(QStringLiteral("删除变量"));
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setVisible(false);  // revealed on card hover
    header->addWidget(m_deleteBtn, 0);

    mainLayout->addLayout(header);

    // === Detail row ===
    m_detail = new QWidget(this);
    m_detail->setVisible(true);
    auto* detailLayout = new QHBoxLayout(m_detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);

    m_refChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Ref, m_detail);
    m_refChip->setPlaceholderText(QStringLiteral("引用名"));
    m_refChip->setText(var.refName);
    m_refChip->setFixedWidth(56);
    detailLayout->addWidget(m_refChip, 0);

    m_valueSpin = new CompactSpinBox(m_detail);
    m_valueSpin->setRange(-99999.0, 99999.0);
    m_valueSpin->setDecimals(2);
    m_valueSpin->setSingleStep(0.5);
    m_valueSpin->setValue(cad::geo::Units::mmToCm(var.value));
    m_valueSpin->setFixedWidth(80);
    m_valueSpin->setFixedHeight(22);
    m_valueSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_valueSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueSpin->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace; font-size: 12px;");
    detailLayout->addWidget(m_valueSpin, 0);

    m_commentEdit = new ElaLineEdit(m_detail);     m_commentEdit->setText(var.comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(22);
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Init display ===
    updateValueLabel();

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { updateValueLabel(); emit edited(variable()); });
    connect(m_refChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(variable()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(variable()); });
    connect(m_valueSpin, &QDoubleSpinBox::editingFinished, this,
            [this]() { updateValueLabel(); emit edited(variable()); });
}
