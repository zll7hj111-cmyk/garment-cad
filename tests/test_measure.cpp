/// @file test_measure.cpp
/// Measure tool regression:
///   - default mode measures the Euclidean two-point distance;
///   - W cycles 距离 → 水平 → 垂直 → 距离, and each committed
///     MeasureVariable carries the right kind and value (|dx| / |dy|);
///   - horizontal/vertical modes REFUSE to commit when the two points
///     coincide on the measured axis (dx≈0 / dy≈0) — no measure is
///     created, the tool stays mid-selection, and a later valid B commits.

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QTimer>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "tools/ToolMeasure.h"
#include "tools/ToolAngleMeasure.h"
#include "ui/MeasureResultDialog.h"
#include "ui/MeasureCard.h"
#include "ui/AngleMeasureCard.h"
#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/LinkedVariable.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "TestHelpers.h"
#include <QSignalSpy>

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

/// Block with two free points at @p a and @p b on working layer @p layerRow.
void makePointBlock(ParamDocument& doc, int layerRow, const Vec2& a, const Vec2& b)
{
    Block block;
    block.layer = layerIdAt(doc, layerRow);
    ParamPoint pa;
    pa.constraint = PointConstraint::Free;
    pa.freePos = a;
    ParamPoint pb;
    pb.constraint = PointConstraint::Free;
    pb.freePos = b;
    block.addPoint(std::move(pa));
    block.addPoint(std::move(pb));
    doc.addBlock(std::move(block));
}

/// Schedule an auto-reject of the modal MeasureResultDialog that
/// commitMeasure() opens — the timer fires inside the dialog's nested event
/// loop during exec(), so the commit finishes without a human.
void armDialogAutoDismiss()
{
    QTimer::singleShot(0, [] {
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget* w : tops) {
            if (auto* dlg = qobject_cast<cad::ui::MeasureResultDialog*>(w)) {
                dlg->reject();
                return;
            }
        }
    });
}

/// Synthesize a left-press at @p p (mm) and deliver it to the tool.
void clickAt(cad::tools::ToolMeasure& tool, const QPointF& p)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(p);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    tool.mousePress(&press);
}

/// Synthesize the W key press (mode cycle).
void pressW(cad::tools::ToolMeasure& tool)
{
    QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    tool.keyPress(&key);
}

void clickAtAngle(cad::tools::ToolAngleMeasure& tool, const QPointF& p)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(p);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    tool.mousePress(&press);
}

cad::test::LineSetup makeSegmentOnLayer(ParamDocument& doc, const QUuid& layerId,
                                       const Vec2& p1Pos, const Vec2& p2Pos)
{
    Block block;
    block.layer = layerId;
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = p1Pos;
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Free;
    p2.freePos = p2Pos;
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

class TestMeasure : public QObject
{
    Q_OBJECT

private slots:
    void distanceModeIsEuclidean();
    void wCyclesModesAndValues();
    void axisCoincidenceRejected();
    // ── 三期: 只读悬停上报 (扫过即看, CONTEXT_STRIP_DESIGN.md §3) ──
    void hoverReportsSegmentToStrip();
    void angleMeasureHoverReportsSegment();
    // ── 角度测量: 图层限制与光标感知方向 ──
    void angleMeasureEnforcesSameLayer();
    void angleMeasureTCrossCursorDirection();
    void angleMeasureResolveKeepsFlips();
    // ── 公式引用测量: MA_xxx / M_xxx / L_xxx ──
    void formulaConsumesAngleMeasure();
    void formulaConsumesDistanceAndLinkedMeasure();
    void formulaUpdatesWhenGeometryMoves();
    // ── 测量卡片悬停: 实时持续显示 + 移开即刻取消 ──
    void measureCardHoverPersistentAndClearOnUnhover();
};

void TestMeasure::distanceModeIsEuclidean()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(30.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);

    clickAt(tool, QPointF(0.0, 0.0));    // A
    armDialogAutoDismiss();
    clickAt(tool, QPointF(30.0, 40.0));  // B: |A-B| = 50 mm

    QCOMPARE(doc.measureVars().size(), size_t(1));
    const MeasureVariable& mv = doc.measureVars().front();
    QCOMPARE(mv.kind, MeasureKind::Distance);
    QVERIFY(std::abs(mv.value - 50.0) < 1e-9);
    tool.deactivate();
}

