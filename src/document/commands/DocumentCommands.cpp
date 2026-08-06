#include "DocumentCommands.h"

#include <QSet>
#include <QtLogging>

#include <cmath>

#include "parametric/ParamDocument.h"

namespace cad::cmd {

// ─── DrawLineCommand ───

DrawLineCommand::DrawLineCommand(cad::param::ParamDocument* doc,
                                 cad::param::Block block,
                                 const cad::param::Attachment& att,
                                 bool hasAttachment,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
    , m_att(att)
    , m_hasAttachment(hasAttachment)
{
    setText(QStringLiteral("画线"));
}

void DrawLineCommand::redo()
{
    m_doc->addBlock(m_block);
    if (m_hasAttachment)
        m_doc->addAttachment(m_att);
}

void DrawLineCommand::undo()
{
    // removeBlock also removes attachments referencing this block
    m_doc->removeBlock(m_block.id);
}

// ─── DeleteBlockCommand ───

DeleteBlockCommand::DeleteBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("删除线段"));

    const auto* b = doc->findBlock(blockId);
    if (!b) return;  // Already gone (e.g. cascaded away earlier in a macro).
    m_block = *b;
    m_valid = true;

    // Cascade set: the block itself + every bridge pinned to it (the model
    // layer releases those bridges as independent segments when the host
    // goes away — their pre-deletion state is snapshotted for undo).
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

    // Snapshot variables auto-deleted with the block (removeBlock purges
    // them in redo; undo must re-publish the exact copies). Measure variables
    // match as an endpoint OR as their owner bridge line.
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc->linkedVars())
            if (lv.sourceBlockId == srcId)
                m_linked.push_back(lv);
        for (const auto& mv : doc->measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                m_measures.push_back(mv);
    }

    // Group cascade snapshot: removing a member shrinks (and may dissolve)
    // its user group — capture the pristine record + membership for undo.
    m_groupId = doc->groupOfBlock(blockId);
    if (!m_groupId.isNull()) {
        if (const auto* g = doc->findGroup(m_groupId))
            m_groupSnapshot = *g;
        m_groupMembers = doc->blocksInGroup(m_groupId);
    }
}

void DeleteBlockCommand::redo()
{
    if (!m_valid) return;
    // removeBlock cascades: erases attachments + releases orphaned bridges.
    m_doc->removeBlock(m_block.id);
}

void DeleteBlockCommand::undo()
{
    if (!m_valid) return;
    // Bridges pinned to the deleted block were RELEASED by redo() (converted
    // to independent segments, still present in the document). Remove the
    // released versions first, then restore the pristine snapshots.
    for (const auto& bridge : m_bridges)
        m_doc->removeBlock(bridge.id);
    m_doc->addBlock(m_block);
    for (const auto& bridge : m_bridges)
        m_doc->addBlock(bridge);
    for (const auto& att : m_attachments)
        m_doc->addAttachment(att);
    // Re-publish the auto-deleted measurement variables.
    for (const auto& lv : m_linked)
        m_doc->addLinked(lv);
    for (const auto& mv : m_measures)
        m_doc->addMeasure(mv);
    // Restore the user group mutated/dissolved by redo's removeBlock().
    if (!m_groupId.isNull())
        m_doc->restoreGroup(m_groupSnapshot, m_groupMembers);
    if (!m_linked.empty() || !m_measures.empty())
        m_doc->resolveAll();
}

// ─── DrawMeasureLineCommand ───

