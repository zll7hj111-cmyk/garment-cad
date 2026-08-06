#include <QtTest>
#include <cmath>

#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Resolver.h"
#include "parametric/Attachment.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

class TestIntersection : public QObject
{
    Q_OBJECT

private slots:
    /// Basic: ray from below hits a horizontal segment at 90°.
    void basicPerpendicular();

    /// Angle relative to segment direction (45° on a horizontal line).
    void angledRay();

    /// No intersection: ray points away (parallel).
    void parallelNoHit();

    /// No intersection: ray in wrong direction (s < 0), unidirectional.
    void wrongDirection();

    /// Bidirectional mode: ray hits even when s < 0.
    void bidirectionalHit();

    /// Intersection at segment endpoint (t = 0 or t = 1).
    void endpointHit();

    /// Formula-driven angle.
    void formulaAngle();

    /// Aim point (指向点): the ray points straight at a point in the SAME block.
    void aimPointSameBlock();

    /// Aim point in ANOTHER block — resolves in the Resolver's cross-block pass.
    void aimPointCrossBlock();

    /// Auxiliary point with interpRefPointId (measurement from another point).
    void auxPointFromRefPoint();

    /// Intersection point as reference for an auxiliary point.
    void auxFromIntersection();

    /// CROSS-BLOCK intersection as reference for an aux point. The intersection's
    /// ray origin lives on a DIFFERENT block, so the intersection resolves in the
    /// Resolver's cross-block pass; the aux point referencing it must still resolve.
    void auxFromCrossBlockIntersection();

    /// An intersection whose AIM point is an interpolated aux point that itself
    /// references a cross-block intersection (user document repro): the aim only
    /// resolves in the 6b pass, so the dependent intersection needs a SECOND
    /// Step-6 pass (shared fixpoint).
    void aimChainThroughInterpolatedResolves();

private:
    /// Helper: create a block with a horizontal segment from (0,0) to (100,0) mm.
    static Block makeHorizontalSegment()
    {
        Block b;
        ParamPoint p1;
        p1.constraint = PointConstraint::Free;
        p1.freePos = Vec2(0.0, 0.0);
        p1.resolved = true;
        p1.resolvedPos = p1.freePos;

        ParamPoint p2;
        p2.constraint = PointConstraint::Free;
        p2.freePos = Vec2(100.0, 0.0);
        p2.resolved = true;
        p2.resolvedPos = p2.freePos;

        b.addPoint(p1);
        b.addPoint(p2);

        Segment seg;
        seg.startPointId = p1.id;
        seg.endPointId = p2.id;
        b.addSegment(seg);

        return b;
    }
};

