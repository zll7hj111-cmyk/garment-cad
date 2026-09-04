#include <QtTest>
#include <QUuid>
#include <QUndoStack>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/ExpressionEvaluator.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/LayerCommands.h"
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Create a minimal horizontal line block and add it to the document.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm, const Vec2& origin = Vec2::zero())
{
    Block block;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

} // namespace

class TestBlockCommands : public QObject
{
    Q_OBJECT

private slots:
    void addBlock_undoRedo();
    void removeBlock_undoRedo();
    void moveBlock_undoRedo();
    void moveCurveAnchor_followDetachAndUndo();
    void setSegmentProperty_bumpsGeometryEpoch();
    void deleteImpactReport_matchesActualCascade();
    void resolveForDrag_followersMoveLive();
    void resolveForDrag_silentSignals();
    void resolveForDrag_incrementalMatchesFull();
    void resolveForDrag_ignoresDetachedAttachments();
    void curveRenderCache_filledByResolve();
    void undoStackLimitIsBounded();
};

void TestBlockCommands::addBlock_undoRedo()
{
    ParamDocument doc;
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);

    Block block;
    block.transform.origin = Vec2{10.0, 20.0};
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    block.addPoint(std::move(p1));

    QUuid blockId = block.id;

    cad::cmd::AddBlockCommand cmd(&doc, std::move(block));
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    QVERIFY(doc.findBlock(blockId) != nullptr);

    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);
    QVERIFY(doc.findBlock(blockId) == nullptr);

    // Redo restores.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    const auto* restored = doc.findBlock(blockId);
    QVERIFY(restored);
    QCOMPARE(restored->transform.origin.x, 10.0);
    QCOMPARE(restored->transform.origin.y, 20.0);
}

// ---------------------------------------------------------------------------
// RemoveBlockCommand: redo removes, undo restores block + state.
// ---------------------------------------------------------------------------

void TestBlockCommands::removeBlock_undoRedo()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 150.0);
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);

    cad::cmd::RemoveBlockCommand cmd(&doc, blockId);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);
    QVERIFY(doc.findBlock(blockId) == nullptr);

    // Undo restores the block.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    QCOMPARE(blk->id, blockId);
    QCOMPARE(static_cast<int>(blk->segments.size()), 1);
    QCOMPARE(blk->segments[0].id, segId);

    // Redo removes again.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);
}

// ---------------------------------------------------------------------------
// MoveBlockCommand: redo moves, undo restores origin, redo re-applies.
// ---------------------------------------------------------------------------

void TestBlockCommands::moveBlock_undoRedo()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 100.0);

    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    Vec2 origBefore = blk->transform.origin;

    Vec2 delta{30.0, -15.0};
    cad::cmd::MoveBlockCommand cmd(&doc, {blockId}, delta);
    cmd.redo();

    blk = doc.findBlock(blockId);
    QVERIFY(blk);
    QVERIFY(std::abs(blk->transform.origin.x - (origBefore.x + 30.0)) < 1e-9);
    QVERIFY(std::abs(blk->transform.origin.y - (origBefore.y - 15.0)) < 1e-9);

    // Undo restores original position.
    cmd.undo();
    blk = doc.findBlock(blockId);
    QVERIFY(blk);
    QVERIFY(std::abs(blk->transform.origin.x - origBefore.x) < 1e-9);
    QVERIFY(std::abs(blk->transform.origin.y - origBefore.y) < 1e-9);

    // Redo re-applies.
    cmd.redo();
    blk = doc.findBlock(blockId);
    QVERIFY(blk);
    QVERIFY(std::abs(blk->transform.origin.x - (origBefore.x + 30.0)) < 1e-9);
    QVERIFY(std::abs(blk->transform.origin.y - (origBefore.y - 15.0)) < 1e-9);
}

// ---------------------------------------------------------------------------
// Curve-anchor follow connection (曲线点跟随): a follow-connected anchor is
// pinned onto its target by the follow post-pass; once the tool detaches the
// connection (first move of a drag) the anchor must follow the cursor instead
// of being pulled back — and MoveCurveAnchorCommand must restore the whole
// connection state on undo / re-detach on redo.
// ---------------------------------------------------------------------------

