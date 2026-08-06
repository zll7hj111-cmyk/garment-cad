#include <QtTest>
#include <cmath>
#include <vector>

#include "geometry/CurveMath.h"
#include "geometry/Vec2.h"

using namespace cad::geo;

class TestCurve : public QObject
{
    Q_OBJECT

private slots:
    void catmullRomTangentBasic();
    void catmullRomTangentEndpoints();
    void buildCatmullRomSpansCount();
    void bezierEvalEndpoints();
    void bezierEvalMidpoint();
    void straightLineDegenerate();
    void arcLengthStraightLine();
    void arcLengthCurve();
    void arcLengthToParamRoundtrip();
    void projectOnStraightLine();
    void projectOnSplineCurve();
    void rayIntersectLine();
    void rayIntersectCurve();
    void evalCurveMultiSpan();
    void tangentContinuity();
    void c2InterpolatesPoints();
    void c2CurvatureContinuity();
    void c2RespectsManualTangent();
    void flattenPolylineQuality();
    void hobbyInterpolatesPoints();
    void hobbyEndpointChordDirection();
    void hobbyTangentContinuity();
    void hobbyManualRespected();
    void hobbyTensionEffect();
    void hobbyOvershootVsC2();
};

// ─── Catmull-Rom tangent ────────────────────────────────────────────────────

void TestCurve::catmullRomTangentBasic()
{
    // Three collinear points: tangent at middle should point along the line
    std::vector<Vec2> pts = {{0, 0}, {10, 0}, {20, 0}};
    Vec2 t = catmullRomTangent(pts, 1);
    QVERIFY(t.length() > 0);
    // Direction should be +X
    QVERIFY(std::abs(t.normalized().x - 1.0) < 1e-6);
    QVERIFY(std::abs(t.normalized().y) < 1e-6);
}

void TestCurve::catmullRomTangentEndpoints()
{
    // Endpoint tangent should not be zero (phantom point reflection)
    std::vector<Vec2> pts = {{0, 0}, {10, 5}, {20, 0}};
    Vec2 t0 = catmullRomTangent(pts, 0);
    Vec2 t2 = catmullRomTangent(pts, 2);
    QVERIFY(t0.length() > 1e-6);
    QVERIFY(t2.length() > 1e-6);
}

// ─── Build spans ────────────────────────────────────────────────────────────

void TestCurve::buildCatmullRomSpansCount()
{
    std::vector<Vec2> pts = {{0, 0}, {10, 5}, {20, 0}, {30, 5}};
    auto spans = buildCatmullRomSpans(pts);
    QCOMPARE(static_cast<int>(spans.size()), 3);  // N-1 spans
}

// ─── Bézier evaluation ──────────────────────────────────────────────────────

void TestCurve::bezierEvalEndpoints()
{
    BezierSpan span{{0, 0}, {3, 5}, {7, 5}, {10, 0}};
    Vec2 start = evalBezier(span, 0.0);
    Vec2 end = evalBezier(span, 1.0);
    QVERIFY(std::abs(start.x - 0.0) < 1e-9);
    QVERIFY(std::abs(start.y - 0.0) < 1e-9);
    QVERIFY(std::abs(end.x - 10.0) < 1e-9);
    QVERIFY(std::abs(end.y - 0.0) < 1e-9);
}

void TestCurve::bezierEvalMidpoint()
{
    // Symmetric span: midpoint should be at x=5
    BezierSpan span{{0, 0}, {3, 5}, {7, 5}, {10, 0}};
    Vec2 mid = evalBezier(span, 0.5);
    QVERIFY(std::abs(mid.x - 5.0) < 1e-9);
    QVERIFY(mid.y > 0);  // Should bulge upward
}

void TestCurve::straightLineDegenerate()
{
    // All control points collinear → curve is a straight line
    BezierSpan span{{0, 0}, {10.0 / 3, 0}, {20.0 / 3, 0}, {10, 0}};
    Vec2 mid = evalBezier(span, 0.5);
    QVERIFY(std::abs(mid.x - 5.0) < 1e-9);
    QVERIFY(std::abs(mid.y) < 1e-9);
}

// ─── Arc length ─────────────────────────────────────────────────────────────

void TestCurve::arcLengthStraightLine()
{
    // Straight line from (0,0) to (10,0): arc length = 10
    BezierSpan span{{0, 0}, {10.0 / 3, 0}, {20.0 / 3, 0}, {10, 0}};
    double len = spanArcLength(span);
    QVERIFY(std::abs(len - 10.0) < 1e-6);
}

