#include "MeasureCard.h"

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

MeasureCard::MeasureCard(const cad::param::MeasureVariable& mv,
                         const QString& sourceLabel,
                         bool alternate, QWidget* parent)
    : CardBase(alternate, parent)
    , m_id(mv.id)
    , m_refName(mv.refName)
    , m_kind(mv.kind)
{
    setAccentRole(cad::ui::CardAccent::Measure);  // 测量 = 陶土竖线 (方案 A)
    setupUi(mv, sourceLabel);
}

cad::param::MeasureVariable MeasureCard::measureVar() const
{
    cad::param::MeasureVariable mv;
    mv.id = m_id;
    mv.name = m_nameChip->text().trimmed();
    mv.refName = m_refName;
    mv.comment = m_noteBtn ? m_noteBtn->note() : QString();
    return mv;
}

QString MeasureCard::indexText(int n) const
{
    return n > 0 ? QStringLiteral("测 %1").arg(n) : QString();
}

void MeasureCard::refreshValue(double valueMm, bool dangling)
{
    GCAD_PERF_SCOPE("card.mRefresh");
    // Value-level no-op guard: cards are synced on EVERY resolve frame during
    // drags, but most measurements are untouched by the gesture — skip the
    // widget update entirely when nothing visible changed (epsilon is well
    // below the 0.01 cm display precision). The kind participates in the
    // guard: virtualized cards are reused across rows with different modes.
    if (m_kind == m_lastShownKind && refreshValueGuard(valueMm, dangling))
        return;
    m_lastShownKind = m_kind;
    // setStyleSheet() re-parses the rule set and re-polishes the whole panel
    // — it must run ONLY when the dangling state actually flips, never per
    // value update (this runs on every resolve during drags).
    if (dangling != m_danglingStyled) {
        m_danglingStyled = dangling;
        setValueLabelDangling(dangling, QStringLiteral("测量来源点已被删除"));
    }
    if (dangling) {
        m_valueLabel->setText(QStringLiteral("—"));
    } else {
        QString text = cad::geo::Units::formatCmTrimmed(valueMm);
        switch (m_kind) {
            case cad::param::MeasureKind::Horizontal:
                text.prepend(QStringLiteral("水平 "));
                break;
            case cad::param::MeasureKind::Vertical:
                text.prepend(QStringLiteral("垂直 "));
                break;
            case cad::param::MeasureKind::Distance:
                break;
        }
        m_valueLabel->setText(text);
    }
}

void MeasureCard::syncFromModel(const cad::param::MeasureVariable& mv,
                                const QString& sourceLabel)
{
    m_nameChip->setText(mv.name);
    m_refName = mv.refName;
    m_refChip->setText(mv.refName);
    m_kind = mv.kind;  // 虚拟化复用: 同一实例可被不同模式的测量行复用
    m_sourceInfo->setText(sourceLabel);
    setCommentSilently(mv.comment);
    refreshValue(mv.value, mv.dangling);
}

void MeasureCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit sourceClicked(m_id);
    QWidget::mousePressEvent(event);
}

void MeasureCard::enterEvent(QEnterEvent* event)
{
    emit sourceClicked(m_id);
    CardBase::enterEvent(event);
}


void MeasureCard::setupUi(const cad::param::MeasureVariable& mv,
                          const QString& sourceLabel)
{
    CardBase::ReadOnlySkeletonSpec spec;
    spec.objectName      = QStringLiteral("MeasureCard");
    spec.indexObjectName = QStringLiteral("measureIndex");
    spec.indexTooltip    = QStringLiteral("测量序号（视图行号）");
    spec.namePlaceholder = QStringLiteral("名称");
    spec.nameText        = mv.name;
    spec.deleteTooltip   = QStringLiteral("删除测量变量");
    spec.lockTooltip     = QStringLiteral("自动测量，不可编辑");
    spec.sourceTooltip   = QStringLiteral("测量来源：两个点及所在图层（只读）");
    spec.sourceLabel     = sourceLabel;
    spec.commentText     = mv.comment;
    spec.refName         = mv.refName;
    spec.refChipWidth    = 72;
    spec.unit            = QStringLiteral("cm");

    buildReadOnlySkeleton(spec,
        [this]() { emit deleteRequested(m_id); },
        [this]() { emit edited(measureVar()); });

    refreshValue(mv.value, mv.dangling);
}
