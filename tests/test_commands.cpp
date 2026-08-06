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
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/LayerCommands.h"
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

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

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    // BlockCommands
    void addBlock_undoRedo();
    void removeBlock_undoRedo();
    void moveBlock_undoRedo();
    void moveCurveAnchor_followDetachAndUndo();

    // Delete-impact report (删除影响报告)
    void deleteImpactReport_matchesActualCascade();

    // Drag live-resolve (阶段1: 拖拽实时求解)
    void resolveForDrag_followersMoveLive();
    void resolveForDrag_silentSignals();

    // Dirty-subgraph resolve (阶段2: 依赖边表 + 脏传播)
    void resolveForDrag_incrementalMatchesFull();
    void resolveForDrag_ignoresDetachedAttachments();

    // Render cache in the data layer (阶段3: 几何缓存前移)
    void curveRenderCache_filledByResolve();

    // Formula topology + case-folding fallback (公式优化)
    void recomputeFormulas_cycleFallsBackToFixpoint();
    void evaluator_caseInsensitiveFallback();

    // VariableCommands
    void addVariable_undoRedo();
    void removeVariable_undoRedo();
    void setVariableValue_undoRedo();
    void addFormula_undoRedo();
    void measureCommands_undoRedo();
    void angleMeasureCommands_undoRedo();

    // DocumentCommands: bake measure copy (烘焙到操作层 = 复制而非移动)
    void bakeMeasureCopy_undoRedo();
    void bakeMeasureCopy_invalidCases();

    // LayerCommands: undo must restore the active layer snapshot
    void layerCommands_activeLayerRestored();
};