void TestMeasure::wCyclesModesAndValues()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(30.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);

    // Each iteration: W → A(0,0) → B(30,40). With points (0,0)/(30,40):
    //   水平 = |dx| = 30, 垂直 = |dy| = 40, 距离 = 50 — and the cycle wraps
    //   back to 水平 on the fourth W.
    const MeasureKind expectedKind[] = {
        MeasureKind::Horizontal, MeasureKind::Vertical,
        MeasureKind::Distance,   MeasureKind::Horizontal,
    };
    const double expectedValue[] = { 30.0, 40.0, 50.0, 30.0 };

    for (int i = 0; i < 4; ++i) {
        pressW(tool);
        clickAt(tool, QPointF(0.0, 0.0));
        armDialogAutoDismiss();
        clickAt(tool, QPointF(30.0, 40.0));
    }

    QCOMPARE(doc.measureVars().size(), size_t(4));
    for (int i = 0; i < 4; ++i) {
        const MeasureVariable& mv = doc.measureVars()[static_cast<size_t>(i)];
        QCOMPARE(mv.kind, expectedKind[i]);
        QVERIFY2(std::abs(mv.value - expectedValue[i]) < 1e-9,
                 qPrintable(QStringLiteral("measure %1 value").arg(i)));
    }
    tool.deactivate();
}

void TestMeasure::axisCoincidenceRejected()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    // First block: two points with the SAME x — a horizontal measure between
    // them must be refused. Second block: a recovery target at (30,40).
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(0.0, 40.0));
    makePointBlock(doc, 1, Vec2(30.0, 40.0), Vec2(60.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);
    pressW(tool);  // → 水平

    clickAt(tool, QPointF(0.0, 0.0));   // A
    // B has the same x → the horizontal measure must be REFUSED: no measure
    // is created, no dialog opens, and the tool STAYS mid-selection.
    clickAt(tool, QPointF(0.0, 40.0));
    QCOMPARE(doc.measureVars().size(), size_t(0));

    // Still in SelectB: a different B commits normally as 水平.
    armDialogAutoDismiss();
    clickAt(tool, QPointF(30.0, 40.0));
    QCOMPARE(doc.measureVars().size(), size_t(1));
    const MeasureVariable& mv = doc.measureVars().front();
    QCOMPARE(mv.kind, MeasureKind::Horizontal);
    QVERIFY(std::abs(mv.value - 30.0) < 1e-9);
    tool.deactivate();
}

// ── 三期: 只读悬停上报 (扫过即看, CONTEXT_STRIP_DESIGN.md §3) ──
// 测量工具悬停线段 → 经 ToolHost 上报悬停点所在线段, 条带只读预览; 移出 → 空。

void TestMeasure::hoverReportsSegmentToStrip()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const auto line = cad::test::makeLine(doc, 100.0);
    doc.resolveAll();

    cad::test::RecordingToolHost host;
    cad::tools::ToolMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    tool.activate(ctx);

    // 悬停近端点 → 点吸附 → 上报该点所在线段。
    cad::test::sendToolMouseMove(tool, QPointF(5.0, 0.0));
    QCOMPARE(host.hoverBlock, line.blockId);
    QCOMPARE(host.hoverSeg, line.segId);

    // 移出 → 上报空 (条带收起)。
    cad::test::sendToolMouseMove(tool, QPointF(300.0, 300.0));
    QVERIFY(host.hoverBlock.isNull());
    QVERIFY(host.hoverSeg.isNull());
    tool.deactivate();
}

