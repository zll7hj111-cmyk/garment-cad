/// @file test_smartpen_aux.cpp
/// Verifies the smart-pen line-body quick-aux-point interaction:
///   Idle hover on a segment body → green X marker appears;
///   click with the X live → QuickAuxDialog opens (aux point creation).
/// This guards the regression where updateSegMarker was never invoked and
/// the X marker / line-body aux points never appeared.

#include <QtTest>
#include <QApplication>
#include <functional>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>
#include <QKeyEvent>

#include "canvas/CanvasScene.h"
#include "tools/ToolSmartPen.h"
#include "tools/ToolSelect.h"
#include "tools/ToolManager.h"
#include "ui/QuickAuxDialog.h"
#include "app/SmartPenPreInputBar.h"
#include "ElaLineEdit.h"
#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// P2 (TOOL_SYSTEM_AUDIT): 直驱工具时的宿主桩 —— 捕获工具切换请求
/// (旧 setToolSwitchRequest 回调的等价物, 经 ToolContext.host 注入)。
struct SwitchHost : cad::tools::ToolHost {
    std::function<void(cad::tools::ToolType)> onSwitch;
    void requestToolSwitch(cad::tools::ToolType type) override { if (onSwitch) onSwitch(type); }
    void setEditTarget(const QUuid&, const QUuid&) override {}
};

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
        if (qobject_cast<cad::ui::QuickAuxDialog*>(w))
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
    void auxPointEndFlipsAndCreatesLockedConnection();  // 名称保持 (默认焊接)

    // ── 预输入 (status-bar pre-input): 名称/长度/角度一次性构造 ──
    void preInputLengthAngleCreatesLineInOneClick();
    void preInputLengthOnlyLocksDistance();
    void preInputAngleOnlyLocksDirection();
    void preInputNameAppliesToTwoClickLine();
    void preInputFormulaDrivesLine();
    void preInputAttachedAngleUsesFollowerConvention();
    void preInputInvalidLengthIsIgnored();
    void preInputSurvivesCancelledStroke();
    void preInputBarShortcutContainment();
    void preInputBarKeyNavigation();
    void preInputBarEscClearsAndReturnsFocus();
    void preInputBarFullTypingWithShortcutLetters();

    // ── 落点确认 (stacked-point disambiguation, 2026-08) ──
    // Aux layer ACTIVE: an exact stack of aux + working start points.
    void stackedStartPicksActiveLayerPoint();
    void stackedStartSwitchByClickingLine();
    void stackedEndConfirmClickLinePicksWorkingPoint();
    void stackedEndConfirmBlankAcceptsActiveLayerDefault();
    void stackedEndConfirmEscCancelsStroke();
    // Working layer ACTIVE: the grayed aux point must stay out of the pool.
    void workingActiveStackedStartPicksWorkingPoint();
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
    SwitchHost host;
    bool fired = false;
    cad::tools::ToolType requested = cad::tools::ToolType::Select;
    host.onSwitch = [&](cad::tools::ToolType t) {
        fired = true;
        requested = t;
    };
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    pen.activate(ctx);

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
    SwitchHost host;
    bool fired = false;
    host.onSwitch = [&](cad::tools::ToolType) { fired = true; };
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    pen.activate(ctx);

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
    SwitchHost host;
    bool fired = false;
    host.onSwitch = [&](cad::tools::ToolType) { fired = true; };
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    pen.activate(ctx);

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
    SwitchHost host;
    bool fired = false;
    cad::tools::ToolType requested = cad::tools::ToolType::Select;
    host.onSwitch = [&](cad::tools::ToolType t) {
        fired = true;
        requested = t;
    };
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    select.activate(ctx);

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
        if (auto* dlg = qobject_cast<cad::ui::QuickAuxDialog*>(w)) {
            dlg->accept();
            break;
        }
    }
    // accept() 同步建点/提交(直连), 断言为模型态; 此处仅排空后续事件, 暂留。
    QTest::qWait(30);

    // The new line FLIPPED: its start = the aux point on B, and a real
    // attachment was created — NOT a free line (free line = no attachment).
    // 新建连接默认勾选「拖动保护」(焊接) (用户拍板 2026-08 复旧).
    QCOMPARE(doc.blocks().size(), size_t(2));   // B + new line
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QVERIFY2(att.isLocked, "aux-point connection must default welded (拖动保护默认勾选)");

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

/// Handle set for a plain two-point line block.
struct LineEx { QUuid blockId; QUuid startId; QUuid endId; QUuid segId; };

