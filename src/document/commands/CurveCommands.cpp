#include "document/commands/CurveCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── AddCurvePointCommand ───

AddCurvePointCommand::AddCurvePointCommand(cad::param::ParamDocument* doc,
                                           const QUuid& blockId,
                                           const QUuid& segmentId,
                                           cad::param::ParamPoint pt,
                                           QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pt(std::move(pt))
    , m_oldType(cad::param::SegmentType::Line)
{
    setText(QStringLiteral("添加曲线点"));
    if (auto* b = m_doc->findBlock(m_blockId))
        if (const auto* s = b->findSegment(m_segmentId))
            m_oldType = s->type;
}

void AddCurvePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    // Insert keeping passPointIds ordered by chord fraction (interpPercent) so
    // the spline passes through the anchors in order along the curve rather
    // than looping back on itself when a point is added mid-curve via Ctrl.
    auto& ids = seg->passPointIds;
    int insertAt = static_cast<int>(ids.size());
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        const auto* pp = block->findPoint(ids[i]);
        if (pp && pp->interpPercent > m_pt.interpPercent) { insertAt = i; break; }
    }
    ids.insert(ids.begin() + insertAt, m_pt.id);
    seg->type = cad::param::SegmentType::Bezier;
    // Curve structure changed WITHOUT any point necessarily moving — bump the
    // epoch explicitly so Block::resolve's stale-cache gate rebuilds the curve
    // cache (and the canvas rebuilds) in the resolveAll below.
    block->touchGeometry();
    m_doc->resolveAll();
}

void AddCurvePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    if (auto* seg = block->findSegment(m_segmentId)) {
        auto& ids = seg->passPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), m_pt.id), ids.end());
        seg->type = m_oldType;
    }
    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pt.id; }),
        pts.end());
    block->rebuildPointIndex();
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── RemoveCurvePointCommand ───

RemoveCurvePointCommand::RemoveCurvePointCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId,
                                                 const QUuid& segmentId,
                                                 const QUuid& pointId,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pointId(pointId)
    , m_oldType(cad::param::SegmentType::Bezier)
{
    setText(QStringLiteral("删除曲线点"));
    // Capture the point's data and its passPointIds slot now (before redo
    // removes it) so undo can restore it exactly.
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (const auto* p = b->findPoint(m_pointId))
            m_pt = *p;
        if (const auto* s = b->findSegment(m_segmentId)) {
            m_oldType = s->type;
            const auto& ids = s->passPointIds;
            auto it = std::find(ids.begin(), ids.end(), m_pointId);
            m_index = (it == ids.end()) ? 0 : static_cast<int>(it - ids.begin());
        }
    }
}

void RemoveCurvePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    auto& ids = seg->passPointIds;
    ids.erase(std::remove(ids.begin(), ids.end(), m_pointId), ids.end());
    if (ids.empty())
        seg->type = cad::param::SegmentType::Line;  // last curve point → straight

    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pointId; }),
        pts.end());
    block->rebuildPointIndex();
    block->touchGeometry();
    m_doc->resolveAll();
}

void RemoveCurvePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    auto& ids = seg->passPointIds;
    const int idx = std::clamp(m_index, 0, static_cast<int>(ids.size()));
    ids.insert(ids.begin() + idx, m_pointId);
    seg->type = m_oldType;
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── MoveCurveAnchorCommand ───

MoveCurveAnchorCommand::MoveCurveAnchorCommand(cad::param::ParamDocument* doc,
                                               const QUuid& blockId,
                                               const QUuid& pointId,
                                               double oldPercent, double oldOffset,
                                               double newPercent, double newOffset,
                                               const QUuid& oldFollowBlockId,
                                               const QUuid& oldFollowPointId,
                                               const cad::geo::Vec2& oldFollowOffset,
                                               const QUuid& newFollowBlockId,
                                               const QUuid& newFollowPointId,
                                               const cad::geo::Vec2& newFollowOffset,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldPercent(oldPercent), m_oldOffset(oldOffset)
    , m_newPercent(newPercent), m_newOffset(newOffset)
    , m_oldFollowBlockId(oldFollowBlockId), m_oldFollowPointId(oldFollowPointId)
    , m_oldFollowOffset(oldFollowOffset)
    , m_newFollowBlockId(newFollowBlockId), m_newFollowPointId(newFollowPointId)
    , m_newFollowOffset(newFollowOffset)
{
    setText(QStringLiteral("调整曲线点"));
}

void MoveCurveAnchorCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->interpPercent = m_newPercent;
    pt->interpOffsetDist = m_newOffset;
    pt->followBlockId = m_newFollowBlockId;
    pt->followPointId = m_newFollowPointId;
    pt->followOffset = m_newFollowOffset;
    m_doc->resolveAll();
}

void MoveCurveAnchorCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->interpPercent = m_oldPercent;
    pt->interpOffsetDist = m_oldOffset;
    pt->followBlockId = m_oldFollowBlockId;
    pt->followPointId = m_oldFollowPointId;
    pt->followOffset = m_oldFollowOffset;
    m_doc->resolveAll();
}

// ─── SetCurveTangentCommand ───

SetCurveTangentCommand::SetCurveTangentCommand(cad::param::ParamDocument* doc,
                                               const QUuid& blockId, const QUuid& pointId,
                                               const cad::geo::Vec2& oldTanIn, const cad::geo::Vec2& oldTanOut, bool oldAuto,
                                               const cad::geo::Vec2& newTanIn, const cad::geo::Vec2& newTanOut, bool newAuto,
                                               bool oldLocked, bool newLocked,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldTanIn(oldTanIn), m_oldTanOut(oldTanOut)
    , m_newTanIn(newTanIn), m_newTanOut(newTanOut)
    , m_oldAuto(oldAuto)
    , m_newAuto(newAuto)
    , m_oldLocked(oldLocked)
    , m_newLocked(newLocked)
{
    setText(QStringLiteral("调整曲线手柄"));
}

void SetCurveTangentCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->tangentIn = m_newTanIn;
    pt->tangentOut = m_newTanOut;
    pt->autoTangent = m_newAuto;
    pt->tangentLocked = m_newLocked;  // Alt+drag may break the lock persistently
    block->touchGeometry();
    m_doc->resolveAll();
}

void SetCurveTangentCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->tangentIn = m_oldTanIn;
    pt->tangentOut = m_oldTanOut;
    pt->autoTangent = m_oldAuto;
    pt->tangentLocked = m_oldLocked;  // restore the pre-drag lock state
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── ReleaseCurveFollowCommand ───

ReleaseCurveFollowCommand::ReleaseCurveFollowCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& pointId,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
{
    setText(QStringLiteral("释放跟随"));
    // Snapshot the pre-release follow state from the model.
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* pt = b->findPoint(pointId)) {
            m_oldFollowBlockId = pt->followBlockId;
            m_oldFollowPointId = pt->followPointId;
            m_oldFollowOffset = pt->followOffset;
        }
    }
}

void ReleaseCurveFollowCommand::redo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* pt = b ? b->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->followBlockId = {};
    pt->followPointId = {};
    pt->followOffset = cad::geo::Vec2::zero();
    m_doc->resolveAll();
}

void ReleaseCurveFollowCommand::undo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* pt = b ? b->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->followBlockId = m_oldFollowBlockId;
    pt->followPointId = m_oldFollowPointId;
    pt->followOffset = m_oldFollowOffset;
    m_doc->resolveAll();
}

} // namespace cad::cmd