void TestBlockCommands::moveCurveAnchor_followDetachAndUndo()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 100.0);
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // working layer (drag domain)

    // Curve anchor on the segment (chord 0..100 along local X).
    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = segId;
    anchor.interpPercent = 0.5;
    anchor.interpOffsetDist = 0.0;
    const QUuid anchorId = anchor.id;
    doc.findBlock(blockId)->addPoint(std::move(anchor));

    // Follow target: a free point at local (60, 10).
    ParamPoint target;
    target.constraint = PointConstraint::Free;
    target.freePos = Vec2{60.0, 10.0};
    const QUuid targetId = target.id;
    doc.findBlock(blockId)->addPoint(std::move(target));

    // Snap-connect: anchor follows the target (zero offset = coincide).
    auto* blk = doc.findBlock(blockId);
    auto* pt = blk->findPoint(anchorId);
    pt->followBlockId = blockId;
    pt->followPointId = targetId;
    pt->followOffset = Vec2::zero();
    doc.resolveAll();

    // The follow post-pass pins the anchor exactly onto the target.
    pt = blk->findPoint(anchorId);
    QVERIFY(pt->resolved);
    QVERIFY((pt->resolvedPos - Vec2{60.0, 10.0}).length() < 1e-9);

    // --- Drag simulation: the tool detaches the follow on the first move, so
    //     the anchor follows the cursor instead of the old target. ---
    pt->followBlockId = QUuid();
    pt->followPointId = QUuid();
    pt->followOffset = Vec2::zero();
    pt->interpPercent = 0.3;
    pt->interpOffsetDist = -5.0;
    doc.resolveAll();
    QVERIFY((pt->resolvedPos - Vec2{30.0, -5.0}).length() < 1e-9);

    // Undo the stroke: the old follow connection and chord params come back
    // (anchor pinned onto the target again); redo re-detaches and re-applies.
    cad::cmd::MoveCurveAnchorCommand cmd(
        &doc, blockId, anchorId,
        0.6, 10.0,                                // old percent/offset (follow-pinned)
        0.3, -5.0,                                // new percent/offset (dragged)
        blockId, targetId, Vec2::zero(),          // old follow (connected)
        QUuid(), QUuid(), Vec2::zero());          // new follow (detached)
    cmd.undo();
    pt = blk->findPoint(anchorId);
    QCOMPARE(pt->followPointId, targetId);
    QVERIFY((pt->resolvedPos - Vec2{60.0, 10.0}).length() < 1e-9);

    cmd.redo();
    pt = blk->findPoint(anchorId);
    QVERIFY(pt->followPointId.isNull());
    QVERIFY((pt->resolvedPos - Vec2{30.0, -5.0}).length() < 1e-9);
}

// ---------------------------------------------------------------------------
// SetSegmentPropertyCommand: a visibility/property-only edit moves no
// geometry, so it must still bump geometryEpoch — the canvas rebuilds its
// cached display state (BlockItem::m_lines) only when that epoch changes.
// Without the bump, the layer panel's eye toggle would have no effect until
// some unrelated geometry edit happens to rebuild the cache.
// ---------------------------------------------------------------------------

void TestBlockCommands::setSegmentProperty_bumpsGeometryEpoch()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);
    doc.resolveAll();

    auto* block = doc.findBlock(ls.blockId);
    QVERIFY(block);
    QVERIFY(block->findSegment(ls.segId));
    const quint64 epoch0 = block->geometryEpoch();

    QUndoStack stack;
    cad::cmd::SetSegmentPropertyCommand::Props props;
    auto* seg = block->findSegment(ls.segId);
    props.name = seg->name;
    props.role = seg->role;
    props.lineStyle = seg->lineStyle;
    props.color = seg->color;
    props.weight = seg->weight;
    props.visible = false;  // ← the only change: hide the line
    props.showName = seg->showName;
    props.showLength = seg->showLength;
    props.lengthFormula = seg->lengthFormula;
    stack.push(new cad::cmd::SetSegmentPropertyCommand(&doc, ls.blockId, ls.segId, props));

    // redo: property applied AND the canvas-invalidation epoch bumped.
    seg = doc.findBlock(ls.blockId)->findSegment(ls.segId);
    QCOMPARE(seg->visible, false);
    QVERIFY(doc.findBlock(ls.blockId)->geometryEpoch() >= epoch0 + 1);

    // undo: restored, epoch bumped again (canvas must re-reveal the line).
    stack.undo();
    seg = doc.findBlock(ls.blockId)->findSegment(ls.segId);
    QCOMPARE(seg->visible, true);
    QVERIFY(doc.findBlock(ls.blockId)->geometryEpoch() >= epoch0 + 2);

    // redo again: hidden once more.
    stack.redo();
    seg = doc.findBlock(ls.blockId)->findSegment(ls.segId);
    QCOMPARE(seg->visible, false);
    QVERIFY(doc.findBlock(ls.blockId)->geometryEpoch() >= epoch0 + 3);
}

