#include "LinkedCard.h"

#include <cmath>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>

#include "CopyChip.h"
#include "IconHelper.h"
#include "geometry/Units.h"
#include "parametric/PerfProbe.h"

namespace {

QString fmtCm(double mm)
{
    const double cm = cad::geo::Units::mmToCm(mm);
    QString s = QString::number(cm, 'f', 2);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

} // namespace

LinkedCard::LinkedCard(const cad::param::LinkedVariable& lv,
                       const QString& sourceLabel,
                       bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_id(lv.id)
    , m_sourceBlockId(lv.sourceBlockId)
    , m_refName(lv.refName)
    , m_sourceLabel(sourceLabel)
    , m_alternate(alternate)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(lv, sourceLabel, alternate);
}

cad::param::LinkedVariable LinkedCard::linkedVar() const
{
    cad::param::LinkedVariable lv;
    lv.id = m_id;
    lv.name = m_nameChip->text().trimmed();
    lv.refName = m_refName;
    lv.comment = m_commentEdit->text().trimmed();
    return lv;
}

void LinkedCard::refreshValue(double valueMm, bool dangling)
{
    GCAD_PERF_SCOPE("card.lRefresh");
    // Value-level no-op guard (see MeasureCard::refreshValue).
    if (m_hasShownValue && dangling == m_danglingStyled &&
        (dangling || std::abs(valueMm - m_lastValueMm) < 1e-3)) {
        return;
    }
    m_hasShownValue = true;
    m_lastValueMm = valueMm;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — only run it when the dangling state actually flips (see MeasureCard).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        if (dangling) {
            m_valueLabel->setStyleSheet(
                "font-size: 11px; font-weight: bold; background: transparent;");
            m_valueLabel->setToolTip(QStringLiteral("源线段已被删除"));
        } else {
            m_valueLabel->setStyleSheet(
                "font-family: 'Consolas','Courier New',monospace;"
                "font-size: 12px; font-weight: bold; background: transparent;");
            m_valueLabel->setToolTip(QString());
        }
    }
    m_valueLabel->setText(dangling ? QStringLiteral("—") : fmtCm(valueMm));
}

void LinkedCard::syncFromModel(const cad::param::LinkedVariable& lv,
                               const QString& sourceLabel)
{
    m_nameChip->setText(lv.name);
    m_refName = lv.refName;
    m_refChip->setText(lv.refName);
    m_sourceLabel = sourceLabel;
    m_sourceInfo->setText(sourceLabel);
    if (!m_commentEdit->hasFocus()) {
        m_commentEdit->blockSignals(true);
        m_commentEdit->setText(lv.comment);
        m_commentEdit->blockSignals(false);
    }
    refreshValue(lv.value, lv.dangling);
}

void LinkedCard::setIndex(int n)
{
    if (!m_indexLabel) return;
    // Pure presentation: only touch the label when the ordinal changed.
    const QString text = n > 0 ? QString::number(n) : QString();
    if (m_indexLabel->text() != text)
        m_indexLabel->setText(text);
}

void LinkedCard::setAlternate(bool alternate)
{
    if (m_alternate == alternate)
        return;
    m_alternate = alternate;
    update();
}

void LinkedCard::paintEvent(QPaintEvent* event)
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

void LinkedCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
    m_deleteBtnSlot->setVisible(false);
}

void LinkedCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
    m_deleteBtnSlot->setVisible(true);
}

void LinkedCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_sourceBlockId);
    QWidget::mousePressEvent(event);
}

void LinkedCard::setupUi(const cad::param::LinkedVariable& lv,
                         const QString& sourceLabel, bool alternate)
{
    setObjectName(QStringLiteral("LinkedCard"));
    (void)alternate;  // 竖线颜色已按 alternate 存为 m_alternate (构造时).

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    // 视图行序号 (虚拟化跨行复用, 每次 (re)bind 重设 — 见 setIndex).
    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setObjectName(QStringLiteral("linkedIndex"));
    m_indexLabel->setStyleSheet("font-size: 11px; background: transparent;");
    m_indexLabel->setToolTip(QStringLiteral("关联参数序号（视图行号）"));
    header->addWidget(m_indexLabel, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(lv.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; font-weight: bold; background: transparent;");
    refreshValue(lv.value, lv.dangling);
    header->addWidget(m_valueLabel, 0);

    // Lock icon (read-only indicator)
    m_lockIcon = new ElaText(QString(), 13, this);
    m_lockIcon->setText(QStringLiteral("\xF0\x9F\x94\x92"));  // 🔒
    m_lockIcon->setStyleSheet("font-size: 10px; background: transparent;");
    m_lockIcon->setToolTip(QStringLiteral("自动测量，不可编辑"));
    m_lockIcon->setFixedWidth(16);
    header->addWidget(m_lockIcon, 0);

    // 悬停占位: 与删除按钮同尺寸, 二者互斥显隐 → 布局空间恒定,
    // 按钮出现/消失不引起行宽挤压或行高变化 (VirtualCardList 不重测).
    m_deleteBtnSlot = new QWidget(this);
    m_deleteBtnSlot->setFixedSize(20, 20);
    header->addWidget(m_deleteBtnSlot, 0);

    m_deleteBtn = new ElaToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(QStringLiteral("删除关联参数"));
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setVisible(false);
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
    m_refChip->setText(lv.refName);
    m_refChip->setCopyEnabled(true);
    m_refChip->setFixedWidth(72);
    detailLayout->addWidget(m_refChip, 0);

    m_sourceInfo = new ElaText(QString(), 13, m_detail);
    m_sourceInfo->setText(sourceLabel);
    m_sourceInfo->setStyleSheet("font-size: 11px; background: transparent;");
    m_sourceInfo->setToolTip(QStringLiteral("测量来源（只读）"));
    detailLayout->addWidget(m_sourceInfo, 0);

    m_commentEdit = new ElaLineEdit(m_detail);     m_commentEdit->setText(lv.comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(22);
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(linkedVar()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(linkedVar()); });
}