// ---------------------------------------------------------------------------
// AddBlockCommand: redo adds, undo removes, redo re-adds.
// ---------------------------------------------------------------------------
void TestCommands::addBlock_undoRedo()
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
void TestCommands::removeBlock_undoRedo()
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
void TestCommands::moveBlock_undoRedo()
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
void TestCommands::moveCurveAnchor_followDetachAndUndo()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 100.0);
    for (auto& b : doc.blocks()) b.layer = 1;  // working layer (drag domain)

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
// Delete-impact report (删除影响报告): the prediction must mirror what
// removeBlock() actually does — attachments vanish, the bridge loses a pin
// and is released, the intersection freezes, linked/measure/angle variables
// die, and formulas referencing the removed measurement names break.
// ---------------------------------------------------------------------------
void TestCommands::deleteImpactReport_matchesActualCascade()
{
    ParamDocument doc;

    // L = the block to delete; F = follower (attached + consuming linked len).
    auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
    auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0);
    doc.findBlock(fId)->segments.front().lengthFormula = QStringLiteral("l_l");
    for (auto& b : doc.blocks()) b.layer = 1;  // working layer BEFORE pins exist

    // Bridge pinned between L and F (deleting L drops it below 2 pins).
    QUuid bridgeId, pin1Id, pin2Id;
    {
        Block bridge;
        bridge.isBridge = true;
        bridge.layer = 1;
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
    for (auto& b : doc.blocks()) b.layer = 1;  // working layer (E was late)

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
void TestCommands::resolveForDrag_followersMoveLive()
{
    ParamDocument doc;
    auto [leaderId, lStartId, lEndId, lSegId] = makeLine(doc, 100.0);
    auto [followerId, fStartId, fEndId, fSegId] = makeLine(doc, 50.0);

    // Real drags happen on working layers (default Block::layer = 0 = aux).
    for (auto& b : doc.blocks()) b.layer = 1;

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
void TestCommands::resolveForDrag_silentSignals()
{
    ParamDocument doc;
    makeLine(doc, 100.0);
    for (auto& b : doc.blocks()) b.layer = 1;


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
void TestCommands::resolveForDrag_incrementalMatchesFull()
{
    ParamDocument doc;

    // Chain: A -> B -> C (B follows A, C follows B).
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 60.0);
    auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 40.0);
    for (auto& b : doc.blocks()) b.layer = 1;  // working layer (drag domain)

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
    for (auto& b : doc.blocks()) b.layer = 1;  // D/E were created after the first pass
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
        bridge.layer = 1;
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
    doc.invalidateLayer(1);
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
void TestCommands::resolveForDrag_ignoresDetachedAttachments()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
    for (auto& b : doc.blocks()) b.layer = 1;  // working layer (drag domain)

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
    doc.invalidateLayer(1);
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
void TestCommands::curveRenderCache_filledByResolve()
{
    ParamDocument doc;

    // Curve block: start (0,0) → curve anchor (50,20) → end (100,0), Bezier.
    Block block;
    block.layer = 1;
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

    // Label cache: parametric midpoint + tangent == direct evaluation.
    const auto mid = cad::geo::evalCurve(entry->spans, 0.5);
    QVERIFY((entry->labelLocal - mid).length() < 1e-12);
    const auto tan = cad::geo::evalCurveTangent(entry->spans, 0.5);
    QVERIFY((entry->labelLocalDir - tan).length() < 1e-12);

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
void TestCommands::recomputeFormulas_cycleFallsBackToFixpoint()
{
    ParamDocument doc;
    Variable v;
    v.name = QStringLiteral("B");
    v.refName = QStringLiteral("b");
    v.value = 840.0;  // mm
    doc.addVariable(v);

    // Mutual cycle: A references F1, F1 references A.
    FormulaVariable a;
    a.name = QStringLiteral("A");
    a.expression = QStringLiteral("F1/2");
    const QUuid aId = a.id;
    doc.addFormula(a);
    FormulaVariable f1;
    f1.name = QStringLiteral("F1");
    f1.expression = QStringLiteral("A/2");
    const QUuid f1Id = f1.id;
    doc.addFormula(f1);

    // Self-reference: S = S+1.
    FormulaVariable s;
    s.name = QStringLiteral("S");
    s.expression = QStringLiteral("S+1");
    const QUuid sId = s.id;
    doc.addFormula(s);

    // A healthy leaf must still evaluate (the cycle fallback runs the full
    // bounded fixpoint, so non-cycle formulas keep working).
    FormulaVariable leaf;
    leaf.name = QStringLiteral("LEAF");
    leaf.expression = QStringLiteral("b/2+6");
    const QUuid leafId = leaf.id;
    doc.addFormula(leaf);

    doc.recomputeFormulas();

    // Cycle members: invalid (their partner never becomes available).
    QVERIFY(doc.findFormula(aId));
    QVERIFY(!doc.findFormula(aId)->valid);
    QVERIFY(doc.findFormula(f1Id));
    QVERIFY(!doc.findFormula(f1Id)->valid);
    QVERIFY(doc.findFormula(sId));
    QVERIFY(!doc.findFormula(sId)->valid);

    // Healthy formulas unaffected by the cycle fallback.
    QVERIFY(doc.findFormula(leafId)->valid);
    const double leafCm = cad::geo::Units::mmToCm(doc.findFormula(leafId)->baseValue);
    QVERIFY(std::abs(leafCm - (84.0 / 2.0 + 6.0)) < 1e-9);
}

// ---------------------------------------------------------------------------
// Case-insensitive variable fallback: a formula referencing a variable in a
// different case than its registered key must resolve through the O(1)
// per-pass normLookup (and produce the same value as the exact-key path).
// ---------------------------------------------------------------------------
void TestCommands::evaluator_caseInsensitiveFallback()
{
    ParamDocument doc;
    // Register the parameter ONLY in uppercase — the formula uses lowercase.
    doc.setParameter(QStringLiteral("B"), 84.0);  // cm

    // Block whose segment length is driven by the lowercase reference.
    Block block;
    block.layer = 1;
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;
    ParamPoint p2; p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distanceFormula = QStringLiteral("b/2");  // lowercase -> case-folded hit
    const QUuid endId = p2.id;
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));
    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    doc.resolveAll();

    // b/2 = 84/2 = 42 cm = 420 mm; the Polar distance (mm) must reflect it.
    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    const ParamPoint* ep = blk->findPoint(endId);
    QVERIFY(ep);
    QVERIFY(ep->resolved);
    QVERIFY(std::abs(ep->resolvedPos.x - 420.0) < 1e-9);

    // Same value through the direct evaluator path (no ctx -> linear scan).
    const auto direct = ExpressionEvaluator::evaluate(
        QStringLiteral("b/2"), QHash<QString, double>{{QStringLiteral("B"), 84.0}});
    QVERIFY(direct.ok);
    QVERIFY(std::abs(direct.value - 42.0) < 1e-9);
}

// ---------------------------------------------------------------------------
// AddVariableCommand: redo adds, undo removes, redo re-adds.
// ---------------------------------------------------------------------------
void TestCommands::addVariable_undoRedo()
{
    ParamDocument doc;
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);

    Variable var;
    var.name = QStringLiteral("胸围");
    var.refName = QStringLiteral("b");
    var.value = 88.0;
    QUuid varId = var.id;

    cad::cmd::AddVariableCommand cmd(&doc, var);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    QVERIFY(doc.findVariable(varId) != nullptr);
    QCOMPARE(doc.findVariable(varId)->value, 88.0);

    cmd.undo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);
    QVERIFY(doc.findVariable(varId) == nullptr);

    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    QCOMPARE(doc.findVariable(varId)->refName, QStringLiteral("b"));
}