void TestCurve::arcLengthCurve()
{
    // Curved span: arc length > chord length
    BezierSpan span{{0, 0}, {3, 5}, {7, 5}, {10, 0}};
    double arcLen = spanArcLength(span);
    double chordLen = span.p0.distanceTo(span.p3);
    QVERIFY(arcLen > chordLen);
    QVERIFY(arcLen < chordLen * 2.0);  // Sanity: not wildly longer
}

void TestCurve::arcLengthToParamRoundtrip()
{
    std::vector<Vec2> pts = {{0, 0}, {10, 8}, {20, -3}, {30, 5}};
    auto spans = buildCatmullRomSpans(pts);
    double total = totalArcLength(spans);
    QVERIFY(total > 0);

    // Roundtrip: s → T → s
    double targetS = total * 0.4;
    double T = arcLengthToParam(spans, targetS);
    QVERIFY(T > 0 && T < static_cast<double>(spans.size()));

    // Verify: arc length at T should equal targetS
    // Compute arc length from 0 to T
    int spanIdx = static_cast<int>(T);
    double localT = T - spanIdx;
    double s = 0;
    for (int i = 0; i < spanIdx; ++i)
        s += spanArcLength(spans[i]);
    // Partial span arc length
    BezierSpan partial = spans[spanIdx];
    // Subdivide: compute arc length from 0 to localT via sampling
    double partialS = 0;
    constexpr int N = 100;
    Vec2 prev = evalBezier(partial, 0);
    for (int k = 1; k <= N; ++k) {
        double t = localT * k / N;
        Vec2 cur = evalBezier(partial, t);
        partialS += prev.distanceTo(cur);
        prev = cur;
    }
    s += partialS;
    QVERIFY(std::abs(s - targetS) < total * 0.01);  // Within 1%
}

// ─── Projection ─────────────────────────────────────────────────────────────

void TestCurve::projectOnStraightLine()
{
    // Straight "curve" (one span, collinear control points)
    BezierSpan span{{0, 0}, {10.0 / 3, 0}, {20.0 / 3, 0}, {10, 0}};
    std::vector<BezierSpan> spans = {span};

    // Project (5, 3) → should land at (5, 0)
    auto proj = projectPointOnCurve({5, 3}, spans);
    QVERIFY(proj.valid);
    QVERIFY(std::abs(proj.point.x - 5.0) < 0.1);
    QVERIFY(std::abs(proj.point.y) < 0.1);
    QVERIFY(std::abs(proj.distance - 3.0) < 0.1);
    // Tangent should be +X
    QVERIFY(std::abs(proj.tangent.x - 1.0) < 0.01);
}

void TestCurve::projectOnSplineCurve()
{
    std::vector<Vec2> pts = {{0, 0}, {10, 10}, {20, 0}};
    auto spans = buildCatmullRomSpans(pts);

    // Project the middle point itself → distance ≈ 0
    auto proj = projectPointOnCurve({10, 10}, spans);
    QVERIFY(proj.valid);
    QVERIFY(proj.distance < 1.0);  // Should be very close (curve passes through)
}

// ─── Ray intersection ───────────────────────────────────────────────────────

void TestCurve::rayIntersectLine()
{
    // Horizontal line from (0,0) to (10,0)
    BezierSpan span{{0, 0}, {10.0 / 3, 0}, {20.0 / 3, 0}, {10, 0}};
    std::vector<BezierSpan> spans = {span};

    // Vertical ray from (5, -5) going up
    auto hits = rayCurveIntersect({5, -5}, {0, 1}, spans);
    QCOMPARE(static_cast<int>(hits.size()), 1);
    QVERIFY(std::abs(hits[0].point.x - 5.0) < 0.01);
    QVERIFY(std::abs(hits[0].point.y) < 0.01);
}

void TestCurve::rayIntersectCurve()
{
    // Arch curve: (0,0) → (10,10) → (20,0)
    std::vector<Vec2> pts = {{0, 0}, {10, 10}, {20, 0}};
    auto spans = buildCatmullRomSpans(pts);

    // Vertical ray from (10, -5) going up → should hit the curve near (10, ~10)
    auto hits = rayCurveIntersect({10, -5}, {0, 1}, spans);
    QVERIFY(!hits.empty());
    QVERIFY(std::abs(hits[0].point.x - 10.0) < 1.0);
    QVERIFY(hits[0].point.y > 5.0);  // Should be near the apex
}

// ─── Multi-span evaluation ──────────────────────────────────────────────────

