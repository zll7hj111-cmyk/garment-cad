#include <QtTest>
#include <QUuid>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Component.h"
#include "document/commands/BreakCommands.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"

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

/// Create a horizontal line block with an optional length formula.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm,
                   const QString& lengthFormula = {})
{
    Block block;
    block.transform.origin = Vec2::zero();
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
    seg.lengthFormula = lengthFormula;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

/// Add an auxiliary (Interpolated) point on a segment.
QUuid addAuxPoint(ParamDocument& doc, const QUuid& blockId,
                  const QUuid& segId, double percent,
                  double constantMm = 0.0, bool fromEnd = false,
                  double offsetDist = 0.0)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    if (!block || !seg) return {};

    ParamPoint pt;
    pt.constraint = PointConstraint::Interpolated;
    pt.hostSegmentId = segId;
    pt.isAuxiliary = true;
    pt.interpPercent = percent;
    pt.interpConstant = constantMm;
    pt.interpFromEnd = fromEnd;
    pt.interpOffsetDist = offsetDist;

    QUuid ptId = pt.id;
    block->addPoint(std::move(pt));
    seg->auxPointIds.push_back(ptId);
    doc.resolveAll();
    return ptId;
}

/// Flatten a curve entry's spans (local) into a world-space polyline.
std::vector<Vec2> worldPolyline(const Block& b,
                                const CurveSpanEntry& e)
{
    std::vector<Vec2> out;
    for (const auto& s : e.spans) {
        cad::geo::BezierSpan ws;
        ws.p0 = b.transform.toWorld(s.p0);
        ws.ctrl1 = b.transform.toWorld(s.ctrl1);
        ws.ctrl2 = b.transform.toWorld(s.ctrl2);
        ws.p3 = b.transform.toWorld(s.p3);
        auto seg = cad::geo::flattenBezierSpans({ws}, 0.05);
        if (out.empty()) out.insert(out.end(), seg.begin(), seg.end());
        else out.insert(out.end(), seg.begin() + 1, seg.end());
    }
    return out;
}

/// Max deviation: for every point of A, the min distance to polyline B's
/// segments (one-directional Hausdorff).
double maxDeviation(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    double worst = 0.0;
    auto distToSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        const Vec2 d = s1 - s0;
        double l2 = d.lengthSquared();
        if (l2 < 1e-12) return p.distanceTo(s0);
        double t = std::clamp((p - s0).dot(d) / l2, 0.0, 1.0);
        return p.distanceTo(s0 + d * t);
    };
    for (const auto& p : a) {
        double best = 1e18;
        for (size_t i = 0; i + 1 < b.size(); ++i)
            best = std::min(best, distToSeg(p, b[i], b[i + 1]));
        worst = std::max(worst, best);
    }
    return worst;
}

/// Build a curve block: Free start (local 0,0) → Polar end, Bezier segment
/// with two CurveAnchor pass points.
struct CurveSetup {
    QUuid blockId;
    QUuid segId;
    QUuid pp1Id;
    QUuid pp2Id;
};

CurveSetup makeCurve(ParamDocument& doc)
{
    Block block;
    block.transform.origin = Vec2(30.0, -40.0);
    block.transform.rotation = 25.0 * M_PI / 180.0;

    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = Vec2::zero();
    QUuid spId = sp.id;

    ParamPoint ep;
    ep.constraint = PointConstraint::Polar;
    ep.refPointId = spId;
    ep.distance = 120.0;
    ep.angle = 0.0;
    QUuid epId = ep.id;

    block.addPoint(std::move(sp));
    block.addPoint(std::move(ep));

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = spId;
    seg.endPointId = epId;
    seg.tension = 0.0;
    QUuid segId = seg.id;

    ParamPoint pp1;
    pp1.constraint = PointConstraint::CurveAnchor;
    pp1.hostSegmentId = segId;
    pp1.interpPercent = 0.33;
    pp1.interpOffsetDist = 18.0;
    pp1.autoTangent = true;
    QUuid pp1Id = pp1.id;

    ParamPoint pp2;
    pp2.constraint = PointConstraint::CurveAnchor;
    pp2.hostSegmentId = segId;
    pp2.interpPercent = 0.66;
    pp2.interpOffsetDist = -14.0;
    pp2.autoTangent = true;
    QUuid pp2Id = pp2.id;

    block.addPoint(std::move(pp1));
    block.addPoint(std::move(pp2));
    seg.passPointIds = {pp1Id, pp2Id};
    QUuid blockId = block.id;
    block.addSegment(std::move(seg));
    doc.addBlock(std::move(block));
    doc.resolveAll();
    return {blockId, segId, pp1Id, pp2Id};
}

} // namespace

class TestBreak : public QObject
{
    Q_OBJECT

private slots:
    void basicBreak();
    void breakWithConstant();
    void breakFromEnd();
    void multiAuxRedistribution();
    void undoRedo();
    void invalidBreakWithOffset();
    void breakWithFormulaConstantAndRef();
    void breakWithFormulaConstantNoRef();
    void breakInheritsLayer();
    void breakRefChainBackKeepsFormula();
    void curveBreakKeepsShape();          // repro 1
    void breakFollowerAtBreakpoint();     // repro 2a
    void breakComponentAtBreakpoint();    // repro 2b
    void curveBreakNearStartKeepsShape();
    void curveBreakAtAnchorKeepsShape();
    void breakFollowerAtOriginalEnd();
    void curveBreakFollowerKeepsDirection();
    void polarEndpointAnchorCycleResolves();
};