// ---------------------------------------------------------------------------
// RemoveVariableCommand: redo removes, undo restores.
// ---------------------------------------------------------------------------
void TestCommands::removeVariable_undoRedo()
{
    ParamDocument doc;

    Variable var;
    var.name = QStringLiteral("腰围");
    var.refName = QStringLiteral("w");
    var.value = 72.0;
    QUuid varId = var.id;
    doc.addVariable(var);
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);

    cad::cmd::RemoveVariableCommand cmd(&doc, varId);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);

    // Undo restores with all fields.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    const auto* v = doc.findVariable(varId);
    QVERIFY(v);
    QCOMPARE(v->name, QStringLiteral("腰围"));
    QCOMPARE(v->refName, QStringLiteral("w"));
    QCOMPARE(v->value, 72.0);

    // Redo removes again.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);
}

// ---------------------------------------------------------------------------
// SetVariableValueCommand: redo sets new value, undo restores old value.
// ---------------------------------------------------------------------------
void TestCommands::setVariableValue_undoRedo()
{
    ParamDocument doc;

    Variable var;
    var.name = QStringLiteral("肩宽");
    var.refName = QStringLiteral("s");
    var.value = 40.0;
    QUuid varId = var.id;
    doc.addVariable(var);

    cad::cmd::SetVariableValueCommand cmd(&doc, varId, 45.0);
    cmd.redo();
    QCOMPARE(doc.findVariable(varId)->value, 45.0);

    cmd.undo();
    QCOMPARE(doc.findVariable(varId)->value, 40.0);

    cmd.redo();
    QCOMPARE(doc.findVariable(varId)->value, 45.0);
}

// ---------------------------------------------------------------------------
// AddFormulaCommand: redo adds formula, undo removes, redo re-adds.
// ---------------------------------------------------------------------------
void TestCommands::addFormula_undoRedo()
{
    ParamDocument doc;
    QCOMPARE(static_cast<int>(doc.formulas().size()), 0);

    FormulaVariable formula;
    formula.name = QStringLiteral("胸宽");
    formula.expression = QStringLiteral("b/2+6");
    QUuid formulaId = formula.id;

    cad::cmd::AddFormulaCommand cmd(&doc, formula);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 1);
    QVERIFY(doc.findFormula(formulaId) != nullptr);
    QCOMPARE(doc.findFormula(formulaId)->expression, QStringLiteral("b/2+6"));

    cmd.undo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 0);
    QVERIFY(doc.findFormula(formulaId) == nullptr);

    cmd.redo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 1);
    QCOMPARE(doc.findFormula(formulaId)->name, QStringLiteral("胸宽"));
}

