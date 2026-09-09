// 圆段打断（CIRCLE_TOOL_DESIGN.md D7）：整圆/圆弧在参数 t 处一分为二，两段都
// 保持 fitKind = Circle、共用圆心、各自持半径（初始都 = r）、包角互补。
// 闭环判据（§11 测试 6c）：两段各采样仍落在原圆上（偏差 < 0.001·r），包角和 == 360°。
#include "test_break_helpers.h"

#include "tools/CircleFactory.h"

namespace {

struct CircleIds {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
    std::vector<QUuid> anchorIds;
};

CircleIds makeCircle(ParamDocument& doc, double radiusMm, double startAngleDeg = 0.0,
                     const Vec2& center = Vec2::zero())
{
    CircleIds ids;
    cad::tools::CircleFactory factory(&doc, doc.undoStack());
    ids.blockId = factory.createCircle(center, radiusMm, startAngleDeg);
    if (Block* b = doc.findBlock(ids.blockId); b && !b->segments.empty()) {
        ids.segId = b->segments.front().id;
        ids.startId = b->segments.front().startPointId;
        ids.endId = b->segments.front().endPointId;
        ids.anchorIds = b->segments.front().passPointIds;
        if (const ParamPoint* sp = b->findPoint(ids.startId))
            ids.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return ids;
}

/// Flatten a segment's fitted spans into a local-space polyline.
std::vector<Vec2> localPolyline(const Block& b, const QUuid& segId)
{
    std::vector<Vec2> out;
    const auto* e = b.curveSpanEntry(segId);
    if (!e) return out;
    for (const auto& s : e->spans) {
        const auto flat = cad::geo::flattenBezierSpans({s}, 0.05);
        if (out.empty()) out.insert(out.end(), flat.begin(), flat.end());
        else out.insert(out.end(), flat.begin() + 1, flat.end());
    }
    return out;
}

/// Worst |‖p − center‖ − r| over the segment's flattened spans (local coords).
double maxRadiusError(const Block& b, const Segment& s, double radiusMm)
{
    const auto* ctr = b.circleCenterPoint(s);
    if (!ctr || !ctr->resolved) return 1e18;
    double worst = 0.0;
    for (const Vec2& p : localPolyline(b, s.id))
        worst = std::max(worst, std::abs(p.distanceTo(ctr->resolvedPos) - radiusMm));
    return worst;
}

/// Collect (sweepDeg, radiusMm, maxRadiusError) for every circle segment.
struct CircleHalf {
    double sweepDeg = 0.0;
    double radiusMm = 0.0;
    double radialErr = 0.0;
};

std::vector<CircleHalf> collectCircleHalves(const ParamDocument& doc, double radiusMm)
{
    std::vector<CircleHalf> halves;
    for (const auto& b : doc.blocks()) {
        for (const auto& s : b.segments) {
            if (s.fitKind != FitKind::Circle) continue;
            halves.push_back({b.circleSweepDeg(s), b.circleRadiusMm(s),
                              maxRadiusError(b, s, radiusMm)});
        }
    }
    return halves;
}

} // namespace

class TestBreakCircle : public QObject
{
    Q_OBJECT

private slots:
    void circleBreakAtAnchorProducesTwoCircleHalves();
    void circleBreakAtInterpolatedSplitsByAngle();
    void circleBreakOnRotatedBaseAngleKeepsRadius();
    void partialArcBreakComplementsSweep();
    void circleBreakUndoRestoresSingleCircle();
    void circleBreakAtSeamIsRejected();
};

// ---------------------------------------------------------------------------
// 整圆在 50% 锚点（180°）处打断：两段都是圆、各 180°、半径不变、采样仍在圆上。
// ---------------------------------------------------------------------------
void TestBreakCircle::circleBreakAtAnchorProducesTwoCircleHalves()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 50.0);
    QVERIFY(!c.blockId.isNull());
    QCOMPARE(c.anchorIds.size(), std::size_t(3));

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, c.anchorIds[1]);
    QVERIFY(cmd.isValid());
    cmd.redo();

    QCOMPARE(doc.blocks().size(), std::size_t(2));
    const auto halves = collectCircleHalves(doc, 50.0);
    QCOMPARE(halves.size(), std::size_t(2));

    double sweepSum = 0.0;
    for (const auto& h : halves) {
        sweepSum += h.sweepDeg;
        QVERIFY2(std::abs(h.sweepDeg - 180.0) < 1e-6,
                 qPrintable(QStringLiteral("half sweep %1 (expected 180)").arg(h.sweepDeg, 0, 'f', 6)));
        QVERIFY2(std::abs(h.radiusMm - 50.0) < 1e-6,
                 qPrintable(QStringLiteral("half radius %1 (expected 50)").arg(h.radiusMm, 0, 'f', 6)));
        // 闭环判据：0.001 · r
        QVERIFY2(h.radialErr < 0.05,
                 qPrintable(QStringLiteral("radial error %1 mm (limit 0.05)").arg(h.radialErr, 0, 'f', 6)));
    }
    QVERIFY2(std::abs(sweepSum - 360.0) < 1e-6,
             qPrintable(QStringLiteral("sweep sum %1 (expected 360)").arg(sweepSum, 0, 'f', 6)));
}