// ---------------------------------------------------------------------------
// Basic break: formula "B/5" at percent=0.5, no constant.
// Front = "(B/5)*0.5", Back = "(B/5)*(1-0.5)".
// Sum of resolved lengths == original length.
// ---------------------------------------------------------------------------
void TestBreak::basicBreak()
{
    ParamDocument doc;
    // Set parameter B = 100 (cm) → B/5 = 20 cm = 200 mm.
    doc.setParameter(QStringLiteral("B"), 100.0);

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    // Add aux point at 50%.
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);
    QVERIFY(!auxId.isNull());

    // Verify aux point resolved at midpoint.
    const auto* blk = doc.findBlock(blockId);
    const auto* aux = blk->findPoint(auxId);
    QVERIFY(aux && aux->resolved);
    QVERIFY(std::abs(aux->resolvedPos.x - 100.0) < 0.01);

    // Execute break.
    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    // After break: should have 2 blocks.
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);

    // Front block (original) should have the front formula.
    const auto* frontBlk = doc.findBlock(blockId);
    QVERIFY(frontBlk);
    QCOMPARE(static_cast<int>(frontBlk->segments.size()), 1);
    const auto& frontSeg = frontBlk->segments[0];
    QCOMPARE(frontSeg.lengthFormula, QStringLiteral("(B/5)*0.5"));

    // Find the back block (the new one).
    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) backBlockId = b.id;
    QVERIFY(!backBlockId.isNull());

    const auto* backBlk = doc.findBlock(backBlockId);
    QVERIFY(backBlk);
    QCOMPARE(static_cast<int>(backBlk->segments.size()), 1);
    const auto& backSeg = backBlk->segments[0];
    QCOMPARE(backSeg.lengthFormula, QStringLiteral("(B/5)*(1-0.5)"));

    // Verify resolved lengths sum to original (200mm).
    // Front: B/5 = 20cm → 200mm; *0.5 = 100mm.
    const auto* fsp = frontBlk->findPoint(frontSeg.startPointId);
    const auto* fep = frontBlk->findPoint(frontSeg.endPointId);
    QVERIFY(fsp && fep && fsp->resolved && fep->resolved);
    double frontLen = fsp->resolvedPos.distanceTo(fep->resolvedPos);

    const auto* bsp = backBlk->findPoint(backSeg.startPointId);
    const auto* bep = backBlk->findPoint(backSeg.endPointId);
    QVERIFY(bsp && bep && bsp->resolved && bep->resolved);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);

    QVERIFY2(std::abs(frontLen + backLen - 200.0) < 0.01,
             qPrintable(QStringLiteral("front=%1 back=%2 sum=%3")
                        .arg(frontLen).arg(backLen).arg(frontLen + backLen)));
    QVERIFY(std::abs(frontLen - 100.0) < 0.01);
    QVERIFY(std::abs(backLen - 100.0) < 0.01);

    // Verify attachment exists connecting back → front.
    QVERIFY(!doc.attachments().empty());
    bool foundAtt = false;
    for (const auto& att : doc.attachments()) {
        if (att.fromBlockId == backBlockId && att.toBlockId == blockId) {
            // 闭合基准: 180° = 打断后 back 沿 front 直行延续（BreakCommands 写入）
            QCOMPARE(att.followerAngle, 180.0);
            QCOMPARE(att.toSegmentId, segId);
            foundAtt = true;
        }
    }
    QVERIFY(foundAtt);
}

// ---------------------------------------------------------------------------
// Break with constant: formula "B/5", percent=0.5, constant=70mm (=7cm).
// Front = "(B/5)*0.5+7", Back = "(B/5)*(1-0.5)-7".
// ---------------------------------------------------------------------------
void TestBreak::breakWithConstant()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);  // B/5 = 20cm = 200mm

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    // constant = 70mm (7cm in formula domain).
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5, 70.0);
    QVERIFY(!auxId.isNull());

    // Aux point should be at 200*0.5 + 70 = 170mm from start.
    const auto* blk = doc.findBlock(blockId);
    const auto* aux = blk->findPoint(auxId);
    QVERIFY(aux && aux->resolved);
    QVERIFY(std::abs(aux->resolvedPos.x - 170.0) < 0.01);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);

    const auto* frontBlk = doc.findBlock(blockId);
    const auto& frontSeg = frontBlk->segments[0];
    QCOMPARE(frontSeg.lengthFormula, QStringLiteral("(B/5)*0.5+7"));

    // Find back block.
    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) backBlockId = b.id;
    const auto* backBlk = doc.findBlock(backBlockId);
    const auto& backSeg = backBlk->segments[0];
    QCOMPARE(backSeg.lengthFormula, QStringLiteral("(B/5)*(1-0.5)-7"));

    // Front length = 200*0.5 + 70 = 170mm, Back = 200 - 170 = 30mm.
    const auto* fsp = frontBlk->findPoint(frontSeg.startPointId);
    const auto* fep = frontBlk->findPoint(frontSeg.endPointId);
    double frontLen = fsp->resolvedPos.distanceTo(fep->resolvedPos);

    const auto* bsp = backBlk->findPoint(backSeg.startPointId);
    const auto* bep = backBlk->findPoint(backSeg.endPointId);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);

    QVERIFY(std::abs(frontLen - 170.0) < 0.01);
    QVERIFY(std::abs(backLen - 30.0) < 0.01);
    QVERIFY(std::abs(frontLen + backLen - 200.0) < 0.01);
}

// ---------------------------------------------------------------------------
// interpFromEnd: percent=0.3, fromEnd=true → effective from start = 0.7.
// Front = "(B/5)*(1-0.3)", Back = "(B/5)*(1-(1-0.3))".
// ---------------------------------------------------------------------------
void TestBreak::breakFromEnd()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);  // B/5 = 200mm

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    // fromEnd=true, percent=0.3 → effective position from start = 1-0.3 = 0.7.
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.3, 0.0, /*fromEnd=*/true);
    QVERIFY(!auxId.isNull());

    // Aux at 200 * 0.7 = 140mm from start.
    const auto* blk = doc.findBlock(blockId);
    const auto* aux = blk->findPoint(auxId);
    QVERIFY(aux && aux->resolved);
    QVERIFY(std::abs(aux->resolvedPos.x - 140.0) < 0.01);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* frontBlk = doc.findBlock(blockId);
    const auto& frontSeg = frontBlk->segments[0];
    // effPercentText = "(1-0.3)" → front = "(B/5)*(1-0.3)"
    QCOMPARE(frontSeg.lengthFormula, QStringLiteral("(B/5)*(1-0.3)"));

    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) backBlockId = b.id;
    const auto* backBlk = doc.findBlock(backBlockId);
    const auto& backSeg = backBlk->segments[0];
    // back = "(B/5)*(1-(1-0.3))"
    QCOMPARE(backSeg.lengthFormula, QStringLiteral("(B/5)*(1-(1-0.3))"));

    // Front = 200*0.7 = 140mm, Back = 200*0.3 = 60mm.
    const auto* fsp = frontBlk->findPoint(frontSeg.startPointId);
    const auto* fep = frontBlk->findPoint(frontSeg.endPointId);
    double frontLen = fsp->resolvedPos.distanceTo(fep->resolvedPos);

    const auto* bsp = backBlk->findPoint(backSeg.startPointId);
    const auto* bep = backBlk->findPoint(backSeg.endPointId);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);

    QVERIFY(std::abs(frontLen - 140.0) < 0.01);
    QVERIFY(std::abs(backLen - 60.0) < 0.01);
}