// ---------------------------------------------------------------------------
// Delete-impact report (删除影响报告): the prediction must mirror what
// removeBlock() actually does — attachments vanish, the bridge loses a pin
// and is released, the intersection freezes, linked/measure/angle variables
// die, and formulas referencing the removed measurement names break.
// ---------------------------------------------------------------------------

void TestBlockCommands::deleteImpactReport_matchesActualCascade()
{
    ParamDocument doc;

    // L = the block to delete; F = follower (attached + consuming linked len).
    auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
    auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0);
    doc.findBlock(fId)->segments.front().lengthFormula = QStringLiteral("l_l");
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // working layer BEFORE pins exist

    // Bridge pinned between L and F (deleting L drops it below 2 pins).
    QUuid bridgeId, pin1Id, pin2Id;
    {
        Block bridge;
        bridge.isBridge = true;
        bridge.layer = layerIdAt(doc, 1);
        ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
        ParamPoint p2; p2.constraint = PointConstraint::Free; p2.freePos = Vec2{80.0, 30.0};
        pin1Id = p1.id; pin2Id = p2.id;
        bridge.addPoint(std::move(p1));
        bridge.addPoint(std::move(p2));
        bridgeId = bridge.id;
        doc.addBlock(std::move(bridge));
        Attachment pin1;
        pin1.isPin = true; pin1.fromBlockId = bridgeId; pin1.fromPointId = pin1Id;
        pin1.toBlockId = lId; pin1.toPointId = lStart;
        QVERIFY(doc.addAttachment(pin1));
        Attachment pin2;
        pin2.isPin = true; pin2.fromBlockId = bridgeId; pin2.fromPointId = pin2Id;
        pin2.toBlockId = fId; pin2.toPointId = fEnd;
        QVERIFY(doc.addAttachment(pin2));
    }

    // Follower attachment F -> L (vanishes with L).
    {
        Attachment att;
        att.fromBlockId = fId; att.fromPointId = fStart;
        att.toBlockId = lId;   att.toPointId = lEnd; att.toSegmentId = lSeg;
        QVERIFY(doc.addAttachment(att));
    }

    // Intersection block E: ray origin = L's start point (cross-block ref).
    auto [eId, eStart, eEnd, eSeg] = makeLine(doc, 70.0);
    QUuid ipId;
    {
        auto* e = doc.findBlock(eId);
        ParamPoint ip;
        ip.constraint = PointConstraint::Intersection;
        ip.refPointA = lStart;
        ip.hostSegmentId = eSeg;
        ip.interAngle = 90.0;
        ipId = ip.id;
        e->addPoint(std::move(ip));
    }
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // working layer (E was late)

    // Variables + formula depending on L.
    MeasureVariable mv;
    mv.blockA = lId; mv.pointA = lStart;
    mv.blockB = fId; mv.pointB = fStart;
    mv.ownerBlockId = lId;
    mv.refName = QStringLiteral("m_l");
    const QUuid mvId = mv.id;
    doc.addMeasure(mv);

    AngleMeasureVariable am;
    am.blockA = lId; am.segmentA = lSeg;
    am.blockB = fId; am.segmentB = fSeg;
    am.refName = QStringLiteral("a_l");
    const QUuid amId = am.id;
    doc.addAngleMeasure(am);

    LinkedVariable lv;
    lv.sourceBlockId = lId;
    lv.sourceSegmentId = lSeg;
    lv.refName = QStringLiteral("l_l");
    const QUuid lvId = lv.id;
    doc.addLinked(lv);

    FormulaVariable fv;
    fv.name = QStringLiteral("F1");
    fv.expression = QStringLiteral("m_l/2+6");
    const QUuid fvId = fv.id;
    doc.addFormula(fv);

    doc.resolveAll();

    // --- Prediction must match the actual cascade. ---
    const auto r = doc.deleteImpactReport(lId);
    QCOMPARE(r.attachmentsRemoved, 2);   // pin1 + F follower
    QCOMPARE(r.bridgesReleased, 1);      // bridge drops to 1 pin
    QCOMPARE(r.intersectionsFrozen, 1);  // ray origin gone
    QCOMPARE(r.linkedFrozen, 1);         // F's segment length formula
    QCOMPARE(r.linkedVarsRemoved, 1);
    QCOMPARE(r.measureVarsRemoved, 1);
    QCOMPARE(r.angleVarsRemoved, 1);
    QCOMPARE(r.formulasBroken, 1);       // F1 references m_l
    QVERIFY(r.hasImpact());

    // --- Execute the deletion and verify every predicted branch. ---
    doc.removeBlock(lId);

    QVERIFY(!doc.findBlock(bridgeId)->isBridge);               // released
    {
        const Block* eb = doc.findBlock(eId);
        const ParamPoint* ip = eb->findPoint(ipId);
        QVERIFY(ip != nullptr);
        QCOMPARE(ip->constraint, PointConstraint::OnSegment);  // frozen
    }
    QVERIFY(doc.findMeasure(mvId) == nullptr);
    QVERIFY(doc.findAngleMeasure(amId) == nullptr);
    QVERIFY(doc.findLinked(lvId) == nullptr);
    QVERIFY(doc.findBlock(fId)->segments.front().lengthFormula.isEmpty());  // frozen
    QVERIFY(!doc.parameters().contains(QStringLiteral("m_l")));  // operand gone
}

