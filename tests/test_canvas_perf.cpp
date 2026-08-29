/// @file test_canvas_perf.cpp
/// Headless rendering + interaction benchmark for curve-heavy documents.
///
/// Measures the REAL per-frame cost of a curve-anchor drag — resolve +
/// cache sync + snap queries + full-viewport repaint — under SOFTWARE
/// rendering (QPainter raster), the worst case on machines without usable
/// hardware OpenGL (VMs, RDP, basic GPU drivers). Prints median µs/frame
/// for single-curve and multi-curve documents, separating COMPUTE cost from
/// REPAINT cost so a bottleneck can be attributed to one side.
///
/// Run: test_canvas_perf

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QPainter>
#include <QPixmap>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <chrono>
#include <algorithm>

#include "TestHelpers.h"
#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "tools/SnapEngine.h"
#include "geometry/Vec2.h"
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

double medianUs(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct CurveSetup {
    QUuid blockId;
    QUuid segId;
};

/// One block: Free start + N CurveAnchor pass-points + Polar end (Bezier).
CurveSetup makeCurveBlock(ParamDocument& doc, int anchors, Vec2 origin)
{
    Block block;
    block.transform.origin = origin;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid p1Id = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1Id;
    p2.distance = 200.0;
    p2.angle = 0.0;
    const QUuid p2Id = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    seg.type = SegmentType::Bezier;

    std::vector<QUuid> passIds;
    for (int i = 0; i < anchors; ++i) {
        ParamPoint pp;
        pp.constraint = PointConstraint::CurveAnchor;
        pp.hostSegmentId = seg.id;
        pp.interpPercent = (i + 1.0) / (anchors + 1.0);
        pp.interpOffsetDist = 30.0 * ((i % 2) ? -1.0 : 1.0);
        pp.autoTangent = true;
        const QUuid id = pp.id;
        block.addPoint(std::move(pp));
        passIds.push_back(id);
    }
    seg.passPointIds = std::move(passIds);
    const QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    const QUuid bId = block.id;
    doc.addBlock(std::move(block));
    return {bId, segId};
}

/// A QOpenGLWidget that records every paintGL's wall time — this captures the
/// REAL windowed render path (scene render into the GL context; swapBuffers
/// happens right after inside QOpenGLWidget), the cost that offscreen
/// view.render() cannot see.
class TimedGLWidget : public QOpenGLWidget
{
public:
    std::vector<double> paintUs;
protected:
    void paintGL() override
    {
        const auto t0 = std::chrono::steady_clock::now();
        QOpenGLWidget::paintGL();
        const auto t1 = std::chrono::steady_clock::now();
        paintUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
};

} // namespace

class TestCanvasPerf : public QObject
{
    Q_OBJECT

private slots:
    void singleCurveFrame();
    void multiCurveFrame();
    void idleSnapQueries();
    void labeledSceneRepaint();   // realistic doc with name/length labels
    void glViewportFrame();       // real windowed OpenGL swap path (env-critical)
    void curveScaling();          // full interaction frame cost vs curve count

private:
    /// One simulated drag frame: move an anchor, narrow resolve scope, resolve
    /// (emits resolved -> syncBlockPositions -> cache rebuild), then run the
    /// idle snap queries and repaint the whole viewport (software raster).
    /// Returns {computeUs, renderUs} medians.
    std::pair<double, double> dragFrameCost(ParamDocument& doc, CanvasScene& scene,
                                            QGraphicsView& view, int iters,
                                            const QUuid& anchorPointId,
                                            cad::tools::SnapEngine& snap,
                                            const Vec2& cursor);
};

std::pair<double, double> TestCanvasPerf::dragFrameCost(
    ParamDocument& doc, CanvasScene& scene, QGraphicsView& view, int iters,
    const QUuid& anchorPointId, cad::tools::SnapEngine& snap, const Vec2& cursor)
{
    Q_UNUSED(scene);
    Block* blk = doc.findBlock(doc.blocks().front().id);
    ParamPoint* anchor = blk->findPoint(anchorPointId);

    std::vector<double> computeUs, renderUs;
    computeUs.reserve(iters);
    renderUs.reserve(iters);
    QPixmap pm(view.viewport()->size());

    for (int i = 0; i < iters; ++i) {
        // Wiggle the dragged anchor (like a real drag).
        anchor->interpOffsetDist = 30.0 + 0.3 * (i % 7);
        doc.invalidateLayer(layerIdAt(doc, 1));

        auto t0 = std::chrono::steady_clock::now();
        doc.resolveAll();                              // solve + cache rebuild
        (void)snap.findSegmentSnap(cursor, &doc, 1.0, 8.0);  // idle preview path
        (void)snap.findCurvePointSnap(cursor, &doc, 1.0);
        auto t1 = std::chrono::steady_clock::now();
        computeUs.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());

        QPainter p(&pm);
        view.render(&p);  // full-viewport software repaint
        p.end();
        auto t2 = std::chrono::steady_clock::now();
        renderUs.push_back(
            std::chrono::duration<double, std::micro>(t2 - t1).count());
    }
    return {medianUs(computeUs), medianUs(renderUs)};
}