// ---------------------------------------------------------------------------
// 整圆在 Interpolated 点（弧长 30%）打断：按角度分割 ⇒ 前 108° / 后 252°。
// ---------------------------------------------------------------------------
void TestBreakCircle::circleBreakAtInterpolatedSplitsByAngle()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 40.0);
    const QUuid auxId = addAuxPoint(doc, c.blockId, c.segId, 0.3);
    QVERIFY(!auxId.isNull());

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto halves = collectCircleHalves(doc, 40.0);
    QCOMPARE(halves.size(), std::size_t(2));
    std::vector<double> sweeps;
    for (const auto& h : halves) {
        sweeps.push_back(h.sweepDeg);
        QVERIFY2(h.radialErr < 0.04,  // 0.001 · 40
                 qPrintable(QStringLiteral("radial error %1 mm").arg(h.radialErr, 0, 'f', 6)));
    }
    std::sort(sweeps.begin(), sweeps.end());
    QVERIFY2(std::abs(sweeps[0] - 108.0) < 0.2,
             qPrintable(QStringLiteral("front sweep %1 (expected 108)").arg(sweeps[0], 0, 'f', 4)));
    QVERIFY2(std::abs(sweeps[1] - 252.0) < 0.2,
             qPrintable(QStringLiteral("back sweep %1 (expected 252)").arg(sweeps[1], 0, 'f', 4)));
}

// ---------------------------------------------------------------------------
// 基准角非 0（a0 = 30°）的整圆：断点角度经 atan2 回绕后两段仍各 180°。
// ---------------------------------------------------------------------------
void TestBreakCircle::circleBreakOnRotatedBaseAngleKeepsRadius()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 20.0, 30.0);
    QCOMPARE(c.anchorIds.size(), std::size_t(3));

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, c.anchorIds[1]);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto halves = collectCircleHalves(doc, 20.0);
    QCOMPARE(halves.size(), std::size_t(2));
    for (const auto& h : halves) {
        QVERIFY2(std::abs(h.sweepDeg - 180.0) < 1e-6,
                 qPrintable(QStringLiteral("rotated half sweep %1").arg(h.sweepDeg, 0, 'f', 6)));
        QVERIFY2(std::abs(h.radiusMm - 20.0) < 1e-6,
                 qPrintable(QStringLiteral("rotated half radius %1").arg(h.radiusMm, 0, 'f', 6)));
        QVERIFY2(h.radialErr < 0.02,
                 qPrintable(QStringLiteral("rotated radial error %1").arg(h.radialErr, 0, 'f', 6)));
    }
}

