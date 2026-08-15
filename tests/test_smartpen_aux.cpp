/// @file test_smartpen_aux.cpp
/// Verifies the smart-pen line-body quick-aux-point interaction:
///   Idle hover on a segment body → green X marker appears;
///   click with the X live → QuickAuxDialog opens (aux point creation).
/// This guards the regression where updateSegMarker was never invoked and
/// the X marker / line-body aux points never appeared.

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>

#include "canvas/CanvasScene.h"
#include "tools/ToolSmartPen.h"
#include "tools/ToolSelect.h"
#include "tools/ToolManager.h"
#include "tools/QuickAuxDialog.h"
#include "parametric/ParamDocument.h"
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

/// Horizontal line block on layer @p layerIndex: (0,0) → (100,0) mm.
QUuid makeLineBlock(ParamDocument& doc, int layerIndex)
{
    Block block;
    block.layer = layerIdAt(doc, layerIndex);
    block.transform.origin = Vec2::zero();
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 100.0;
    p2.angle = 0.0;
    const QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return blockId;
}

/// True when the scene contains a VISIBLE QGraphicsPathItem that is not a
/// BlockItem (i.e. the smart pen's green X marker).
bool hasVisibleXMarker(CanvasScene& scene)
{
    const auto items = scene.items();
    for (QGraphicsItem* it : items) {
        if (auto* path = qgraphicsitem_cast<QGraphicsPathItem*>(it)) {
            if (path->isVisible())
                return true;
        }
    }
    return false;
}

/// Count open QuickAuxDialog top-level widgets.
int openAuxDialogs()
{
    int n = 0;
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        if (qobject_cast<cad::tools::QuickAuxDialog*>(w))
            ++n;
    }
    return n;
}

} // namespace

class TestSmartPenAux : public QObject
{
    Q_OBJECT

private slots:
    void idleHoverShowsXMarker();
    void xMarkerHiddenOnBlankMove();
    void clickWithXOpensAuxDialog();
    void clickWithoutXStartsStroke();

    // Blank-space right-click tool switch (空白右键互切)
    void penBlankRightClickSwitchesToSelect();
    void penEntityRightClickDoesNotSwitch();
    void penDrawingRightClickCancelsNotSwitches();
    void selectBlankRightClickSwitchesToPen();

    // End-point snap with a free start: the line FLIPS (吸附点成为起点),
    // geometry unchanged, and a real connection is created (终点连接终点).
    void endSnapFlipsLineAndCreatesConnection();
    /// Aux point as stroke END with a free start: the line FLIPS (same rule as
    /// endSnapFlipsLineAndCreatesConnection) and the connection is created
    /// AND 拖动保护-locked by default — otherwise the stroke degrades into a
    /// free line with no attachment (拖动保护失效).
    void auxPointEndFlipsAndCreatesLockedConnection();

    // ── 预输入 (status-bar pre-input): 名称/长度/角度一次性构造 ──
    void preInputLengthAngleCreatesLineInOneClick();
    void preInputLengthOnlyLocksDistance();
    void preInputAngleOnlyLocksDirection();
    void preInputNameAppliesToTwoClickLine();
    void preInputFormulaDrivesLine();
    void preInputAttachedAngleUsesFollowerConvention();
    void preInputInvalidLengthIsIgnored();
    void preInputSurvivesCancelledStroke();
};

void TestSmartPenAux::idleHoverShowsXMarker()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    QVERIFY(!hasVisibleXMarker(scene));  // nothing before hover

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(50.0, 0.0));  // user coords: on the segment body
    move.setButton(Qt::NoButton);
    pen.mouseMove(&move);

    QVERIFY2(hasVisibleXMarker(scene),
             "X marker did not appear while hovering a segment body (BUG)");
    pen.deactivate();
}

void TestSmartPenAux::xMarkerHiddenOnBlankMove()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(50.0, 0.0));
    pen.mouseMove(&move);
    QVERIFY(hasVisibleXMarker(scene));

    // Move to empty space: the marker must disappear.
    move.setScenePos(QPointF(300.0, 300.0));
    pen.mouseMove(&move);
    QVERIFY(!hasVisibleXMarker(scene));
    pen.deactivate();
}

