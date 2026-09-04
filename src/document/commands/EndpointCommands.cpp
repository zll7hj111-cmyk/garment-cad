#include "document/commands/EndpointCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── SetEndTargetCommand (终点连接 = 终点指向) ───

void SetEndTargetCommand::apply(cad::param::Block* b, const QUuid& tb,
                                const QUuid& tp, double off,
                                const QString& offFormula)
{
    b->endTargetBlockId = tb;
    b->endTargetPointId = tp;
    b->endTargetOffset = off;
    b->endTargetOffsetFormula = offFormula;
}

SetEndTargetCommand::SetEndTargetCommand(cad::param::ParamDocument* doc,
                                         const QUuid& blockId,
                                         const QUuid& targetBlockId,
                                         const QUuid& targetPointId,
                                         double offsetDeg,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_newBlock(targetBlockId)
    , m_newPoint(targetPointId)
    , m_newOffset(offsetDeg)
{
    setText(QStringLiteral("设置终点连接"));
    if (const auto* b = doc->findBlock(blockId)) {
        m_oldBlock = b->endTargetBlockId;
        m_oldPoint = b->endTargetPointId;
        m_oldOffset = b->endTargetOffset;
        m_oldOffsetFormula = b->endTargetOffsetFormula;
    }
}

void SetEndTargetCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId))
        apply(b, m_newBlock, m_newPoint, m_newOffset, m_newOffsetFormula);
    m_doc->resolveAll();
}

void SetEndTargetCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId))
        apply(b, m_oldBlock, m_oldPoint, m_oldOffset, m_oldOffsetFormula);
    m_doc->resolveAll();
}

// ─── ConnectEndCommand (终点连接一步 undo: 指向 + 桥接测量) ───

ConnectEndCommand::ConnectEndCommand(cad::param::ParamDocument* doc,
                                     const QUuid& blockId,
                                     const QUuid& segmentId,
                                     const QUuid& targetBlockId,
                                     const QUuid& targetPointId,
                                     double offsetDeg, bool bridgeLand,
                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_targetBlockId(targetBlockId)
    , m_targetPointId(targetPointId)
    , m_offsetDeg(offsetDeg)
    , m_bridgeLand(bridgeLand)
{
    setText(QStringLiteral("终点连接"));
    if (const auto* b = doc->findBlock(blockId)) {
        m_oldBlock = b->endTargetBlockId;
        m_oldPoint = b->endTargetPointId;
        m_oldOffset = b->endTargetOffset;
        m_oldOffsetFormula = b->endTargetOffsetFormula;
        if (const auto* s = b->findSegment(segmentId)) {
            m_oldLengthFormula = s->lengthFormula;
            if (const auto* ep = b->findPoint(s->endPointId))
                m_oldEndDistFormula = ep->distanceFormula;
            // 既有归属测量 (本线自己的桥接测量): 重定向时更新其目标点。
            if (auto* mv = doc->findMeasureByOwner(blockId)) {
                if (!s->lengthFormula.isEmpty()
                    && mv->refName.compare(s->lengthFormula,
                                           Qt::CaseInsensitive) == 0) {
                    m_ownerMeasureId = mv->id;
                    m_oldMeasureBlockB = mv->blockB;
                    m_oldMeasurePointB = mv->pointB;
                }
            }
        }
    }
}

void ConnectEndCommand::redo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (!b || !s) return;

    // 1) 终点指向 (含偏移)。
    SetEndTargetCommand::apply(b, m_targetBlockId, m_targetPointId,
                               m_offsetDeg, QString());

    // 2) 桥接落点 + 终点 Polar 且无既有长度/距离公式 → 自动发布测量 M_xxx:
    //    终点 distanceFormula 驱动几何 (精确落点), 线段 lengthFormula 是测量
    //    线标记 —— 与 SmartPen 桥接创建 (LineFactory::createBridgeLine) 同语义;
    //    已有公式 = 保持用户公式, 仅指向。
    auto* ep = b->findPoint(s->endPointId);
    const bool canDrive = ep
        && ep->constraint == cad::param::PointConstraint::Polar
        && ep->distanceFormula.isEmpty() && s->lengthFormula.isEmpty();
    if (m_bridgeLand && canDrive
        && !m_targetBlockId.isNull() && !m_targetPointId.isNull()) {
        const auto* startPt = b->findPoint(s->startPointId);
        const auto* targetBlk = m_doc->findBlock(m_targetBlockId);
        const auto* targetPt = targetBlk
            ? targetBlk->findPoint(m_targetPointId) : nullptr;
        if (startPt && startPt->resolved && targetPt && targetPt->resolved) {
            cad::param::MeasureVariable mv;
            mv.blockA = m_blockId;
            mv.pointA = s->startPointId;
            mv.blockB = m_targetBlockId;
            mv.pointB = m_targetPointId;
            mv.value = b->transform.toWorld(startPt->resolvedPos)
                           .distanceTo(targetBlk->transform.toWorld(
                               targetPt->resolvedPos));
            mv.name = s->name;
            mv.refName = QStringLiteral("M_")
                + cad::param::Serial::randomPrefix().toUpper();
            mv.ownerBlockId = m_blockId;  // 删线即删测量 (与桥接创建同约定)
            m_addedMeasureId = mv.id;
            m_doc->addMeasure(mv);
            ep->distanceFormula = mv.refName;
            s->lengthFormula = mv.refName;
        }
    } else if (m_bridgeLand && !m_ownerMeasureId.isNull()
               && !m_targetBlockId.isNull() && !m_targetPointId.isNull()) {
        // 重定向: 已有归属测量 → 目标点改指新宿主 (终点继续精确落点)。
        if (auto* mv = m_doc->findMeasure(m_ownerMeasureId)) {
            mv->blockB = m_targetBlockId;
            mv->pointB = m_targetPointId;
            const auto* startPt = b->findPoint(s->startPointId);
            const auto* targetBlk = m_doc->findBlock(m_targetBlockId);
            const auto* targetPt = targetBlk
                ? targetBlk->findPoint(m_targetPointId) : nullptr;
            if (startPt && startPt->resolved && targetPt && targetPt->resolved)
                mv->value = b->transform.toWorld(startPt->resolvedPos)
                                .distanceTo(targetBlk->transform.toWorld(
                                    targetPt->resolvedPos));
        }
    }
    m_doc->resolveAll();
}

void ConnectEndCommand::undo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (b) {
        SetEndTargetCommand::apply(b, m_oldBlock, m_oldPoint, m_oldOffset,
                                   m_oldOffsetFormula);
        if (s) {
            s->lengthFormula = m_oldLengthFormula;
            if (auto* ep = b->findPoint(s->endPointId))
                ep->distanceFormula = m_oldEndDistFormula;
        }
    }
    if (!m_addedMeasureId.isNull())
        m_doc->removeMeasure(m_addedMeasureId);
    if (!m_ownerMeasureId.isNull()) {
        if (auto* mv = m_doc->findMeasure(m_ownerMeasureId)) {
            mv->blockB = m_oldMeasureBlockB;
            mv->pointB = m_oldMeasurePointB;
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

} // namespace cad::cmd