// ---------------------------------------------------------------------------
// Measure commands: remove/set round-trips keep all fields restorable.
// ---------------------------------------------------------------------------
void TestCommands::measureCommands_undoRedo()
{
    ParamDocument doc;
    const auto lineA = makeLine(doc, 100.0);
    const auto lineB = makeLine(doc, 80.0, Vec2{0.0, 50.0});

    MeasureVariable mv;
    mv.name = QStringLiteral("桥接距离");
    mv.refName = QStringLiteral("M_test1");
    mv.blockA = lineA.blockId;
    mv.pointA = lineA.endId;
    mv.blockB = lineB.blockId;
    mv.pointB = lineB.startId;
    mv.comment = QStringLiteral("备注");
    const QUuid mvId = mv.id;
    doc.addMeasure(mv);
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);

    // Set: rename + comment round-trip.
    MeasureVariable edited = mv;
    edited.name = QStringLiteral("新名称");
    edited.comment = QStringLiteral("新备注");
    cad::cmd::SetMeasureCommand setCmd(&doc, edited);
    setCmd.redo();
    QCOMPARE(doc.findMeasure(mvId)->name, QStringLiteral("新名称"));
    QCOMPARE(doc.findMeasure(mvId)->comment, QStringLiteral("新备注"));
    setCmd.undo();
    QCOMPARE(doc.findMeasure(mvId)->name, QStringLiteral("桥接距离"));
    QCOMPARE(doc.findMeasure(mvId)->comment, QStringLiteral("备注"));

    // Remove: undo restores every field.
    cad::cmd::RemoveMeasureCommand rmCmd(&doc, mvId);
    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 0);
    QVERIFY(doc.findMeasure(mvId) == nullptr);

    rmCmd.undo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);
    const auto* restored = doc.findMeasure(mvId);
    QVERIFY(restored);
    QCOMPARE(restored->name, QStringLiteral("桥接距离"));
    QCOMPARE(restored->refName, QStringLiteral("M_test1"));
    QCOMPARE(restored->blockA, lineA.blockId);
    QCOMPARE(restored->pointA, lineA.endId);
    QCOMPARE(restored->blockB, lineB.blockId);
    QCOMPARE(restored->pointB, lineB.startId);

    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 0);
}

void TestCommands::angleMeasureCommands_undoRedo()
{
    ParamDocument doc;
    const auto lineA = makeLine(doc, 100.0);
    const auto lineB = makeLine(doc, 80.0, Vec2{0.0, 50.0});

    AngleMeasureVariable am;
    am.name = QStringLiteral("夹角");
    am.refName = QStringLiteral("MA_test1");
    am.blockA = lineA.blockId;
    am.segmentA = lineA.segId;
    am.blockB = lineB.blockId;
    am.segmentB = lineB.segId;
    am.comment = QStringLiteral("备注");
    const QUuid amId = am.id;
    doc.addAngleMeasure(am);
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 1);

    // Set: rename round-trip.
    AngleMeasureVariable edited = am;
    edited.name = QStringLiteral("新夹角");
    cad::cmd::SetAngleMeasureCommand setCmd(&doc, edited);
    setCmd.redo();
    QCOMPARE(doc.findAngleMeasure(amId)->name, QStringLiteral("新夹角"));
    setCmd.undo();
    QCOMPARE(doc.findAngleMeasure(amId)->name, QStringLiteral("夹角"));

    // Remove: undo restores every field.
    cad::cmd::RemoveAngleMeasureCommand rmCmd(&doc, amId);
    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 0);

    rmCmd.undo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 1);
    const auto* restored = doc.findAngleMeasure(amId);
    QVERIFY(restored);
    QCOMPARE(restored->refName, QStringLiteral("MA_test1"));
    QCOMPARE(restored->segmentA, lineA.segId);
    QCOMPARE(restored->segmentB, lineB.segId);

    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 0);
}

