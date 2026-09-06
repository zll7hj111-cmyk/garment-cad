#include "DocumentCommands.h"

#include <QSet>
#include <QtLogging>

#include <cmath>

#include "parametric/ParamDocument.h"

namespace cad::cmd {

// ─── DrawLineCommand ───

DrawLineCommand::DrawLineCommand(cad::param::ParamDocument* doc,
                                 cad::param::Block block,
                                 std::optional<cad::param::Attachment> att,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
    , m_att(std::move(att))
{
    setText(QStringLiteral("画线"));
}

void DrawLineCommand::redo()
{
    m_doc->addBlock(m_block);
    if (m_att.has_value())
        m_doc->addAttachment(*m_att);
}

void DrawLineCommand::undo()
{
    // removeBlock also removes attachments referencing this block
    m_doc->removeBlock(m_block.id);
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
                                               const QUuid& targetLayerId,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("烘焙到操作层"));

    if (!doc) return;
    // The target must be an existing WORKING layer (baking into the aux
    // calculation layer makes no sense).
    if (targetLayerId.isNull() || doc->layersView().layerIndex(targetLayerId) < 0
        || doc->layersView().isAuxLayer(targetLayerId))
        return;

    // The hit block must be a measurement line (owns a MeasureVariable).
    const cad::param::MeasureVariable* mv =
        doc->measurementsView().measureByOwner(sourceMeasureBlockId);
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
    m_newBlock.layer = targetLayerId;
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
    // no owned/linked variables, so removeBlock() alone
    // restores the pre-redo state; the source measure line and its variable
    // were never touched.
    m_doc->removeBlock(m_newBlock.id);
}

} // namespace cad::cmd
