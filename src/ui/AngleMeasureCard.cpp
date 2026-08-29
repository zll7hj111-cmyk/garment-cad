#include "AngleMeasureCard.h"

#include <cmath>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>

#include "CopyChip.h"
#include "geometry/Units.h"
#include "parametric/PerfProbe.h"

AngleMeasureCard::AngleMeasureCard(const cad::param::AngleMeasureVariable& am,
                                   const QString& sourceLabel,
                                   bool alternate, QWidget* parent)
    : CardBase(alternate, parent)
    , m_id(am.id)
     , m_refName(am.refName)
{
    setAccentRole(cad::ui::CardAccent::Measure);  // 角度测量 = 陶土竖线 (方案 A)
    setupUi(am, sourceLabel);
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

QString AngleMeasureCard::indexText(int n) const
{
    return n > 0 ? QStringLiteral("角 %1").arg(n) : QString();
}

void AngleMeasureCard::refreshValue(double valueDeg, bool dangling)
{
    GCAD_PERF_SCOPE("card.aRefresh");
    // Value-level no-op guard (see CardBase::refreshValueGuard).
    if (refreshValueGuard(valueDeg, dangling))
        return;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — only run it when the dangling state actually flips (see CardBase).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        setValueLabelDangling(dangling, QStringLiteral("测量来源线段已被删除"));
    }
    m_valueLabel->setText(dangling ? QStringLiteral("—") : cad::geo::Units::formatDegTrimmed(valueDeg));
}

void AngleMeasureCard::syncFromModel(const cad::param::AngleMeasureVariable& am,
                                     const QString& sourceLabel)
{
    m_nameChip->setText(am.name);
    m_refName = am.refName;
    m_refChip->setText(am.refName);
    m_sourceInfo->setText(sourceLabel);
    setCommentSilently(am.comment);
    refreshValue(am.value, am.dangling);
}

void AngleMeasureCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_id);
    QWidget::mousePressEvent(event);
}

void AngleMeasureCard::enterEvent(QEnterEvent* event)
{
    emit sourceClicked(m_id);
    CardBase::enterEvent(event);
}

void AngleMeasureCard::setupUi(const cad::param::AngleMeasureVariable& am,
                               const QString& sourceLabel)
{
    CardBase::ReadOnlySkeletonSpec spec;
    spec.objectName      = QStringLiteral("AngleMeasureCard");
    spec.indexObjectName = QStringLiteral("angleIndex");
    spec.indexTooltip    = QStringLiteral("角度测量序号（视图行号）");
    spec.namePlaceholder = QStringLiteral("名称");
    spec.nameText        = am.name;
    spec.deleteTooltip   = QStringLiteral("删除角度测量变量");
    spec.lockTooltip     = QStringLiteral("自动测量，不可编辑");
    spec.sourceTooltip   = QStringLiteral("测量来源：两条线段及所在图层（只读）");
    spec.sourceLabel     = sourceLabel;
    spec.commentText     = am.comment;
    spec.refName         = am.refName;
    spec.refChipWidth    = 84;

    buildReadOnlySkeleton(spec,
        [this]() { emit deleteRequested(m_id); },
        [this]() { emit edited(angleMeasureVar()); });

    refreshValue(am.value, am.dangling);
}