void TestCanvasPerf::singleCurveFrame()
{
    constexpr int kAnchors = 5;

    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);
    CanvasScene scene(&doc);
    const auto cs = makeCurveBlock(doc, kAnchors, Vec2::zero());
    doc.resolveAll();

    // CanvasScene::addBlockItem is driven by blockAdded — connect already done
    // in the scene ctor, but the block was added after: emit manually via
    // resolveAll? blockAdded fires in addBlock, and the scene was constructed
    // BEFORE the block — the signal connection exists, so the item is live.
    QVERIFY(scene.findBlockItem(cs.blockId) != nullptr);

    QGraphicsView view(&scene);
    view.resize(1600, 1000);
    scene.setSceneRect(-800, -800, 3000, 2000);

    cad::tools::SnapEngine snap;
    const Block* blk = doc.findBlock(cs.blockId);
    const Segment* seg = blk->findSegment(cs.segId);
    const QUuid anchorId = seg->passPointIds[kAnchors / 2];

    const auto [computeUs, renderUs] =
        dragFrameCost(doc, scene, view, 120, anchorId, snap, Vec2(500.0, 200.0));

    qInfo().noquote() << QStringLiteral(
        "\n[canvas-perf] single curve (%1 anchors, software render 1600x1000)\n"
        "  per drag frame  : compute %2 us + repaint %3 us = %4 us\n"
        "  -> fps budget   : %5 fps")
        .arg(kAnchors)
        .arg(computeUs, 0, 'f', 1)
        .arg(renderUs, 0, 'f', 1)
        .arg(computeUs + renderUs, 0, 'f', 1)
        .arg(1e6 / (computeUs + renderUs), 0, 'f', 0);

    // Loose sanity guard: a single curve must not consume the whole frame
    // budget even on software rendering (16.7 ms @ 60fps).
    QVERIFY2(computeUs + renderUs < 8000.0,
             "single-curve frame exceeds 8 ms — interaction will feel janky");
}

void TestCanvasPerf::multiCurveFrame()
{
    constexpr int kCurves = 10;
    constexpr int kAnchors = 5;

    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);
    CanvasScene scene(&doc);
    std::vector<CurveSetup> setups;
    for (int i = 0; i < kCurves; ++i)
        setups.push_back(makeCurveBlock(doc, kAnchors, Vec2(0.0, 220.0 * i)));
    doc.resolveAll();
    QVERIFY(scene.findBlockItem(setups.front().blockId) != nullptr);

    QGraphicsView view(&scene);
    view.resize(1600, 1000);
    scene.setSceneRect(-800, -800, 3000, 3000);

    cad::tools::SnapEngine snap;
    const Block* blk = doc.findBlock(setups.front().blockId);
    const Segment* seg = blk->findSegment(setups.front().segId);
    const QUuid anchorId = seg->passPointIds[kAnchors / 2];

    const auto [computeUs, renderUs] =
        dragFrameCost(doc, scene, view, 120, anchorId, snap, Vec2(500.0, 200.0));

    qInfo().noquote() << QStringLiteral(
        "\n[canvas-perf] %1 curves (%2 anchors each, software render 1600x1000)\n"
        "  per drag frame  : compute %3 us + repaint %4 us = %5 us\n"
        "  -> fps budget   : %6 fps")
        .arg(kCurves).arg(kAnchors)
        .arg(computeUs, 0, 'f', 1)
        .arg(renderUs, 0, 'f', 1)
        .arg(computeUs + renderUs, 0, 'f', 1)
        .arg(1e6 / (computeUs + renderUs), 0, 'f', 0);

    QVERIFY2(computeUs + renderUs < 16000.0,
             "multi-curve frame exceeds 16 ms — the canvas will feel stuck");
}

