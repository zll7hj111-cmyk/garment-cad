#include "VariableCard.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaDoubleSpinBox.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>

#include "CopyChip.h"
#include "geometry/Units.h"
#include "Theme.h"

namespace {

class CompactSpinBox : public ElaDoubleSpinBox
{
public:
    using ElaDoubleSpinBox::ElaDoubleSpinBox;
    QString textFromValue(double value) const override
    {
        return cad::geo::Units::trimTrailingZeros(
            QString::number(value, 'f', decimals()));
    }
};

} // namespace

VariableCard::VariableCard(const cad::param::Variable& var, bool alternate,
                           QWidget* parent)
    : CardBase(alternate, parent)
    , m_id(var.id)
{
    setupUi(var);
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
    m_valueLabel->setText(
        cad::geo::Units::formatCmTrimmed(cad::geo::Units::cmToMm(m_valueSpin->value())));
}

void VariableCard::setupUi(const cad::param::Variable& var)
{
    setObjectName(QStringLiteral("VariableCard"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    // 视图行序号 (虚拟化跨行复用, 每次 (re)bind 重设 — 见 setIndex).
    header->addWidget(createIndexLabel(QStringLiteral("varIndex"),
                                       QStringLiteral("变量序号（视图行号）")), 0);

    header->addWidget(createNameChip(cad::ui::CopyChip::Variant::Name,
                                     QStringLiteral("名称"), var.name), 1);

    header->addWidget(createValueLabel(), 0);

    appendDeleteButton(header, QStringLiteral("删除变量"));

    mainLayout->addLayout(header);

    // === Detail row ===
    m_detail = new QWidget(this);
    m_detail->setVisible(true);
    auto* detailLayout = new QHBoxLayout(m_detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);

    m_refChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Ref, m_detail);
    m_refChip->setPlaceholderText(QString());  // 无引用名时保持纯空, 不显示占位文字
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
    m_valueSpin->setStyleSheet(cad::ui::ThemeTokens::kMonospaceMd);
    detailLayout->addWidget(m_valueSpin, 0);

    m_commentEdit = createCommentEdit(m_detail);
    m_commentEdit->setText(var.comment);
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
