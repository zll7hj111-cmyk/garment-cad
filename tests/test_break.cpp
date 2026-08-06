#include <QtTest>
#include <QUuid>
#include <QUndoStack>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "document/commands/BreakCommands.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

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
            QCOMPARE(att.followerAngle, 0.0);
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
    blk->layer = 1;
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);

    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    // The new back block must inherit layer 1 (never fall back to layer 0).
    int backLayers = 0;
    for (const auto& b : doc.blocks())
        if (b.id != blockId) {
            QCOMPARE(b.layer, 1);
            ++backLayers;
        }
    QCOMPARE(backLayers, 1);

    // Undo restores, and redo keeps inheriting.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    cmd.redo();
    for (const auto& b : doc.blocks())
        if (b.id != blockId)
            QCOMPARE(b.layer, 1);
}

QTEST_MAIN(TestBreak)
#include "test_break.moc"
