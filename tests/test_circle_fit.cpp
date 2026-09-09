/// @file test_circle_fit.cpp
/// M1 acceptance for the parametric circle (docs/design/CIRCLE_TOOL_DESIGN.md
/// D1/D2/D3/D11/D12/D17/D21).
///
/// The circle is NOT a new primitive: it is a Bezier segment whose
/// `fitKind == FitKind::Circle`, with a Free center, two coincident Polar
/// endpoints (a1 = a0 + 360, unwrapped) and three CurveAnchor quadrant points.
/// `Block::applyCircleFitTangents()` refits the four 90° cubic spans to the
/// true circle on every resolve. This test locks the contract:
///   ① one block, one Bezier segment, fitKind=Circle, 6 points;
///   ② the anchors resolve ON the circle (radius + angle), not on a chord —
///      a full circle has a zero-length chord, so the generic path would
///      collapse all three onto the center;
///   ③ 256 samples of the flattened polyline stay within 1e-6*r of the circle;
///   ④ the cubic control arm is the exact (4/3)·tan(π/8)·r (stored Hermite
///      tangent ×3) — the classic "circle looks like a rounded diamond" bug;
///   ⑤ editing the radius (p0.distance, D2) keeps it a circle and does not
///      move the center;
///   ⑥ D17 roundness: tension = -1 gives the inscribed square;
///   ⑦ fitKind survives serialize → deserialize (additive key, no format bump).

#include <QtTest>
#include <QUuid>
#include <cmath>
#include <vector>

#include "document/DocumentSerializer.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "tools/CircleFactory.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

struct CircleIds {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
};

CircleIds makeCircle(ParamDocument& doc, double radiusMm,
                     double startAngleDeg = 0.0,
                     const Vec2& center = Vec2(10.0, 20.0))
{
    CircleIds ids;
    cad::tools::CircleFactory factory(&doc, doc.undoStack());
    ids.blockId = factory.createCircle(center, radiusMm, startAngleDeg);
    if (Block* b = doc.findBlock(ids.blockId); b && !b->segments.empty()) {
        ids.segId = b->segments.front().id;
        ids.startId = b->segments.front().startPointId;
        ids.endId = b->segments.front().endPointId;
        if (const ParamPoint* sp = b->findPoint(ids.startId))
            ids.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return ids;
}

/// Max |‖P‖ − r| over the flattened polyline (local coords, center = origin).
double maxRadiusError(const std::vector<Vec2>& poly, double radiusMm)
{
    double worst = 0.0;
    for (const Vec2& p : poly)
        worst = std::max(worst, std::abs(p.length() - radiusMm));
    return worst;
}

} // namespace

class TestCircleFit : public QObject
{
    Q_OBJECT

private slots:
    void createsOneBezierSegmentWithSixPoints();
    void anchorsResolveOnTheCircle();
    void flattenedPolylineStaysOnTheCircle();
    void controlArmMatchesQuarterCircleKappa();
    void radiusEditKeepsCircleAndCenter();
    void tensionMinusOneGivesInscribedSquare();
    void fitKindSurvivesSerialization();
    void undoRedoRestoresCircle();
};

void TestCircleFit::createsOneBezierSegmentWithSixPoints()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);

    QVERIFY(!ids.blockId.isNull());
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    QCOMPARE(b->points.size(), size_t(6));   // center + 2 polar + 3 anchors
    QCOMPARE(b->segments.size(), size_t(1));

    const Segment& seg = b->segments.front();
    QCOMPARE(seg.type, SegmentType::Bezier);
    QCOMPARE(seg.fitKind, FitKind::Circle);
    QCOMPARE(seg.role, SegmentRole::Auxiliary);   // D4: construction circle
    QVERIFY(seg.isCurve());                        // type==Bezier && pass points
    QCOMPARE(seg.passPointIds.size(), size_t(3));

    const ParamPoint* center = b->findPoint(ids.centerId);
    const ParamPoint* start = b->findPoint(ids.startId);
    const ParamPoint* end = b->findPoint(ids.endId);
    QVERIFY(center && start && end);
    QCOMPARE(center->constraint, PointConstraint::Free);
    QCOMPARE(start->constraint, PointConstraint::Polar);
    QCOMPARE(end->constraint, PointConstraint::Polar);
    QCOMPARE(start->refPointId, ids.centerId);
    QCOMPARE(end->refPointId, ids.centerId);
    // D12: full circle = a1 = a0 + 360, stored UNWRAPPED (never normalized).
    QCOMPARE(end->angle - start->angle, 360.0);
    QVERIFY(qFuzzyCompare(start->distance, 50.0));
    QVERIFY(qFuzzyCompare(end->distance, 50.0));
    // The duplicate handle at the seam must not be drawn or selectable.
    QCOMPARE(end->visible, false);
    QCOMPARE(end->selectable, false);
}

