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
    , m_alternate(alternate)
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

void VariableCard::setIndex(int n)
{
    if (!m_indexLabel) return;
    // Pure presentation: only touch the label when the ordinal changed.
    const QString text = n > 0 ? QString::number(n) : QString();
    if (m_indexLabel->text() != text)
        m_indexLabel->setText(text);
}

void VariableCard::setAlternate(bool alternate)
{
    if (m_alternate == alternate)
        return;
    m_alternate = alternate;
    update();
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

    // Left accent bar — 行交替竖线: 偶数行蓝 / 奇数行橙 (2026-08 用户拍板
    // 统一蓝橙交替, 替代原类型色条与背景斑马纹).
    p.setPen(Qt::NoPen);
    p.setBrush(m_alternate ? QColor(0xF5, 0x9E, 0x0B)   // 橙
                           : QColor(0x2F, 0x6F, 0xED)); // 蓝
    p.drawRoundedRect(0, 2, 3, height() - 4, 1.5, 1.5);
}

void VariableCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
    m_deleteBtnSlot->setVisible(false);
}

void VariableCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
    m_deleteBtnSlot->setVisible(true);
}

void VariableCard::setupUi(const cad::param::Variable& var, bool alternate)
{
    setObjectName(QStringLiteral("VariableCard"));
    (void)alternate;  // 竖线颜色已按 alternate 存为 m_alternate (构造时).

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    // 视图行序号 (虚拟化跨行复用, 每次 (re)bind 重设 — 见 setIndex).
    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setObjectName(QStringLiteral("varIndex"));
    m_indexLabel->setStyleSheet("font-size: 11px; background: transparent;");
    m_indexLabel->setToolTip(QStringLiteral("变量序号（视图行号）"));
    header->addWidget(m_indexLabel, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(var.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; font-weight: bold; background: transparent;");
    header->addWidget(m_valueLabel, 0);

    // 悬停占位: 与删除按钮同尺寸, 二者互斥显隐 → 布局空间恒定,
    // 按钮出现/消失不引起行宽挤压或行高变化 (VirtualCardList 不重测).
    m_deleteBtnSlot = new QWidget(this);
    m_deleteBtnSlot->setFixedSize(20, 20);
    header->addWidget(m_deleteBtnSlot, 0);

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
