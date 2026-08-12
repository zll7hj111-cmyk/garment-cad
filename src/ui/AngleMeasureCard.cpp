#include "AngleMeasureCard.h"

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
#include "parametric/PerfProbe.h"

namespace {

QString fmtDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s + QChar(0x00B0);  // "°"
}

} // namespace

AngleMeasureCard::AngleMeasureCard(const cad::param::AngleMeasureVariable& am,
                                   const QString& sourceLabel,
                                   bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_id(am.id)
    , m_sourceBlockId(am.blockA)
    , m_refName(am.refName)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(am, sourceLabel, alternate);
}

cad::param::AngleMeasureVariable AngleMeasureCard::angleMeasureVar() const
{
    cad::param::AngleMeasureVariable am;
    am.id = m_id;
    am.name = m_nameChip->text().trimmed();
    am.refName = m_refName;
    am.comment = m_commentEdit->text().trimmed();
    return am;
}

void AngleMeasureCard::refreshValue(double valueDeg, bool dangling)
{
    GCAD_PERF_SCOPE("card.aRefresh");
    // Value-level no-op guard (see MeasureCard::refreshValue).
    if (m_hasShownValue && dangling == m_danglingStyled &&
        (dangling || std::abs(valueDeg - m_lastValueDeg) < 1e-3)) {
        return;
    }
    m_hasShownValue = true;
    m_lastValueDeg = valueDeg;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — only run it when the dangling state actually flips (see MeasureCard).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        if (dangling) {
            m_valueLabel->setStyleSheet(
                "font-size: 11px; font-weight: bold; background: transparent;");
            m_valueLabel->setToolTip(QStringLiteral("测量来源线段已被删除"));
        } else {
            m_valueLabel->setStyleSheet(
                "font-family: 'Consolas','Courier New',monospace;"
                "font-size: 12px; font-weight: bold; background: transparent;");
            m_valueLabel->setToolTip(QString());
        }
    }
    m_valueLabel->setText(dangling ? QStringLiteral("—") : fmtDeg(valueDeg));
}

void AngleMeasureCard::syncFromModel(const cad::param::AngleMeasureVariable& am,
                                     const QString& sourceLabel)
{
    m_nameChip->setText(am.name);
    m_refName = am.refName;
    m_refChip->setText(am.refName);
    m_sourceInfo->setText(sourceLabel);
    m_sourceBlockId = am.blockA;
    if (!m_commentEdit->hasFocus()) {
        m_commentEdit->blockSignals(true);
        m_commentEdit->setText(am.comment);
        m_commentEdit->blockSignals(false);
    }
    refreshValue(am.value, am.dangling);
}

void AngleMeasureCard::setIndex(int n)
{
    if (!m_indexLabel) return;
    // Pure presentation: only touch the label when the ordinal changed.
    const QString text = n > 0 ? QStringLiteral("角 %1").arg(n) : QString();
    if (m_indexLabel->text() != text)
        m_indexLabel->setText(text);
}

void AngleMeasureCard::paintEvent(QPaintEvent* event)
{
        QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Left accent bar (purple for angle measure variables).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8E, 0x44, 0xAD));
    p.drawRoundedRect(0, 2, 3, height() - 4, 1.5, 1.5);
}

void AngleMeasureCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
}

void AngleMeasureCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
}

void AngleMeasureCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_sourceBlockId);
    QWidget::mousePressEvent(event);
}

void AngleMeasureCard::setupUi(const cad::param::AngleMeasureVariable& am,
                               const QString& sourceLabel, bool alternate)
{
    setObjectName(QStringLiteral("AngleMeasureCard"));
    (void)alternate;  // ElaScrollPageArea paints the card from the active theme.

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setStyleSheet("font-size: 11px; background: transparent;");
    m_indexLabel->setToolTip(QStringLiteral("角度测量序号（视图行号）"));
    header->addWidget(m_indexLabel, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(am.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; font-weight: bold; background: transparent;");
    refreshValue(am.value, am.dangling);
    header->addWidget(m_valueLabel, 0);

    m_lockIcon = new ElaText(QString(), 13, this);
    m_lockIcon->setText(QStringLiteral("\xF0\x9F\x94\x92"));  // 🔒
    m_lockIcon->setStyleSheet("font-size: 10px; background: transparent;");
    m_lockIcon->setToolTip(QStringLiteral("自动测量，不可编辑"));
    m_lockIcon->setFixedWidth(16);
    header->addWidget(m_lockIcon, 0);

    m_deleteBtn = new ElaToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(QStringLiteral("删除角度测量变量"));
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
    m_refChip->setPlaceholderText(QStringLiteral("引用名"));
    m_refChip->setText(am.refName);
    m_refChip->setCopyEnabled(true);
    m_refChip->setFixedWidth(84);
    detailLayout->addWidget(m_refChip, 0);

    m_sourceInfo = new ElaText(QString(), 13, m_detail);
    m_sourceInfo->setText(sourceLabel);
    m_sourceInfo->setStyleSheet("font-size: 11px; background: transparent;");
    m_sourceInfo->setToolTip(QStringLiteral("测量来源：两条线段（只读）"));
    detailLayout->addWidget(m_sourceInfo, 0);

    m_commentEdit = new ElaLineEdit(m_detail);     m_commentEdit->setText(am.comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(22);
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(angleMeasureVar()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(angleMeasureVar()); });
}