void TestSmartPenAux::clickWithXOpensAuxDialog()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Hover to arm the X marker.
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(50.0, 0.0));
    pen.mouseMove(&move);
    QVERIFY(hasVisibleXMarker(scene));

    // Click on the segment body → QuickAuxDialog must open.
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(50.0, 0.0));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    pen.mousePress(&press);

    QVERIFY2(openAuxDialogs() > 0,
             "clicking the X marker did not open the quick-aux dialog (BUG)");

    // Close the dialog (deactivate does this too).
    pen.deactivate();
}

void TestSmartPenAux::clickWithoutXStartsStroke()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Click far away from any segment: no dialog, and a stroke starts
    // (a preview line appears in the scene).
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(300.0, 300.0));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    pen.mousePress(&press);

    QCOMPARE(openAuxDialogs(), 0);
    QVERIFY2(hasVisibleXMarker(scene) == false,
             "no X marker expected away from segments");
    pen.deactivate();
}

// ---------------------------------------------------------------------------
// Blank-space right-click tool switch (空白右键互切): in IDLE state a right
// click on empty space fires the injected switch request (智能笔 → 选择);
// entity right-clicks stay reserved (no switch), and Drawing-state right
// clicks keep their cancel semantics.
// ---------------------------------------------------------------------------
void TestSmartPenAux::penBlankRightClickSwitchesToSelect()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    bool fired = false;
    cad::tools::ToolType requested = cad::tools::ToolType::Select;
    pen.setToolSwitchRequest([&](cad::tools::ToolType t) {
        fired = true;
        requested = t;
    });

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(300.0, 300.0));  // blank space
    press.setButton(Qt::RightButton);
    press.setButtons(Qt::RightButton);
    pen.mousePress(&press);

    QVERIFY(fired);
    QCOMPARE(requested, cad::tools::ToolType::Select);
    pen.deactivate();
}

void TestSmartPenAux::penEntityRightClickDoesNotSwitch()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    bool fired = false;
    pen.setToolSwitchRequest([&](cad::tools::ToolType) { fired = true; });

    // Right-click ON the segment: reserved for a future context menu.
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(50.0, 0.0));
    press.setButton(Qt::RightButton);
    press.setButtons(Qt::RightButton);
    pen.mousePress(&press);

    QVERIFY(!fired);
    pen.deactivate();
}

void TestSmartPenAux::penDrawingRightClickCancelsNotSwitches()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    bool fired = false;
    pen.setToolSwitchRequest([&](cad::tools::ToolType) { fired = true; });

    // Start a stroke (Idle + left click on blank space → Drawing).
    QGraphicsSceneMouseEvent left(QEvent::GraphicsSceneMousePress);
    left.setScenePos(QPointF(300.0, 300.0));
    left.setButton(Qt::LeftButton);
    left.setButtons(Qt::LeftButton);
    pen.mousePress(&left);

    // Right-click during Drawing: cancels the stroke, does NOT switch.
    QGraphicsSceneMouseEvent right(QEvent::GraphicsSceneMousePress);
    right.setScenePos(QPointF(300.0, 300.0));
    right.setButton(Qt::RightButton);
    right.setButtons(Qt::RightButton);
    pen.mousePress(&right);

    QVERIFY(!fired);
    pen.deactivate();
}

void TestSmartPenAux::selectBlankRightClickSwitchesToPen()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);
    doc.resolveAll();

    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    bool fired = false;
    cad::tools::ToolType requested = cad::tools::ToolType::Select;
    select.setToolSwitchRequest([&](cad::tools::ToolType t) {
        fired = true;
        requested = t;
    });

    // Idle + blank-space right-click → switch to the smart pen.
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(300.0, 300.0));
    press.setButton(Qt::RightButton);
    press.setButtons(Qt::RightButton);
    select.mousePress(&press);

    QVERIFY(fired);
    QCOMPARE(requested, cad::tools::ToolType::SmartPen);
    select.deactivate();
}