// ---------------------------------------------------------------------------
// Multiple aux points: one before break, one after.
// Verify redistribution to correct blocks.
// ---------------------------------------------------------------------------
void TestBreak::multiAuxRedistribution()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 300.0);

    // Aux A at 20% (60mm), Aux B at 50% (150mm, break point), Aux C at 80% (240mm).
    QUuid auxA = addAuxPoint(doc, blockId, segId, 0.2);
    QUuid auxB = addAuxPoint(doc, blockId, segId, 0.5);  // break here
    QUuid auxC = addAuxPoint(doc, blockId, segId, 0.8);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxB);
    QVERIFY(cmd.isValid());
    cmd.redo();

    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);

    // Front block should contain auxA (20% < 50%).
    const auto* frontBlk = doc.findBlock(blockId);
    QVERIFY(frontBlk);
    const auto& frontSeg = frontBlk->segments[0];
    QVERIFY(std::find(frontSeg.auxPointIds.begin(), frontSeg.auxPointIds.end(), auxA)
            != frontSeg.auxPointIds.end());
    // auxC should NOT be in front.
    QVERIFY(std::find(frontSeg.auxPointIds.begin(), frontSeg.auxPointIds.end(), auxC)
            == frontSeg.auxPointIds.end());

    // Back block should contain auxC.
    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) backBlockId = b.id;
    const auto* backBlk = doc.findBlock(backBlockId);
    QVERIFY(backBlk);
    const auto& backSeg = backBlk->segments[0];
    QVERIFY(std::find(backSeg.auxPointIds.begin(), backSeg.auxPointIds.end(), auxC)
            != backSeg.auxPointIds.end());
    // auxA should NOT be in back.
    QVERIFY(std::find(backSeg.auxPointIds.begin(), backSeg.auxPointIds.end(), auxA)
            == backSeg.auxPointIds.end());

    // auxC's hostSegmentId should be updated to the back segment.
    const auto* movedC = backBlk->findPoint(auxC);
    QVERIFY(movedC);
    QCOMPARE(movedC->hostSegmentId, backSeg.id);
}

// ---------------------------------------------------------------------------
// Undo/Redo: break then undo restores original state.
// ---------------------------------------------------------------------------
void TestBreak::undoRedo()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    // Snapshot pre-break state.
    const int blocksBefore = static_cast<int>(doc.blocks().size());
    QCOMPARE(blocksBefore, 1);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);

    // Undo: should restore to 1 block with original structure.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);

    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    QCOMPARE(static_cast<int>(blk->segments.size()), 1);
    QCOMPARE(blk->segments[0].lengthFormula, QStringLiteral("B/5"));
    QCOMPARE(blk->segments[0].startPointId, startId);
    QCOMPARE(blk->segments[0].endPointId, endId);

    // Aux point should be restored as Interpolated.
    const auto* aux = blk->findPoint(auxId);
    QVERIFY(aux);
    QCOMPARE(aux->constraint, PointConstraint::Interpolated);
    QVERIFY(aux->isAuxiliary);
    QCOMPARE(aux->interpPercent, 0.5);

    // Redo again should work.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);
}