// ---------------------------------------------------------------------------
// BakeMeasureCopyCommand (烘焙到操作层): baking = COPY, not move — the source
// measurement line stays on its layer as the measurement owner while a NEW
// free line appears on the target working layer at the source's world pose,
// its length live-linked to the measurement variable (M_xxx).
// ---------------------------------------------------------------------------
void TestCommands::bakeMeasureCopy_undoRedo()
{
    ParamDocument doc;
    QVERIFY(doc.layerCount() >= 2);
    QVERIFY(doc.isAuxLayer(0));
    const int targetLayer = 1;

    // Source measure line on the AUX layer: 120 mm, world-rotated 30°.
    Block mline;
    mline.layer = 0;
    mline.transform.origin = Vec2{50.0, -40.0};
    mline.transform.rotation = 30.0 * M_PI / 180.0;
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    ParamPoint p2; p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1.id; p2.distance = 120.0; p2.angle = 0.0;
    const QUuid mStart = p1.id, mEnd = p2.id;
    mline.addPoint(std::move(p1));
    mline.addPoint(std::move(p2));
    Segment mseg; mseg.startPointId = mStart; mseg.endPointId = mEnd;
    mline.addSegment(std::move(mseg));
    const QUuid mlId = mline.id;
    doc.addBlock(std::move(mline));

    MeasureVariable mv;
    mv.refName = QStringLiteral("M_bake1");
    mv.blockA = mlId; mv.pointA = mStart;
    mv.blockB = mlId; mv.pointB = mEnd;
    mv.ownerBlockId = mlId;
    const QUuid mvId = mv.id;
    doc.addMeasure(mv);
    doc.resolveAll();

    // findMeasureByOwner: reverse lookup by owner (and miss for strangers).
    QVERIFY(doc.findMeasureByOwner(mlId) != nullptr);
    QCOMPARE(doc.findMeasureByOwner(mlId)->id, mvId);
    QVERIFY(doc.findMeasureByOwner(QUuid::createUuid()) == nullptr);
    QVERIFY(doc.findMeasureByOwner(QUuid()) == nullptr);

    // Source world endpoints — the copy must land at exactly this pose.
    const auto* src = doc.findBlock(mlId);
    QVERIFY(src);
    const Vec2 srcStart = src->transform.toWorld(src->findPoint(mStart)->resolvedPos);
    const Vec2 srcEnd   = src->transform.toWorld(src->findPoint(mEnd)->resolvedPos);
    const double srcRotation = src->transform.rotation;

    cad::cmd::BakeMeasureCopyCommand cmd(&doc, mlId, targetLayer);
    QVERIFY(cmd.isValid());
    const QUuid newId = cmd.newBlockId();

    // --- redo: the copy appears on the target working layer ---
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);
    const auto* baked = doc.findBlock(newId);
    QVERIFY(baked);
    QCOMPARE(baked->layer, targetLayer);
    QCOMPARE(static_cast<int>(baked->segments.size()), 1);
    // Length = live link to the measurement variable (NOT the owner of it).
    QCOMPARE(baked->segments.front().lengthFormula, QStringLiteral("M_bake1"));
    QCOMPARE(doc.findMeasure(mvId)->ownerBlockId, mlId);
    // Free line: no attachments, no end aim.
    QVERIFY(doc.attachments().empty());
    QVERIFY(baked->endTargetBlockId.isNull());
    QVERIFY(!baked->isBridge);
    // World pose mirrors the source measure line; the M_bake1 formula
    // resolves to the measured length (120 mm).
    QVERIFY(std::abs(baked->transform.rotation - srcRotation) < 1e-9);
    const auto& bSeg = baked->segments.front();
    const Vec2 bStart = baked->transform.toWorld(
        baked->findPoint(bSeg.startPointId)->resolvedPos);
    const Vec2 bEnd = baked->transform.toWorld(
        baked->findPoint(bSeg.endPointId)->resolvedPos);
    QVERIFY((bStart - srcStart).length() < 1e-6);
    QVERIFY((bEnd - srcEnd).length() < 1e-6);
    QVERIFY(std::abs((bEnd - bStart).length() - 120.0) < 1e-6);

    // --- undo: the copy vanishes; source line + variable untouched ---
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    QVERIFY(doc.findBlock(newId) == nullptr);
    QVERIFY(doc.findBlock(mlId) != nullptr);
    QCOMPARE(doc.findBlock(mlId)->layer, 0);
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);
    QCOMPARE(doc.findMeasure(mvId)->ownerBlockId, mlId);
    QCOMPARE(doc.findMeasure(mvId)->refName, QStringLiteral("M_bake1"));

    // --- redo again restores the copy (stable id) ---
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);
    baked = doc.findBlock(newId);
    QVERIFY(baked);
    QCOMPARE(baked->layer, targetLayer);
    QCOMPARE(baked->segments.front().lengthFormula, QStringLiteral("M_bake1"));
}