void TestCanvasPerf::idleSnapQueries()
{
    constexpr int kCurves = 10;
    constexpr int kAnchors = 5;

    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);
    for (int i = 0; i < kCurves; ++i)
        makeCurveBlock(doc, kAnchors, Vec2(0.0, 220.0 * i));
    doc.resolveAll();

    cad::tools::SnapEngine snap;
    const Vec2 cursor(500.0, 200.0);  // away from all curves (cull path)
    const Vec2 nearCursor(50.0, 100.0);  // near the first curve (project path)

    std::vector<double> farUs, nearUs;
    constexpr int kIters = 200;
    for (int i = 0; i < kIters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        (void)snap.findSegmentSnap(cursor, &doc, 1.0, 8.0);
        (void)snap.findCurvePointSnap(cursor, &doc, 1.0);
        auto t1 = std::chrono::steady_clock::now();
        farUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

        auto t2 = std::chrono::steady_clock::now();
        (void)snap.findSegmentSnap(nearCursor, &doc, 1.0, 8.0);
        (void)snap.findCurvePointSnap(nearCursor, &doc, 1.0);
        auto t3 = std::chrono::steady_clock::now();
        nearUs.push_back(std::chrono::duration<double, std::micro>(t3 - t2).count());
    }

    qInfo().noquote() << QStringLiteral(
        "\n[canvas-perf] idle mouse-move snap queries (%1 curves)\n"
        "  cursor far from curves (bbox cull): %2 us\n"
        "  cursor near a curve (projection)  : %3 us")
        .arg(kCurves).arg(medianUs(farUs), 0, 'f', 1).arg(medianUs(nearUs), 0, 'f', 1);

    QVERIFY2(medianUs(farUs) < 500.0,
             "idle snap queries must stay cheap even with many curves");
}

void TestCanvasPerf::labeledSceneRepaint()
{
    // Realistic document: 20 blocks (10 straight lines + 10 curves), every
    // segment shows its length label, every point shows its name label — the
    // drawText-heavy case. Measures ONLY the full-viewport repaint cost.
    constexpr int kBlocks = 20;
    constexpr int kAnchors = 5;

    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);
    std::vector<CurveSetup> setups;
    std::vector<QUuid> blockIds;
    for (int i = 0; i < kBlocks; ++i) {
        if (i % 2 == 0) {
            setups.push_back(makeCurveBlock(doc, kAnchors, Vec2(0.0, 260.0 * i)));
            blockIds.push_back(setups.back().blockId);
        } else {
            // Straight line block with a length label.
            Block block;
            block.transform.origin = Vec2(0.0, 260.0 * i);
            ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
            const QUuid p1Id = p1.id;
            ParamPoint p2; p2.constraint = PointConstraint::Polar; p2.refPointId = p1Id;
            p2.distance = 180.0; p2.angle = 10.0;
            const QUuid p2Id = p2.id;
            p1.name = QStringLiteral("A%1").arg(i);
            p2.name = QStringLiteral("B%1").arg(i);
            p1.showName = true;
            p2.showName = true;
            block.addPoint(std::move(p1));
            block.addPoint(std::move(p2));
            Segment seg; seg.startPointId = p1Id; seg.endPointId = p2Id;
            seg.showLength = true;
            seg.showName = true;
            seg.name = QStringLiteral("L%1").arg(i);
            const QUuid bId = block.id;
            block.addSegment(std::move(seg));
            doc.addBlock(std::move(block));
            blockIds.push_back(bId);
        }
    }
    doc.resolveAll();

    CanvasScene scene(&doc);
    QGraphicsView view(&scene);
    view.resize(1600, 1000);
    scene.setSceneRect(-800, -800, 3000, 6000);

    // Force a full repaint once, then time subsequent identical repaints
    // (the cost the animation timer pays every 16 ms while a hover runs).
    QPixmap pm(view.viewport()->size());
    {
        QPainter p(&pm);
        view.render(&p);
        p.end();
    }

    std::vector<double> repaintUs;
    constexpr int kIters = 60;
    for (int i = 0; i < kIters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        QPainter p(&pm);
        view.render(&p);
        p.end();
        auto t1 = std::chrono::steady_clock::now();
        repaintUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    qInfo().noquote() << QStringLiteral(
        "\n[canvas-perf] labeled scene repaint (%1 blocks, names+lengths, 1600x1000)\n"
        "  full repaint median : %2 us\n"
        "  -> animation timer @16ms keeps ~%3 fps of headroom")
        .arg(kBlocks).arg(medianUs(repaintUs), 0, 'f', 1)
        .arg(std::max(1.0, 16000.0 / medianUs(repaintUs)), 0, 'f', 0);

    QVERIFY2(medianUs(repaintUs) < 8000.0,
             "labeled full repaint exceeds 8 ms — hover animation would stutter");
}