void TestMeasure::angleMeasureHoverReportsSegment()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const auto line = cad::test::makeLine(doc, 100.0);
    doc.resolveAll();

    cad::test::RecordingToolHost host;
    cad::tools::ToolAngleMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    tool.activate(ctx);

    // 线身中点 → 线段吸附 → 上报。
    cad::test::sendToolMouseMove(tool, QPointF(50.0, 0.0));
    QCOMPARE(host.hoverBlock, line.blockId);
    QCOMPARE(host.hoverSeg, line.segId);

    cad::test::sendToolMouseMove(tool, QPointF(300.0, 300.0));
    QVERIFY(host.hoverBlock.isNull());
    QVERIFY(host.hoverSeg.isNull());
    tool.deactivate();
}

void TestMeasure::angleMeasureEnforcesSameLayer()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);
    doc.addLayer(QStringLiteral("Layer 2"));
    const QUuid layer2 = layerIdAt(doc, 2);

    // Segment A on Layer 1 (horizontal, 0 to 100)
    makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(100.0, 0.0));
    // Segment B on Layer 2 (vertical at x=0, 0 to 100)
    makeSegmentOnLayer(doc, layer2, Vec2(0.0, 0.0), Vec2(0.0, 100.0));
    // Segment C on Layer 1 (vertical at x=100, 0 to 100)
    makeSegmentOnLayer(doc, layer1, Vec2(100.0, 0.0), Vec2(100.0, 100.0));
    doc.resolveAll();

    cad::tools::ToolAngleMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    tool.activate(ctx);

    // Click line A on Layer 1
    clickAtAngle(tool, QPointF(50.0, 0.0));
    // Click line B on Layer 2 (cross-layer) -> should be rejected!
    clickAtAngle(tool, QPointF(0.0, 50.0));
    QCOMPARE(doc.angleMeasures().size(), size_t(0));

    // Still in SelectB: Click line C on Layer 1 (same layer) -> should commit!
    clickAtAngle(tool, QPointF(100.0, 50.0));
    QCOMPARE(doc.angleMeasures().size(), size_t(1));

    tool.deactivate();
}

void TestMeasure::angleMeasureTCrossCursorDirection()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    // Vertical line: (0, 0) -> (0, 50)  (User direction: up)
    makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(0.0, 50.0));
    // Horizontal line: (-50, 0) -> (50, 0) (User direction: left to right)
    makeSegmentOnLayer(doc, layer1, Vec2(-50.0, 0.0), Vec2(50.0, 0.0));
    doc.resolveAll();

    cad::tools::ToolAngleMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    tool.activate(ctx);

    // Case 1: Pick vertical line, then pick RIGHT side of horizontal line (x > 0).
    // Vertical ray is (0, 1), horizontal ray is (1, 0).
    // Follower angle from vertical (90 deg) to right horizontal (0 deg) = -90 deg.
    clickAtAngle(tool, QPointF(0.0, 25.0));   // Vertical line upper half
    clickAtAngle(tool, QPointF(25.0, 0.0));    // Horizontal line right half

    QCOMPARE(doc.angleMeasures().size(), size_t(1));
    const auto& am1 = doc.angleMeasures().front();
    QCOMPARE(am1.flipA, false);
    QCOMPARE(am1.flipB, false);
    QVERIFY(std::abs(am1.value - (-90.0)) < 1e-6);

    // Case 2: Pick vertical line, then pick LEFT side of horizontal line (x < 0).
    // Vertical ray is (0, 1), horizontal ray is (-1, 0).
    // Follower angle from vertical (90 deg) to left horizontal (180 deg) = +90 deg.
    clickAtAngle(tool, QPointF(0.0, 25.0));   // Vertical line upper half
    clickAtAngle(tool, QPointF(-25.0, 0.0));   // Horizontal line left half

    QCOMPARE(doc.angleMeasures().size(), size_t(2));
    const auto& am2 = doc.angleMeasures()[1];
    QCOMPARE(am2.flipA, false);
    QCOMPARE(am2.flipB, true);
    QVERIFY(std::abs(am2.value - 90.0) < 1e-6);

    tool.deactivate();
}

