#include "VariableCard.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaDoubleSpinBox.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QMouseEvent>

#include "CompoundChip.h"
#include "geometry/Units.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include "ui/NoteButton.h"

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
    setAccentRole(cad::ui::CardAccent::Variable);  // 变量 = 碳灰竖线 (方案 A)
    setupUi(var);
}

cad::param::Variable VariableCard::variable() const
{
    cad::param::Variable v;
    v.id = m_id;
    v.name = m_compoundChip->name();
    v.refName = m_compoundChip->refName();
    v.value = cad::geo::Units::cmToMm(m_valueSpin->value());
    v.comment = m_noteBtn ? m_noteBtn->note() : QString();
    return v;
}

void VariableCard::focusName()
{
    m_compoundChip->focusNameEdit();
}

void VariableCard::syncFromModel(const cad::param::Variable& var)
{
    m_compoundChip->setName(var.name);
    m_compoundChip->setRefName(var.refName);
    if (!m_valueSpin->hasFocus()) {
        m_valueSpin->blockSignals(true);
        m_valueSpin->setValue(cad::geo::Units::mmToCm(var.value));
        m_valueSpin->blockSignals(false);
    }
    setCommentSilently(var.comment);
}

void VariableCard::setupUi(const cad::param::Variable& var)
{
    setObjectName(QStringLiteral("VariableCard"));
    setFixedHeight(34);

    auto* rowLayout = new QHBoxLayout(this);
    rowLayout->setContentsMargins(10, 5, 8, 5);
    rowLayout->setSpacing(6);

    // 视图行序号 (虚拟化跨行复用, 每次 (re)bind 重设 — 见 setIndex).
    m_indexLabel = createIndexLabel(QStringLiteral("varIndex"),
                                    QStringLiteral("变量序号（视图行号）"));
    m_indexLabel->setAlignment(Qt::AlignCenter);
    m_indexLabel->setFixedWidth(18);
    rowLayout->addWidget(m_indexLabel, 0);

    // 复合胶囊标签 [ refName | name ]：充分利用整行可用空间
    m_compoundChip = new cad::ui::CompoundChip(this);
    m_compoundChip->setName(var.name);
    m_compoundChip->setRefName(var.refName);
    m_compoundChip->setMinimumWidth(110);
    rowLayout->addWidget(m_compoundChip, 1);

    // 数值输入框 (cm)
    m_valueSpin = new CompactSpinBox(this);
    m_valueSpin->setRange(-99999.0, 99999.0);
    m_valueSpin->setDecimals(2);
    m_valueSpin->setSingleStep(0.5);
    m_valueSpin->setValue(cad::geo::Units::mmToCm(var.value));
    m_valueSpin->setFixedWidth(74);
    m_valueSpin->setFixedHeight(22);
    m_valueSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_valueSpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueSpin->setStyleSheet(cad::ui::ThemeTokens::kMonospaceMd);
    m_valueSpin->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("变量数值 (cm)"),
        QStringLiteral("输入该参数的基准尺寸数值，内部存储单位为厘米（cm）")));
    rowLayout->addWidget(m_valueSpin, 0);

    // 注释便利贴按钮 (NoteButton, 22px 高度与数值框对齐)
    m_noteBtn = createNoteButton(this, 22);
    m_noteBtn->setPlaceholder(QStringLiteral("变量说明…"));
    m_noteBtn->setNote(var.comment);
    rowLayout->addWidget(m_noteBtn, 0);

    // 悬停删除按钮
    appendDeleteButton(rowLayout, QStringLiteral("删除变量"));

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_compoundChip, &cad::ui::CompoundChip::nameEdited, this,
            [this](const QString&) { emit edited(variable()); });
    connect(m_compoundChip, &cad::ui::CompoundChip::refEdited, this,
            [this](const QString&) { emit edited(variable()); });
    connect(m_valueSpin, &QDoubleSpinBox::editingFinished, this,
            [this]() { emit edited(variable()); });
    connect(m_noteBtn, &cad::ui::NoteButton::noteEdited, this,
            [this](const QString&) { emit edited(variable()); });
}