// ---------------------------------------------------------------------------
// Break point measured through an intersection ref chain with a formula
// constant (the P492 case: P492 = P491 + DART, P491 = P490(intersection)+15):
//   - the break point must STAY at its resolved position,
//   - the parametric anchor survives: the break point becomes a Polar point
//     referenced to its ref point with the offset formula preserved,
//   - the intersection / interpolated chain points stay on the front segment
//     and still resolve (no dependency deadlock),
//   - changing the variable later moves the break point.
// ---------------------------------------------------------------------------
void TestBreak::breakWithFormulaConstantAndRef()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);   // B/5 = 200mm
    doc.setParameter(QStringLiteral("DART"), 3.0);  // 3cm = 30mm

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    // Ray origin for the intersection in a SECOND block (cross-block, like the
    // real P489 case): the intersection's ray comes from another block.
    Block originBlock;
    originBlock.transform.origin = Vec2(50.0, -50.0);
    ParamPoint rayOrigin;
    rayOrigin.constraint = PointConstraint::Free;
    rayOrigin.freePos = Vec2::zero();
    QUuid rayOriginId = rayOrigin.id;
    QUuid originBlockId = originBlock.id;
    originBlock.addPoint(std::move(rayOrigin));
    doc.addBlock(std::move(originBlock));
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);

    // NOTE: fetch the block pointer AFTER addBlock — the vector may have
    // reallocated (dangling pointer otherwise).
    auto* block = doc.findBlock(blockId);

    // P490-analog: intersection point on the segment at x=50 (ray at 90°).
    ParamPoint inter;
    inter.constraint = PointConstraint::Intersection;
    inter.hostSegmentId = segId;
    inter.isAuxiliary = true;
    inter.refPointA = rayOriginId;
    inter.interAngle = 90.0;
    QUuid interId = inter.id;
    block->addPoint(std::move(inter));
    block->findSegment(segId)->auxPointIds.push_back(interId);

    // P491-analog: interpolated point 15mm past the intersection.
    ParamPoint mid;
    mid.constraint = PointConstraint::Interpolated;
    mid.hostSegmentId = segId;
    mid.isAuxiliary = true;
    mid.interpPercent = 0.0;
    mid.interpConstant = 15.0;   // 15mm from the ref point
    mid.interpRefPointId = interId;
    QUuid midId = mid.id;
    block->addPoint(std::move(mid));
    block->findSegment(segId)->auxPointIds.push_back(midId);

    // P492: measured from mid + formula constant DART → 50+15+30 = 95mm.
    ParamPoint bp;
    bp.constraint = PointConstraint::Interpolated;
    bp.hostSegmentId = segId;
    bp.isAuxiliary = true;
    bp.interpPercent = 0.0;
    bp.interpConstant = 0.0;             // stale cached value
    bp.interpConstantFormula = QStringLiteral("DART"); // real offset: 3cm = 30mm
    bp.interpRefPointId = midId;
    QUuid bpId = bp.id;
    block->addPoint(std::move(bp));
    block->findSegment(segId)->auxPointIds.push_back(bpId);
    doc.resolveAll();

    const auto* aux = doc.findBlock(blockId)->findPoint(bpId);
    QVERIFY(aux && aux->resolved);
    QVERIFY2(std::abs(aux->resolvedPos.x - 95.0) < 0.01,
             qPrintable(QStringLiteral("break point resolved at %1")
                        .arg(aux->resolvedPos.x)));

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, bpId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    QCOMPARE(static_cast<int>(doc.blocks().size()), 3);  // front + back + origin

    // Front block: endpoint is the break point, must stay at 95mm.
    const auto* frontBlk = doc.findBlock(blockId);
    QVERIFY(frontBlk);
    const auto& frontSeg = frontBlk->segments[0];
    QCOMPARE(frontSeg.endPointId, bpId);
    const auto* fep = frontBlk->findPoint(bpId);
    QVERIFY(fep && fep->resolved);
    QVERIFY2(std::abs(fep->resolvedPos.x - 95.0) < 0.01,
             qPrintable(QStringLiteral("front endpoint at %1")
                        .arg(fep->resolvedPos.x)));

    // The parametric anchor survives: Polar from the ref point, distance still
    // driven by the DART formula (cm domain), direction along the segment.
    QCOMPARE(fep->constraint, PointConstraint::Polar);
    QCOMPARE(fep->refPointId, midId);
    QCOMPARE(fep->distanceFormula, QStringLiteral("DART"));
    QVERIFY2(std::abs(fep->distance - 30.0) < 0.01,
             qPrintable(QStringLiteral("polar distance %1").arg(fep->distance)));

    // The whole chain stays fully parametric: the intersection keeps its
    // Intersection constraint, the mid point keeps its Interpolated ref — only
    // the break point becomes a Polar anchored to the mid point.
    const auto* fMidPt = frontBlk->findPoint(midId);
    QVERIFY(fMidPt);
    QCOMPARE(fMidPt->constraint, PointConstraint::Interpolated);
    QCOMPARE(fMidPt->interpRefPointId, interId);
    const auto* fInterPt = frontBlk->findPoint(interId);
    QVERIFY(fInterPt);
    QCOMPARE(fInterPt->constraint, PointConstraint::Intersection);
    QCOMPARE(fInterPt->hostSegmentId, segId);

    // The intersection + interpolated chain stays on the front segment and
    // keeps resolving (no dependency deadlock).
    const auto* fInter = frontBlk->findPoint(interId);
    const auto* fMid = frontBlk->findPoint(midId);
    QVERIFY(fInter && fInter->resolved);
    QVERIFY(fMid && fMid->resolved);
    QVERIFY2(std::abs(fInter->resolvedPos.x - 50.0) < 0.01,
             qPrintable(QStringLiteral("intersection at %1")
                        .arg(fInter->resolvedPos.x)));
    QVERIFY2(std::abs(fMid->resolvedPos.x - 65.0) < 0.01,
             qPrintable(QStringLiteral("mid point at %1")
                        .arg(fMid->resolvedPos.x)));
    QVERIFY(std::find(frontSeg.auxPointIds.begin(), frontSeg.auxPointIds.end(), interId)
            != frontSeg.auxPointIds.end());
    QVERIFY(std::find(frontSeg.auxPointIds.begin(), frontSeg.auxPointIds.end(), midId)
            != frontSeg.auxPointIds.end());

    // Back block: starts at the break point position (world (95,0)).
    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId && b.id != originBlockId) backBlockId = b.id;
    const auto* backBlk = doc.findBlock(backBlockId);
    QVERIFY(backBlk);
    const auto& backSeg = backBlk->segments[0];
    const auto* bsp = backBlk->findPoint(backSeg.startPointId);
    QVERIFY(bsp && bsp->resolved);
    const cad::geo::Vec2 backStartWorld = backBlk->transform.toWorld(bsp->resolvedPos);
    QVERIFY2(std::abs(backStartWorld.x - 95.0) < 0.01 && std::abs(backStartWorld.y) < 0.01,
             qPrintable(QStringLiteral("back start at (%1,%2)")
                        .arg(backStartWorld.x).arg(backStartWorld.y)));

    // Total length preserved: front 95 + back 105 = 200.
    const auto* fsp = frontBlk->findPoint(frontSeg.startPointId);
    double frontLen = fsp->resolvedPos.distanceTo(fep->resolvedPos);
    const auto* bep = backBlk->findPoint(backSeg.endPointId);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);
    QVERIFY2(std::abs(frontLen - 95.0) < 0.01,
             qPrintable(QStringLiteral("front len %1").arg(frontLen)));
    QVERIFY2(std::abs(backLen - 105.0) < 0.01,
             qPrintable(QStringLiteral("back len %1").arg(backLen)));

    // Changing DART moves the break point (constraint alive): 4cm → 105mm.
    doc.setParameter(QStringLiteral("DART"), 4.0);
    const auto* fep2 = doc.findBlock(blockId)->findPoint(bpId);
    QVERIFY(fep2 && fep2->resolved);
    QVERIFY2(std::abs(fep2->resolvedPos.x - 105.0) < 0.01,
             qPrintable(QStringLiteral("front endpoint after DART=4: %1")
                        .arg(fep2->resolvedPos.x)));

    // The back block follows the break point (its start rides the endpoint).
    const auto* bsp2 = doc.findBlock(backBlockId)->findPoint(backSeg.startPointId);
    QVERIFY(bsp2 && bsp2->resolved);
    const cad::geo::Vec2 backStart2 = doc.findBlock(backBlockId)->transform.toWorld(bsp2->resolvedPos);
    QVERIFY2(std::abs(backStart2.x - 105.0) < 0.01,
             qPrintable(QStringLiteral("back start after DART=4 at %1")
                        .arg(backStart2.x)));

    // THE core requirement: the intersection stays DYNAMIC. Moving the ray
    // origin block (the "neckline" parameter) moves the intersection → mid →
    // break point chain: 50→60, 65→75, 105→115.
    auto* ob = doc.findBlock(originBlockId);
    ob->transform.origin.x = 60.0;
    doc.resolveAll();
    const auto* i2 = doc.findBlock(blockId)->findPoint(interId);
    const auto* m2 = doc.findBlock(blockId)->findPoint(midId);
    const auto* b2 = doc.findBlock(blockId)->findPoint(bpId);
    QVERIFY(i2 && i2->resolved && m2 && m2->resolved && b2 && b2->resolved);
    QVERIFY2(std::abs(i2->resolvedPos.x - 60.0) < 0.01,
             qPrintable(QStringLiteral("intersection after ray move at %1")
                        .arg(i2->resolvedPos.x)));
    QVERIFY2(std::abs(m2->resolvedPos.x - 75.0) < 0.01,
             qPrintable(QStringLiteral("mid after ray move at %1")
                        .arg(m2->resolvedPos.x)));
    QVERIFY2(std::abs(b2->resolvedPos.x - 115.0) < 0.01,
             qPrintable(QStringLiteral("break point after ray move at %1")
                        .arg(b2->resolvedPos.x)));
}

