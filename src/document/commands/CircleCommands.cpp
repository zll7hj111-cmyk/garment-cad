#include "document/commands/CircleCommands.h"

#include "document/CommandTexts.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"

namespace cad::cmd {

namespace {

/// 本命令要冻结的点：起点 + 全部分段锚 + 终点（圆心不在其列）。
[[nodiscard]] std::vector<QUuid> circlePointIds(const cad::param::Segment& seg)
{
    std::vector<QUuid> ids;
    ids.reserve(seg.passPointIds.size() + 2);
    ids.push_back(seg.startPointId);
    ids.insert(ids.end(), seg.passPointIds.begin(), seg.passPointIds.end());
    ids.push_back(seg.endPointId);
    return ids;
}

/// 把一个点冻结在当前位置：约束改 `Free`，当前局部解算位置写进 `freePos`，
/// 清掉 Polar 专属字段（半径/角度权威随 fitKind 一起失效，留着会误导 UI 与
/// 序列化）。切向与 `autoTangent` **不动** —— 见头文件里的 D14 偏离说明。
void freezePoint(cad::param::ParamPoint& pt)
{
    if (pt.resolved)
        pt.freePos = pt.resolvedPos;
    pt.constraint = cad::param::PointConstraint::Free;
    pt.refPointId = QUuid();
    pt.refSegmentId = QUuid();
    pt.distance = 0.0;
    pt.angle = 0.0;
    pt.distanceFormula.clear();
    pt.angleFormula.clear();
}

} // namespace

void detachCircleInPlace(cad::param::ParamDocument* doc,
                         const QUuid& blockId, const QUuid& segmentId)
{
    if (!doc) return;
    auto* block = doc->findBlock(blockId);
    auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg || seg->fitKind != cad::param::FitKind::Circle) return;

    // 冻结前先解算一次：freePos 要写的是**当前**解算位置，而拟合切向也只有
    // 解算过才是当前值（applyCircleFitTangents() 每轮 resolve 写入）。
    doc->resolveAll();

    for (const QUuid& pid : circlePointIds(*seg)) {
        if (auto* pt = block->findPoint(pid))
            freezePoint(*pt);
    }
    seg->fitKind = cad::param::FitKind::None;
    doc->touchAndResolve(blockId);
}

DetachCircleCommand::DetachCircleCommand(cad::param::ParamDocument* doc,
                                         const QUuid& blockId, const QUuid& segmentId,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
{
    setText(cad::cmd::texts::kDetachCircle);
    // 快照本段涉及的全部点（含未解算的公式字段）—— undo 必须逐字段还原。
    if (const auto* b = doc ? doc->findBlock(blockId) : nullptr) {
        if (const auto* s = b->findSegment(segmentId)) {
            m_oldFitKind = s->fitKind;
            for (const QUuid& pid : circlePointIds(*s)) {
                if (const auto* pt = b->findPoint(pid))
                    m_oldPoints.push_back(*pt);
            }
        }
    }
}

void DetachCircleCommand::redo()
{
    detachCircleInPlace(m_doc, m_blockId, m_segmentId);
}

void DetachCircleCommand::undo()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    for (const auto& old : m_oldPoints) {
        if (auto* pt = block->findPoint(old.id))
            *pt = old;
    }
    seg->fitKind = m_oldFitKind;
    m_doc->touchAndResolve(m_blockId);
}

} // namespace cad::cmd