/// Line block on @p layerIndex: Free start at @p origin + Polar end
/// (@p lengthMm along @p angleDeg, degrees CCW), one segment.
LineEx makeLineBlockEx(ParamDocument& doc, int layerIndex, Vec2 origin,
                       double lengthMm, double angleDeg)
{
    Block block;
    block.layer = layerIdAt(doc, layerIndex);
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lengthMm;
    p2.angle = angleDeg;
    const QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    const QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
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

void TestSmartPenAux::preInputBarShortcutContainment()
{
    cad::app::SmartPenPreInputBar bar;
    bar.focusFirstNameField();

    // Verify all tool shortcut keys ('V', 'L', 'C', 'R', 'B', 'I', 'A', 'H', 'W', 'N', 'M')
    // are accepted by ShortcutOverride inside SmartPenPreInputBar so they never leak to window actions.
    const QVector<int> shortcutKeys = {
        Qt::Key_V, Qt::Key_L, Qt::Key_C, Qt::Key_R, Qt::Key_B,
        Qt::Key_I, Qt::Key_A, Qt::Key_H, Qt::Key_W, Qt::Key_N, Qt::Key_M
    };

    for (int key : shortcutKeys) {
        QKeyEvent scEvent(QEvent::ShortcutOverride, key, Qt::NoModifier);
        scEvent.ignore();
        QCoreApplication::sendEvent(bar.nameEdit(), &scEvent);
        QVERIFY2(scEvent.isAccepted(), QString("ShortcutOverride for key %1 must be accepted").arg(key).toUtf8().constData());
    }

    // Also with Ctrl modifier (Ctrl+Z, Ctrl+Y, Ctrl+D, Ctrl+S, etc.):
    const QVector<int> ctrlKeys = { Qt::Key_Z, Qt::Key_Y, Qt::Key_D, Qt::Key_S, Qt::Key_1, Qt::Key_2 };
    for (int key : ctrlKeys) {
        QKeyEvent scEvent(QEvent::ShortcutOverride, key, Qt::ControlModifier);
        scEvent.ignore();
        QCoreApplication::sendEvent(bar.lengthEdit(), &scEvent);
        QVERIFY2(scEvent.isAccepted(), QString("ShortcutOverride for Ctrl+%1 must be accepted").arg(key).toUtf8().constData());
    }
}

void TestSmartPenAux::preInputBarKeyNavigation()
{
    QWidget dummyCanvas;
    cad::app::SmartPenPreInputBar bar;
    bar.setCanvasView(&dummyCanvas);

    // Focus first field:
    bar.focusFirstNameField();
    QCOMPARE(bar.focusWidget(), bar.nameEdit());

    // Tab -> lengthEdit
    QKeyEvent tab1(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.nameEdit(), &tab1);
    QCOMPARE(bar.focusWidget(), bar.lengthEdit());

    // Tab -> angleEdit
    QKeyEvent tab2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.lengthEdit(), &tab2);
    QCOMPARE(bar.focusWidget(), bar.angleEdit());

    // Tab -> nameEdit (cycle)
    QKeyEvent tab3(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.angleEdit(), &tab3);
    QCOMPARE(bar.focusWidget(), bar.nameEdit());

    // Backtab -> angleEdit
    QKeyEvent btab1(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QCoreApplication::sendEvent(bar.nameEdit(), &btab1);
    QCOMPARE(bar.focusWidget(), bar.angleEdit());

    // Return in nameEdit -> lengthEdit
    bar.focusFirstNameField();
    QKeyEvent ret1(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.nameEdit(), &ret1);
    QCOMPARE(bar.focusWidget(), bar.lengthEdit());

    // Return in lengthEdit -> angleEdit
    QKeyEvent ret2(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.lengthEdit(), &ret2);
    QCOMPARE(bar.focusWidget(), bar.angleEdit());

    // Return in angleEdit -> canvasView
    QKeyEvent ret3(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.angleEdit(), &ret3);
}

void TestSmartPenAux::preInputBarEscClearsAndReturnsFocus()
{
    QWidget dummyCanvas;
    cad::app::SmartPenPreInputBar bar;
    bar.setCanvasView(&dummyCanvas);

    bar.nameEdit()->setText("test_line");
    bar.lengthEdit()->setText("10");
    bar.focusFirstNameField();
    QCOMPARE(bar.focusWidget(), bar.nameEdit());

    // First Esc clears nameEdit text
    QKeyEvent esc1(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.nameEdit(), &esc1);
    QCOMPARE(bar.nameText().isEmpty(), true);

    // Esc on empty field clears all fields
    bar.focusFirstNameField();
    QKeyEvent esc2(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(bar.nameEdit(), &esc2);
    QCOMPARE(bar.lengthText().isEmpty(), true);
}

void TestSmartPenAux::preInputBarFullTypingWithShortcutLetters()
{
    cad::app::SmartPenPreInputBar bar;
    bar.show();

    bar.focusFirstNameField();
    // Simulate typing "V_collar_L1" which has V and L (tool shortcuts)
    QTest::keyClicks(bar.nameEdit(), "V_collar_L1");
    QCOMPARE(bar.nameText(), QStringLiteral("V_collar_L1"));

    bar.focusLengthField();
    // Simulate typing formula "C+R/2" which has C and R
    QTest::keyClicks(bar.lengthEdit(), "C+R/2");
    QCOMPARE(bar.lengthText(), QStringLiteral("C+R/2"));

    bar.focusAngleField();
    // Simulate typing formula "A_angle-90" which has A
    QTest::keyClicks(bar.angleEdit(), "A_angle-90");
    QCOMPARE(bar.angleText(), QStringLiteral("A_angle-90"));
}

// ---------------------------------------------------------------------------
// 落点确认 (stacked-point disambiguation, 2026-08): 辅助层激活时工作层点
// 仍是合法捕捉目标（单向参照契约），但同点堆叠时默认取活动层点；不满意
// 可点选候选线段切换落点。终点的多候选堆叠进入 ConfirmEnd 确认态。
// ---------------------------------------------------------------------------

/// Two blocks starting at the SAME origin: a working-layer horizontal line
/// and an aux-layer vertical line — an exact cross-layer stack at (0,0).
void TestSmartPenAux::stackedStartPicksActiveLayerPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));   // aux layer ACTIVE
    CanvasScene scene(&doc);
    const LineEx w = makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);  // working (0,0)→(100,0)
    const LineEx a = makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);  // aux    (0,0)→(0,60)
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Click the stacked spot: findSnap resolves the exact tie to the ACTIVE
    // layer and the leader auto-pick agrees (candidate on the snapped
    // point), so the attachment targets the AUX point — not the working
    // block created first (the old traversal-order trap).
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);

    // Free end → commit.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(50.0, 50.0));
    pen.mousePress(&press2);

    QCOMPARE(doc.blocks().size(), size_t(3));   // working + aux + new line
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.toBlockId, a.blockId);
    QCOMPARE(att.toPointId, a.startId);
    QCOMPARE(doc.blocks().back().layer, layerIdAt(doc, 0));  // 新线归活动层
    pen.deactivate();
}

