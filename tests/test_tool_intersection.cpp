/// @file test_tool_intersection.cpp
/// Drives the ToolIntersection interaction flow end-to-end:
///   select target segment → select ray origin → hover an aim point (指向点)
///   → click commits an Intersection point whose ray points at the aim point.
/// Guards regressions in the aim-point flow (建立不了交点).

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolIntersection.h"
#include "tools/ToolManager.h"
#include "tools/SnapEngine.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Vec2.h"

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

/// One block on layer 0 with:
///   segment L: (0,0) → (100,0)          (target)
///   origin A:  (20,-40)                 (ray origin, below L)
///   aim B:     (60,10)                  (between A and L — ray A→B hits L)
///   miss C:    (150,10)                 (right of L — ray A→C misses L)
struct Setup {
    ParamDocument doc;
    CanvasScene scene{&doc};
    CanvasView view{&scene};
    QUuid blockId;
    QUuid segId;
    QUuid originId;
    QUuid aimId;
    QUuid missId;

    Setup()
    {
        doc.setActiveLayer(layerIdAt(doc, 0));
        Block block;
        block.layer = layerIdAt(doc, 0);

        ParamPoint p1;
        p1.constraint = PointConstraint::Free;
        p1.freePos = Vec2(0.0, 0.0);
        const QUuid sId = p1.id;
        ParamPoint p2;
        p2.constraint = PointConstraint::Free;
        p2.freePos = Vec2(100.0, 0.0);
        const QUuid eId = p2.id;
        block.addPoint(std::move(p1));
        block.addPoint(std::move(p2));

        Segment seg;
        seg.startPointId = sId;
        seg.endPointId = eId;
        segId = seg.id;
        block.addSegment(std::move(seg));

        ParamPoint a;
        a.constraint = PointConstraint::Free;
        a.freePos = Vec2(20.0, -40.0);
        originId = a.id;
        block.addPoint(std::move(a));

        ParamPoint b;
        b.constraint = PointConstraint::Free;
        b.freePos = Vec2(60.0, 10.0);
        b.name = QStringLiteral("B");
        aimId = b.id;
        block.addPoint(std::move(b));

        ParamPoint c;
        c.constraint = PointConstraint::Free;
        c.freePos = Vec2(150.0, 10.0);
        c.name = QStringLiteral("C");
        missId = c.id;
        block.addPoint(std::move(c));

        blockId = block.id;
        doc.addBlock(std::move(block));
        doc.resolveAll();
    }
};

/// Count Intersection points currently in the document.
int intersectionCount(const ParamDocument& doc)
{
    int n = 0;
    for (const auto& b : doc.blocks())
        for (const auto& pt : b.points)
            if (pt.constraint == PointConstraint::Intersection) ++n;
    return n;
}

} // namespace

class TestToolIntersection : public QObject
{
    Q_OBJECT

private slots:
    /// Full aim-point flow: select L → select A → click B → intersection
    /// created IMMEDIATELY (one-step borrow).
    void aimPointCreatesIntersection();

    /// Regression: plain angle aiming (no borrowed point) still commits.
    void angleModeStillWorks();

    /// Aim point must be stored parametrically (interAimPointId set).
    void aimPointStoredParametrically();

    /// A borrowed ray that MISSES the segment locks (BorrowAim) instead of
    /// silently failing; the direction stays locked, right-click unlocks it.
    void missLocksDirectionUntilCancelled();

    /// The FULL event chain (QTest mouse events through the CanvasView →
    /// ToolManager dispatch) reproduces the real GUI flow.
    void fullViewEventChain();

    /// A slightly-off click (within the 12px snap radius) still borrows the
    /// point that the hover preview showed.
    void offCenterClickStillBorrows();

    /// The aim point lives on the GRAYED AUX layer while a working layer is
    /// active — it must still be borrowable (no attachment is created, so the
    /// aux-active snap restriction is lifted for this tool).
    void auxLayerAimPointWorks();

    /// An INTERPOLATED auxiliary point (smart-pen line-body aux point) can
    /// also be borrowed as the ray aim.
    void auxPointOnSegmentCanBeBorrowed();
};

void TestToolIntersection::aimPointCreatesIntersection()
{
    Setup s;
    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));  // user coords (+Y up)
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    // 1. Select the target segment (hover body, then click).
    move(50.0, 0.0);
    press(50.0, 0.0);

    // 2. Select the ray origin A.
    move(20.0, -40.0);
    press(20.0, -40.0);

    // 3. Click the aim point B → the intersection is created IMMEDIATELY.
    move(60.0, 10.0);
    press(60.0, 10.0);

    // Expect exactly one Intersection point, resolved at ray A→B ∩ L:
    // A=(20,-40), B=(60,10) → dir (40,50); y=0 at t=40/50=0.8 → x=20+40*0.8=52.
    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 52.0) < 1e-6);
    QVERIFY(std::abs(ix->resolvedPos.y - 0.0) < 1e-6);
}

