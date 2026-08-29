#include "LinkedCard.h"

#include <cmath>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>

#include "CopyChip.h"
#include "geometry/Units.h"
#include "parametric/PerfProbe.h"

LinkedCard::LinkedCard(const cad::param::LinkedVariable& lv,
                       const QString& sourceLabel,
                       bool alternate, QWidget* parent)
    : CardBase(alternate, parent)
    , m_id(lv.id)
    , m_sourceBlockId(lv.sourceBlockId)
    , m_refName(lv.refName)
    , m_sourceLabel(sourceLabel)
{
    setAccentRole(cad::ui::CardAccent::Linked);  // 关联 = 钴蓝竖线 (方案 A)
    setupUi(lv, sourceLabel);
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
    // Value-level no-op guard (see CardBase::refreshValueGuard).
    if (refreshValueGuard(valueMm, dangling))
        return;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — only run it when the dangling state actually flips (see CardBase).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        setValueLabelDangling(dangling, QStringLiteral("源线段已被删除"));
    }
    m_valueLabel->setText(dangling ? QStringLiteral("—") : cad::geo::Units::formatCmTrimmed(valueMm));
}

void LinkedCard::syncFromModel(const cad::param::LinkedVariable& lv,
                               const QString& sourceLabel)
{
    m_nameChip->setText(lv.name);
    m_refName = lv.refName;
    m_refChip->setText(lv.refName);
    m_sourceLabel = sourceLabel;
    m_sourceInfo->setText(sourceLabel);
    setCommentSilently(lv.comment);
    refreshValue(lv.value, lv.dangling);
}

void LinkedCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_sourceBlockId);
    QWidget::mousePressEvent(event);
}

void LinkedCard::setupUi(const cad::param::LinkedVariable& lv,
                         const QString& sourceLabel)
{
    CardBase::ReadOnlySkeletonSpec spec;
    spec.objectName      = QStringLiteral("LinkedCard");
    spec.indexObjectName = QStringLiteral("linkedIndex");
    spec.indexTooltip    = QStringLiteral("关联参数序号（视图行号）");
    spec.namePlaceholder = QStringLiteral("名称");
    spec.nameText        = lv.name;
    spec.deleteTooltip   = QStringLiteral("删除关联参数");
    spec.lockTooltip     = QStringLiteral("自动测量，不可编辑");
    spec.sourceTooltip   = QStringLiteral("测量来源（只读）");
    spec.sourceLabel     = sourceLabel;
    spec.commentText     = lv.comment;
    spec.refName         = lv.refName;
    spec.refChipWidth    = 72;
    spec.unit            = QStringLiteral("cm");

    buildReadOnlySkeleton(spec,
        [this]() { emit deleteRequested(m_id); },
        [this]() { emit edited(linkedVar()); });

    refreshValue(lv.value, lv.dangling);
}