// ---------------------------------------------------------------------------
// BakeMeasureCopyCommand rejection paths: unknown source or a non-working
// target layer must produce an inert (invalid) command.
// ---------------------------------------------------------------------------
void TestCommands::bakeMeasureCopy_invalidCases()
{
    ParamDocument doc;

    // Unknown source block → invalid no-op.
    cad::cmd::BakeMeasureCopyCommand noSrc(&doc, QUuid::createUuid(), 1);
    QVERIFY(!noSrc.isValid());
    noSrc.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);
    noSrc.undo();   // must not crash / mutate
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);

    // A real measure line, but...
    const auto line = makeLine(doc, 100.0);
    MeasureVariable mv;
    mv.refName = QStringLiteral("M_bake2");
    mv.blockA = line.blockId; mv.pointA = line.startId;
    mv.blockB = line.blockId; mv.pointB = line.endId;
    mv.ownerBlockId = line.blockId;
    doc.addMeasure(mv);
    doc.resolveAll();

    // ...the AUX layer is not a valid bake target.
    cad::cmd::BakeMeasureCopyCommand auxTarget(&doc, line.blockId, 0);
    QVERIFY(!auxTarget.isValid());

    // ...an out-of-range layer is rejected too.
    cad::cmd::BakeMeasureCopyCommand oobTarget(&doc, line.blockId, 99);
    QVERIFY(!oobTarget.isValid());

    // A block WITHOUT an owned measure variable is not bakeable.
    const auto plain = makeLine(doc, 60.0, Vec2{0.0, 80.0});
    cad::cmd::BakeMeasureCopyCommand notMeasure(&doc, plain.blockId, 1);
    QVERIFY(!notMeasure.isValid());

    // A measure line whose segment END POINT never resolves is rejected
    // (Polar referencing a nonexistent point stays unresolved after resolveAll).
    Block dangling;
    dangling.layer = 0;
    ParamPoint dp1; dp1.constraint = PointConstraint::Free; dp1.freePos = Vec2::zero();
    ParamPoint dp2; dp2.constraint = PointConstraint::Polar;
    dp2.refPointId = QUuid::createUuid();   // dangling reference
    dp2.distance = 50.0; dp2.angle = 0.0;
    const QUuid dStart = dp1.id;
    dangling.addPoint(std::move(dp1));
    dangling.addPoint(std::move(dp2));
    Segment dseg; dseg.startPointId = dStart;
    dseg.endPointId = QUuid::createUuid();  // endpoint id matches no point
    dangling.addSegment(std::move(dseg));
    const QUuid dId = dangling.id;
    doc.addBlock(std::move(dangling));
    MeasureVariable dmv;
    dmv.refName = QStringLiteral("M_bake3");
    dmv.ownerBlockId = dId;
    doc.addMeasure(dmv);
    doc.resolveAll();
    cad::cmd::BakeMeasureCopyCommand unresolved(&doc, dId, 1);
    QVERIFY(!unresolved.isValid());

    // A ZERO-LENGTH measure line is rejected (degenerate direction).
    Block zero;
    zero.layer = 0;
    ParamPoint zp1; zp1.constraint = PointConstraint::Free; zp1.freePos = Vec2::zero();
    ParamPoint zp2; zp2.constraint = PointConstraint::Polar;
    zp2.refPointId = zp1.id; zp2.distance = 0.0; zp2.angle = 0.0;
    const QUuid zStart = zp1.id, zEnd = zp2.id;
    zero.addPoint(std::move(zp1));
    zero.addPoint(std::move(zp2));
    Segment zseg; zseg.startPointId = zStart; zseg.endPointId = zEnd;
    zero.addSegment(std::move(zseg));
    const QUuid zId = zero.id;
    doc.addBlock(std::move(zero));
    MeasureVariable zmv;
    zmv.refName = QStringLiteral("M_bake4");
    zmv.blockA = zId; zmv.pointA = zStart;
    zmv.blockB = zId; zmv.pointB = zEnd;
    zmv.ownerBlockId = zId;
    doc.addMeasure(zmv);
    doc.resolveAll();
    cad::cmd::BakeMeasureCopyCommand zeroLen(&doc, zId, 1);
    QVERIFY(!zeroLen.isValid());
}