void TestSmartPenAux::stackedStartSwitchByClickingLine()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));   // aux layer ACTIVE
    CanvasScene scene(&doc);
    const LineEx w = makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);  // working
    makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);                   // aux
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Start on the stacked spot → active-layer (aux) point.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);

    // 先选活动层，不满意再点线: clicking the WORKING line's body switches
    // the START POINT to the working point (a legal, stacked candidate) —
    // the click is NOT a placement.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(50.0, 0.0));
    pen.mousePress(&press2);
    QCOMPARE(doc.blocks().size(), size_t(2));   // still no line committed

    // Free end → the attachment now targets the WORKING point.
    QGraphicsSceneMouseEvent press3(QEvent::GraphicsSceneMousePress);
    makePress(press3, QPointF(50.0, 50.0));
    pen.mousePress(&press3);

    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.toBlockId, w.blockId);
    QCOMPARE(att.toPointId, w.startId);
    pen.deactivate();
}

void TestSmartPenAux::stackedEndConfirmClickLinePicksWorkingPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));   // aux layer ACTIVE
    CanvasScene scene(&doc);
    const LineEx w = makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);  // working
    makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);                   // aux
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // Free start at blank space.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(200.0, 200.0));
    pen.mousePress(&press1);

    // End click on the stacked spot: TWO candidates → ConfirmEnd state.
    // Nothing is committed by this click.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(0.0, 0.0));
    pen.mousePress(&press2);
    QCOMPARE(doc.blocks().size(), size_t(2));
    QCOMPARE(doc.attachments().size(), size_t(0));

    // Click the WORKING line's body → confirm THAT point; the free start
    // flips: new line starts on the working point, ends at the old start.
    QGraphicsSceneMouseEvent press3(QEvent::GraphicsSceneMousePress);
    makePress(press3, QPointF(50.0, 0.0));
    pen.mousePress(&press3);

    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.toBlockId, w.blockId);
    QCOMPARE(att.toPointId, w.startId);

    const Block& nb = doc.blocks().back();
    const Segment& ns = nb.segments.front();
    const ParamPoint* sp = nb.findPoint(ns.startPointId);
    QVERIFY(sp && sp->resolved);
    QVERIFY(nb.transform.toWorld(sp->resolvedPos).distanceTo(Vec2::zero()) < 1e-6);
    const ParamPoint* ep = nb.findPoint(ns.endPointId);
    QVERIFY(ep && ep->resolved);
    QVERIFY(nb.transform.toWorld(ep->resolvedPos).distanceTo(Vec2(200.0, 200.0)) < 1e-6);
    pen.deactivate();
}