void TestMeasure::angleMeasureResolveKeepsFlips()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(0.0, 50.0));
    makeSegmentOnLayer(doc, layer1, Vec2(-50.0, 0.0), Vec2(50.0, 0.0));
    doc.resolveAll();

    cad::tools::ToolAngleMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    tool.activate(ctx);

    // Pick left side -> flipB == true, value == 90 deg
    clickAtAngle(tool, QPointF(0.0, 25.0));
    clickAtAngle(tool, QPointF(-25.0, 0.0));
    tool.deactivate();

    QCOMPARE(doc.angleMeasures().size(), size_t(1));
    QVERIFY(std::abs(doc.angleMeasures()[0].value - 90.0) < 1e-6);

    // Re-resolve document: the value must remain 90 deg because flipB was persisted!
    doc.resolveAll();
    QVERIFY(std::abs(doc.angleMeasures()[0].value - 90.0) < 1e-6);
}

void TestMeasure::formulaConsumesAngleMeasure()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    // Two orthogonal lines on Layer 1:
    // Line A: (0, 0) -> (0, 50)
    // Line B: (-50, 0) -> (50, 0)
    makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(0.0, 50.0));
    makeSegmentOnLayer(doc, layer1, Vec2(-50.0, 0.0), Vec2(50.0, 0.0));
    doc.resolveAll();

    cad::tools::ToolAngleMeasure tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    tool.activate(ctx);

    clickAtAngle(tool, QPointF(0.0, 25.0));
    clickAtAngle(tool, QPointF(25.0, 0.0)); // angle is -90 deg
    tool.deactivate();

    QCOMPARE(doc.angleMeasures().size(), size_t(1));
    const auto& am = doc.angleMeasures().front();
    const QString refName = am.refName;
    QVERIFY(!refName.isEmpty());
    QVERIFY(std::abs(am.value - (-90.0)) < 1e-6);

    // 1. Add formula referencing the angle measure: e.g. "MA_xxx * 2"
    FormulaVariable f;
    f.name = QStringLiteral("DoubleAngle");
    f.expression = QStringLiteral("%1 * 2").arg(refName);
    doc.addFormula(f);

    const auto* storedF = doc.findFormula(f.id);
    QVERIFY(storedF != nullptr);
    QVERIFY2(storedF->valid, qPrintable(storedF->error));
    // -90 * 2 = -180 deg
    QVERIFY(std::abs(storedF->baseValue - cad::geo::Units::cmToMm(-180.0)) < 1e-6);

    // 2. Case-insensitive formula: e.g. "ma_xxx + 10"
    FormulaVariable f2;
    f2.name = QStringLiteral("LowerRef");
    f2.expression = QStringLiteral("%1 + 10").arg(refName.toLower());
    doc.addFormula(f2);

    const auto* storedF2 = doc.findFormula(f2.id);
    QVERIFY(storedF2 != nullptr);
    QVERIFY2(storedF2->valid, qPrintable(storedF2->error));
    QVERIFY(std::abs(storedF2->baseValue - cad::geo::Units::cmToMm(-80.0)) < 1e-6);

    // 3. Document parameters contain the formula results
    QVERIFY(std::abs(doc.parameter("DoubleAngle") - (-180.0)) < 1e-6);
    QVERIFY(std::abs(doc.parameter("LowerRef") - (-80.0)) < 1e-6);
}