// ---------------------------------------------------------------------------
// Break point WITHOUT a ref point but with a formula constant (e.g. a dart
// width variable): the linear split keeps both formulas and must re-evaluate
// the cached (stale) interpConstant, otherwise the numeric distances and the
// formula values disagree.
// ---------------------------------------------------------------------------
void TestBreak::breakWithFormulaConstantNoRef()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);   // B/5 = 200mm
    doc.setParameter(QStringLiteral("DART"), 3.0);  // 3cm = 30mm

    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    auto* block = doc.findBlock(blockId);
    ParamPoint bp;
    bp.constraint = PointConstraint::Interpolated;
    bp.hostSegmentId = segId;
    bp.isAuxiliary = true;
    bp.interpPercent = 0.0;
    bp.interpConstant = 0.0;             // stale cached value
    bp.interpConstantFormula = QStringLiteral("DART"); // 3cm = 30mm from start
    QUuid bpId = bp.id;
    block->addPoint(std::move(bp));
    block->findSegment(segId)->auxPointIds.push_back(bpId);
    doc.resolveAll();

    const auto* aux = doc.findBlock(blockId)->findPoint(bpId);
    QVERIFY(aux && aux->resolved);
    QVERIFY2(std::abs(aux->resolvedPos.x - 30.0) < 0.01,
             qPrintable(QStringLiteral("break point resolved at %1")
                        .arg(aux->resolvedPos.x)));

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, bpId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    // Formulas survive the split.
    const auto* frontBlk = doc.findBlock(blockId);
    const auto& frontSeg = frontBlk->segments[0];
    QCOMPARE(frontSeg.lengthFormula, QStringLiteral("(B/5)*0+DART"));

    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) backBlockId = b.id;
    const auto* backBlk = doc.findBlock(backBlockId);
    const auto& backSeg = backBlk->segments[0];
    QCOMPARE(backSeg.lengthFormula, QStringLiteral("(B/5)*(1-0)-DART"));

    // Front = 30mm, back = 170mm, total 200mm.
    const auto* fsp = frontBlk->findPoint(frontSeg.startPointId);
    const auto* fep = frontBlk->findPoint(frontSeg.endPointId);
    double frontLen = fsp->resolvedPos.distanceTo(fep->resolvedPos);
    const auto* bsp = backBlk->findPoint(backSeg.startPointId);
    const auto* bep = backBlk->findPoint(backSeg.endPointId);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);
    QVERIFY2(std::abs(frontLen - 30.0) < 0.01,
             qPrintable(QStringLiteral("front len %1").arg(frontLen)));
    QVERIFY2(std::abs(backLen - 170.0) < 0.01,
             qPrintable(QStringLiteral("back len %1").arg(backLen)));

    // Parametric: DART=4 → front follows to 40mm, back shrinks to 160mm.
    doc.setParameter(QStringLiteral("DART"), 4.0);
    const auto* fep2 = frontBlk->findPoint(frontSeg.endPointId);
    const auto* bep2 = backBlk->findPoint(backSeg.endPointId);
    QVERIFY(fep2 && bep2 && fep2->resolved && bep2->resolved);
    double frontLen2 = fsp->resolvedPos.distanceTo(fep2->resolvedPos);
    double backLen2 = bsp->resolvedPos.distanceTo(bep2->resolvedPos);
    QVERIFY2(std::abs(frontLen2 - 40.0) < 0.01,
             qPrintable(QStringLiteral("front len after DART=4: %1").arg(frontLen2)));
    QVERIFY2(std::abs(backLen2 - 160.0) < 0.01,
             qPrintable(QStringLiteral("back len after DART=4: %1").arg(backLen2)));
}

// ---------------------------------------------------------------------------
// Invalid break: aux point with offset → command is not valid.
// ---------------------------------------------------------------------------
void TestBreak::invalidBreakWithOffset()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);

    // Add aux with offset distance (not on the segment line).
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5, 0.0, false,
                              /*offsetDist=*/50.0);
    QVERIFY(!auxId.isNull());

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(!cmd.isValid());

    // Executing should be a no-op.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
}

// ---------------------------------------------------------------------------
// The back block created by a break must inherit the ORIGINAL block's layer.
// Regression: the new block defaulted to layer 0 (the auxiliary calculation
// layer), so on a non-active auxiliary layer the back half rendered grayed /
// invisible on canvas — "the back half vanishes" (L222 user report).
// ---------------------------------------------------------------------------
void TestBreak::breakInheritsLayer()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);

    // Put the block on a working layer (not the default auxiliary layer 0).
    auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    blk->layer = layerIdAt(doc, 1);
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);

    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    // The new back block must inherit layer 1 (never fall back to layer 0).
    int backLayers = 0;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) {
            QCOMPARE(b.layer, layerIdAt(doc, 1));
            ++backLayers;
        }
    QCOMPARE(backLayers, 1);

    // Undo restores, and redo keeps inheriting.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    cmd.redo();
    for (const auto& b : doc.blocks())
        if (b.id != blockId)
            QCOMPARE(b.layer, layerIdAt(doc, 1));
}