void TestSmartPenAux::stackedEndConfirmBlankAcceptsActiveLayerDefault()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));   // aux layer ACTIVE
    CanvasScene scene(&doc);
    const LineEx a = makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);  // aux
    makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);                   // working
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(200.0, 200.0));
    pen.mousePress(&press1);

    // End click on the stacked spot → ConfirmEnd.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(0.0, 0.0));
    pen.mousePress(&press2);
    QCOMPARE(doc.blocks().size(), size_t(2));

    // Blank click accepts the DEFAULT = the active (aux) layer's point.
    QGraphicsSceneMouseEvent press3(QEvent::GraphicsSceneMousePress);
    makePress(press3, QPointF(300.0, 300.0));
    pen.mousePress(&press3);

    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.toBlockId, a.blockId);
    QCOMPARE(att.toPointId, a.startId);
    pen.deactivate();
}

void TestSmartPenAux::stackedEndConfirmEscCancelsStroke()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));   // aux layer ACTIVE
    CanvasScene scene(&doc);
    makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);  // working
    makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);  // aux
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(200.0, 200.0));
    pen.mousePress(&press1);

    // End click on the stacked spot → ConfirmEnd.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(0.0, 0.0));
    pen.mousePress(&press2);
    QCOMPARE(doc.blocks().size(), size_t(2));

    // Esc cancels the whole stroke — nothing is created.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    pen.keyPress(&esc);
    QCOMPARE(doc.blocks().size(), size_t(2));
    QCOMPARE(doc.attachments().size(), size_t(0));

    // The pen is back in Idle: a fresh blank press starts a new stroke
    // instead of being swallowed by a stale confirm state.
    QGraphicsSceneMouseEvent press3(QEvent::GraphicsSceneMousePress);
    makePress(press3, QPointF(300.0, 300.0));
    pen.mousePress(&press3);
    QGraphicsSceneMouseEvent press4(QEvent::GraphicsSceneMousePress);
    makePress(press4, QPointF(350.0, 300.0));
    pen.mousePress(&press4);
    QCOMPARE(doc.blocks().size(), size_t(3));   // the fresh stroke committed
    pen.deactivate();
}

void TestSmartPenAux::workingActiveStackedStartPicksWorkingPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));   // working layer ACTIVE (aux grayed)
    CanvasScene scene(&doc);
    const LineEx w = makeLineBlockEx(doc, 1, Vec2::zero(), 100.0, 0.0);  // working
    makeLineBlockEx(doc, 0, Vec2::zero(), 60.0, 90.0);                   // aux
    doc.resolveAll();

    QGraphicsView view(&scene);
    cad::tools::ToolSmartPen pen;
    pen.activate(scene, &doc);

    // The stacked spot: the grayed aux point is NOT a snap target (one-way
    // rule); the working point is the only candidate.
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    makePress(press1, QPointF(0.0, 0.0));
    pen.mousePress(&press1);

    // Clicking the GRAYED aux line during the rubber band must NOT switch
    // the start (its point is not a legal attachment target) — the click is
    // ignored, no line is placed.
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    makePress(press2, QPointF(0.0, 30.0));
    pen.mousePress(&press2);
    QCOMPARE(doc.blocks().size(), size_t(2));

    // Free end → attachment targets the working point.
    QGraphicsSceneMouseEvent press3(QEvent::GraphicsSceneMousePress);
    makePress(press3, QPointF(50.0, 50.0));
    pen.mousePress(&press3);

    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment& att = doc.attachments().front();
    QCOMPARE(att.toBlockId, w.blockId);
    QCOMPARE(att.toPointId, w.startId);
    pen.deactivate();
}

QTEST_MAIN(TestSmartPenAux)
#include "test_smartpen_aux.moc"