void TestSmartPenAux::endSnapFlipsLineAndCreatesConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);   // B: (0,0) → (100,0), end point at (100,0)
    doc.resolveAll();

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Stroke 1: click blank (200,100) → free start.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(200.0, 100.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    pen.mousePress(&press1);

    // Stroke 2: click B's END point (100,0) → the line FLIPS: the snap point
    // becomes the new line's start, the original start (200,100) becomes the
    // free end. A real attachment is created (end-to-end connection).
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    press2.setScenePos(QPointF(100.0, 0.0));
    press2.setButton(Qt::LeftButton);
    press2.setButtons(Qt::LeftButton);
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(2));   // B + new line
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    const Segment& ns = nb.segments.front();
    // Flipped start = the snapped point (B's end, 100,0).
    const ParamPoint* sp = nb.findPoint(ns.startPointId);
    QVERIFY(sp && sp->resolved);
    QVERIFY(nb.transform.toWorld(sp->resolvedPos).distanceTo(Vec2(100.0, 0.0)) < 1e-6);
    // Free end = the original start position (200,100): geometry unchanged.
    const ParamPoint* ep = nb.findPoint(ns.endPointId);
    QVERIFY(ep && ep->resolved);
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(200.0, 100.0)) < 1e-6);
    // Attachment: new line's start → B's end point.
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.fromBlockId, nb.id);
    QCOMPARE(att.fromPointId, ns.startPointId);
    const Block& b = doc.blocks().front();
    QCOMPARE(att.toBlockId, b.id);
    QCOMPARE(att.toPointId, b.segments.front().endPointId);
    pen.deactivate();
}

void TestSmartPenAux::auxPointEndFlipsAndCreatesLockedConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    const QUuid hostId = makeLineBlock(doc, 0);   // B: (0,0) → (100,0)
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Stroke 1: click blank (200,100) → free start.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(200.0, 100.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    pen.mousePress(&press1);

    // Hover B's body to arm the X marker.
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(50.0, 0.0));
    move.setButton(Qt::NoButton);
    pen.mouseMove(&move);
    QVERIFY(hasVisibleXMarker(scene));

    // Click the body → QuickAuxDialog opens (aux point as stroke END).
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    press2.setScenePos(QPointF(50.0, 0.0));
    press2.setButton(Qt::LeftButton);
    press2.setButtons(Qt::LeftButton);
    pen.mousePress(&press2);
    QVERIFY2(openAuxDialogs() > 0, "X click must open the quick-aux dialog");

    // Accept the dialog → aux point is created, the stroke commits.
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        if (auto* dlg = qobject_cast<cad::tools::QuickAuxDialog*>(w)) {
            dlg->accept();
            break;
        }
    }
    QTest::qWait(30);

    // The new line FLIPPED: its start = the aux point on B, and a real
    // attachment was created — NOT a free line (free line = no attachment =
    // 拖动保护失效). The connection is 拖动保护-locked by default.
    QCOMPARE(doc.blocks().size(), size_t(2));   // B + new line
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QVERIFY2(att.isLocked, "aux-point connection must default to 拖动保护");

    const Block& nb = doc.blocks().back();
    const Segment& ns = nb.segments.front();
    QCOMPARE(att.fromBlockId, nb.id);
    QCOMPARE(att.fromPointId, ns.startPointId);
    QCOMPARE(att.toBlockId, hostId);
    // The aux point lives on B (its serial is a P* tag).
    const Block& b = doc.blocks().front();
    QVERIFY(b.findPoint(att.toPointId) != nullptr);

    // Geometry unchanged: flipped start = aux point (50,0), free end = (200,100).
    const ParamPoint* sp = nb.findPoint(ns.startPointId);
    QVERIFY(sp && sp->resolved);
    QVERIFY(nb.transform.toWorld(sp->resolvedPos).distanceTo(Vec2(50.0, 0.0)) < 1e-6);
    const ParamPoint* ep = nb.findPoint(ns.endPointId);
    QVERIFY(ep && ep->resolved);
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(200.0, 100.0)) < 1e-6);
    pen.deactivate();
}