// ---------------------------------------------------------------------------
// RefChain break (打断点通过引用链锚定在交点上): 前段保留 Polar 引用链,
// 后段必须保留原公式的变量驱动 — back = (B/5) − 前段快照, 不再退化为
// 纯数值 (用户要求: 原公式减去前半段表达式 = 后半段公式).
// ---------------------------------------------------------------------------
void TestBreak::breakRefChainBackKeepsFormula()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("B"), 100.0);   // B/5 = 20cm = 200mm
    doc.setParameter(QStringLiteral("DART"), 3.0);  // 3cm = 30mm

    // L: (0,0)→(200,0)，长度公式 B/5。
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0, "B/5");

    // 射线原点块（模拟真实 P489 的跨块交点）。
    Block originBlock;
    originBlock.transform.origin = Vec2(50.0, -50.0);
    ParamPoint rayOrigin;
    rayOrigin.constraint = PointConstraint::Free;
    rayOrigin.freePos = Vec2::zero();
    QUuid rayOriginId = rayOrigin.id;
    QUuid originBlockId = originBlock.id;
    originBlock.addPoint(std::move(rayOrigin));
    doc.addBlock(std::move(originBlock));

    auto* block = doc.findBlock(blockId);

    // 交点 inter 在 x=50（射线 90°）。
    ParamPoint inter;
    inter.constraint = PointConstraint::Intersection;
    inter.hostSegmentId = segId;
    inter.isAuxiliary = true;
    inter.refPointA = rayOriginId;
    inter.interAngle = 90.0;
    QUuid interId = inter.id;
    block->addPoint(std::move(inter));
    block->findSegment(segId)->auxPointIds.push_back(interId);

    // mid: inter + 15mm → x=65。
    ParamPoint mid;
    mid.constraint = PointConstraint::Interpolated;
    mid.hostSegmentId = segId;
    mid.isAuxiliary = true;
    mid.interpPercent = 0.0;
    mid.interpConstant = 15.0;
    mid.interpRefPointId = interId;
    QUuid midId = mid.id;
    block->addPoint(std::move(mid));
    block->findSegment(segId)->auxPointIds.push_back(midId);

    // bp: mid + DART(30mm) → x=95。
    ParamPoint bp;
    bp.constraint = PointConstraint::Interpolated;
    bp.hostSegmentId = segId;
    bp.isAuxiliary = true;
    bp.interpPercent = 0.0;
    bp.interpConstant = 0.0;
    bp.interpConstantFormula = QStringLiteral("DART");
    bp.interpRefPointId = midId;
    QUuid bpId = bp.id;
    block->addPoint(std::move(bp));
    block->findSegment(segId)->auxPointIds.push_back(bpId);
    doc.resolveAll();

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, bpId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    // 前段：Polar 引用链保留（bp 引用 mid + DART 公式）。
    const auto* frontBlk = doc.findBlock(blockId);
    QVERIFY(frontBlk);
    const auto* fep = frontBlk->findPoint(bpId);
    QVERIFY(fep);
    QCOMPARE(fep->constraint, PointConstraint::Polar);
    QCOMPARE(fep->refPointId, midId);
    QCOMPARE(fep->distanceFormula, QStringLiteral("DART"));

    // 后段公式 = 原公式 − 前段测量变量（打断自动发布前段长度）。
    const QString refName =
        QStringLiteral("L") + frontBlk->segments[0].serial;
    QUuid backBlockId;
    for (const auto& b : doc.blocks())
        if (b.id != blockId && b.id != originBlockId) backBlockId = b.id;
    QVERIFY(!backBlockId.isNull());
    const auto* backBlk = doc.findBlock(backBlockId);
    QVERIFY(backBlk);
    QCOMPARE(backBlk->segments[0].lengthFormula,
             QStringLiteral("(B/5)-%1").arg(refName));

    // 自动发布的测量变量存在（source = 前段，值 = 前段长度 95mm）。
    const auto* lv = doc.findLinkedBySource(blockId, segId);
    QVERIFY(lv);
    QCOMPARE(lv->refName, refName);
    QVERIFY2(std::abs(lv->value - 95.0) < 0.01,
             qPrintable(QStringLiteral("published value %1").arg(lv->value)));

    // 变量驱动：B=120 → B/5=24cm=240mm → 后段 = 240−95 = 145mm。
    doc.setParameter(QStringLiteral("B"), 120.0);
    backBlk = doc.findBlock(backBlockId);
    const auto* bsp = backBlk->findPoint(backBlk->segments[0].startPointId);
    const auto* bep = backBlk->findPoint(backBlk->segments[0].endPointId);
    QVERIFY(bsp && bep && bsp->resolved && bep->resolved);
    double backLen = bsp->resolvedPos.distanceTo(bep->resolvedPos);
    QVERIFY2(std::abs(backLen - 145.0) < 0.01,
             qPrintable(QStringLiteral("back len after B=120: %1").arg(backLen)));

    // 严格守恒：DART=4 → 前段 95→105mm，后段必须自动补偿 240−105=135mm，
    // 总长恒 = B/5 = 240mm（前段动态变化不再与后段脱节）。
    doc.setParameter(QStringLiteral("DART"), 4.0);
    const auto* fep2 = doc.findBlock(blockId)->findPoint(bpId);
    QVERIFY(fep2 && fep2->resolved);
    backBlk = doc.findBlock(backBlockId);
    const auto* bsp2 = backBlk->findPoint(backBlk->segments[0].startPointId);
    const auto* bep2 = backBlk->findPoint(backBlk->segments[0].endPointId);
    QVERIFY(bsp2 && bep2 && bsp2->resolved && bep2->resolved);
    const double frontLen = fep2->resolvedPos.distanceTo(
        doc.findBlock(blockId)->findPoint(frontBlk->segments[0].startPointId)
            ->resolvedPos);
    const double backLen2 = bsp2->resolvedPos.distanceTo(bep2->resolvedPos);
    QVERIFY2(std::abs(frontLen - 105.0) < 0.01,
             qPrintable(QStringLiteral("front after DART=4: %1").arg(frontLen)));
    QVERIFY2(std::abs(backLen2 - 135.0) < 0.01,
             qPrintable(QStringLiteral("back after DART=4: %1").arg(backLen2)));
    QVERIFY2(std::abs(frontLen + backLen2 - 240.0) < 0.02,
             qPrintable(QStringLiteral("total %1").arg(frontLen + backLen2)));

    // undo 一步恢复：原块还原、自动发布的变量被移除。
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);  // L + origin
    const auto* blk2 = doc.findBlock(blockId);
    QVERIFY(blk2);
    QCOMPARE(blk2->segments[0].lengthFormula, QStringLiteral("B/5"));
    QVERIFY(doc.findLinkedBySource(blockId, segId) == nullptr);

    // redo 重新发布（变量重建，公式一致）。
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 3);
    const auto* lv2 = doc.findLinkedBySource(blockId, segId);
    QVERIFY(lv2);
    QUuid backBlockId2;
    for (const auto& b : doc.blocks())
        if (b.id != blockId && b.id != originBlockId) backBlockId2 = b.id;
    QCOMPARE(doc.findBlock(backBlockId2)->segments[0].lengthFormula,
             QStringLiteral("(B/5)-%1").arg(refName));
}