// ---------------------------------------------------------------------------
// resolveForDrag: simulating a drag frame — the leader origin is nudged and
// the follower must CASCADE live (same final state as a full resolveAll).
// ---------------------------------------------------------------------------

void TestBlockCommands::resolveForDrag_followersMoveLive()
{
    ParamDocument doc;
    auto [leaderId, lStartId, lEndId, lSegId] = makeLine(doc, 100.0);
    auto [followerId, fStartId, fEndId, fSegId] = makeLine(doc, 50.0);

    // Real drags happen on working layers (default Block::layer = 0 = aux).
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId  = followerId;
    att.fromPointId  = fStartId;
    att.toBlockId    = leaderId;
    att.toPointId    = lEndId;
    att.toSegmentId  = lSegId;
    att.followerAngle  = 0.0;
    QVERIFY(doc.addAttachment(att));

    const auto* leader = doc.findBlock(leaderId);
    const auto* follower = doc.findBlock(followerId);
    QVERIFY(leader && follower);
    const Vec2 leaderOrig = leader->transform.origin;
    const Vec2 followerOrig = follower->transform.origin;

    // Drag frame: nudge the leader, then resolve live (exactly what
    // ToolSelect::updateDrag does per mouse-move).
    const Vec2 delta{30.0, -15.0};
    auto* mutableLeader = doc.findBlock(leaderId);
    mutableLeader->transform.origin = leaderOrig + delta;
    doc.invalidateLayer(mutableLeader->layer);
    doc.resolveForDrag();

    // Follower must have cascaded: its start point snapped to the leader's
    // end point (leader origin moved by delta, segment extends +100mm along X).
    leader = doc.findBlock(leaderId);
    follower = doc.findBlock(followerId);
    QVERIFY(leader && follower);
    const Vec2 leaderEnd = leader->transform.origin + Vec2{100.0, 0.0};
    const Vec2 followerStart = follower->transform.toWorld(follower->findPoint(fStartId)->resolvedPos);
    QVERIFY((followerStart - leaderEnd).length() < 1e-6);

    // And the follower ORIGIN followed (not left behind at the pre-drag spot).
    QVERIFY((follower->transform.origin - followerOrig).length() > 1.0);

    // Drag-frame live result must equal a full resolveAll (deterministic).
    const Vec2 liveFollowerOrigin = follower->transform.origin;
    mutableLeader = doc.findBlock(leaderId);
    mutableLeader->transform.origin = leaderOrig;
    doc.resolveAll();
    mutableLeader = doc.findBlock(leaderId);
    mutableLeader->transform.origin = leaderOrig + delta;
    doc.resolveAll();
    follower = doc.findBlock(followerId);
    QVERIFY(follower);
    QVERIFY((follower->transform.origin - liveFollowerOrigin).length() < 1e-9);
}

