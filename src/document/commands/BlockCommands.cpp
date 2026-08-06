#include "BlockCommands.h"

#include <QSet>

#include <algorithm>

#include "parametric/ParamDocument.h"

namespace cad::cmd {

// ─── AddBlockCommand ───

AddBlockCommand::AddBlockCommand(cad::param::ParamDocument* doc,
                                 cad::param::Block block,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
{
    setText(QStringLiteral("添加线段"));
}

void AddBlockCommand::redo()
{
    m_doc->addBlock(m_block);
}

void AddBlockCommand::undo()
{
    m_doc->removeBlock(m_block.id);
}

// ─── RemoveBlockCommand ───

RemoveBlockCommand::RemoveBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("删除线段"));

    // Save the block for undo.
    if (const auto* b = doc->findBlock(blockId))
        m_block = *b;

    // Cascade set: the block + every bridge pinned to it (the model layer
    // releases those bridges as independent segments when the host goes
    // away — their pre-deletion state is snapshotted for undo).
    QSet<QUuid> cascade{blockId};
    for (const QUuid& bridgeId : doc->bridgesPinnedTo(blockId)) {
        if (cascade.contains(bridgeId)) continue;
        cascade.insert(bridgeId);
        if (const auto* bridge = doc->findBlock(bridgeId))
            m_bridges.push_back(*bridge);
    }

    // Snapshot every attachment touching any block in the cascade set.
    QSet<QUuid> seen;
    for (const auto& att : doc->attachments()) {
        if (!cascade.contains(att.fromBlockId) && !cascade.contains(att.toBlockId))
            continue;
        if (seen.contains(att.id)) continue;
        seen.insert(att.id);
        m_attachments.push_back(att);
    }

    // Linked variables sourced from the cascade set are auto-deleted with the
    // block, and their exact-match consumers (length-linked copies) get baked
    // to plain numbers (长度固化为数值). Snapshot both for undo.
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc->linkedVars())
            if (lv.sourceBlockId == srcId)
                m_linked.push_back(lv);
        for (const QUuid& cid : doc->linkedConsumerBlocks(srcId)) {
            if (cascade.contains(cid)) continue;   // removed & restored anyway
            const bool taken = std::any_of(
                m_bakedConsumers.begin(), m_bakedConsumers.end(),
                [&cid](const cad::param::Block& b) { return b.id == cid; });
            if (taken) continue;
            if (const auto* cb = doc->findBlock(cid))
                m_bakedConsumers.push_back(*cb);
        }
        // Measure variables referencing the cascade set (as an endpoint OR as
        // their owner bridge line) are auto-deleted by removeBlock(); snapshot
        // for undo restore.
        for (const auto& mv : doc->measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                m_measures.push_back(mv);
    }
}

void RemoveBlockCommand::redo()
{
    m_doc->removeBlock(m_block.id);
}

void RemoveBlockCommand::undo()
{
    // Bridges pinned to the removed block were RELEASED by redo() (converted
    // to independent segments, still present in the document). Remove the
    // released versions first, then restore the pristine snapshots.
    for (const auto& bridge : m_bridges)
        m_doc->removeBlock(bridge.id);
    m_doc->addBlock(m_block);
    for (const auto& bridge : m_bridges)
        m_doc->addBlock(bridge);
    for (const auto& att : m_attachments)
        m_doc->addAttachment(att);
    // Re-publish the auto-deleted linked variables, then restore the pristine
    // formulas of their consumers (baked to numbers by redo).
    for (const auto& lv : m_linked)
        m_doc->addLinked(lv);
    for (const auto& mv : m_measures)
        m_doc->addMeasure(mv);
    for (const auto& snap : m_bakedConsumers) {
        if (auto* b = m_doc->findBlock(snap.id))
            *b = snap;
    }
    if (!m_linked.empty() || !m_measures.empty() || !m_bakedConsumers.empty())
        m_doc->resolveAll();
}

// ─── MoveBlockCommand ───

MoveBlockCommand::MoveBlockCommand(cad::param::ParamDocument* doc,
                                   const QList<QUuid>& blockIds,
                                   const cad::geo::Vec2& delta,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockIds(blockIds)
    , m_delta(delta)
{
    setText(QStringLiteral("移动"));
}

void MoveBlockCommand::redo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin + m_delta;
    }
    m_doc->resolveAll();
}

void MoveBlockCommand::undo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin - m_delta;
    }
    m_doc->resolveAll();
}

bool MoveBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = static_cast<const MoveBlockCommand*>(other);
    if (cmd->m_blockIds != m_blockIds)
        return false;
    m_delta = m_delta + cmd->m_delta;
    return true;
}