DrawMeasureLineCommand::DrawMeasureLineCommand(cad::param::ParamDocument* doc,
                                               cad::param::Block block,
                                               cad::param::MeasureVariable mv,
                                               std::optional<cad::param::Attachment> followAtt,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
    , m_mv(std::move(mv))
    , m_followAtt(std::move(followAtt))
{
    setText(QStringLiteral("画测量线"));
}

void DrawMeasureLineCommand::redo()
{
    m_doc->addMeasure(m_mv);
    m_doc->addBlock(m_block);
    if (m_followAtt) {
        // Defensive: with the one-way cross-layer rule the aux→working
        // followAtt is legitimate, but addAttachment can still reject
        // (missing points, cycle, value-cycle guard). Surface any rejection
        // instead of silently leaving the measure line unfollowed.
        m_attAdded = m_doc->addAttachment(*m_followAtt);
        if (!m_attAdded) {
            // No Q_ASSERT here: a mid-redo abort would kill the debug process
            // halfway through the transaction. Warn in BOTH builds and leave
            // the undo path symmetric (m_attAdded == false → nothing removed).
            qWarning("DrawMeasureLineCommand::redo: addAttachment rejected "
                     "(follower=%s leader=%s)",
                     qPrintable(m_followAtt->fromBlockId.toString()),
                     qPrintable(m_followAtt->toBlockId.toString()));
        }
    }
}

void DrawMeasureLineCommand::undo()
{
    // Symmetric with redo: remove ONLY an attachment that was actually
    // established (a rejected edge was never added to the document).
    if (m_followAtt && m_attAdded)
        m_doc->removeAttachment(m_followAtt->id);
    m_doc->removeBlock(m_block.id);
    m_doc->removeMeasure(m_mv.id);
}

// ─── BakeMeasureCopyCommand ───

BakeMeasureCopyCommand::BakeMeasureCopyCommand(cad::param::ParamDocument* doc,
                                               const QUuid& sourceMeasureBlockId,
                                               int targetLayerIndex,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("烘焙到操作层"));

    if (!doc) return;
    // The target must be an existing WORKING layer (baking into the aux
    // calculation layer makes no sense).
    if (targetLayerIndex < 0 || targetLayerIndex >= doc->layerCount()
        || doc->isAuxLayer(targetLayerIndex))
        return;

    // The hit block must be a measurement line (owns a MeasureVariable).
    const cad::param::MeasureVariable* mv =
        doc->findMeasureByOwner(sourceMeasureBlockId);
    const cad::param::Block* src = doc->findBlock(sourceMeasureBlockId);
    if (!src || !mv || src->segments.empty()) return;

    const cad::param::Segment& srcSeg = src->segments.front();
    const cad::param::ParamPoint* pSp = src->findPoint(srcSeg.startPointId);
    const cad::param::ParamPoint* pEp = src->findPoint(srcSeg.endPointId);
    if (!pSp || !pEp || !pSp->resolved || !pEp->resolved) return;

    const cad::geo::Vec2 startWorld = src->transform.toWorld(pSp->resolvedPos);
    const cad::geo::Vec2 endWorld   = src->transform.toWorld(pEp->resolvedPos);
    const cad::geo::Vec2 delta = endWorld - startWorld;
    if (delta.lengthSquared() < 1e-10) return;

    // Free line on the target layer, same block-building pattern as
    // ToolSmartPen::createBridgeLine — but with NO attachment, NO endTarget
    // and the measurement's OWNER left untouched (still the source line).
    m_newBlock.layer = targetLayerIndex;
    m_newBlock.transform.origin = startWorld;
    m_newBlock.transform.rotation = std::atan2(delta.y, delta.x);

    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    ptStart.serial = doc->newPointSerial();
    const QUuid startId = ptStart.id;

    // End point: Polar along local X (block rotation carries the world angle).
    // Length stays a LIVE link to the measurement (M_xxx).
    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = delta.length();
    ptEnd.distanceFormula = mv->refName;
    ptEnd.angle = 0.0;
    ptEnd.serial = doc->newPointSerial();
    const QUuid endId = ptEnd.id;

    m_newBlock.addPoint(std::move(ptStart));
    m_newBlock.addPoint(std::move(ptEnd));

    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    seg.lengthFormula = mv->refName;
    seg.serial = doc->newLineSerial();
    m_newBlock.addSegment(std::move(seg));

    m_valid = true;
}

void BakeMeasureCopyCommand::redo()
{
    if (!m_valid) return;
    m_doc->addBlock(m_newBlock);
    m_doc->resolveAll();
}

void BakeMeasureCopyCommand::undo()
{
    if (!m_valid) return;
    // Full-snapshot symmetry (快照完整性): redo() mutated the document in
    // exactly one way — adding m_newBlock. The copy carries no attachments,
    // no group membership, no owned/linked variables, so removeBlock() alone
    // restores the pre-redo state; the source measure line and its variable
    // were never touched.
    m_doc->removeBlock(m_newBlock.id);
}

} // namespace cad::cmd