// ---------------------------------------------------------------------------
// resolveForDrag must be PANEL-SILENT: resolved() (canvas sync) yes,
// documentChanged() (panel rebuild) no. resolveAll emits both.
// ---------------------------------------------------------------------------

void TestBlockCommands::resolveForDrag_silentSignals()
{
    ParamDocument doc;
    makeLine(doc, 100.0);
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);


    QSignalSpy resolvedSpy(&doc, &ParamDocument::resolved);
    QSignalSpy docChangedSpy(&doc, &ParamDocument::documentChanged);

    auto* blk = const_cast<Block*>(&doc.blocks().front());
    blk->transform.origin = blk->transform.origin + Vec2{5.0, 5.0};
    doc.invalidateLayer(blk->layer);
    doc.resolveForDrag();

    QCOMPARE(resolvedSpy.count(), 1);      // canvas position sync fires
    QCOMPARE(docChangedSpy.count(), 0);    // panels stay silent during drag

    doc.resolveAll();
    QCOMPARE(resolvedSpy.count(), 2);
    QCOMPARE(docChangedSpy.count(), 1);    // gesture commit refreshes panels
}

// ---------------------------------------------------------------------------
// resolveForDrag with dirty-subgraph narrowing (阶段2): the incremental pass
// must reproduce a full resolveAll bit-for-bit — including cross-block
// references (endpoint aim, intersection ray origin) and a pinned bridge.
// ---------------------------------------------------------------------------

void TestBlockCommands::resolveForDrag_incrementalMatchesFull()
{
    ParamDocument doc;

    // Chain: A -> B -> C (B follows A, C follows B).
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 60.0);
    auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 40.0);
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // working layer (drag domain)

    Attachment ab;
    ab.fromBlockId = bId; ab.fromPointId = bStart;
    ab.toBlockId = aId;   ab.toPointId = aEnd; ab.toSegmentId = aSeg;
    QVERIFY(doc.addAttachment(ab));
    Attachment bc;
    bc.fromBlockId = cId; bc.fromPointId = cStart;
    bc.toBlockId = bId;   bc.toPointId = bEnd; bc.toSegmentId = bSeg;
    QVERIFY(doc.addAttachment(bc));

    // Aim block D: aims its segment at A's end point (cross-block reference).
    auto [dId, dStart, dEnd, dSeg] = makeLine(doc, 50.0);
    {
        auto* d = doc.findBlock(dId);
        d->endTargetBlockId = aId;
        d->endTargetPointId = aEnd;
    }

    // Intersection block E: ray origin = A's start point (cross-block ref).
    auto [eId, eStart, eEnd, eSeg2] = makeLine(doc, 70.0);
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // D/E were created after the first pass
    {
        auto* e = doc.findBlock(eId);
        ParamPoint ip;
        ip.constraint = PointConstraint::Intersection;
        ip.refPointA = aStart;
        ip.hostSegmentId = eSeg2;
        ip.interAngle = 90.0;
        e->addPoint(std::move(ip));
    }

    // Bridge pinned between A and B (hosts move → bridge stretches).
    QUuid bridgeId, pin1Id, pin2Id;
    {
        Block bridge;
        bridge.isBridge = true;
        bridge.layer = layerIdAt(doc, 1);
        ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
        ParamPoint p2; p2.constraint = PointConstraint::Free; p2.freePos = Vec2{80.0, 30.0};
        pin1Id = p1.id; pin2Id = p2.id;
        bridge.addPoint(std::move(p1));
        bridge.addPoint(std::move(p2));
        bridgeId = bridge.id;
        doc.addBlock(std::move(bridge));
        Attachment pin1;
        pin1.isPin = true; pin1.fromBlockId = bridgeId; pin1.fromPointId = pin1Id;
        pin1.toBlockId = aId; pin1.toPointId = aStart;
        QVERIFY(doc.addAttachment(pin1));
        Attachment pin2;
        pin2.isPin = true; pin2.fromBlockId = bridgeId; pin2.fromPointId = pin2Id;
        pin2.toBlockId = bId; pin2.toPointId = bEnd;
        QVERIFY(doc.addAttachment(pin2));
    }
    doc.resolveAll();

    // Drag frame: nudge A (the whole graph cascades from it).
    const Vec2 delta{25.0, -10.0};
    {
        auto* a = doc.findBlock(aId);
        a->transform.origin = a->transform.origin + delta;
    }
    doc.invalidateLayer(layerIdAt(doc, 1));
    doc.resolveForDrag({aId});

    auto snapshot = [&doc](const QUuid& id) {
        const auto* b = doc.findBlock(id);
        QList<Vec2> pts;
        for (const auto& p : b->points) pts.push_back(p.resolvedPos);
        return std::make_tuple(b->transform.origin, b->transform.rotation, pts);
    };
    const auto incA = snapshot(aId);
    const auto incB = snapshot(bId);
    const auto incC = snapshot(cId);
    const auto incD = snapshot(dId);
    const auto incE = snapshot(eId);
    const auto incBridge = snapshot(bridgeId);

    // Full resolve from the same pre-drag state must reproduce everything.
    {
        auto* a = doc.findBlock(aId);
        a->transform.origin = a->transform.origin - delta;
    }
    doc.resolveAll();
    {
        auto* a = doc.findBlock(aId);
        a->transform.origin = a->transform.origin + delta;
    }
    doc.resolveAll();

    auto compare = [](const auto& inc, const auto& full, const char* what) {
        const auto& [incO, incR, incPts] = inc;
        const auto& [fullO, fullR, fullPts] = full;
        QVERIFY2((incO - fullO).length() < 1e-9, what);
        QVERIFY2(std::abs(incR - fullR) < 1e-9, what);
        QVERIFY2(incPts.size() == fullPts.size(), what);
        for (int i = 0; i < incPts.size(); ++i)
            QVERIFY2((incPts[i] - fullPts[i]).length() < 1e-9, what);
    };
    compare(incA, snapshot(aId), "A");
    compare(incB, snapshot(bId), "B");
    compare(incC, snapshot(cId), "C");
    compare(incD, snapshot(dId), "D (aim)");
    compare(incE, snapshot(eId), "E (intersect)");
    compare(incBridge, snapshot(bridgeId), "bridge");
}