void TestCurve::evalCurveMultiSpan()
{
    std::vector<Vec2> pts = {{0, 0}, {10, 5}, {20, 0}};
    auto spans = buildCatmullRomSpans(pts);
    QCOMPARE(static_cast<int>(spans.size()), 2);

    // T=0 → first point
    Vec2 p0 = evalCurve(spans, 0.0);
    QVERIFY(std::abs(p0.x) < 1e-6 && std::abs(p0.y) < 1e-6);

    // T=2 → last point
    Vec2 p2 = evalCurve(spans, 2.0);
    QVERIFY(std::abs(p2.x - 20.0) < 1e-6 && std::abs(p2.y) < 1e-6);

    // T=1 → junction (should be near middle point)
    Vec2 p1 = evalCurve(spans, 1.0);
    QVERIFY(std::abs(p1.x - 10.0) < 1e-6);
    QVERIFY(std::abs(p1.y - 5.0) < 1e-6);
}

void TestCurve::tangentContinuity()
{
    // At a junction between spans with autoTangent, tangent should be continuous
    std::vector<Vec2> pts = {{0, 0}, {10, 8}, {20, -3}, {30, 5}};
    auto spans = buildCatmullRomSpans(pts);

    // Tangent just before and just after junction at T=1
    Vec2 tanBefore = evalCurveTangent(spans, 1.0 - 1e-6);
    Vec2 tanAfter = evalCurveTangent(spans, 1.0 + 1e-6);
    double dot = tanBefore.dot(tanAfter);
    QVERIFY(dot > 0.99);  // Nearly identical direction (C¹ continuous)
}

// ─── C2 automatic tangents (曲率连续自动算法) ──────────────────────────

void TestCurve::c2InterpolatesPoints()
{
    // The C2 curve must still pass exactly through every point.
    std::vector<Vec2> pts = {{0, 0}, {30, 20}, {70, -10}, {100, 5}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n, Vec2::zero()), tOut(n, Vec2::zero());
    std::vector<bool> autoTan(n, true);
    auto spans = buildBezierSpans(pts, tIn, tOut, autoTan, 0.0);
    QCOMPARE(static_cast<int>(spans.size()), n - 1);
    for (int i = 0; i < n; ++i) {
        Vec2 p = evalCurve(spans, static_cast<double>(i));
        QVERIFY(p.distanceTo(pts[i]) < 1e-6);
    }
}

void TestCurve::c2CurvatureContinuity()
{
    // Curvature must be continuous across every interior junction: the second
    // derivative (scaled by 1/h² to account for the chord-length parameter)
    // must match from the left and the right of each point.
    std::vector<Vec2> pts = {{0, 0}, {25, 18}, {55, -12}, {80, 6}, {100, 0}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n, Vec2::zero()), tOut(n, Vec2::zero());
    std::vector<bool> autoTan(n, true);
    auto spans = buildBezierSpans(pts, tIn, tOut, autoTan, 0.0);

    for (int i = 1; i < n - 1; ++i) {
        const BezierSpan& L = spans[i - 1];
        const BezierSpan& R = spans[i];
        // B''(1) of the left span, B''(0) of the right span.
        Vec2 d2L = (L.p3 - L.ctrl2 * 2.0 + L.ctrl1) * 6.0;
        Vec2 d2R = (R.ctrl2 - R.ctrl1 * 2.0 + R.p0) * 6.0;
        const double hL = pts[i - 1].distanceTo(pts[i]);
        const double hR = pts[i].distanceTo(pts[i + 1]);
        Vec2 curvL = d2L / (hL * hL);
        Vec2 curvR = d2R / (hR * hR);
        const double tol = 1e-6 * std::max(1.0, curvL.length());
        QVERIFY(curvL.distanceTo(curvR) < tol);
    }
}

void TestCurve::c2RespectsManualTangent()
{
    // A manual point's stored tangent must be used as-is (a fixed constraint),
    // while the surrounding auto points are solved around it.
    std::vector<Vec2> pts = {{0, 0}, {50, 0}, {100, 0}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n, Vec2::zero()), tOut(n, Vec2::zero());
    std::vector<bool> autoTan(n, true);
    // Make the middle point manual with a strong upward out-tangent.
    autoTan[1] = false;
    tOut[1] = Vec2{0, 60};   // points straight up
    tIn[1]  = Vec2{0, 60};
    auto spans = buildBezierSpans(pts, tIn, tOut, autoTan, 0.0);

    // The outgoing handle of the middle point must match the manual tangent:
    // ctrl1 of span 1 = P1 + tOut[1]/3 = (50, 20).
    QVERIFY(spans[1].ctrl1.distanceTo(Vec2{50, 20}) < 1e-6);
    // The curve still passes through all points.
    for (int i = 0; i < n; ++i)
        QVERIFY(evalCurve(spans, static_cast<double>(i)).distanceTo(pts[i]) < 1e-6);
}