void TestToolIntersection::angleModeStillWorks()
{
    Setup s;
    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);      // select L
    move(20.0, -40.0);
    press(20.0, -40.0);    // select origin A

    // Aim at blank space (no point under cursor) and commit.
    move(30.0, -10.0);
    press(30.0, -10.0);

    // Ray from A toward (30,-10): dir (10,30); y=0 at t=40/30 → x=20+10*4/3≈33.33.
    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 100.0 / 3.0) < 1e-6);
    QVERIFY(ix->interAimPointId.isNull());  // no aim reference in angle mode
}

void TestToolIntersection::aimPointStoredParametrically()
{
    Setup s;
    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);
    move(20.0, -40.0);
    press(20.0, -40.0);
    move(60.0, 10.0);
    press(60.0, 10.0);   // one-step borrow B → intersection created

    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    for (const auto& pt : block->points) {
        if (pt.constraint != PointConstraint::Intersection) continue;
        // The aim reference is stored AND the fallback angle is non-default.
        QCOMPARE(pt.interAimPointId, s.aimId);
        QVERIFY(std::abs(pt.interAngle - 51.3401917) < 1e-3);  // atan2(50,40) in deg
        return;
    }
    QFAIL("no intersection point created");
}

void TestToolIntersection::missLocksDirectionUntilCancelled()
{
    Setup s;
    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto pressRight = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::RightButton);
        e.setButtons(Qt::RightButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);      // select L
    move(20.0, -40.0);
    press(20.0, -40.0);    // select origin A

    // Click the MISS point C (150,10): ray A→C crosses y=0 at x=124, beyond
    // the segment [0,100] — no intersection. The direction LOCKS (BorrowAim)
    // instead of silently failing; nothing is created.
    move(150.0, 10.0);
    press(150.0, 10.0);
    QCOMPARE(intersectionCount(s.doc), 0);

    // The locked direction keeps pointing at C even when the cursor moves
    // far away: committing still misses → still nothing created.
    move(500.0, 300.0);
    press(500.0, 300.0);
    QCOMPARE(intersectionCount(s.doc), 0);

    // Right-click unlocks the borrow (back to AimAngle).
    pressRight(500.0, 300.0);

    // Now clicking B creates the intersection normally.
    move(60.0, 10.0);
    press(60.0, 10.0);
    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 52.0) < 1e-6);
    QCOMPARE(ix->interAimPointId, s.aimId);
}

void TestToolIntersection::fullViewEventChain()
{
    Setup s;
    s.view.resize(900, 600);
    s.view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&s.view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&s.scene);
    tm.setParamDocument(&s.doc);
    tm.switchTool(cad::tools::ToolType::Intersection);
    s.view.setToolManager(&tm);  // real GUI wiring (MainWindow does the same)

    // Map user coords (+Y up) → viewport pixels via the real view transform.
    auto vp = [&](double x, double y) {
        return s.view.mapFromScene(QPointF(x, -y));
    };

    // Real QMouseEvent through the viewport → CanvasView::mousePressEvent →
    // ToolManager dispatch (bypasses QTest's window-activation check only).
    auto sendMove = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        QMouseEvent move(QEvent::MouseMove, vpPos,
                         s.view.viewport()->mapToGlobal(vpPos),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(s.view.viewport(), &move);
        QTest::qWait(20);
    };
    auto sendClick = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = s.view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(s.view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(s.view.viewport(), &release);
        QTest::qWait(20);
    };

    // Select target segment (hover body, then click).
    sendMove(50.0, 0.0);
    sendClick(50.0, 0.0);
    // Select ray origin A.
    sendClick(20.0, -40.0);
    // Click the aim point B → intersection created immediately.
    sendClick(60.0, 10.0);

    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 52.0) < 1e-6);
    QVERIFY(std::abs(ix->resolvedPos.y - 0.0) < 1e-6);
    QCOMPARE(ix->interAimPointId, s.aimId);
}

void TestToolIntersection::offCenterClickStillBorrows()
{
    Setup s;
    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);      // select L
    move(20.0, -40.0);
    press(20.0, -40.0);    // select origin A

    // Hover exactly on B, then click 9px away — beyond the 8px hover radius
    // but within the 12px borrow radius: the click still borrows B and the
    // intersection is created immediately.
    move(60.0, 10.0);
    press(69.0, 10.0);
    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 52.0) < 1e-6);
    QCOMPARE(ix->interAimPointId, s.aimId);
}