// ---------------------------------------------------------------------------
// resolveForDrag with ignored attachments (阶段2): a follower whose
// cross-selection attachment is pending removal must NOT be pulled back to its
// leader during the drag — without the ignore it snaps as usual.
// ---------------------------------------------------------------------------

void TestBlockCommands::resolveForDrag_ignoresDetachedAttachments()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
    for (const auto& b : doc.blocks()) if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);  // working layer (drag domain)

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    QVERIFY(doc.addAttachment(att));

    const Vec2 bOrig = doc.findBlock(bId)->transform.origin;

    // Drag A while the cross-selection attachment is IGNORED: B must stay put
    // (the dragged set moves as one rigid body; the link breaks at release).
    {
        auto* a = doc.findBlock(aId);
        a->transform.origin = a->transform.origin + Vec2{40.0, 0.0};
    }
    doc.invalidateLayer(layerIdAt(doc, 1));
    doc.resolveForDrag({aId}, {att.id});
    const Vec2 bAfterIgnore = doc.findBlock(bId)->transform.origin;
    QVERIFY((bAfterIgnore - bOrig).length() < 1e-9);

    // Same frame WITHOUT the ignore: the resolver snaps B onto A's end.
    doc.resolveForDrag({aId});
    const Vec2 bAfterSnap = doc.findBlock(bId)->transform.origin;
    QVERIFY((bAfterSnap - bOrig).length() > 1.0);

    // The document still holds the attachment (it is only skipped, not
    // removed — removal happens through the undo command at drag end).
    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
}

// ---------------------------------------------------------------------------
// 阶段3 render cache: CurveSpanEntry must carry the flatten polyline, label
// midpoint/tangent and exact arc length (computed once per resolve in the
// DATA layer) — consistent with direct CurveMath evaluation, so the canvas
// only rotates/flips instead of recomputing per frame.
// ---------------------------------------------------------------------------