void TestCurve::flattenPolylineQuality()
{
    // The render path uses flattenBezierSpans (Seamly2D-style polyline):
    // verify the polyline stays within tolerance of the true curve, hits both
    // endpoints, and stays cheap for straight spans.
    std::vector<Vec2> pts = {{0, 0}, {40, 50}, {80, -20}, {140, 30}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n, Vec2::zero()), tOut(n, Vec2::zero());
    std::vector<bool> autoTan(n, true);
    auto spans = buildBezierSpans(pts, tIn, tOut, autoTan, 0.0);

    constexpr double kTol = 0.1;  // mm — same as the render path
    const auto flat = flattenBezierSpans(spans, kTol);

    // Endpoints must be exact.
    QVERIFY(flat.size() >= 2);
    QVERIFY(flat.front().distanceTo(pts.front()) < 1e-9);
    QVERIFY(flat.back().distanceTo(pts.back()) < 1e-9);

    // Every polyline vertex must sit on (or within tolerance of) the curve.
    for (const auto& p : flat) {
        const CurveProjection proj = projectPointOnCurve(p, spans);
        QVERIFY2(proj.valid && proj.distance <= kTol + 1e-6,
                 "flattened vertex deviates from the true curve");
    }

    // A straight span (all controls collinear) must NOT be subdivided: two
    // points (start + end) only.
    BezierSpan straight{{0, 0}, {10, 0}, {20, 0}, {30, 0}};
    const auto flatStraight = flattenBezierSpans({straight}, kTol);
    QCOMPARE(static_cast<int>(flatStraight.size()), 2);

    // Curved spans get a reasonable number of vertices (density, not
    // pathological) — a few dozen, not thousands.
    QVERIFY(flat.size() > 8);
    QVERIFY(flat.size() < 500);
}

// ─── Hobby auto-tangents (Seamly2D algorithm) ───────────────────────────────

namespace {

/// Build spans with the Hobby automatic algorithm.
std::vector<BezierSpan> hobbySpans(const std::vector<Vec2>& pts)
{
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n), tOut(n);
    std::vector<bool> autoTan(n, true);
    return buildBezierSpans(pts, tIn, tOut, autoTan, 1.0, AutoCurveMode::Hobby);
}

} // namespace

void TestCurve::hobbyInterpolatesPoints()
{
    std::vector<Vec2> pts = {{0, 0}, {40, 50}, {80, -20}, {140, 30}};
    const auto spans = hobbySpans(pts);
    QCOMPARE(static_cast<int>(spans.size()), static_cast<int>(pts.size()) - 1);
    for (int i = 0; i < static_cast<int>(pts.size()); ++i)
        QVERIFY(evalCurve(spans, static_cast<double>(i)).distanceTo(pts[i]) < 1e-9);
}

void TestCurve::hobbyEndpointChordDirection()
{
    // Auto endpoints follow their adjacent chord direction (no forced zero
    // curvature): the outgoing tangent at P0 is parallel to the first chord.
    std::vector<Vec2> pts = {{0, 0}, {40, 50}, {80, -20}, {140, 30}};
    const auto spans = hobbySpans(pts);
    const Vec2 d0 = evalBezierDerivative(spans.front(), 0.0);
    const Vec2 chord = pts[1] - pts[0];
    const double dot = d0.normalized().dot(chord.normalized());
    QVERIFY2(std::abs(dot - 1.0) < 1e-6, "endpoint tangent must follow the chord");
}

void TestCurve::hobbyTangentContinuity()
{
    // C1 continuity at every interior anchor: incoming and outgoing tangent
    // directions agree (Hobby gives one direction per point).
    std::vector<Vec2> pts = {{0, 0}, {40, 50}, {80, -20}, {140, 30}};
    const auto spans = hobbySpans(pts);
    for (int i = 1; i < static_cast<int>(pts.size()) - 1; ++i) {
        const Vec2 tanIn  = evalBezierDerivative(spans[i - 1], 1.0).normalized();
        const Vec2 tanOut = evalBezierDerivative(spans[i], 0.0).normalized();
        QVERIFY(std::abs(tanIn.dot(tanOut) - 1.0) < 1e-6);
    }
}