void TestCanvasPerf::glViewportFrame()
{
    // The real app renders through QOpenGLWidget + FullViewportUpdate. On
    // machines without a usable GL driver (VMs, RDP, basic GPUs) every
    // makeCurrent/swapBuffers can cost tens of milliseconds — a per-frame tax
    // that no amount of scene-side optimization can remove. This test
    // measures that tax with a windowed QOpenGLWidget.
    const QString platform = qEnvironmentVariable("QT_QPA_PLATFORM");
    if (platform == QLatin1String("offscreen")) {
        QSKIP("windowed platform required (OpenGL swap path)");
    }

    constexpr int kCurves = 10;
    constexpr int kAnchors = 5;
    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);
    CanvasScene scene(&doc);
    for (int i = 0; i < kCurves; ++i)
        makeCurveBlock(doc, kAnchors, Vec2(0.0, 220.0 * i));
    doc.resolveAll();

    QGraphicsView view(&scene);
    auto* gl = new TimedGLWidget();
    QSurfaceFormat fmt;
    fmt.setSamples(4);
    gl->setFormat(fmt);
    view.setViewport(gl);
    view.setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    view.resize(1600, 1000);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        QSKIP("no window exposed — headless session");
    cad::test::grabStable(view);  // GL context + 首帧渲染完成(两帧一致), 替代 300ms 墙钟

    qInfo().noquote() << QStringLiteral(
        "[canvas-perf] OpenGL context valid: %1")
        .arg(gl->isValid() ? QStringLiteral("yes") : QStringLiteral("NO (software fallback?)"));

    // Measure the GL pipeline directly: grabFramebuffer forces a full render
    // (makeCurrent + scene draw + readback) through the real GL backend, which
    // is what every viewport repaint costs on the windowed path. This does not
    // depend on window paint events, so it works even in a sandboxed session.
    std::vector<double> grabUs;
    for (int i = 0; i < 40; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        QImage img = gl->grabFramebuffer();
        const auto t1 = std::chrono::steady_clock::now();
        grabUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        if (img.isNull()) {
            QVERIFY2(false, "grabFramebuffer returned null — GL readback failed");
            return;
        }
    }
    std::vector<double> copy = grabUs;
    std::sort(copy.begin(), copy.end());
    const double med = copy[copy.size() / 2];

    qInfo().noquote() << QStringLiteral(
        "\n[canvas-perf] OpenGL framebuffer grab (%1 curves, 1600x1000, %2 samples)\n"
        "  median grab : %3 us   (every viewport repaint costs at least this)\n"
        "  worst grab  : %4 us\n"
        "  note: CanvasView auto-detects software GL and falls back to raster,\n"
        "        so a slow GL path here is environmental, not a code defect")
        .arg(kCurves).arg(grabUs.size())
        .arg(med, 0, 'f', 1)
        .arg(copy.back(), 0, 'f', 1);

    // Only fail on a completely broken GL path (100 ms+); slow-but-working
    // software GL is expected on VMs and is handled by the auto-fallback.
    QVERIFY2(med < 100000.0,
             "OpenGL path is catastrophically slow (>100 ms) — check the driver");
}