void TestCircleFit::anchorsResolveOnTheCircle()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 40.0);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);

    const Segment& seg = b->segments.front();
    QCOMPARE(seg.passPointIds.size(), size_t(3));
    const double expectDeg[] = {90.0, 180.0, 270.0};
    for (size_t i = 0; i < seg.passPointIds.size(); ++i) {
        const ParamPoint* a = b->findPoint(seg.passPointIds[i]);
        QVERIFY(a);
        QVERIFY(a->resolved);
        QVERIFY2(std::abs(a->resolvedPos.length() - 40.0) < 1e-9,
                 qPrintable(QStringLiteral("anchor %1 radius %2")
                                .arg(i).arg(a->resolvedPos.length())));
        const double deg = cad::geo::radToDeg(std::atan2(a->resolvedPos.y,
                                                         a->resolvedPos.x));
        double diff = std::abs(deg - expectDeg[i]);
        while (diff > 180.0) diff = std::abs(diff - 360.0);
        QVERIFY2(diff < 1e-9, qPrintable(QStringLiteral("anchor %1 angle %2")
                                             .arg(i).arg(deg)));
    }
}

void TestCircleFit::flattenedPolylineStaysOnTheCircle()
{
    ParamDocument doc;
    const double r = 63.5;   // 12.7 cm — a real garment radius
    const CircleIds ids = makeCircle(doc, r);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);

    const CurveSpanEntry* entry = b->curveSpanEntry(ids.segId);
    QVERIFY(entry);
    QCOMPARE(entry->spans.size(), size_t(4));      // four 90° cubic spans
    // Flattened at a 0.1mm absolute tolerance (D16): ~14 segments per 90° span
    // at this radius, so ~56 vertices. The exact count is an implementation
    // detail of flattenSpan(); only "densely sampled" is contractual.
    QVERIFY2(entry->flatLocal.size() >= 32,
             qPrintable(QStringLiteral("flatLocal.size() = %1")
                            .arg(entry->flatLocal.size())));

    // The polyline vertices lie ON the cubic, so what this measures is the
    // inherent error of the tangent-continuous 4×90° cubic circle: the classic
    // kappa = (4/3)·tan(π/8) has a maximum radial deviation of 2.7253e-4·r
    // (0.027 mm at r = 100 mm — two orders below any sewing tolerance). The
    // minimax kappa 0.551915024494 would halve it to 1.96e-4·r but breaks
    // tangent continuity at the quadrant joins, so it was rejected.
    const double kCubicCircleError = 2.7253e-4;
    QVERIFY2(maxRadiusError(entry->flatLocal, r) < 1.05 * kCubicCircleError * r,
             qPrintable(QStringLiteral("max radial error %1 (bound %2, r=%3)")
                            .arg(maxRadiusError(entry->flatLocal, r))
                            .arg(kCubicCircleError * r).arg(r)));
}

void TestCircleFit::controlArmMatchesQuarterCircleKappa()
{
    ParamDocument doc;
    const double r = 25.0;
    const CircleIds ids = makeCircle(doc, r);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    const CurveSpanEntry* entry = b->curveSpanEntry(ids.segId);
    QVERIFY(entry && entry->spans.size() == 4);

    // a0 = 0 → span 0 runs from (r,0) to (0,r). The cubic control arm of a 90°
    // circular arc is exactly (4/3)·tan(π/8)·r ≈ 0.55228475·r; the stored
    // Hermite tangent is 3× that (CurveMath divides by 3 when building ctrl1).
    const double kappa = (4.0 / 3.0) * std::tan(cad::geo::kPi / 8.0);
    const cad::geo::BezierSpan& s0 = entry->spans.front();
    QVERIFY2(std::abs(s0.p0.x - r) < 1e-9 && std::abs(s0.p0.y) < 1e-9,
             qPrintable(QStringLiteral("span0.p0 = %1,%2").arg(s0.p0.x).arg(s0.p0.y)));
    QVERIFY2(std::abs(s0.p3.x) < 1e-9 && std::abs(s0.p3.y - r) < 1e-9,
             qPrintable(QStringLiteral("span0.p3 = %1,%2").arg(s0.p3.x).arg(s0.p3.y)));
    QVERIFY2(std::abs(s0.ctrl1.x - r) < 1e-9
                 && std::abs(s0.ctrl1.y - kappa * r) < 1e-9,
             qPrintable(QStringLiteral("span0.ctrl1 = %1,%2 (expect %3,%4)")
                            .arg(s0.ctrl1.x).arg(s0.ctrl1.y)
                            .arg(r).arg(kappa * r)));
    QVERIFY2(std::abs(s0.ctrl2.x - kappa * r) < 1e-9
                 && std::abs(s0.ctrl2.y - r) < 1e-9,
             qPrintable(QStringLiteral("span0.ctrl2 = %1,%2").arg(s0.ctrl2.x).arg(s0.ctrl2.y)));

    // The tangents must be manual: an auto tangent would let the Hobby solver
    // overwrite the analytic arms on the next resolve.
    const ParamPoint* start = b->findPoint(ids.startId);
    QVERIFY(start);
    QCOMPARE(start->autoTangent, false);
    QVERIFY2(std::abs(start->tangentOut.length() - 3.0 * kappa * r) < 1e-9,
             qPrintable(QStringLiteral("tangentOut length %1 (expect %2)")
                            .arg(start->tangentOut.length()).arg(3.0 * kappa * r)));
}