// ---------------------------------------------------------------------------
// 圆弧（120°）打断：两段包角互补（60° + 60°），不补成整圆。
// ---------------------------------------------------------------------------
void TestBreakCircle::partialArcBreakComplementsSweep()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 30.0);
    auto* b = doc.findBlock(c.blockId);
    QVERIFY(b);
    auto* ep = b->findPoint(c.endId);
    QVERIFY(ep);
    ep->angle = 120.0;  // 整圆 → 120° 圆弧
    doc.resolveAll();

    const auto* seg = b->findSegment(c.segId);
    QVERIFY(seg);
    QCOMPARE(seg->passPointIds.size(), std::size_t(3));
    const QUuid midAnchor = seg->passPointIds[1];  // 50% → 60°

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, midAnchor);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto halves = collectCircleHalves(doc, 30.0);
    QCOMPARE(halves.size(), std::size_t(2));
    double sweepSum = 0.0;
    for (const auto& h : halves) {
        sweepSum += h.sweepDeg;
        QVERIFY2(std::abs(h.sweepDeg - 60.0) < 1e-6,
                 qPrintable(QStringLiteral("arc half sweep %1 (expected 60)").arg(h.sweepDeg, 0, 'f', 6)));
        QVERIFY2(h.radialErr < 0.03,
                 qPrintable(QStringLiteral("arc radial error %1").arg(h.radialErr, 0, 'f', 6)));
    }
    QVERIFY2(std::abs(sweepSum - 120.0) < 1e-6,
             qPrintable(QStringLiteral("arc sweep sum %1 (expected 120)").arg(sweepSum, 0, 'f', 6)));
}

// ---------------------------------------------------------------------------
// undo：恢复为单个整圆，锚点回到 25/50/75%，端点角度不归一化。
// ---------------------------------------------------------------------------
void TestBreakCircle::circleBreakUndoRestoresSingleCircle()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 50.0);

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, c.anchorIds[0]);
    QVERIFY(cmd.isValid());
    cmd.redo();
    QCOMPARE(doc.blocks().size(), std::size_t(2));

    cmd.undo();
    QCOMPARE(doc.blocks().size(), std::size_t(1));
    const auto* b = doc.findBlock(c.blockId);
    QVERIFY(b);
    const auto* seg = b->findSegment(c.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::Circle);
    QVERIFY(seg->isCurve());
    QVERIFY2(std::abs(b->circleSweepDeg(*seg) - 360.0) < 1e-6, "sweep not restored");
    QVERIFY2(std::abs(b->circleRadiusMm(*seg) - 50.0) < 1e-6, "radius not restored");
    QCOMPARE(seg->passPointIds.size(), std::size_t(3));
    for (std::size_t i = 0; i < seg->passPointIds.size(); ++i) {
        const auto* a = b->findPoint(seg->passPointIds[i]);
        QVERIFY(a);
        QCOMPARE(a->constraint, PointConstraint::CurveAnchor);
        QVERIFY2(std::abs(a->interpPercent - 0.25 * static_cast<double>(i + 1)) < 1e-12,
                 "anchor percent not restored");
    }
    const auto* sp = b->findPoint(c.startId);
    const auto* ep = b->findPoint(c.endId);
    QVERIFY(sp && ep);
    QVERIFY2(std::abs(ep->angle - (sp->angle + 360.0)) < 1e-9, "full-circle sweep must not normalize");
}

// ---------------------------------------------------------------------------
// 断点落在端点（弧长 0%）时流水线拒绝打断：圆段退化守卫（segLenMm == 0）之外的
// 角度守卫。canBreak 只看约束/宿主，实际拒绝发生在 gatherBreakGeometry。
// ---------------------------------------------------------------------------
void TestBreakCircle::circleBreakAtSeamIsRejected()
{
    ParamDocument doc;
    const CircleIds c = makeCircle(doc, 25.0);
    const QUuid auxId = addAuxPoint(doc, c.blockId, c.segId, 0.0);
    QVERIFY(!auxId.isNull());

    cad::cmd::BreakSegmentCommand cmd(&doc, c.blockId, c.segId, auxId);
    cmd.redo();  // gather 拒绝 ⇒ 空操作

    QCOMPARE(doc.blocks().size(), std::size_t(1));
    const auto* b = doc.findBlock(c.blockId);
    QVERIFY(b);
    const auto* seg = b->findSegment(c.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::Circle);
    QVERIFY2(std::abs(b->circleSweepDeg(*seg) - 360.0) < 1e-6,
             "seam break must leave the full circle untouched");
}

QTEST_MAIN(TestBreakCircle)
#include "test_break_circle.moc"