// ---------------------------------------------------------------------------
// LayerCommands: add/remove-layer redo mutates m_activeLayer (the model layer
// clamps/shifts it) — undo must restore the PRE-COMMAND active layer, not the
// model-adjusted leftover.
// ---------------------------------------------------------------------------
void TestCommands::layerCommands_activeLayerRestored()
{
    ParamDocument doc;   // [aux(0), 图层 1(1)], active = 1
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), 1);

    doc.addLayer(QStringLiteral("w2"));   // index 2
    doc.addLayer(QStringLiteral("w3"));   // index 3
    QCOMPARE(doc.layerCount(), 4);

    // --- AddLayerCommand: undo restores the pre-command active layer ---
    doc.setActiveLayer(1);
    cad::cmd::AddLayerCommand addCmd(&doc, QStringLiteral("w4"));
    addCmd.redo();
    QCOMPARE(doc.layerCount(), 5);
    QCOMPARE(doc.activeLayer(), 4);          // the new layer becomes active
    addCmd.undo();
    QCOMPARE(doc.layerCount(), 4);
    QCOMPARE(doc.activeLayer(), 1);          // snapshot restored (not clamped to 3)

    // --- RemoveLayerCommand: undo restores the active layer removed/shifted
    //     by removeLayer(). Deleting the ACTIVE layer leaves the index
    //     pointing at a DIFFERENT layer until undo restores the snapshot.
    doc.setActiveLayer(2);
    QCOMPARE(doc.layers()[2].name, QStringLiteral("w2"));
    cad::cmd::RemoveLayerCommand rmCmd(&doc, 2);
    rmCmd.redo();
    QCOMPARE(doc.layerCount(), 3);
    // Model adjustment kept index 2, which now names w3 — the wrong layer.
    QCOMPARE(doc.activeLayer(), 2);
    QCOMPARE(doc.layers()[doc.activeLayer()].name, QStringLiteral("w3"));
    rmCmd.undo();
    QCOMPARE(doc.layerCount(), 4);
    QCOMPARE(doc.activeLayer(), 2);
    QCOMPARE(doc.layers()[doc.activeLayer()].name, QStringLiteral("w2"));  // restored

    // Redo/undo round-trip keeps the restoration stable.
    rmCmd.redo();
    rmCmd.undo();
    QCOMPARE(doc.activeLayer(), 2);
    QCOMPARE(doc.layers()[doc.activeLayer()].name, QStringLiteral("w2"));
}

QTEST_MAIN(TestCommands)
#include "test_commands.moc"