void TestIntersection::basicPerpendicular()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;
    const QUuid p1Id = b.points[0].id;

    // Origin point A at (50, -30) — below the segment.
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(50.0, -30.0);
    b.addPoint(a);

    // Intersection point: ray from A at 90° relative to segment direction.
    // Segment direction = 0° (along +X). Ray at 90° = straight up (+Y).
    // Should hit the segment at (50, 0).
    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;
    ix.interBidirectional = false;
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    QVERIFY(std::abs(resolved->resolvedPos.x - 50.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::angledRay()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    // Origin at (0, -100). Ray at 45° relative to segment (which is along +X).
    // Ray direction = 45° from +X = (cos45, sin45).
    // From (0,-100): parametric ray = (0 + s*cos45, -100 + s*sin45).
    // Hit y=0: s*sin45 = 100 → s = 100/sin45 = 141.42.
    // x = 141.42 * cos45 = 100. So intersection at (100, 0) = endpoint.
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(0.0, -100.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 45.0;
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    QVERIFY(std::abs(resolved->resolvedPos.x - 100.0) < 1e-4);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-4);
}

void TestIntersection::parallelNoHit()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    // Origin at (50, -30). Ray at 0° = parallel to segment → no intersection.
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(50.0, -30.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 0.0;  // Parallel!
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(!resolved->resolved);  // Should NOT resolve.
}

void TestIntersection::wrongDirection()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    // Origin at (50, 30) — ABOVE the segment. Ray at 90° = straight up (+Y).
    // The segment is below, so the ray goes away from it → no hit (s < 0).
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(50.0, 30.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;  // Points up, segment is below.
    ix.interBidirectional = false;
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(!resolved->resolved);  // No hit in ray mode.
}

void TestIntersection::bidirectionalHit()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    // Same setup as wrongDirection, but bidirectional = true.
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(50.0, 30.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;
    ix.interBidirectional = true;  // Full line → hits below too.
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    QVERIFY(std::abs(resolved->resolvedPos.x - 50.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::endpointHit()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    // Origin at (0, -50). Ray at 90° → hits at (0, 0) = segment start (t=0).
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(0.0, -50.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    QVERIFY(std::abs(resolved->resolvedPos.x - 0.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::formulaAngle()
{
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(50.0, -30.0);
    b.addPoint(a);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 0.0;  // Overridden by formula.
    ix.interAngleFormula = QStringLiteral("45+45");  // = 90°.
    b.addPoint(ix);

    QHash<QString, double> params;
    b.resolve(params);

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    QVERIFY(std::abs(resolved->resolvedPos.x - 50.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::aimPointSameBlock()
{
    // Horizontal segment L from (0,0) to (100,0). Origin A at (0,-50); aim
    // point B at (80,20) in the same block. The ray points straight at B
    // regardless of interAngle (45° here, used only as the fallback).
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;

    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(0.0, -50.0);
    b.addPoint(a);
    const QUuid originId = a.id;

    ParamPoint aim;
    aim.constraint = PointConstraint::Free;
    aim.freePos = Vec2(80.0, 20.0);
    b.addPoint(aim);
    const QUuid aimId = aim.id;

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = originId;
    ix.hostSegmentId = segId;
    ix.interAngle = 45.0;  // Ignored while the aim point exists.
    ix.interAimPointId = aimId;
    b.addPoint(ix);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(ix.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    // Ray from (0,-50) through (80,20): direction (80,70). Hit y=0 at
    // s = 50/70 (unnormalized param) → x = 80 * 50/70 = 4000/70 ≈ 57.142857.
    QVERIFY(std::abs(resolved->resolvedPos.x - 4000.0 / 70.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::aimPointCrossBlock()
{
    // The aim point lives in a DIFFERENT block: Block 1 holds the segment L
    // and the intersection point (origin A also on block 1), Block 2 holds
    // the aim point B. The direction must be computed in world space by the
    // Resolver's cross-block pass (Step 6).
    Block b1 = makeHorizontalSegment();
    const QUuid segId = b1.segments[0].id;

    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(0.0, -50.0);
    b1.addPoint(a);
    const QUuid originId = a.id;

    Block b2;
    ParamPoint aim;
    aim.constraint = PointConstraint::Free;
    aim.freePos = Vec2(80.0, 20.0);
    b2.addPoint(aim);
    const QUuid aimId = aim.id;

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = originId;
    ix.hostSegmentId = segId;
    ix.interAngle = 45.0;  // Fallback only.
    ix.interAimPointId = aimId;
    b1.addPoint(ix);
    const QUuid ixId = ix.id;

    std::vector<Block> blocks;
    blocks.push_back(b1);
    blocks.push_back(b2);
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const ParamPoint* resolved = blocks[0].findPoint(ixId);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    // Same geometry as aimPointSameBlock: x = 4000/70 ≈ 57.142857.
    QVERIFY(std::abs(resolved->resolvedPos.x - 4000.0 / 70.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::auxPointFromRefPoint()
{
    // Test interpRefPointId: auxiliary point measured from another point
    // on the same segment instead of the endpoint.
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;
    const QUuid p1Id = b.points[0].id;  // (0,0)
    const QUuid p2Id = b.points[1].id;  // (100,0)

    // Create a reference point at (40, 0) on the segment (OnSegment, ratio=0.4).
    ParamPoint refPt;
    refPt.constraint = PointConstraint::OnSegment;
    refPt.refPointA = p1Id;
    refPt.refPointB = p2Id;
    refPt.ratio = 0.4;  // → (40, 0)
    b.addPoint(refPt);

    // Auxiliary point: 10mm from refPt along segment direction.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.interpRefPointId = refPt.id;  // Measure from refPt!
    aux.interpPercent = 0.0;          // No percentage offset.
    aux.interpConstant = 10.0;        // 10mm along direction.
    aux.interpFromEnd = false;        // Direction: start→end (+X).
    aux.isAuxiliary = true;
    b.addPoint(aux);

    b.resolve();

    const ParamPoint* resolved = b.findPoint(aux.id);
    QVERIFY(resolved);
    QVERIFY(resolved->resolved);
    // Expected: refPt(40,0) + 10mm along +X = (50, 0).
    QVERIFY(std::abs(resolved->resolvedPos.x - 50.0) < 1e-6);
    QVERIFY(std::abs(resolved->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::auxFromIntersection()
{
    // Full workflow: intersection point → aux point measured from it.
    Block b = makeHorizontalSegment();
    const QUuid segId = b.segments[0].id;
    const QUuid p1Id = b.points[0].id;
    const QUuid p2Id = b.points[1].id;

    // Origin A at (60, -40).
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(60.0, -40.0);
    b.addPoint(a);

    // Intersection: ray from A at 90° → hits segment at (60, 0).
    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = a.id;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;
    ix.isAuxiliary = true;
    b.addPoint(ix);

    // Auxiliary point: 15mm from the intersection point along +X.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.interpRefPointId = ix.id;  // Measure from intersection!
    aux.interpPercent = 0.0;
    aux.interpConstant = 15.0;  // 15mm.
    aux.interpFromEnd = false;
    aux.isAuxiliary = true;
    b.addPoint(aux);

    b.resolve();

    // Verify intersection at (60, 0).
    const ParamPoint* rIx = b.findPoint(ix.id);
    QVERIFY(rIx && rIx->resolved);
    QVERIFY(std::abs(rIx->resolvedPos.x - 60.0) < 1e-6);

    // Verify aux at (75, 0) = 60 + 15.
    const ParamPoint* rAux = b.findPoint(aux.id);
    QVERIFY(rAux && rAux->resolved);
    QVERIFY(std::abs(rAux->resolvedPos.x - 75.0) < 1e-6);
    QVERIFY(std::abs(rAux->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::auxFromCrossBlockIntersection()
{
    // Block 1: horizontal segment L1 from (0,0) to (100,0).
    Block b1 = makeHorizontalSegment();
    const QUuid segId = b1.segments[0].id;

    // Block 2: a separate block holding the ray origin A at (60, -40) (world).
    Block b2;
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(60.0, -40.0);
    b2.addPoint(a);
    const QUuid originPointId = a.id;

    // Intersection point lives ON b1's segment but its ray origin is on b2
    // (cross-block). It resolves in the Resolver's cross-block pass.
    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = originPointId;      // on b2 (different block!)
    ix.hostSegmentId = segId;           // on b1
    ix.interAngle = 90.0;               // ray straight up → hits L1 at (60, 0)
    ix.isAuxiliary = true;
    b1.addPoint(ix);
    const QUuid ixId = ix.id;

    // Auxiliary point on L1 measured from the cross-block intersection (+15mm).
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.interpRefPointId = ixId;        // references the cross-block intersection
    aux.interpPercent = 0.0;
    aux.interpConstant = 15.0;
    aux.interpFromEnd = false;
    aux.isAuxiliary = true;
    b1.addPoint(aux);
    const QUuid auxId = aux.id;

    // Resolve the whole system (two blocks, no attachments).
    std::vector<Block> blocks;
    blocks.push_back(b1);
    blocks.push_back(b2);
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const Block& rb1 = blocks[0];

    // Intersection resolves at (60, 0) via the cross-block pass.
    const ParamPoint* rIx = rb1.findPoint(ixId);
    QVERIFY(rIx && rIx->resolved);
    QVERIFY(std::abs(rIx->resolvedPos.x - 60.0) < 1e-6);

    // The aux point referencing it must ALSO resolve (this was the bug: it
    // stayed unresolved because the intersection resolved after Step 1).
    const ParamPoint* rAux = rb1.findPoint(auxId);
    QVERIFY(rAux && rAux->resolved);
    QVERIFY(std::abs(rAux->resolvedPos.x - 75.0) < 1e-6);  // 60 + 15
    QVERIFY(std::abs(rAux->resolvedPos.y - 0.0) < 1e-6);
}

void TestIntersection::aimChainThroughInterpolatedResolves()
{
    // Block A: horizontal segment L_A from (0,0) to (100,0).
    Block a;
    ParamPoint a1;
    a1.constraint = PointConstraint::Free;
    a1.freePos = Vec2(0.0, 0.0);
    const QUuid a1Id = a1.id;
    ParamPoint a2;
    a2.constraint = PointConstraint::Free;
    a2.freePos = Vec2(100.0, 0.0);
    const QUuid a2Id = a2.id;
    a.addPoint(std::move(a1));
    a.addPoint(std::move(a2));
    Segment segA;
    segA.startPointId = a1Id;
    segA.endPointId = a2Id;
    const QUuid segAId = segA.id;
    a.addSegment(std::move(segA));

    // Block B: cross-block ray origin O at (50,-40).
    Block b;
    ParamPoint o;
    o.constraint = PointConstraint::Free;
    o.freePos = Vec2(50.0, -40.0);
    b.addPoint(std::move(o));
    const QUuid oId = o.id;

    // P324: intersection on L_A with origin O (cross-block) — Step 6 pass 1.
    ParamPoint ix1;
    ix1.constraint = PointConstraint::Intersection;
    ix1.refPointA = oId;
    ix1.hostSegmentId = segAId;
    ix1.interAngle = 90.0;
    const QUuid ix1Id = ix1.id;
    a.addPoint(std::move(ix1));

    // P325: interpolated aux on L_A measured FROM P324 (+15mm along +X) —
    // only resolves in the 6b pass AFTER P324.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segAId;
    aux.interpRefPointId = ix1Id;
    aux.interpPercent = 0.0;
    aux.interpConstant = 15.0;
    aux.isAuxiliary = true;
    const QUuid auxId = aux.id;
    a.addPoint(std::move(aux));

    // P347: intersection on L_A aiming AT P325 (cross-block interpolated) —
    // needs a SECOND Step-6 pass after P325 resolved in 6b.
    ParamPoint ix2;
    ix2.constraint = PointConstraint::Intersection;
    ix2.refPointA = oId;
    ix2.hostSegmentId = segAId;
    ix2.interAngle = 45.0;  // fallback, overridden by the aim point
    ix2.interAimPointId = auxId;
    const QUuid ix2Id = ix2.id;
    a.addPoint(std::move(ix2));

    std::vector<Block> blocks;
    blocks.push_back(std::move(a));
    blocks.push_back(std::move(b));
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    // P324 = (50,0); P325 = (65,0); P347 = ray O→P325 ∩ L_A = (65,0).
    const Block& ra = blocks[0];
    const ParamPoint* rIx2 = ra.findPoint(ix2Id);
    QVERIFY2(rIx2 && rIx2->resolved,
             "chained aim intersection must resolve after the 6b pass");
    QVERIFY(std::abs(rIx2->resolvedPos.x - 65.0) < 1e-6);
    QVERIFY(std::abs(rIx2->resolvedPos.y - 0.0) < 1e-6);
}

QTEST_GUILESS_MAIN(TestIntersection)
#include "test_intersection.moc"