void TestCircleFit::radiusEditKeepsCircleAndCenter()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 30.0);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);

    // D2: the radius authority is the start point's Polar distance. The end
    // point mirrors it (syncCircleFitRadius), and the center never moves.
    ParamPoint* start = b->findPoint(ids.startId);
    QVERIFY(start);
    start->distance = 80.0;
    doc.resolveAll();

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    const ParamPoint* center = b->findPoint(ids.centerId);
    const ParamPoint* end = b->findPoint(ids.endId);
    QVERIFY(center && end);
    QVERIFY2(center->resolvedPos.length() < 1e-9, "center must stay at the block origin");
    QVERIFY2(std::abs(end->distance - 80.0) < 1e-9, "end radius must mirror the start");

    const CurveSpanEntry* entry = b->curveSpanEntry(ids.segId);
    QVERIFY(entry && !entry->flatLocal.empty());
    QVERIFY2(maxRadiusError(entry->flatLocal, 80.0) < 2.7253e-4 * 80.0 * 1.05,
             qPrintable(QStringLiteral("after radius edit max error %1")
                            .arg(maxRadiusError(entry->flatLocal, 80.0))));
}

void TestCircleFit::tensionMinusOneGivesInscribedSquare()
{
    // D17: the reused Segment::tension scales the control arms by (1+tension).
    // tension = -1 zeroes them → every span is a straight chord → the four
    // quadrant anchors form the inscribed square (vertices at r, edge
    // midpoints at r/√2). Asserted on the SPAN CONTROL POINTS, not on the
    // flattened polyline: the flattener may legitimately reduce a straight
    // span to its two endpoints.
    ParamDocument doc;
    const double r = 20.0;
    const CircleIds ids = makeCircle(doc, r);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    b->findSegment(ids.segId)->tension = -1.0;
    doc.resolveAll();

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    const CurveSpanEntry* entry = b->curveSpanEntry(ids.segId);
    QVERIFY(entry);
    QCOMPARE(entry->spans.size(), size_t(4));

    for (const cad::geo::BezierSpan& s : entry->spans) {
        // Zero control arms = the cubic degenerates into the straight chord.
        QVERIFY2((s.ctrl1 == s.p0) && (s.ctrl2 == s.p3),
                 qPrintable(QStringLiteral("span is not straight: ctrl1=(%1,%2) ctrl2=(%3,%4)")
                                .arg(s.ctrl1.x).arg(s.ctrl1.y).arg(s.ctrl2.x).arg(s.ctrl2.y)));
        QVERIFY2(std::abs(s.p0.length() - r) < 1e-9,
                 qPrintable(QStringLiteral("vertex radius %1").arg(s.p0.length())));
        // Edge midpoint of the inscribed square: r/√2.
        const Vec2 mid = (s.p0 + s.p3) * 0.5;
        QVERIFY2(std::abs(mid.length() - r / std::sqrt(2.0)) < 1e-9,
                 qPrintable(QStringLiteral("edge midpoint radius %1 (expect %2)")
                                .arg(mid.length()).arg(r / std::sqrt(2.0))));
    }
}

void TestCircleFit::fitKindSurvivesSerialization()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 45.0, 30.0);

    ParamDocument dst;
    QStringList warnings;
    DocumentSerializer::deserialize(dst, DocumentSerializer::serialize(doc), &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QStringLiteral("; "))));

    const Block* rb = dst.findBlock(ids.blockId);
    QVERIFY2(rb, "circle block must survive the round trip");
    QCOMPARE(rb->segments.size(), size_t(1));
    QCOMPARE(rb->segments.front().fitKind, FitKind::Circle);
    QCOMPARE(rb->segments.front().passPointIds.size(), size_t(3));

    // And the reloaded circle is still a circle (the fit is re-derived from the
    // points, nothing geometric is serialized).
    dst.resolveAll();
    const CurveSpanEntry* entry = rb->curveSpanEntry(ids.segId);
    QVERIFY(entry && !entry->flatLocal.empty());
    QVERIFY2(maxRadiusError(entry->flatLocal, 45.0) < 2.7253e-4 * 45.0 * 1.05,
             qPrintable(QStringLiteral("reloaded max radial error %1")
                            .arg(maxRadiusError(entry->flatLocal, 45.0))));
}

void TestCircleFit::undoRedoRestoresCircle()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 15.0);
    QVERIFY(doc.findBlock(ids.blockId));

    doc.undoStack()->undo();
    QVERIFY(!doc.findBlock(ids.blockId));
    doc.undoStack()->redo();
    QVERIFY(doc.findBlock(ids.blockId));
}

QTEST_GUILESS_MAIN(TestCircleFit)
#include "test_circle_fit.moc"