void TestMeasure::formulaConsumesDistanceAndLinkedMeasure()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    // Add a segment with length 50mm (= 5cm)
    auto setup = makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(50.0, 0.0));
    doc.resolveAll();

    // 1. LinkedVariable for this segment (length = 50mm = 5cm)
    const auto* blk = doc.findBlock(setup.blockId);
    const auto* seg = blk->findSegment(setup.segId);
    auto lv = LinkedVariable::fromSegment(*blk, *seg);
    const QString lvRef = lv.refName;
    doc.addLinked(std::move(lv));

    // 2. MeasureVariable between (0,0) and (50,0) -> dist = 50mm = 5cm
    MeasureVariable mv;
    mv.blockA = setup.blockId;
    mv.pointA = setup.startId;
    mv.blockB = setup.blockId;
    mv.pointB = setup.endId;
    mv.refName = QStringLiteral("M_TEST");
    mv.value = 50.0;
    doc.addMeasure(mv);

    // Add formulas
    FormulaVariable f1;
    f1.name = QStringLiteral("LinkedCalc");
    f1.expression = QStringLiteral("%1 * 3").arg(lvRef); // 5cm * 3 = 15cm
    doc.addFormula(f1);

    FormulaVariable f2;
    f2.name = QStringLiteral("DistCalc");
    f2.expression = QStringLiteral("%1 + 2").arg(mv.refName); // 5cm + 2 = 7cm
    doc.addFormula(f2);

    const auto* sf1 = doc.findFormula(f1.id);
    const auto* sf2 = doc.findFormula(f2.id);
    QVERIFY(sf1 && sf1->valid);
    QVERIFY(sf2 && sf2->valid);
    QVERIFY(std::abs(cad::geo::Units::mmToCm(sf1->baseValue) - 15.0) < 1e-6);
    QVERIFY(std::abs(cad::geo::Units::mmToCm(sf2->baseValue) - 7.0) < 1e-6);
}

void TestMeasure::formulaUpdatesWhenGeometryMoves()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    auto setup = makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(100.0, 0.0));
    doc.resolveAll();

    const auto* blk = doc.findBlock(setup.blockId);
    const auto* seg = blk->findSegment(setup.segId);
    auto lv = LinkedVariable::fromSegment(*blk, *seg);
    const QString lvRef = lv.refName;
    doc.addLinked(std::move(lv));

    FormulaVariable f;
    f.name = QStringLiteral("SegHalf");
    f.expression = QStringLiteral("%1 / 2").arg(lvRef); // 10cm / 2 = 5cm
    doc.addFormula(f);

    const auto* sf = doc.findFormula(f.id);
    QVERIFY(sf && sf->valid);
    QVERIFY(std::abs(cad::geo::Units::mmToCm(sf->baseValue) - 5.0) < 1e-6);

    // Now move the segment end point from (100, 0) to (200, 0) -> length becomes 200mm = 20cm
    auto* mutBlk = doc.findBlock(setup.blockId);
    auto* p2 = mutBlk->findPoint(setup.endId);
    p2->freePos = Vec2(200.0, 0.0);
    doc.resolveAll();

    // The formula should have automatically re-evaluated to 20cm / 2 = 10cm
    const auto* sfAfter = doc.findFormula(f.id);
    QVERIFY(sfAfter && sfAfter->valid);
    QVERIFY(std::abs(cad::geo::Units::mmToCm(sfAfter->baseValue) - 10.0) < 1e-6);
    QVERIFY(std::abs(doc.parameter("SegHalf") - 10.0) < 1e-6);
}