void TestCanvasPerf::curveScaling()
{
    // "Straight lines are smooth, but the more curves the jankier" — measure
    // the FULL per-frame interaction cost as a function of curve count:
    //   drag frame  = anchor move + resolve + cache rebuild + snap + repaint
    //   hover frame = idle snap queries + Qt scene hit-test (items()+shape)
    //                 + repaint  (what every mouse move pays in the curve tool)
    constexpr int kAnchors = 5;
    constexpr int kIters = 100;

    for (int count : {1, 10, 30}) {
        ParamDocument doc;
        doc.setParameter(QStringLiteral("b"), 84.0);
        CanvasScene scene(&doc);
        std::vector<CurveSetup> setups;
        for (int i = 0; i < count; ++i)
            setups.push_back(makeCurveBlock(doc, kAnchors, Vec2(0.0, 220.0 * i)));
        doc.resolveAll();
        if (scene.findBlockItem(setups.front().blockId) == nullptr) {
            QVERIFY2(false, "block item missing");
            return;
        }

        QGraphicsView view(&scene);
        view.resize(1600, 1000);
        scene.setSceneRect(-800, -800, 3000, 220.0 * count + 1000);

        cad::tools::SnapEngine snap;
        const Block* blk = doc.findBlock(setups.front().blockId);
        const Segment* seg = blk->findSegment(setups.front().segId);
        const QUuid anchorId = seg->passPointIds[kAnchors / 2];

        // Cursor ON the first curve: the near-projection snap path + hover hit.
        const Vec2 cursor(100.0, 110.0);

        // ── Drag frame (anchor drag: resolve + sync + cache rebuild + snap + repaint) ──
        std::vector<double> dragUs;
        QPixmap pm(view.viewport()->size());
        Block* dragBlk = doc.findBlock(setups.front().blockId);
        ParamPoint* anchor = dragBlk->findPoint(anchorId);
        for (int i = 0; i < kIters; ++i) {
            anchor->interpOffsetDist = 30.0 + 0.3 * (i % 7);
            doc.invalidateLayer(layerIdAt(doc, 1));
            auto t0 = std::chrono::steady_clock::now();
            doc.resolveAll();
            (void)snap.findSnap(cursor, &doc, 1.0, -1.0, anchorId);
            auto t1 = std::chrono::steady_clock::now();
            QPainter p(&pm);
            view.render(&p);
            p.end();
            auto t2 = std::chrono::steady_clock::now();
            dragUs.push_back(
                std::chrono::duration<double, std::micro>(t2 - t0).count());
        }

        // ── Hover frame (idle mouse move: snap + Qt hit-test + repaint) ──
        std::vector<double> hoverUs;
        const QPointF sceneCursor = cad::geo::Coord::toScene(cursor.x, cursor.y);
        for (int i = 0; i < kIters; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            (void)snap.findSegmentSnap(cursor, &doc, 1.0, 8.0);
            (void)snap.findCurvePointSnap(cursor, &doc, 1.0);
            const auto hits = scene.items(sceneCursor);  // Qt hit-test (shape())
            Q_UNUSED(hits);
            auto t1 = std::chrono::steady_clock::now();
            QPainter p(&pm);
            view.render(&p);
            p.end();
            auto t2 = std::chrono::steady_clock::now();
            hoverUs.push_back(
                std::chrono::duration<double, std::micro>(t2 - t0).count());
        }

        qInfo().noquote() << QStringLiteral(
            "[canvas-perf] %1 curves, 5 anchors: drag frame %2 us, hover frame %3 us")
            .arg(count)
            .arg(medianUs(dragUs), 0, 'f', 1)
            .arg(medianUs(hoverUs), 0, 'f', 1);

        // Scaling sanity: 30 curves must stay within a 16.7 ms frame budget.
        if (count == 30) {
            QVERIFY2(medianUs(dragUs) < 16000.0 && medianUs(hoverUs) < 16000.0,
                     "30-curve interaction frame exceeds 16 ms — scaling bug");
        }
    }
}

QTEST_MAIN(TestCanvasPerf)
#include "test_canvas_perf.moc"