// ─── RotateBlockCommand ───

RotateBlockCommand::RotateBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const cad::param::Transform2D& oldTf,
                                       const cad::param::Transform2D& newTf,
                                       const QUuid& oldEndTargetBlock,
                                       const QUuid& oldEndTargetPoint,
                                       const QUuid& newEndTargetBlock,
                                       const QUuid& newEndTargetPoint,
                                       const QUuid& releasedAttId,
                                       const cad::param::Attachment& releasedAttBackup,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_oldTf(oldTf)
    , m_newTf(newTf)
    , m_oldEndTargetBlock(oldEndTargetBlock)
    , m_oldEndTargetPoint(oldEndTargetPoint)
    , m_newEndTargetBlock(newEndTargetBlock)
    , m_newEndTargetPoint(newEndTargetPoint)
    , m_releasedAttId(releasedAttId)
    , m_releasedAttBackup(releasedAttBackup)
{
    setText(QStringLiteral("旋转"));
}

void RotateBlockCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_newTf;
        b->endTargetBlockId = m_newEndTargetBlock;
        b->endTargetPointId = m_newEndTargetPoint;
    }
    // 旋转 = 放弃跟随: the pivot was moved off the attachment point, so the
    // rotation detaches the follower link (undo restores it).
    if (!m_releasedAttId.isNull())
        m_doc->removeAttachment(m_releasedAttId);
    m_doc->resolveAll();
}

void RotateBlockCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_oldTf;
        b->endTargetBlockId = m_oldEndTargetBlock;
        b->endTargetPointId = m_oldEndTargetPoint;
    }
    if (!m_releasedAttId.isNull())
        m_doc->addAttachment(m_releasedAttBackup);
    m_doc->resolveAll();
}

// ─── DuplicateBlocksCommand ───

DuplicateBlocksCommand::DuplicateBlocksCommand(cad::param::ParamDocument* doc,
                                               cad::param::DuplicateResult result,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
{
    setText(QStringLiteral("复制 %1 条线段").arg(m_result.blocks.size()));
}

void DuplicateBlocksCommand::redo()
{
    // Linked variables first: their refName must be in the parameter map
    // before the cloned blocks resolve their length formulas.
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    for (const auto& b : m_result.blocks)
        m_doc->addBlock(b);
    for (const auto& att : m_result.attachments)
        m_doc->addAttachment(att);
    // Group clone (副本成新组): the clone set re-forms a user group.
    if (m_result.newGroup)
        m_doc->restoreGroup(*m_result.newGroup, m_result.newGroupMembers);
}

void DuplicateBlocksCommand::undo()
{
    // Dissolve the cloned group first (its members vanish right after).
    if (m_result.newGroup)
        m_doc->dissolveGroup(m_result.newGroup->id);
    // removeBlock also drops any attachment touching the clone; cloned
    // bridges may get mutated (released) when their first pin goes away,
    // but they are removed right after, so the mutation is irrelevant.
    for (const auto& att : m_result.attachments)
        m_doc->removeAttachment(att.id);
    for (const auto& b : m_result.blocks)
        m_doc->removeBlock(b.id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

// ─── RotateCopyCommand (旋转复制) ───

RotateCopyCommand::RotateCopyCommand(cad::param::ParamDocument* doc,
                                     cad::param::DuplicateResult result,
                                     const QUuid& originalBlockId,
                                     const QUuid& pivotPointId,
                                     const QUuid& clonePivotPointId,
                                     const QUuid& leaderSegmentId,
                                     double followerAngle,
                                     const QString& followerAngleFormula,
                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
    , m_originalBlockId(originalBlockId)
    , m_pivotPointId(pivotPointId)
    , m_clonePivotPointId(clonePivotPointId)
    , m_leaderSegmentId(leaderSegmentId)
{
    setText(QStringLiteral("旋转复制"));
    // The clone→original attachment is a normal follower whose follower angle
    // is measured RELATIVE to the original's current direction (so a
    // later rotation of the original keeps the copy's relative angle).
    m_att.fromBlockId = m_result.blocks.empty() ? QUuid() : m_result.blocks.front().id;
    m_att.fromPointId = m_clonePivotPointId;
    m_att.toBlockId = m_originalBlockId;
    m_att.toPointId = m_pivotPointId;
    m_att.toSegmentId = m_leaderSegmentId;
    m_att.followerAngle = followerAngle;
    m_att.followerAngleFormula = followerAngleFormula;
    m_att.rotationMode = cad::param::RotationMode::Angle;
}

void RotateCopyCommand::redo()
{
    if (m_result.blocks.empty()) return;
    // Linked variables first (their refName must resolve before the clone).
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    m_doc->addBlock(m_result.blocks.front());
    m_doc->addAttachment(m_att);
    m_doc->resolveAll();
}

void RotateCopyCommand::undo()
{
    if (m_result.blocks.empty()) return;
    m_doc->removeAttachment(m_att.id);
    m_doc->removeBlock(m_result.blocks.front().id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

bool RotateBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = static_cast<const RotateBlockCommand*>(other);
    if (cmd->m_blockId != m_blockId)
        return false;
    m_newTf = cmd->m_newTf;  // keep the oldest m_oldTf
    return true;
}

// ─── SetSegmentPropertyCommand ───

SetSegmentPropertyCommand::SetSegmentPropertyCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& segmentId,
    const Props& newProps,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newProps(newProps)
{
    setText(QStringLiteral("修改线段属性"));

    // Capture old state
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* s = b->findSegment(segmentId)) {
            m_oldProps.name = s->name;
            m_oldProps.role = s->role;
            m_oldProps.lineStyle = s->lineStyle;
            m_oldProps.color = s->color;
            m_oldProps.weight = s->weight;
            m_oldProps.visible = s->visible;
            m_oldProps.showName = s->showName;
            m_oldProps.showLength = s->showLength;
            m_oldProps.lengthFormula = s->lengthFormula;
        }
    }
}

void SetSegmentPropertyCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            s->name = m_newProps.name;
            s->role = m_newProps.role;
            s->lineStyle = m_newProps.lineStyle;
            s->color = m_newProps.color;
            s->weight = m_newProps.weight;
            s->visible = m_newProps.visible;
            s->showName = m_newProps.showName;
            s->showLength = m_newProps.showLength;
            s->lengthFormula = m_newProps.lengthFormula;
        }
    }
    m_doc->resolveAll();
}

void SetSegmentPropertyCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            s->name = m_oldProps.name;
            s->role = m_oldProps.role;
            s->lineStyle = m_oldProps.lineStyle;
            s->color = m_oldProps.color;
            s->weight = m_oldProps.weight;
            s->visible = m_oldProps.visible;
            s->showName = m_oldProps.showName;
            s->showLength = m_oldProps.showLength;
            s->lengthFormula = m_oldProps.lengthFormula;
        }
    }
    m_doc->resolveAll();
}

// ─── AddAuxPointCommand ───

AddAuxPointCommand::AddAuxPointCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const QUuid& segmentId,
                                       cad::param::ParamPoint pt,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pt(std::move(pt))
{
    setText(QStringLiteral("新建辅助点"));
}

void AddAuxPointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    seg->auxPointIds.push_back(m_pt.id);
    m_doc->resolveAll();
}

void AddAuxPointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // Linear undo guarantees any line that borrowed this point (pushed AFTER
    // this command) has already been undone — no dangling attachment remains.
    if (auto* seg = block->findSegment(m_segmentId)) {
        auto& ids = seg->auxPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), m_pt.id), ids.end());
    }
    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pt.id; }),
        pts.end());
    block->rebuildPointIndex();
    m_doc->resolveAll();
}

// ─── MovePointCommand ───

MovePointCommand::MovePointCommand(cad::param::ParamDocument* doc,
                                   const QUuid& blockId, const QUuid& pointId,
                                   const cad::geo::Vec2& oldPos,
                                   const cad::geo::Vec2& newPos,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldPos(oldPos)
    , m_newPos(newPos)
{
    setText(QStringLiteral("移动锚点"));
}

void MovePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* pt = block->findPoint(m_pointId);
    if (!pt) return;
    pt->freePos = m_newPos;
    pt->constraint = cad::param::PointConstraint::Free;
    m_doc->resolveAll();
}

void MovePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* pt = block->findPoint(m_pointId);
    if (!pt) return;
    pt->freePos = m_oldPos;
    pt->constraint = cad::param::PointConstraint::Free;
    m_doc->resolveAll();
}

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
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldTanIn(oldTanIn), m_oldTanOut(oldTanOut)
    , m_newTanIn(newTanIn), m_newTanOut(newTanOut)
    , m_oldAuto(oldAuto)
    , m_newAuto(newAuto)
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
    ++block->geometryEpoch;  // tangent change reshapes curve → force cache rebuild
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
    ++block->geometryEpoch;  // tangent change reshapes curve → force cache rebuild
    m_doc->resolveAll();
}

} // namespace cad::cmd