// ---------------------------------------------------------------------------
// 预输入 (status-bar pre-input): the pending 名称/长度/角度 are consumed by
// the next committed line and then cleared (一次性预输入).
// ---------------------------------------------------------------------------

namespace {

void makePress(QGraphicsSceneMouseEvent& press, const QPointF& userPos)
{
    press.setScenePos(userPos);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
}

} // namespace

void TestSmartPenAux::preInputLengthAngleCreatesLineInOneClick()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.name     = QString::fromUtf8("肩线");
    in.lengthCm = QStringLiteral("10");
    in.angleDeg = QStringLiteral("90");
    pen.setPreInput(in);

    // 长度+角度齐全 → 一击成线: start (100,100), end (100,200) mm.
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    makePress(press, QPointF(100.0, 100.0));
    pen.mousePress(&press);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    QCOMPARE(nb.segments.size(), size_t(1));
    QCOMPARE(nb.segments.front().name, QString::fromUtf8("肩线"));

    const ParamPoint* sp = nb.findPoint(nb.segments.front().startPointId);
    const ParamPoint* ep = nb.findPoint(nb.segments.front().endPointId);
    QVERIFY(sp && ep && sp->resolved && ep->resolved);
    QVERIFY(nb.transform.toWorld(sp->resolvedPos).distanceTo(Vec2(100.0, 100.0)) < 1e-6);
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(100.0, 200.0)) < 1e-6);
    QVERIFY(std::abs(ep->distance - 100.0) < 1e-6);
    QVERIFY(std::abs(ep->angle - 90.0) < 1e-6);

    // 一次性语义: 工具侧立即清空已使用的预输入.
    QVERIFY(pen.preInput().name.isEmpty());
    QVERIFY(pen.preInput().lengthCm.isEmpty());
    QVERIFY(pen.preInput().angleDeg.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputLengthOnlyLocksDistance()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.lengthCm = QStringLiteral("10");
    pen.setPreInput(in);

    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);
    // 第二击只决定方向: (160,120) 方向 → 长度锁 100mm → end (80,60).
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(160.0, 120.0));
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    const ParamPoint* ep = nb.findPoint(nb.segments.front().endPointId);
    QVERIFY(ep && ep->resolved);
    const Vec2 end = nb.transform.toWorld(ep->resolvedPos);
    QVERIFY(end.distanceTo(Vec2(80.0, 60.0)) < 1e-6);
    QVERIFY(std::abs(ep->distance - 100.0) < 1e-6);
    QVERIFY(pen.preInput().lengthCm.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputAngleOnlyLocksDirection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.angleDeg = QStringLiteral("30");
    pen.setPreInput(in);

    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);
    // 第二击沿固定 30° 射线投影: end = dir(30°) * ((40,80)·dir(30°)).
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(40.0, 80.0));
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    const ParamPoint* ep = nb.findPoint(nb.segments.front().endPointId);
    QVERIFY(ep && ep->resolved);
    const Vec2 end = nb.transform.toWorld(ep->resolvedPos);

    const double rad = 30.0 * 3.14159265358979323846 / 180.0;
    const Vec2 dir(std::cos(rad), std::sin(rad));
    const double t = Vec2(40.0, 80.0).dot(dir);
    QVERIFY(end.distanceTo(dir * t) < 1e-6);
    // Polar 存储角 = 绝对世界角 (自由起点).
    QVERIFY(std::abs(ep->angle - 30.0) < 1e-6);
    QVERIFY(pen.preInput().angleDeg.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputNameAppliesToTwoClickLine()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.name = QString::fromUtf8("口袋线");
    pen.setPreInput(in);

    // 仅名称预输入 → 保持经典两击流程, 名称落在创建的线段上.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(100.0, 0.0));
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    QCOMPARE(nb.segments.front().name, QString::fromUtf8("口袋线"));
    QVERIFY(pen.preInput().name.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputFormulaDrivesLine()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.name     = QString::fromUtf8("公式线");
    in.lengthCm = QStringLiteral("5+5");
    in.angleDeg = QStringLiteral("30+60");
    pen.setPreInput(in);

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    makePress(press, QPointF(0.0, 0.0));
    pen.mousePress(&press);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    const Segment& ns = nb.segments.front();
    QCOMPARE(ns.name, QString::fromUtf8("公式线"));
    QCOMPARE(ns.lengthFormula, QStringLiteral("5+5"));
    const ParamPoint* ep = nb.findPoint(ns.endPointId);
    QVERIFY(ep && ep->resolved);
    QCOMPARE(ep->distanceFormula, QStringLiteral("5+5"));
    QCOMPARE(ep->angleFormula, QStringLiteral("30+60"));
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(0.0, 100.0)) < 1e-6);
    QVERIFY(pen.preInput().lengthCm.isEmpty());
    QVERIFY(pen.preInput().angleDeg.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputAttachedAngleUsesFollowerConvention()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeLineBlock(doc, 0);  // B: (0,0) → (100,0), end point at (100,0)
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.name     = QString::fromUtf8("延伸线");
    in.lengthCm = QStringLiteral("10");
    in.angleDeg = QStringLiteral("180");  // 闭合基准: 180° = 沿 leader 直行延伸
    pen.setPreInput(in);

    // 吸附 B 的终点 → 一击成线, 跟随角 180° = 继续沿 +X 延伸 100mm.
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    makePress(press, QPointF(100.0, 0.0));
    pen.mousePress(&press);

    QCOMPARE(doc.blocks().size(), size_t(2));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    QCOMPARE(nb.segments.front().name, QString::fromUtf8("延伸线"));

    const ParamPoint* ep = nb.findPoint(nb.segments.front().endPointId);
    QVERIFY(ep && ep->resolved);
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(200.0, 0.0)) < 1e-6);

    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.fromBlockId, nb.id);
    QVERIFY(std::abs(att.followerAngle - 180.0) < 1e-6);
    QVERIFY(pen.preInput().angleDeg.isEmpty());
    pen.deactivate();
}

