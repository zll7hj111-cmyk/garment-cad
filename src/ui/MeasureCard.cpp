#include "MeasureCard.h"

#include <cmath>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
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

MeasureCard::MeasureCard(const cad::param::MeasureVariable& mv,
                         const QString& sourceLabel,
                         bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_id(mv.id)
    , m_refName(mv.refName)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(mv, sourceLabel, alternate);
}

cad::param::MeasureVariable MeasureCard::measureVar() const
{
    cad::param::MeasureVariable mv;
    mv.id = m_id;
    mv.name = m_nameChip->text().trimmed();
    mv.refName = m_refName;
    mv.comment = m_commentEdit->text().trimmed();
    return mv;
}

void MeasureCard::refreshValue(double valueMm, bool dangling)
{
    GCAD_PERF_SCOPE("card.mRefresh");
    // Value-level no-op guard: cards are synced on EVERY resolve frame during
    // drags, but most measurements are untouched by the gesture — skip the
    // widget update entirely when nothing visible changed (epsilon is well
    // below the 0.01 cm display precision).
    if (m_hasShownValue && dangling == m_danglingStyled &&
        (dangling || std::abs(valueMm - m_lastValueMm) < 1e-3)) {
        return;
    }
    m_hasShownValue = true;
    m_lastValueMm = valueMm;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — it must run ONLY when the dangling state actually flips, never per
    // value update (this runs on every resolve during drags).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        if (dangling) {
            m_valueLabel->setStyleSheet(
                "font-size: 11px; font-weight: bold; color: #E74C3C; background: transparent;");
            m_valueLabel->setToolTip(QStringLiteral("测量来源点已被删除"));
        } else {
            m_valueLabel->setStyleSheet(
                "font-family: 'Consolas','Courier New',monospace;"
                "font-size: 12px; font-weight: bold; color: #B45309; background: transparent;");
            m_valueLabel->setToolTip(QString());
        }
    }
    m_valueLabel->setText(dangling ? QStringLiteral("—") : fmtCm(valueMm));
}

void MeasureCard::syncFromModel(const cad::param::MeasureVariable& mv,
                                const QString& sourceLabel)
{
    m_nameChip->setText(mv.name);
    m_refName = mv.refName;
    m_refChip->setText(mv.refName);
    m_sourceInfo->setText(sourceLabel);
    if (!m_commentEdit->hasFocus()) {
        m_commentEdit->blockSignals(true);
        m_commentEdit->setText(mv.comment);
        m_commentEdit->blockSignals(false);
    }
    refreshValue(mv.value, mv.dangling);
}

void MeasureCard::setIndex(int n)
{
    if (!m_indexLabel) return;
    // Pure presentation: only touch the label when the ordinal changed.
    const QString text = n > 0 ? QStringLiteral("测 %1").arg(n) : QString();
    if (m_indexLabel->text() != text)
        m_indexLabel->setText(text);
}

void MeasureCard::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Left accent bar (amber for measure variables).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xFF, 0x98, 0x00));
    p.drawRoundedRect(0, 2, 3, height() - 4, 1.5, 1.5);
}

void MeasureCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
}

void MeasureCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
}

void MeasureCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_id);
    QWidget::mousePressEvent(event);
}

void MeasureCard::setupUi(const cad::param::MeasureVariable& mv,
                          const QString& sourceLabel, bool alternate)
{
    setObjectName(QStringLiteral("MeasureCard"));
    const QString bg = alternate ? QStringLiteral("#FBF7F0") : QStringLiteral("#FFFFFF");
    setStyleSheet(QStringLiteral(
        "QWidget#MeasureCard {"
        "  background-color: %1;"
        "  border: 1px solid #E0E4E8;"
        "  border-radius: 6px;"
        "}"
        "QWidget#MeasureCard:hover { border: 1px solid #FFCC80; }"
    ).arg(bg));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_indexLabel = new QLabel(this);
    m_indexLabel->setStyleSheet(
        "font-size: 11px; color: #B45309; background: transparent;");
    m_indexLabel->setToolTip(QStringLiteral("测量序号（视图行号）"));
    header->addWidget(m_indexLabel, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, this);
    m_nameChip->setPlaceholderText(QStringLiteral("名称"));
    m_nameChip->setText(mv.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; font-weight: bold; color: #B45309; background: transparent;");
    refreshValue(mv.value, mv.dangling);
    header->addWidget(m_valueLabel, 0);

    m_lockIcon = new QLabel(this);
    m_lockIcon->setText(QStringLiteral("\xF0\x9F\x94\x92"));  // 🔒
    m_lockIcon->setStyleSheet("font-size: 10px; background: transparent;");
    m_lockIcon->setToolTip(QStringLiteral("自动测量，不可编辑"));
    m_lockIcon->setFixedWidth(16);
    header->addWidget(m_lockIcon, 0);

    m_deleteBtn = new QToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(QStringLiteral("删除测量变量"));
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setVisible(false);
    m_deleteBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; border-radius: 10px; }"
        "QToolButton:hover { background: #E74C3C; }");
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
    m_refChip->setText(mv.refName);
    m_refChip->setCopyEnabled(true);
    m_refChip->setFixedWidth(72);
    detailLayout->addWidget(m_refChip, 0);

    m_sourceInfo = new QLabel(m_detail);
    m_sourceInfo->setText(sourceLabel);
    m_sourceInfo->setStyleSheet(
        "font-size: 11px; color: #78909C; background: transparent;");
    m_sourceInfo->setToolTip(QStringLiteral("测量来源：两个点（只读）"));
    detailLayout->addWidget(m_sourceInfo, 0);

    m_commentEdit = new QLineEdit(mv.comment, m_detail);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(22);
    m_commentEdit->setStyleSheet(
        "QLineEdit { font-size: 11px; font-style: italic; color: #85929E;"
        "  background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 0 6px; }"
        "QLineEdit:hover { border: 1px solid #B0BEC5; background: #FFF; }"
        "QLineEdit:focus { border: 1px solid #FF9800; background: #FFF; }");
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(measureVar()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(measureVar()); });
}