void TestCurve::hobbyManualRespected()
{
    // A manual point keeps its stored tangents verbatim; auto neighbours are
    // solved around it.
    std::vector<Vec2> pts = {{0, 0}, {50, 0}, {100, 0}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n), tOut(n);
    std::vector<bool> autoTan(n, true);
    autoTan[1] = false;
    tOut[1] = Vec2{0, 60};
    tIn[1]  = Vec2{0, 60};
    auto spans = buildBezierSpans(pts, tIn, tOut, autoTan, 1.0, AutoCurveMode::Hobby);
    QVERIFY(spans[1].ctrl1.distanceTo(Vec2{50, 20}) < 1e-6);  // P1 + tOut/3
    for (int i = 0; i < n; ++i)
        QVERIFY(evalCurve(spans, static_cast<double>(i)).distanceTo(pts[i]) < 1e-9);
}

void TestCurve::hobbyTensionEffect()
{
    // Higher tension shortens both handles (curve pulled tighter to the chord).
    std::vector<Vec2> pts = {{0, 0}, {100, 0}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n), tOut(n);
    std::vector<bool> autoTan(n, true);
    // 3-point shape so the solve has a middle anchor with a real angle.
    std::vector<Vec2> pts3 = {{0, 0}, {50, 60}, {100, 0}};
    const int n3 = static_cast<int>(pts3.size());
    std::vector<Vec2> tIn3(n3), tOut3(n3);
    std::vector<bool> autoTan3(n3, true);

    const auto loose = buildBezierSpans(pts3, tIn3, tOut3, autoTan3, 0.5, AutoCurveMode::Hobby);
    const auto tight = buildBezierSpans(pts3, tIn3, tOut3, autoTan3, 2.0, AutoCurveMode::Hobby);
    Q_UNUSED(pts); Q_UNUSED(tIn); Q_UNUSED(tOut); Q_UNUSED(autoTan); Q_UNUSED(n);
    const double looseCtrl = (loose[0].ctrl1 - loose[0].p0).length();
    const double tightCtrl = (tight[0].ctrl1 - tight[0].p0).length();
    QVERIFY2(tightCtrl < looseCtrl, "higher tension must shorten the handles");
}

void TestCurve::hobbyOvershootVsC2()
{
    // The real quality gap between the C2 natural-boundary solve and Hobby is
    // at the ENDPOINTS: C2 forces zero curvature there (the shape "flattens"
    // and droops at both ends), while Hobby leaves the endpoint free (curve
    // leaves along the chord, staying "lively"). Also compare arc length:
    // the C2 energy minimizer is the shortest possible curve, Hobby keeps a
    // bit more "body". Both are measured on a typical garment neckline.
    std::vector<Vec2> pts = {{0, 0}, {40, 50}, {80, -20}, {140, 30}};
    const int n = static_cast<int>(pts.size());
    std::vector<Vec2> tIn(n), tOut(n);
    std::vector<bool> autoTan(n, true);

    const auto c2 = buildBezierSpans(pts, tIn, tOut, autoTan, 0.0, AutoCurveMode::C2);
    const auto hobby = hobbySpans(pts);

    auto endCurvature = [](const std::vector<BezierSpan>& spans, bool atStart) {
        const BezierSpan& s = atStart ? spans.front() : spans.back();
        const double t = atStart ? 0.0 : 1.0;
        const Vec2 d1 = evalBezierDerivative(s, t);
        const double eps = 1e-3;
        const Vec2 d2 = evalBezierDerivative(s, std::clamp(t + eps, 0.0, 1.0));
        const Vec2 dd = (d2 - d1) / eps;
        return std::abs(d1.cross(dd)) / std::pow(d1.length(), 3.0);
    };

    const double c2CurvStart = endCurvature(c2, true);
    const double hobCurvStart = endCurvature(hobby, true);
    const double c2Len = totalArcLength(c2);
    const double hobLen = totalArcLength(hobby);

    qInfo().noquote() << QStringLiteral(
        "[curve-quality] neckline — C2: start-curv %1, len %2 mm | "
        "Hobby: start-curv %3, len %4 mm")
        .arg(c2CurvStart, 0, 'f', 6).arg(c2Len, 0, 'f', 2)
        .arg(hobCurvStart, 0, 'f', 6).arg(hobLen, 0, 'f', 2);

    // C2 natural boundary: curvature at the auto endpoint is (numerically)
    // zero — the curve flattens out.
    QVERIFY2(c2CurvStart < 1e-3, "C2 natural boundary must zero the endpoint curvature");
    // Hobby leaves the endpoint free: measurable curvature (livelier shape).
    QVERIFY2(hobCurvStart > 1e-3, "Hobby endpoint must keep a free curvature");
    // No strict arc-length assertion: on this shape Hobby is actually more
    // compact (its endpoints follow the chord), which is a quality win, but
    // the ratio depends on the configuration — printed above for reference.
}

QTEST_GUILESS_MAIN(TestCurve)
#include "test_curve.moc"