// ---------------------------------------------------------------------------
// REPRO 1 (曲线打断不能维持形状): breaking a Bezier segment must keep the
// original shape for both halves (world-space Hausdorff deviation ≈ 0).
// ---------------------------------------------------------------------------
void TestBreak::curveBreakKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    // Break point: interpolated (arc-length 50%) on the curve.
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);
    QVERIFY(!auxId.isNull());

    const auto* preBlk = doc.findBlock(blockId);
    const auto* preEntry = preBlk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*preBlk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    std::vector<Vec2> broken;
    int curveSegCount = 0;
    for (const auto& b : doc.blocks()) {
        for (const auto& s : b.segments) {
            if (!s.isCurve()) continue;
            ++curveSegCount;
            if (const auto* e = b.curveSpanEntry(s.id)) {
                const auto p = worldPolyline(b, *e);
                broken.insert(broken.end(), p.begin(), p.end());
            }
        }
    }
    QCOMPARE(curveSegCount, 2);  // both halves must stay curves

    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("curve shape deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3, qPrintable(QStringLiteral("front/back drifted %1 mm from original").arg(d1, 0, 'f', 4)));
    QVERIFY2(d2 < 0.3, qPrintable(QStringLiteral("broken curve drifted %1 mm from original").arg(d2, 0, 'f', 4)));
}

/// Collect every curve polyline (world space) in the document.
static std::vector<Vec2> collectCurvePolylines(const ParamDocument& doc)
{
    std::vector<Vec2> out;
    for (const auto& b : doc.blocks()) {
        for (const auto& s : b.segments) {
            if (!s.isCurve()) continue;
            if (const auto* e = b.curveSpanEntry(s.id)) {
                const auto p = worldPolyline(b, *e);
                out.insert(out.end(), p.begin(), p.end());
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 曲线打断形状保持 — 断点在第一个 pass 点之前（前段无 pass 点 → 中点插入
// 路径，de Casteljau 半参数化）也必须按原曲线形状重建。
// ---------------------------------------------------------------------------
void TestBreak::curveBreakNearStartKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.08);
    QVERIFY(!auxId.isNull());

    const auto* preBlk = doc.findBlock(blockId);
    const auto* preEntry = preBlk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*preBlk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto broken = collectCurvePolylines(doc);
    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("near-start deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3 && d2 < 0.3,
             qPrintable(QStringLiteral("near-start break drifted (%1, %2) mm")
                        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4)));
}

// ---------------------------------------------------------------------------
// 曲线打断形状保持 — 断点本身就是链点（CurveAnchor / pass point）：无子跨度
// 细分（hasSubSpans=false），冻结原曲线逐点 Hobby in/out 切线必须精确。
// ---------------------------------------------------------------------------
void TestBreak::curveBreakAtAnchorKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    // pp1 is a CurveAnchor pass point at 33% chord — break right at it.
    const auto* blk = doc.findBlock(blockId);
    const auto* pp1 = blk->findPoint(pp1Id);
    QVERIFY(pp1);

    const auto* preEntry = blk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*blk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, pp1Id);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto broken = collectCurvePolylines(doc);
    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("anchor break deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3 && d2 < 0.3,
             qPrintable(QStringLiteral("anchor break drifted (%1, %2) mm")
                        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4)));
}

// ---------------------------------------------------------------------------
// 原端点上的连接：打断后应被重指到后段终点（原线尾部）——连接保留、
// 位置不动。
// ---------------------------------------------------------------------------
void TestBreak::breakFollowerAtOriginalEnd()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2(200.0, 0.0);
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 0.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = endId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("end follower jumped %1 mm").arg(after.distanceTo(before))));

    // Connection survives, re-pointed to the BACK block's end.
    QUuid backBlockId;
    for (const auto& bb : doc.blocks())
        if (bb.id != blockId) backBlockId = bb.id;
    QVERIFY(!backBlockId.isNull());
    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) {
            attAlive = true;
            QCOMPARE(a.toBlockId, backBlockId);
        }
    QVERIFY(attAlive);

    // Undo/redo round-trip keeps the connection and position.
    cmd.undo();
    bool attBack = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attBack = true;
    QVERIFY(attBack);
    cmd.redo();
    bool attAlive2 = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive2 = true;
    QVERIFY(attAlive2);
}

// ---------------------------------------------------------------------------
// 曲线断点上的跟随线：打断后世界朝向必须零跳变（角度补偿 refDeltaRad）。
// ---------------------------------------------------------------------------
void TestBreak::curveBreakFollowerKeepsDirection()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2::zero();
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 20.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 55.0;  // some non-trivial relative angle
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 p0before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);
    const Vec2 p1before = blk->transform.toWorld(blk->findPoint(b2Id)->resolvedPos);
    const double dirBefore = std::atan2(p1before.y - p0before.y, p1before.x - p0before.x);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 p0after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    const Vec2 p1after = blk2->transform.toWorld(blk2->findPoint(b2Id)->resolvedPos);
    const double dirAfter = std::atan2(p1after.y - p0after.y, p1after.x - p0after.x);
    double dAng = std::abs(dirAfter - dirBefore);
    dAng = std::fmod(dAng, 2.0 * M_PI);
    if (dAng > M_PI) dAng = 2.0 * M_PI - dAng;
    const double dAngDeg = dAng * 180.0 / M_PI;
    qInfo().noquote() << QStringLiteral("follower dir before=%1 after=%2 deltaDeg=%3")
        .arg(dirBefore * 180.0 / M_PI, 0, 'f', 3)
        .arg(dirAfter * 180.0 / M_PI, 0, 'f', 3).arg(dAngDeg, 0, 'f', 4);

    QVERIFY2(dAngDeg < 0.01,
             qPrintable(QStringLiteral("curve follower rotated %1 deg on break").arg(dAngDeg)));
    QVERIFY2(p0after.distanceTo(p0before) < 0.01,
             qPrintable(QStringLiteral("curve follower jumped %1 mm")
                        .arg(p0after.distanceTo(p0before))));
    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive = true;
    QVERIFY(attAlive);
}