void TestToolIntersection::auxLayerAimPointWorks()
{
    // Working layer (1) active; the aim point lives on the aux layer (0),
    // which layerSnappable() normally excludes unless active.
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    CanvasView view(&scene);

    Block wb;
    wb.layer = layerIdAt(doc, 1);
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2(0.0, 0.0);
    const QUuid sId = p1.id;
    ParamPoint p2;
    p2.constraint = PointConstraint::Free;
    p2.freePos = Vec2(100.0, 0.0);
    const QUuid eId = p2.id;
    wb.addPoint(std::move(p1));
    wb.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = sId;
    seg.endPointId = eId;
    const QUuid segId = seg.id;
    wb.addSegment(std::move(seg));
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = Vec2(20.0, -40.0);
    const QUuid originId = a.id;
    wb.addPoint(std::move(a));
    const QUuid wbId = wb.id;
    doc.addBlock(std::move(wb));

    Block ab;
    ab.layer = layerIdAt(doc, 0);  // aux layer
    ParamPoint d;
    d.constraint = PointConstraint::Free;
    d.freePos = Vec2(90.0, 20.0);
    d.name = QStringLiteral("D");
    const QUuid aimId = d.id;
    ab.addPoint(std::move(d));
    doc.addBlock(std::move(ab));

    doc.resolveAll();

    cad::tools::ToolIntersection tool;
    tool.activate(scene, &doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);      // select target L (working layer)
    move(20.0, -40.0);
    press(20.0, -40.0);    // select origin A
    move(90.0, 20.0);
    press(90.0, 20.0);     // click the AUX-layer aim point → one-step create

    // Ray A→D: A=(20,-40), D=(90,20), dir (70,60); y=0 at t=2/3 → x=66.67.
    QCOMPARE(intersectionCount(doc), 1);
    const Block* wbRes = doc.blockById(wbId);
    QVERIFY(wbRes);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : wbRes->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 200.0 / 3.0) < 1e-6);
    QVERIFY(std::abs(ix->resolvedPos.y - 0.0) < 1e-6);
    QCOMPARE(ix->interAimPointId, aimId);  // cross-block + cross-layer aim kept
}

void TestToolIntersection::auxPointOnSegmentCanBeBorrowed()
{
    Setup s;
    // Add an Interpolated auxiliary point at 50% of L → (50,0).
    {
        Block* block = s.doc.blockById(s.blockId);
        QVERIFY(block);
        ParamPoint p;
        p.constraint = PointConstraint::Interpolated;
        p.hostSegmentId = s.segId;
        p.interpPercent = 0.5;
        p.interpConstant = 0.0;
        p.isAuxiliary = true;
        block->addPoint(std::move(p));
    }
    s.doc.resolveAll();

    // Sanity: the aux point resolved and is findable by the snap engine
    // (the same engine the tool uses for borrowing).
    cad::tools::SnapEngine se;
    auto auxSnap = se.findSnap(Vec2(50.0, 0.0), &s.doc, 1.0, 12.0, {},
                               nullptr, /*ignoreLayerFilter=*/true);
    QVERIFY2(auxSnap.has_value(), "aux point not found by snap engine");

    cad::tools::ToolIntersection tool;
    tool.activate(s.scene, &s.doc);

    auto press = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
        e.setScenePos(QPointF(x, y));
        e.setButton(Qt::LeftButton);
        e.setButtons(Qt::LeftButton);
        tool.mousePress(&e);
    };
    auto move = [&](double x, double y) {
        QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
        e.setScenePos(QPointF(x, y));
        tool.mouseMove(&e);
    };

    move(50.0, 0.0);
    press(50.0, 0.0);      // select target L
    move(20.0, -40.0);
    press(20.0, -40.0);    // select origin A
    move(50.0, 0.0);
    press(50.0, 0.0);      // click the AUX point on L → borrow + create

    // Ray A→aux: A=(20,-40) to (50,0); the intersection is the aux point
    // itself at (50,0).
    QCOMPARE(intersectionCount(s.doc), 1);
    const Block* block = s.doc.blockById(s.blockId);
    QVERIFY(block);
    const ParamPoint* ix = nullptr;
    for (const auto& pt : block->points)
        if (pt.constraint == PointConstraint::Intersection) ix = &pt;
    QVERIFY(ix);
    QVERIFY(ix->resolved);
    QVERIFY(std::abs(ix->resolvedPos.x - 50.0) < 1e-6);
    QVERIFY(std::abs(ix->resolvedPos.y - 0.0) < 1e-6);
}

QTEST_MAIN(TestToolIntersection)
#include "test_tool_intersection.moc"