void TestSmartPenAux::preInputInvalidLengthIsIgnored()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.lengthCm = QStringLiteral("abc");  // 无效 → 忽略, 不阻断画线
    in.angleDeg = QStringLiteral("45");
    pen.setPreInput(in);

    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(100.0, 100.0));
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(1));
    const Block& nb = doc.blocks().back();
    const ParamPoint* ep = nb.findPoint(nb.segments.front().endPointId);
    QVERIFY(ep && ep->resolved);
    // 长度失效 → 退化为角度锁定; (100,100) 在 45° 射线上, 距离为投影长度.
    const Vec2 end = nb.transform.toWorld(ep->resolvedPos);
    QVERIFY(end.distanceTo(Vec2(100.0, 100.0)) < 1e-6);
    QVERIFY(std::abs(ep->angle - 45.0) < 1e-6);
    QVERIFY(pen.preInput().angleDeg.isEmpty());
    QCOMPARE(pen.preInput().lengthCm, QStringLiteral("abc"));  // 未生效的不清
    pen.deactivate();
}

void TestSmartPenAux::preInputSurvivesCancelledStroke()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    QGraphicsView view(&scene);

    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);
    cad::tools::LinePreInput in;
    in.lengthCm = QStringLiteral("10");
    pen.setPreInput(in);

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    makePress(press, QPointF(0.0, 0.0));
    pen.mousePress(&press);

    // 右键取消画线 → 值未使用, 预输入必须保留.
    QGraphicsSceneMouseEvent right(QEvent::GraphicsSceneMousePress);
    right.setScenePos(QPointF(50.0, 50.0));
    right.setButton(Qt::RightButton);
    right.setButtons(Qt::RightButton);
    pen.mousePress(&right);

    QCOMPARE(doc.blocks().size(), size_t(0));
    QCOMPARE(pen.preInput().lengthCm, QStringLiteral("10"));
    pen.deactivate();
}

QTEST_MAIN(TestSmartPenAux)
#include "test_smartpen_aux.moc"