void TestMeasure::measureCardHoverPersistentAndClearOnUnhover()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const QUuid layer1 = layerIdAt(doc, 1);

    auto setup1 = makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(100.0, 0.0));
    auto setup2 = makeSegmentOnLayer(doc, layer1, Vec2(0.0, 0.0), Vec2(0.0, 100.0));
    doc.resolveAll();

    // 1. Test CanvasScene::flashMeasure persistence (no auto-timeout) and clearMeasureHighlight
    const int baseItemCount = scene.items().size();
    MeasureVariable mv;
    mv.blockA = setup1.blockId;
    mv.pointA = setup1.startId;
    mv.blockB = setup1.blockId;
    mv.pointB = setup1.endId;
    mv.kind = MeasureKind::Distance;
    doc.addMeasure(mv);

    bool flashed = scene.flashMeasure(mv.blockA, mv.pointA, mv.blockB, mv.pointB, mv.kind, mv.id);
    QVERIFY(flashed);
    // 2 rings + 1 line = 3 items added
    QCOMPARE(scene.items().size(), baseItemCount + 3);

    // Wait 50 ms to ensure it does not disappear unexpectedly
    QTest::qWait(50);
    QCOMPARE(scene.items().size(), baseItemCount + 3);

    // clearMeasureHighlight with matching id -> removed immediately
    scene.clearMeasureHighlight(mv.id);
    QCOMPARE(scene.items().size(), baseItemCount);

    // 2. Test CanvasScene::highlightMeasureBlock persistence and clear
    scene.highlightMeasureBlock(mv.id, setup1.blockId);
    auto* bi = scene.findBlockItem(setup1.blockId);
    QVERIFY(bi && bi->toolLocked());

    scene.clearMeasureHighlight(mv.id);
    QVERIFY(bi && !bi->toolLocked());

    // 3. Test CanvasScene::flashAngleMeasure persistence and clear
    AngleMeasureVariable am;
    am.blockA = setup1.blockId;
    am.segmentA = setup1.segId;
    am.blockB = setup2.blockId;
    am.segmentB = setup2.segId;
    doc.addAngleMeasure(am);

    bool angleFlashed = scene.flashAngleMeasure(am.blockA, am.segmentA, am.blockB, am.segmentB,
                                                am.flipA, am.flipB, am.id);
    QVERIFY(angleFlashed);
    // 2 segment lines + 1 arc = 3 items added
    QCOMPARE(scene.items().size(), baseItemCount + 3);

    scene.clearMeasureHighlight(am.id);
    QCOMPARE(scene.items().size(), baseItemCount);

    // 4. Test MeasureCard signals and syncFromModel
    MeasureCard card(mv, QStringLiteral("P1·P2"));
    QSignalSpy spyClick(&card, &MeasureCard::sourceClicked);
    QSignalSpy spyClear(&card, &MeasureCard::highlightCleared);

    // Simulate mouse hover enter
    QEnterEvent enterEv(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&card, &enterEv);
    QCOMPARE(spyClick.count(), 1);
    QCOMPARE(spyClick.takeFirst().at(0).toUuid(), mv.id);

    // syncFromModel with new ID
    MeasureVariable mv2;
    mv2.name = QStringLiteral("M2");
    mv2.refName = QStringLiteral("M_two");
    card.syncFromModel(mv2, QStringLiteral("P3·P4"));
    QCOMPARE(card.measureId(), mv2.id);

    // Simulate enter again
    QApplication::sendEvent(&card, &enterEv);
    QCOMPARE(spyClick.count(), 1);
    QCOMPARE(spyClick.takeFirst().at(0).toUuid(), mv2.id);

    // Simulate mouse leave
    QEvent leaveEv(QEvent::Leave);
    QApplication::sendEvent(&card, &leaveEv);
    QCOMPARE(spyClear.count(), 1);
    QCOMPARE(spyClear.takeFirst().at(0).toUuid(), mv2.id);

    // 5. Test AngleMeasureCard signals and syncFromModel
    AngleMeasureCard aCard(am, QStringLiteral("S1·S2"));
    QSignalSpy spyAClick(&aCard, &AngleMeasureCard::sourceClicked);
    QSignalSpy spyAClear(&aCard, &AngleMeasureCard::highlightCleared);

    QApplication::sendEvent(&aCard, &enterEv);
    QCOMPARE(spyAClick.count(), 1);
    QCOMPARE(spyAClick.takeFirst().at(0).toUuid(), am.id);

    AngleMeasureVariable am2;
    am2.name = QStringLiteral("MA2");
    am2.refName = QStringLiteral("MA_two");
    aCard.syncFromModel(am2, QStringLiteral("S3·S4"));
    QCOMPARE(aCard.angleMeasureId(), am2.id);

    QApplication::sendEvent(&aCard, &leaveEv);
    QCOMPARE(spyAClear.count(), 1);
    QCOMPARE(spyAClear.takeFirst().at(0).toUuid(), am2.id);
}

QTEST_MAIN(TestMeasure)
#include "test_measure.moc"