void TestBlockCommands::curveRenderCache_filledByResolve()
{
    ParamDocument doc;

    // Curve block: start (0,0) → curve anchor (50,20) → end (100,0), Bezier.
    Block block;
    block.layer = layerIdAt(doc, 1);
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    ParamPoint p2; p2.constraint = PointConstraint::CurveAnchor; p2.freePos = Vec2{50.0, 20.0};
    ParamPoint p3; p3.constraint = PointConstraint::Free; p3.freePos = Vec2{100.0, 0.0};
    const QUuid sId = p1.id, mId = p2.id, eId = p3.id;

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = sId;
    seg.endPointId = eId;
    seg.passPointIds = {mId};
    const QUuid segId = seg.id;
    p2.hostSegmentId = segId;  // anchors are positioned on their host's chord

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    block.addPoint(std::move(p3));
    block.addSegment(std::move(seg));

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    doc.resolveAll();

    const auto* b = doc.findBlock(blockId);
    QVERIFY(b);
    const auto* entry = b->curveSpanEntry(segId);
    QVERIFY(entry);
    QVERIFY(!entry->spans.empty());

    // Flatten cache: non-empty, starts at the first anchor, matches a direct
    // flatten of the spans (the canvas trusts this cache for rendering).
    QVERIFY(!entry->flatLocal.empty());
    QVERIFY((entry->flatLocal.front() - entry->anchors.front()).length() < 1e-9);
    const auto direct = cad::geo::flattenBezierSpans(entry->spans, 0.1);
    QCOMPARE(static_cast<int>(entry->flatLocal.size()),
             static_cast<int>(direct.size()));
    for (size_t i = 0; i < direct.size(); ++i)
        QVERIFY((entry->flatLocal[i] - direct[i]).length() < 1e-12);

    // Label cache: ARC-LENGTH midpoint of the whole curve + its tangent ==
    // direct evaluation (2026-08-28: was the parametric middle of span 0,
    // which drifted toward the curve start on multi-span curves).
    const double tMid = cad::geo::arcLengthToParam(entry->spans,
                                                   entry->arcLengthMm * 0.5);
    const auto mid = cad::geo::evalCurve(entry->spans, tMid);
    QVERIFY((entry->labelLocal - mid).length() < 1e-12);
    const auto tan = cad::geo::evalCurveTangent(entry->spans, tMid);
    QVERIFY((entry->labelLocalDir - tan).length() < 1e-12);
    // The anchor resolves onto the chord (percent 0.5, offset 0 → (50,0)),
    // so this "curve" is a straight 100mm line: the arc-length midpoint must
    // be (50,0) — the old span-0 parametric midpoint happened to coincide
    // here, multi-span asymmetric curves are where it drifted.
    QVERIFY(entry->labelLocal.distanceTo(Vec2{50.0, 0.0}) < 1e-3);

    // Arc length cache == direct integration.
    QVERIFY(entry->arcLengthMm > 0.0);
    QVERIFY(std::abs(entry->arcLengthMm
                     - cad::geo::totalArcLength(entry->spans)) < 1e-9);
}

// ---------------------------------------------------------------------------
// Formula graph cycle (公式自引用/循环引用): the evaluator must detect the
// cycle and fall back to the legacy bounded fixpoint — both formulas end up
// invalid (each pass fails on the unresolved partner), no infinite loop.
// ---------------------------------------------------------------------------

void TestBlockCommands::undoStackLimitIsBounded()
{
    ParamDocument doc;
    QVERIFY(doc.undoStack());
    QCOMPARE(doc.undoStack()->undoLimit(), ParamDocument::kUndoStackLimit);

    // Push more commands than the limit; the stack must cap out and keep the
    // most recent ones (the oldest are dropped, not queued forever).
    const int extra = 20;
    for (int i = 0; i < ParamDocument::kUndoStackLimit + extra; ++i) {
        Variable var;
        var.name = QStringLiteral("v%1").arg(i);
        var.refName = QStringLiteral("ref%1").arg(i);
        var.value = static_cast<double>(i);
        doc.undoStack()->push(new cad::cmd::AddVariableCommand(&doc, var));
    }
    QCOMPARE(doc.undoStack()->count(), ParamDocument::kUndoStackLimit);
    QVERIFY(doc.undoStack()->count() <= ParamDocument::kUndoStackLimit);

    // The retained history is still functional: undo/redo round-trip works.
    const int before = static_cast<int>(doc.variables().size());
    doc.undoStack()->undo();
    QCOMPARE(static_cast<int>(doc.variables().size()), before - 1);
    doc.undoStack()->redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), before);
    QCOMPARE(doc.undoStack()->count(), ParamDocument::kUndoStackLimit);
}

QTEST_MAIN(TestBlockCommands)
#include "test_block_commands.moc"