// ---------------------------------------------------------------------------
// REPRO 2a (端点连接打断位置跳变): a follower line attached at the break
// point must keep its position AND its connection after the break.
// ---------------------------------------------------------------------------
void TestBreak::breakFollowerAtBreakpoint()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2(100.0, 0.0);
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 0.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    QUuid bSegId = bs.id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    qInfo().noquote() << QStringLiteral("follower before=(%1,%2) after=(%3,%4)")
        .arg(before.x, 0, 'f', 3).arg(before.y, 0, 'f', 3)
        .arg(after.x, 0, 'f', 3).arg(after.y, 0, 'f', 3);

    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive = true;
    qInfo() << "follower attachment alive:" << attAlive;

    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("follower jumped %1 mm").arg(after.distanceTo(before))));
    QVERIFY(attAlive);  // the connection must survive the break
}

// ---------------------------------------------------------------------------
// REPRO 2b (组件连接打断位置跳变): a component attached at the break point
// must keep its position AND its connection after the break.
// ---------------------------------------------------------------------------
void TestBreak::breakComponentAtBreakpoint()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    // Component member A: starts at (100, 0) → (150, 0).
    Block ma;
    ma.transform.origin = Vec2(100.0, 0.0);
    ParamPoint ma1;
    ma1.constraint = PointConstraint::Free;
    ma1.freePos = Vec2::zero();
    QUuid ma1Id = ma1.id;
    ParamPoint ma2;
    ma2.constraint = PointConstraint::Free;
    ma2.freePos = Vec2(50.0, 0.0);
    QUuid ma2Id = ma2.id;
    ma.addPoint(std::move(ma1));
    ma.addPoint(std::move(ma2));
    Segment mas;
    mas.startPointId = ma1Id;
    mas.endPointId = ma2Id;
    ma.addSegment(std::move(mas));
    QUuid maId = doc.addBlock(std::move(ma));

    Block mb;
    mb.transform.origin = Vec2(150.0, 0.0);
    ParamPoint mb1;
    mb1.constraint = PointConstraint::Free;
    mb1.freePos = Vec2::zero();
    QUuid mb1Id = mb1.id;
    ParamPoint mb2;
    mb2.constraint = PointConstraint::Free;
    mb2.freePos = Vec2(40.0, 30.0);
    QUuid mb2Id = mb2.id;
    mb.addPoint(std::move(mb1));
    mb.addPoint(std::move(mb2));
    Segment mbs;
    mbs.startPointId = mb1Id;
    mbs.endPointId = mb2Id;
    mb.addSegment(std::move(mbs));
    QUuid mbId = doc.addBlock(std::move(mb));
    doc.resolveAll();

    Component comp;
    comp.name = QStringLiteral("C");
    comp.memberBlockIds = {maId, mbId};
    QUuid compId = doc.addComponent(comp);

    Attachment att;
    att.fromComponentId = compId;
    att.fromPointId = ma1Id;  // exposed endpoint at the break point
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(maId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(ma1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(maId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(ma1Id)->resolvedPos);
    qInfo().noquote() << QStringLiteral("component member before=(%1,%2) after=(%3,%4)")
        .arg(before.x, 0, 'f', 3).arg(before.y, 0, 'f', 3)
        .arg(after.x, 0, 'f', 3).arg(after.y, 0, 'f', 3);

    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromComponentId == compId) attAlive = true;
    qInfo() << "component attachment alive:" << attAlive;

    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("component jumped %1 mm").arg(after.distanceTo(before))));
    QVERIFY(attAlive);  // the component connection must survive the break
}

// ---------------------------------------------------------------------------
// 3.gcad 循环复现（引擎级修复回归）：线段端点（Polar）锚在本线段交点辅助点
// 上 = 冷启动死锁（端点无缓存位姿 → 线段退化 → 交点永不解析 → 端点永不解析）。
// 修复：退化线段启动种子（极角公式暂时锚段起点），不动点随后收敛到真锚。
// 不动点解析值：P175=(7.5,0)，P176=(22.5,0)（= P175 + 15mm @0°，两轮收敛）。
// ---------------------------------------------------------------------------
void TestBreak::polarEndpointAnchorCycleResolves()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 15.0);

    // 交点射线锚点块（起点位于 (7.5, -50)，射线 90° 竖直向上）。
    auto [bId, bStartId, bEndId, bSegId] = makeLine(doc, 50.0);
    {
        auto* bBlk = doc.findBlock(bId);
        bBlk->transform.origin = Vec2(7.5, -50.0);
        doc.resolveAll();
    }

    // P175: 交点（host=线段本身, 锚点=另一块起点, 相对宿主角 90°）。
    QUuid auxId;
    {
        auto* blk = doc.findBlock(blockId);
        auto* seg = blk->findSegment(segId);
        ParamPoint pt;
        pt.constraint = PointConstraint::Intersection;
        pt.hostSegmentId = segId;
        pt.refPointA = bStartId;
        pt.interAngle = 90.0;
        pt.isAuxiliary = true;
        auxId = pt.id;
        blk->addPoint(std::move(pt));
        seg->auxPointIds.push_back(auxId);
    }

    // 复现 3.gcad：把线段端点锚到同段交点（循环依赖）。
    {
        auto* blk = doc.findBlock(blockId);
        auto* seg = blk->findSegment(segId);
        auto* ep = blk->findPoint(seg->endPointId);
        ep->refPointId = auxId;
    }

    // 冷启动（全新文档 = 无缓存位姿）——修复前此调用后端点永久 unresolved。
    doc.resolveAll();

    const auto* blk = doc.findBlock(blockId);
    const auto* seg = blk->findSegment(segId);
    const auto* sp = blk->findPoint(seg->startPointId);
    const auto* ep = blk->findPoint(seg->endPointId);
    const auto* aux = blk->findPoint(auxId);
    qInfo().noquote() << QStringLiteral("aux=(%1,%2) ep=(%3,%4)")
        .arg(aux->resolvedPos.x, 0, 'f', 3).arg(aux->resolvedPos.y, 0, 'f', 3)
        .arg(ep->resolvedPos.x, 0, 'f', 3).arg(ep->resolvedPos.y, 0, 'f', 3);
    QVERIFY2(aux->resolved, "intersection anchored to own segment must resolve");
    QVERIFY2(ep->resolved, "polar endpoint anchored to own-segment aux must resolve");
    QVERIFY2(aux->resolvedPos.distanceTo(Vec2(7.5, 0.0)) < 0.01,
             "aux must converge to the fixed point");
    QVERIFY2(ep->resolvedPos.distanceTo(Vec2(22.5, 0.0)) < 0.01,
             "endpoint must converge to the fixed point");
    (void)sp;
}

QTEST_MAIN(TestBreak)
#include "test_break.moc"
