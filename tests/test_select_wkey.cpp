#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QStandardItemModel>
#include <QMenu>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "canvas/HudItem.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "ElaText.h"
#include "ElaPushButton.h"
#include "ui/LinePropertyDialog.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentRefCard.h"
#include "ui/PointRefEdit.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"   // ReverseSegmentCommand (换向不解耦验证)
#include "parametric/ParamDocument.h"
#include "parametric/FormulaVariable.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "TestHelpers.h"
#include "geometry/CurveMath.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::layerIdAt;

/// End-to-end regression test for the select tool's W key (多选 ↔ 单选 toggle):
/// a REAL QKeyEvent travels CanvasView::keyPressEvent → ToolManager::dispatch
/// → ToolSelect::keyPress, and the resulting mode change is verified through
/// the observable click behaviour (Single replaces, Multi adds).
class TestSelectWKey : public QObject
{
    Q_OBJECT

private slots:
    void wTogglesMultiSelectionThroughFullEventChain();
    void ctrlDragCopiesAfterConfirm();
    void ctrlDragUnselectedBlockDirectly();
    void ctrlClickJitterDoesNotDuplicate();
    void ctrlDragUndoRedo();
    void multiSelectMarqueeSelectsBothLines();
    void endpointClickAfterConfirmKeepsSelectionOperable();
    void endpointDragConnectsToTargetEndToEnd();
    void clickSelectsWithoutDragging();
    void singleModeOverlapPrefersSelectedSegmentPoint();
    void singleModeOverlapWinsEvenWhenOtherPointCloser();
    void multiModeOverlapEntersSourceConfirm();
    void unselectedEndpointPressFallsBackToSelect();
    void bodyDragMovesLine();
    void quickDetachKeyD();
    void angleOnlyEndpointDragReconnects();
    // ── 有对齐点方向 + 变量时接入新线段不覆盖变量 (2026-xx 用户报告:
    //    有对齐点方向时接入新线段 = 仅连接, 原对齐点方向与变量不变,
    //    回车 = 确定连接而非确定角度) ──
    void angleRefWithFormulaReattachKeepsFormula();
    // ── 连接角度会话 (二期: 角度输入并入上下文属性条, AngleHud 退场) ──
    void angleSessionStripInputDrivesConnection();
    // ── 重叠线段消歧 (2026-10) ──
    void overlapHoverShowsClusterHint();
    void overlapClickCyclesWithWKey();
    void overlapPickCandidateByIndex();
    // ── 曲线命中/选择 (2026-10 用户报告: 选择工具对曲线的选中判定比较迷) ──
    void curveBodyClickSelects();
    void curveBodyDragMoves();
    // ── 双击打开线条属性面板: 交叉点处活跃层优先 (2026-11 用户报告:
    //    辅助层线段双击时灵时不灵) ──
    void doubleClickCrossingPrefersActiveLayer();
    // ── 连接卡片两维独立 (2026-xx 用户拍板: 双 拆开/重连 按钮) ──
    void connectionCardNewSemantics();
    // ── 角度基准两点化 — 点2 输入框 (2026-08-31 修复「点2 完全无效」) ──
    void angleRefPoint2TwoPointBasis();
    // ── 对齐点可输入 + 自动态两点回填 (2026-09 设计修正) ──
    void alignPointEditableAndAutoStateTwoPointBackfill();
    // ── 终点指向超出延伸 (2026-09 规则表 ⑤): 指定长度 > 目标距离时终点
    //    越过目标点、方向保持指向角 ──
    void endTargetOvershootExtendsAlongAim();
    // ── 终点连接行 (2026-xx 每端完整连接: 起点 Attachment + 终点 endTarget) ──
    void connectionCardEndConnection();
    // ── 选择工具多选移动图层 ──
    void multiSelectMoveToLayer();
    void multiSelectRightClickMoveToLayerMenu();
};

/// Strongly-curved Bezier block (off-chord anchor 45mm) on the active layer.
/// The control polygon (start→anchor→end) deviates a lot from the actual
/// drawn curve — a pick region based on it misses clicks on the curve body.
struct CurveSetup {
    QUuid blockId;
    QUuid segId;
};

CurveSetup makeCurveBlock(ParamDocument& doc)
{
    Block block;
    block.transform.origin = Vec2::zero();
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid p1Id = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1Id;
    p2.distance = 120.0;
    p2.angle = 0.0;
    QUuid p2Id = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    seg.tension = 0.0;
    QUuid segId = seg.id;

    ParamPoint pp;
    pp.constraint = PointConstraint::CurveAnchor;
    pp.hostSegmentId = segId;
    pp.interpPercent = 0.5;
    pp.interpOffsetDist = 90.0;   // 90mm off-chord: strong curvature
    pp.autoTangent = true;
    QUuid ppId = pp.id;

    block.addPoint(std::move(pp));
    seg.passPointIds = {ppId};
    QUuid blockId = block.id;
    block.addSegment(std::move(seg));
    doc.addBlock(std::move(block));
    doc.resolveAll();
    return {blockId, segId};
}

/// Create a straight line block on an EXPLICIT layer (addBlock with a null
/// layer would land on the first working layer, never the aux calc layer):
/// Free start at @p origin, Polar end (angleDeg, lenMm). Returns block id.
QUuid addLineOnLayer(ParamDocument& doc, const QUuid& layer,
                     const Vec2& origin, double angleDeg, double lenMm)
{
    Block block;
    block.layer = layer;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid s = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = s;
    p2.distance = lenMm;
    p2.angle = angleDeg;
    QUuid e = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = s;
    seg.endPointId = e;
    block.addSegment(std::move(seg));

    QUuid id = block.id;
    doc.addBlock(std::move(block));
    return id;
}

/// Curve point at parametric position t (0..spanCount of the WHOLE curve), world coords.
Vec2 curvePointAt(const Block& b, const QUuid& segId, double t){
    const auto* entry = b.curveSpanEntry(segId);
    Q_ASSERT(entry && !entry->spans.empty());
    return b.transform.toWorld(cad::geo::evalCurve(entry->spans, t));
}

/// 数值采样整条曲线, 返回“距控制折线最远”的曲线点 (transform 必须为恒等 —
/// 测试夹具的块都在原点). 那正是旧“控制折线命中区”漏掉的位置: 点在曲线上
/// 但距折线边 > 8px → items() 命中失败.
Vec2 curveWorstHitPoint(const Block& b, const QUuid& segId, double* distOut = nullptr)
{
    const auto* entry = b.curveSpanEntry(segId);
    Q_ASSERT(entry && entry->spans.size() >= 2);
    const auto& anchors = entry->anchors;
    auto distToSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        const Vec2 d = s1 - s0;
        const double l2 = d.lengthSquared();
        if (l2 < 1e-12) return p.distanceTo(s0);
        const double t = std::clamp((p - s0).dot(d) / l2, 0.0, 1.0);
        return p.distanceTo(s0 + d * t);
    };
    const double spanCount = static_cast<double>(entry->spans.size());
    Vec2 best = curvePointAt(b, segId, 0.5);
    double bestD = -1.0;
    for (int k = 1; k < 200; ++k) {
        const Vec2 p = curvePointAt(b, segId, spanCount * static_cast<double>(k) / 200.0);
        double d = 1e18;
        for (size_t i = 0; i + 1 < anchors.size(); ++i)
            d = std::min(d, distToSeg(p, anchors[i], anchors[i + 1]));
        if (d > bestD) { bestD = d; best = p; }
    }
    if (distOut) *distOut = bestD;
    return best;
}

void TestSelectWKey::wTogglesMultiSelectionThroughFullEventChain()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));  // makeLine blocks land on the first working layer
    CanvasScene scene(&doc);

    // Two horizontal lines, stacked vertically: A at y=0, B at y=-50.
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);        // default active tool is Select
    view.setInputDispatcher(&tm);

    // Map user coords (+Y up) → viewport pixels via the real view transform.
    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto pressW = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* itemA = scene.findBlockItem(a.blockId);
    auto* itemB = scene.findBlockItem(b.blockId);
    QVERIFY(itemA);
    QVERIFY(itemB);

    // Diagnostics: the clicked viewport point must actually hit a block item.
    {
        const QPointF scenePt = view.mapToScene(vp(50.0, 0.0));
        const auto hits = scene.items(scenePt);
        qInfo("click(50,0) -> scene(%g,%g), %d items", scenePt.x(), scenePt.y(),
              int(hits.size()));
        for (QGraphicsItem* it : hits)
            qInfo("  item: %s", it->type() == BlockItem::Type ? "BlockItem" : "other");
    }

    // Default mode is Single: clicking B replaces the selection of A.
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(!itemA->toolSelected());   // Single replaced A

    // W toggles to Multi: the current selection SURVIVES the toggle, so the
    // user can keep adding to it (regression: it used to be cleared, which
    // made W look broken — picking one block then W then another lost the
    // first one).
    pressW();
    QVERIFY(itemB->toolSelected());    // preserved across the toggle
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());    // added
    QVERIFY(itemB->toolSelected());    // B still selected

    // W again toggles back to Single: the multi-set is dropped (single-click
    // replace semantics) and clicking B replaces the selection.
    pressW();
    QVERIFY(!itemA->toolSelected());
    QVERIFY(!itemB->toolSelected());   // Multi → Single clears the set
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(!itemA->toolSelected());   // Single replaced again
}

// Ctrl+drag must copy from a SELECTED line (2026-09 取消确认基准: 选中即
// 就绪, 无需右键确认). Regression: the gesture used to require the Confirm
// state; the whole chain must work end-to-end.
void TestSelectWKey::ctrlDragCopiesAfterConfirm()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);   // endCopyDrag commits via the undo stack
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) Click to select the line (Selecting state — 选中即就绪).
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(doc.blocks().size(), size_t(1));

    // 2) Ctrl+press on the selected line and drag +120 mm right → copy fires.
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(80.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(170.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(170.0, 0.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    const Block* orig = doc.findBlock(a.blockId);
    const Block* clone = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { clone = &b; break; }
    QVERIFY(clone);
    // Clone landed at origin + (120, 0): same shape, new position.
    QVERIFY(clone->transform.origin.distanceTo(Vec2(120.0, 0.0)) < 1e-6);
    QVERIFY(orig->segments.size() == clone->segments.size());
}

// 未选中的线段直接按住 Ctrl 拖拽：必须直接发起快捷复制，原图不动且生成副本
void TestSelectWKey::ctrlDragUnselectedBlockDirectly()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 此时线段 A 完全未选中
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());

    // 直接 Ctrl+按住在未选线段上并向右拖动 100mm
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 0.0), Qt::LeftButton, Qt::ControlModifier);

    // 成功复制为两条
    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* orig = doc.findBlock(a.blockId);
    QVERIFY(orig);
    // 原线段位置未被破坏移动 (原点仍为 0,0)
    QVERIFY(orig->transform.origin.distanceTo(Vec2::zero()) < 1e-6);

    const Block* clone = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { clone = &b; break; }
    QVERIFY(clone);
    QVERIFY(clone->transform.origin.distanceTo(Vec2(100.0, 0.0)) < 1e-6);
}

// 按住 Ctrl 在图元上微颤点击 (< 5px 屏幕抖动)：视为误触，不生成幽灵死线
void TestSelectWKey::ctrlClickJitterDoesNotDuplicate()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 按住 Ctrl 点击，但只有 1 像素屏幕位移 (模拟手抖微颤)
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, hit + QPoint(1, 0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, hit + QPoint(1, 0), Qt::LeftButton, Qt::ControlModifier);

    // 不应生成新副本，仍只有 1 个 block
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(stack.count(), 0);
}

// 快捷复制的 Undo / Redo 回放与撤销
void TestSelectWKey::ctrlDragUndoRedo()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 快捷复制
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 0.0), Qt::LeftButton, Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.undoStack() != nullptr);
    QCOMPARE(doc.undoStack()->count(), 1);

    // 撤销
    doc.undoStack()->undo();
    QCOMPARE(doc.blocks().size(), size_t(1));

    // 重做
    doc.undoStack()->redo();
    QCOMPARE(doc.blocks().size(), size_t(2));
}

// 多选模式下空白处拖拽必须显示并应用框选（回归：手势提取时丢失了
// setState(Marquee)，导致 mouseMove/mouseRelease 不进 Marquee 分支——
// 框选框不更新、释放不应用，表现为“W 进多选后框选消失”）。
void TestSelectWKey::multiSelectMarqueeSelectsBothLines()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton
                                                                 : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* itemA = scene.findBlockItem(a.blockId);
    auto* itemB = scene.findBlockItem(b.blockId);
    QVERIFY(itemA);
    QVERIFY(itemB);

    // W → 多选模式，然后从空白处（左上方）拖框到右下，覆盖两条线。
    QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    QApplication::sendEvent(&view, &key);
    // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
    // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);

    const QPoint start = vp(-150.0, 120.0);   // empty space above both lines
    const QPoint end   = vp(220.0, -180.0);    // covers A (y=0) and B (y=-50)
    sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, end, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoModifier);

    // Both lines are selected by the marquee toggle.
    QVERIFY(itemA->toolSelected());
    QVERIFY(itemB->toolSelected());
}

// 回归（用户报告：选中线段后再次点击线段上的点，线段卡进莫名状态、
// 无法移动，画布残留一个莫名的圆点）：根因是 ConnectGesture 内部状态
// 从不与工具状态同步 —— beginConnect 只把工具状态推进到 Connecting，
// 手势自己的 m_state 停在 Idle，active() 恒为 false，mouseRelease 不再
// 路由，连接手势永远无法收尾：工具状态卡死在 Connecting、源点光环
// （z=9998 的圆环）留在画布上。修复后一次点击端点（不拖动）应立即
// 回到 Confirmed、不残留任何覆盖物、线段仍可正常拖动。
void TestSelectWKey::endpointClickAfterConfirmKeepsSelectionOperable()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);   // (0,0)-(100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    // 连接手势覆盖物 = 源点光环(9998)/吸附环(9999)：计数作为"无残留"判据。
    auto overlayCount = [&]() {
        int n = 0;
        for (QGraphicsItem* it : scene.items())
            if (it->zValue() >= 9998.0) ++n;
        return n;
    };

    // 1) 点击选中 (2026-09 取消确认基准, 无右键确认)。
    const QPoint body = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoModifier);

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    const int overlaysBefore = overlayCount();

    // 2) 再次点击线段端点 (0,0)，不拖动。
    const QPoint ep = vp(0.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, ep, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, ep, Qt::LeftButton, Qt::NoModifier);

    // 3) 状态必须回到 Selecting（而不是卡死在 Connecting），
    //    画布上不得残留连接手势的圆点/圆环。
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    // 覆盖物计数 = 连接收尾的可观测判据; 整批 ctest 负载下曾偶发不稳 →
    // waitUntil 锁定判据(同步清理时立即返回)。
    QVERIFY2(cad::test::waitUntil([&] { return overlayCount() == overlaysBefore; }),
             "连接手势收尾后覆盖物应清零");

    // 4) 线段仍可正常拖动：按住线身拖 +20mm。
    const QPoint dragStart = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, dragStart, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(70.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(70.0, 0.0), Qt::LeftButton,
              Qt::NoModifier);
    const Block* blk = doc.findBlock(a.blockId);
    QVERIFY(blk);
    // 登记过的偶发点: 拖拽后坐标断言(整批 ctest 负载下"效果未落定") → waitUntil 锁定。
    QVERIFY2(cad::test::waitUntil([&] {
        const Block* b = doc.findBlock(a.blockId);
        return b && b->transform.origin.distanceTo(Vec2(20.0, 0.0)) < 1e-6;
    }), "拖拽 +20mm 后 origin 应为 (20,0)");
}

// 回归：确认后从端点拖到另一条线的端点必须真正建立连接（同一根因
// 曾让整个连接手势失效：拖动中块不跟随、释放不吸附、状态卡死）；且
// 连接收尾后手势内部状态必须复位，否则后续点击被吞（无法再选中）。
void TestSelectWKey::endpointDragConnectsToTargetEndToEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0}); // (0,-50)-(100,-50)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    // 连接角度会话记录器 (二期): 手势经 ToolHost 上报会话开始/结束。
    QUuid sessBlock, sessSeg, beginAtt;
    double sessAngle = 0.0;
    int sessionReports = 0;
    connect(&tm, &cad::tools::ToolManager::connectAngleSessionChanged,
            &tm, [&](const QUuid& bid, const QUuid& sid, const QUuid& aid, double a) {
                ++sessionReports;
                if (!aid.isNull()) { sessBlock = bid; sessSeg = sid; beginAtt = aid; sessAngle = a; }
            });

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 点击选中 A (2026-09 取消确认基准, 无右键确认)。
    const QPoint body = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, body, Qt::LeftButton, Qt::NoModifier);

    // 2) 从 A 的右端点 (100,0) 拖到 B 的右端点 (100,-50)。
    const QPoint from = vp(100.0, 0.0);
    const QPoint to   = vp(100.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(105.0, -15.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(105.0, -40.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoModifier);

    // 3) 连接必须已建立：1 条 attachment，A 的端点吸到 B 的端点上。
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Block* ba = doc.findBlock(a.blockId);
    const Block* bb = doc.findBlock(b.blockId);
    QVERIFY(ba && bb);
    QVERIFY(ba->worldPos(a.endId).distanceTo(bb->worldPos(b.endId)) < 1e-6);

    // 4) 手势进入角度输入，会话经 ToolHost 上报 (条带显示跟随线段)。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    QVERIFY2(sessionReports >= 1, "进入 AngleInput 必须上报连接角度会话");
    QVERIFY2(!beginAtt.isNull(), "会话必须携带被调角度的附件 id");
    QCOMPARE(sessBlock, a.blockId);
    QVERIFY(!sessSeg.isNull());
    QVERIFY(std::abs(sessAngle) < 1e-6 || std::abs(sessAngle - 180.0) < 1e-6);

    // 5) Esc 收尾（保留连接、角度回退到保向初值），回到 Idle。
    ts->connectAngleCancelled();   // 条带 Esc → 工具 → 手势
    QTest::qWait(30);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY2(sessionReports >= 2, "收尾后必须上报会话结束 (全 null)");

    // 6) 连接结束后点击不得被残留手势吞掉（回归：手势内部状态若不复位，
    //    active() 恒真，后续左键全部被 mousePress 的连接分支拦截）。
    const QPoint bodyB = vp(50.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, bodyB, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, bodyB, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
}

// 2026-09 取消确认基准: 单击线身 (press+release 无移动) = 只选中不拖动;
// 单选模式第二次点击另一条线 = 替换选择.
void TestSelectWKey::clickSelectsWithoutDragging()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0}); // (0,-50)-(100,-50)
    doc.resolveAll();
    const cad::geo::Vec2 a0 = doc.findBlock(a.blockId)->transform.origin;

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 单击 A 线身: 只选中 A (B 不选), A 未移动 (拖动未触发).
    click(50.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(doc.findBlock(a.blockId)->transform.origin.distanceTo(a0) < 1e-9);

    // 单选: 再单击 B 线身 → 选择替换为 B.
    click(50.0, -50.0);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());
}

// ─────────────────────────────────────────────────────────────────────────────
// 用户设计提案（2026-09）：单选模式下「先点选线段、再点击重叠部位」时，
// 重叠点集合中属于当前选中线段的端点必须默认胜出 —— 连接源点 = 选中线段的
// 所属点，而不是遍历序/最近点惯性。两线端点精确重叠于 (100,0)：
// 选 A → 从 A 端点发起连接；选 B → 从 B 端点发起。
// ─────────────────────────────────────────────────────────────────────────────
void TestSelectWKey::singleModeOverlapPrefersSelectedSegmentPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{100.0, 0.0}); // (100,0)-(200,0): start 与 A.end 精确重叠
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto click = [&](double x, double y) {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoModifier);
    };
    auto pressEsc = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    auto* itemA = scene.findBlockItem(a.blockId);
    auto* itemB = scene.findBlockItem(b.blockId);
    QVERIFY(itemA && itemB);
    const Block* blkA = doc.findBlock(a.blockId);
    const Block* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkB);

    const Vec2 a0 = blkA->transform.origin;  // (0,0)
    const Vec2 b0 = blkB->transform.origin;  // (100,0)
    const QPoint j = vp(100.0, 0.0);         // 重叠点

    // ── 阶段1: 单选选中 A → 点击重叠点 → 连接从 A 的端点发起 (A 跟随光标) ──
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());
    sendMouse(QEvent::MouseButtonPress, j, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseMove, vp(140.0, 45.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY2(blkA->transform.origin.distanceTo(a0 + Vec2(40.0, 45.0)) < 1e-6,
             "选 A 后点重叠点, 连接源点应是 A 的端点 (A 跟随光标)");
    QVERIFY2(blkB->transform.origin.distanceTo(b0) < 1e-6,
             "重叠点处 B 的端点不得抢走源点 (B 不应动)");
    pressEsc();   // 取消手势, 恢复原位
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    QVERIFY(blkA->transform.origin.distanceTo(a0) < 1e-6);

    // ── 阶段2: 单选改选 B → 点同一重叠点 → 连接从 B 的端点发起 (B 跟随) ──
    click(150.0, 0.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(!itemA->toolSelected());
    sendMouse(QEvent::MouseButtonPress, j, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseMove, vp(140.0, 45.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY2(blkB->transform.origin.distanceTo(b0 + Vec2(40.0, 45.0)) < 1e-6,
             "选 B 后点重叠点, 连接源点应是 B 的端点 (B 跟随光标)");
    QVERIFY2(blkA->transform.origin.distanceTo(a0) < 1e-6,
             "A 不应动");
    pressEsc();
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
}

// 更强的惯性对抗断言: B 的起点略微偏离 A 的终点 (100.6, 0.4), 光标精确落在
// B 的起点上 —— 但选中 A 时仍必须从 A 的端点发起 (选中线段的点 > 最近点惯性);
// 选中 B 时即使光标精确落在 A 的端点上, 仍从 B 的端点发起。
void TestSelectWKey::singleModeOverlapWinsEvenWhenOtherPointCloser()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                          // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{100.6, 0.4});        // start=(100.6,0.4) 偏离 A.end
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto click = [&](double x, double y) {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoModifier);
    };
    auto pressEsc = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    const Block* blkA = doc.findBlock(a.blockId);
    const Block* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkB);
    const Vec2 a0 = blkA->transform.origin;   // (0,0)
    const Vec2 b0 = blkB->transform.origin;   // (100.6,0.4)

    // 选 A → 光标精确落在 B 的起点 (100.6,0.4) → 仍从 A 端点发起 (A 跟随)
    click(50.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    const QPoint onB = vp(100.6, 0.4);
    sendMouse(QEvent::MouseButtonPress, onB, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseMove, vp(160.0, 60.0), Qt::NoButton, Qt::NoModifier);
    // grabOffset = a0 − A.end = (−100,0); anchor=(160,60) → origin=(60,60)
    QVERIFY2(blkA->transform.origin.distanceTo(Vec2(60.0, 60.0)) < 1e-6,
             "光标在 B 点上时选中线 A 的点仍必须胜出 (A 跟随)");
    QVERIFY(blkB->transform.origin.distanceTo(b0) < 1e-6);
    pressEsc();
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    QVERIFY(blkA->transform.origin.distanceTo(a0) < 1e-6);

    // 选 B → 光标精确落在 A 的终点 (100,0) → 仍从 B 起点发起 (B 跟随)
    click(150.6, 0.4);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    const QPoint onA = vp(100.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, onA, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseMove, vp(160.0, 60.0), Qt::NoButton, Qt::NoModifier);
    // grabOffset = b0 − B.start = (0,0); anchor=(160,60) → origin=(160,60)
    QVERIFY2(blkB->transform.origin.distanceTo(Vec2(160.0, 60.0)) < 1e-6,
             "光标在 A 点上时选中线 B 的点仍必须胜出 (B 跟随)");
    QVERIFY(blkA->transform.origin.distanceTo(a0) < 1e-6);
    pressEsc();
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
}

// 多选模式对照组: 两个已选线段的端点重叠 → 进入 ConfirmSource (点选候选线段
// 确认从哪个端点发起连接) —— 现有的"点选线段选点"机制 (与单选模式的
// "选中线段即默认源点"互为补充)。
void TestSelectWKey::multiModeOverlapEntersSourceConfirm()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{100.0, 0.0}); // (100,0)-(200,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto click = [&](double x, double y) {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoModifier);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);

    // W → 多选; 点 A、B 线身加入选择集
    QKeyEvent keyW(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    QApplication::sendEvent(&view, &keyW);
    // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
    // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);
    click(50.0, 0.0);
    click(150.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 两个已选线段的端点都叠在 (100,0) → press 进入 ConfirmSource
    const QPoint j = vp(100.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, j, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::ConfirmSource);
    sendMouse(QEvent::MouseButtonRelease, j, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::ConfirmSource);  // 等待点选, 不直接发起

    // Esc 取消 → 回 Selecting, 选择集保留
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
    // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
}

// 2026-09 取消确认基准: 按住线身移动即拖动 (无需右键确认).
// 锚点 = press 位置, 与旧"press 即拖"位移语义一致.
// 回归 (修复 2026-09 WIP 重构引入的崩溃): 单选默认模式下, 已有选中时按下
// 未选线段的端点 — 端点候选全部被"选中块"过滤 → 缺守卫时 validCands.front()
// 在空 vector 上断言 (MSVC: front() called on empty vector). 修复: 无合法候选
// 退回普通选中 (未选线段的端点按下 = 普通线段选中); 已选线段的端点仍发起连接.
void TestSelectWKey::unselectedEndpointPressFallsBackToSelect()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                    // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});  // (0,-50)-(100,-50): 端点与 A 不重叠
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto click = [&](double x, double y) {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoModifier);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);

    // 单选模式: 先选 A.
    click(50.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());

    // 按未选线段 B 的端点附近 (抓取半径内, 端点候选被过滤为空) → 修复前崩溃;
    // 现在 = 普通选中 B (逐端点取点会落在端点标记上 miss 线身, 取 95mm 处).
    click(95.0, -50.0);
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());

    // 对照: 再按已选 B 的端点 → 正常发起连接手势 (无回归).
    sendMouse(QEvent::MouseButtonPress, vp(100.0, -50.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, -50.0), Qt::LeftButton, Qt::NoModifier);
    // release 同步收尾连接手势(状态翻转已在 sendEvent 内完成); 无统一可观测条件, 暂留排空。
    QTest::qWait(20);
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);
}

void TestSelectWKey::bodyDragMovesLine()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);   // (0,0)-(100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 按住 A 线身移动: press(50,0) → move(80,0) 超阈值触发拖动 (锚点=50)
    // → move(150,0) delta=100 → release(150,0) 提交 → origin=(100,0).
    const QPoint body = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(80.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 0.0), Qt::LeftButton,
              Qt::NoModifier);

    QVERIFY(doc.findBlock(a.blockId)->transform.origin
                .distanceTo(Vec2(100.0, 0.0)) < 1e-6);
    // 拖动结束清选回 Idle (endDrag clearSelectionAndIdle).
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());
}

// ---------------------------------------------------------------------------
// 曲线点击选择 (2026-10 用户报告: 选择工具对曲线判定比较迷).
// 命中断言点必须取在“实际绘制的曲线”上, 并远离控制折线 — 旧实现命中区
// 用控制折线 (shapePath), 强弯曲曲线偏离控制折线十几毫米, 点在曲线上会被
// 漏掉 (或点在折线附近就算命中).
// ---------------------------------------------------------------------------
void TestSelectWKey::curveBodyClickSelects()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto [blockId, segId] = makeCurveBlock(doc);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 曲线参数 T=0.5 处 (第一段跨度的中点): 弧腹点在控制折线内腹, 距折线边
    // 数毫米~十几毫米, 远超 8px 命中带. 断言该前提, 再点击选中.
    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    const auto* entry = blk->curveSpanEntry(segId);
    QVERIFY(entry && entry->spans.size() == 2);
    double dPolyBest = -1.0;
    const Vec2 hitPt = curveWorstHitPoint(*blk, segId, &dPolyBest);
    qInfo().noquote() << QStringLiteral("curve hit point (%1,%2) dist-to-control-polygon=%3")
        .arg(hitPt.x, 0, 'f', 2).arg(hitPt.y, 0, 'f', 2).arg(dPolyBest, 0, 'f', 2);
    QVERIFY2(dPolyBest > 8.0,
             qPrintable(QStringLiteral("测试前提不成立: 曲线点距控制折线只有 %1 mm")
                        .arg(dPolyBest, 0, 'f', 2)));

    click(hitPt.x, hitPt.y);
    QVERIFY2(scene.findBlockItem(blockId)->toolSelected(),
             "点击曲线线身必须选中该块 (命中区 = 实际曲线, 非控制折线)");

    // 低缩放 (view 0.5x): 命中带 = hoverRadiusPx(8) ÷ 0.5 = 16 场景单位,
    // 点击距曲线 12 单位仍在带内; 旧 boundingRect ±10 边距会被场景空间索引
    // (BSP, 按 boundingRect 快速剔除) 直接拒掉 — 曲线在低缩放下更难点中.
    view.scale(0.5, 0.5);
    // scale 同步生效(视图变换即刻改变); 无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);
    {
        const auto* e2 = blk->curveSpanEntry(segId);
        QVERIFY(e2 && !e2->spans.empty());
        const Vec2 p0 = curvePointAt(*blk, segId, 0.5);
        const Vec2 tan = cad::geo::evalCurveDerivative(e2->spans, 0.5);
        const double tlen = std::hypot(tan.x, tan.y);
        QVERIFY(tlen > 1e-9);
        const Vec2 off = p0 + Vec2(-tan.y / tlen, tan.x / tlen) * 12.0;
        click(2000.0, 2000.0);  // 空白处 → 清选
        QVERIFY(!scene.findBlockItem(blockId)->toolSelected());
        click(off.x, off.y);
        QVERIFY2(scene.findBlockItem(blockId)->toolSelected(),
                 "低缩放下命中带内的点击必须选中 (boundingRect 需覆盖 pick band)");
    }
}

// 曲线线身拖动: 点曲线中段按住拖动, 块整体平移 (选中即拖).
void TestSelectWKey::curveBodyDragMoves()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto [blockId, segId] = makeCurveBlock(doc);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const auto* blk = doc.findBlock(blockId);
    const Vec2 hitPt = curveWorstHitPoint(*blk, segId);

    // press on the curve body → move past the drag threshold → commit.
    const QPoint body = vp(hitPt.x, hitPt.y);
    sendMouse(QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(hitPt.x + 30.0, hitPt.y), Qt::NoButton,
              Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(hitPt.x + 60.0, hitPt.y), Qt::NoButton,
              Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(hitPt.x + 60.0, hitPt.y),
              Qt::LeftButton, Qt::NoModifier);

    QVERIFY(doc.findBlock(blockId)->transform.origin
                .distanceTo(Vec2(60.0, 0.0)) < 1e-6);
}

void TestSelectWKey::quickDetachKeyD()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0}); // (0,-50)-(100,-50)
    doc.resolveAll();

    // 建立连接 (用户拍板 2026-08 复旧: 新建连接默认**焊接** → isLocked=true,
    // 拖动保护默认勾选; 拆散走 D 键快拆 / 面板取消勾选)。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    QVERIFY2(doc.attachments().front().isLocked,
             "新建连接默认勾选拖动保护 (焊接)");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 选中 follower B。注意: addAttachment 后 Resolver 已把 B 吸附/旋转
    //    到新位置 (起点吸到 A 的端点) — 取 B 当前解析后的中点点击保证命中。
    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 mid =
        (bb->worldPos(b.startId) + bb->worldPos(b.endId)) * 0.5;
    click(mid.x, mid.y);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) D 键快拆: 解除位置吸附、保留角度跟随 (angleOnly)。
    QKeyEvent keyD(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier);
    QApplication::sendEvent(&view, &keyD);
    // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
    // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);

    // 3) 连接已转为仅角度: 位置自由 + 自动解锁 (位置自由 ↔ 拖动保护互斥)。
    //    影子基准 (DETACH_SHADOW_DESIGN.md §3/§7.1, 2026-xx 翻案活引用语义):
    //    基准换代为隐藏影子块 (master = 本体 A), offset 原样保留 (R2)。
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& after = doc.attachments().front();
    QVERIFY2(after.angleOnly, "D 键应把连接转为仅角度 (拆开保留角度)");
    QVERIFY2(!after.isLocked, "拆开自动清除拖动保护");
    {
        const auto* shadow = doc.blockById(after.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "基准应为影子块 (拆开影子基准)");
        QVERIFY2(shadow->shadowMasterBlockId == a.blockId, "影子 master = 本体 A");
        QVERIFY2(after.followerAngle == 90.0, "offset 原样保留 (R2)");
        QVERIFY2(doc.findBlock(a.blockId) != shadow, "影子与本体无引用关系 (克隆=值拷贝)");
    }

    // 4) undo: 恢复完整连接 (焊接态原样还原 — SetAttachmentAngleOnlyCommand
    //    快照 isLocked=true, undo 回放); 影子随 undo 一并删除 (基准还原本体)。
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& undone = doc.attachments().front();
    QVERIFY2(!undone.angleOnly, "undo 应恢复完整连接");
    QVERIFY2(undone.isLocked,
             "undo 应恢复原来的焊接态 (默认焊, 快照还原不得丢锁)");
    QVERIFY2(undone.toBlockId == a.blockId, "基准还原为本体 (活引用恢复)");
    QVERIFY2(!doc.findShadowOfMaster(a.blockId), "undo 应删除影子块");
}

// 影子挂载路由 (DETACH_SHADOW_DESIGN.md §7.4, 2026-xx 翻案「双基准」语义):
// 线段处于拆开影子基准态 (angleOnly, 基准 = 影子块) 时, 拖动端点重新建立
// 位置连接 → 挂到非本体 = 影子挂载新宿主 (Att1 = 影子→宿主, Δ 反算保向;
// Att2 恢复位置钉点重新焊接) —— 形成 宿主→影子→本线 双连接链 (R3 链式随动)。
// 仅连接语义不变 (followerAngle/公式原样、不进角度会话); undo 整步回拆开态。
void TestSelectWKey::angleOnlyEndpointDragReconnects()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                    // (0,0)-(100,0) 旧基准线
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});  // (0,-50)-(100,-50) 跟随线
    auto c = makeLine(doc, 100.0, Vec2{300.0, -50.0}); // (300,-50)-(400,-50) 新宿主
    doc.resolveAll();

    // B 完整连接 A (B.start 吸到 A.end, 垂直 90°), 再 D 键快拆 → 仅角度
    // (位置自由、角度仍跟随 A = 使用引用线段但没有连接线段)。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QVERIFY(doc.findAttachment(att.id));
    doc.setAttachmentAngleOnly(att.id, true);
    doc.resolveAll();
    const auto* detached = doc.findAttachment(att.id);
    QVERIFY2(detached && detached->angleOnly, "预置: 仅角度 (拆开保留角度)");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // B 起点吸在 A.end (100,0), 垂直 90° → 终点 (100,100)。
    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 bEnd = bb->worldPos(b.endId);
    const cad::geo::Vec2 bMid = (bb->worldPos(b.startId) + bEnd) * 0.5;

    // 1) 选中 B。
    sendMouse(QEvent::MouseButtonPress, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) 从 B 端点 (100,100) 拖到 C 终点 (400,-50)。
    const QPoint from = vp(bEnd.x, bEnd.y);
    const QPoint to   = vp(400.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(200.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoModifier);

    // 3) 影子挂载路由 (DETACH_SHADOW_DESIGN.md §7.4, 2026-xx 翻案双基准):
    //    拖端点释放到 C = 影子挂载 (Att1 = 影子→C, Δ 反算保向) + Att2 恢复
    //    位置钉点重新焊接 —— 形成 C→影子→B 双连接链 (R3 链式随动)。
    //    followerAngle 原样保留 (仅连接, 不反算覆盖)。
    QCOMPARE(doc.attachments().size(), size_t(2));
    const cad::param::Attachment* after = doc.findAttachment(att.id);
    QVERIFY(after);
    QVERIFY2(!after->angleOnly, "释放到目标后应恢复完整位置连接 (Att2)");
    QVERIFY2(after->isLocked, "重挂恢复完整连接应重新焊接 (拖动保护默认勾选)");
    const auto* shadowBlk = doc.blockById(after->toBlockId);
    QVERIFY2(shadowBlk && shadowBlk->isShadow, "Att2 基准仍为影子块 (换代不回退)");
    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a2 : doc.attachments()) {
        if (!a2.isPin && a2.fromBlockId == after->toBlockId) { att1 = &a2; break; }
    }
    QVERIFY2(att1, "影子挂载应生成 Att1 (影子→新宿主)");
    QVERIFY2(att1->toBlockId == c.blockId, "影子应挂到新宿主线段");
    QVERIFY2(att1->toPointId == c.endId, "影子锚点应钉在新宿主端点");
    QVERIFY2(after->followerAngle == 90.0,
             "仅连接: followerAngle 应原样保留, 不得反算覆盖 (旧实现 bug)");
    const cad::param::Block* bb2 = doc.findBlock(b.blockId);
    const cad::param::Block* cc = doc.findBlock(c.blockId);
    QVERIFY(bb2 && cc);
    QVERIFY(bb2->worldPos(b.startId).distanceTo(cc->worldPos(c.endId)) < 1e-6);

    // 3b) R3 链式随动: 宿主 C 旋转 +30° → B 跟随旋转 +30° (位置+角度链式)。
    {
        const double rotB0 = bb2->transform.rotation;
        doc.blockById(c.blockId)->transform.rotation += 30.0 * M_PI / 180.0;
        doc.resolveAll();
        QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation
                          - (rotB0 + 30.0 * M_PI / 180.0)) < 1e-9,
                 "L3 旋转 → 影子随动 → B 链式跟转 (R3)");
    }

    // 4) 仅连接: 直接完成连接回 Idle (不进入角度输入会话)。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(doc.attachments().size(), size_t(2));

    // 5) undo: 回到快拆后的仅角度态 (Att1 删除, 影子保留冻结当前方向,
    //    位置/角度原样) — 影子挂载整步可撤销。
    doc.undoStack()->undo();
    const cad::param::Attachment* undone2 = doc.findAttachment(att.id);
    QVERIFY(undone2);
    QVERIFY2(undone2->angleOnly, "undo 应回到仅角度态");
    QVERIFY2(doc.findAttachment(att.id)->toBlockId == shadowBlk->id,
             "undo 后 Att2 仍指向影子");
    bool att1Gone = true;
    for (const auto& a2 : doc.attachments())
        if (!a2.isPin && a2.fromBlockId == shadowBlk->id) att1Gone = false;
    QVERIFY2(att1Gone, "undo 应删除 Att1 (挂载关系)");
}

// ── 有对齐点方向 + 变量时接入新线段不覆盖变量 (2026-xx 用户报告) ─────────
// 线段处于拆开影子基准态 (angleOnly) 且使用了自定义对齐点方向
// (angleRefBlockId 非空) 与变量 (followerAngleFormula, 如 LL=90) 时, 拖端点
// 接入新线段 (影子挂载路由, §7.4):
//   · 影子挂载只动位置维度 (Att1 + Att2 焊接) —— 自定义对齐点方向与变量
//     原样保留, 不反算覆盖、不清变量 (旧实现把变量烘成数值)。
//   · 不进入角度输入会话 (回车 = 确定连接而非确定角度), 直接完成连接。
//   · undo 单步回到拖前仅角度态。
void TestSelectWKey::angleRefWithFormulaReattachKeepsFormula()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                     // (0,0)-(100,0) 旧基准线
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});   // (0,-50)-(100,-50) 跟随线
    auto c = makeLine(doc, 100.0, Vec2{300.0, -50.0});  // (300,-50)-(400,-50) 新宿主
    doc.resolveAll();

    // 变量 LL = 90 (度)。
    FormulaVariable ll;
    ll.name = QStringLiteral("LL");
    ll.expression = QStringLiteral("90");
    doc.addFormula(ll);

    // B 完整连接 A (B.start 吸到 A.end), 再 D 键快拆 → 仅角度。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    doc.setAttachmentAngleOnly(att.id, true);
    doc.resolveAll();
    const auto* detached = doc.findAttachment(att.id);
    QVERIFY2(detached && detached->angleOnly, "预置: 仅角度 (拆开保留角度)");

    // 用户自定义对齐点方向 = A (旧基准线), 并设变量 LL=90 驱动跟随角。
    auto* mut = doc.findAttachment(att.id);
    QVERIFY(mut);
    mut->angleRefBlockId = a.blockId;
    mut->angleRefSegmentId = a.segId;
    mut->angleRefPointId = a.endId;
    mut->followerAngleFormula = QStringLiteral("LL");
    doc.resolveAll();
    QVERIFY2(!mut->angleRefBlockId.isNull(), "预置: 用户自定义对齐点方向");
    QVERIFY2(mut->followerAngleFormula == QStringLiteral("LL"),
             "预置: 变量驱动跟随角");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 bEnd = bb->worldPos(b.endId);
    const cad::geo::Vec2 bMid = (bb->worldPos(b.startId) + bEnd) * 0.5;

    // 1) 选中 B。
    sendMouse(QEvent::MouseButtonPress, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) 从 B 端点拖到 C 终点 (400,-50)。
    const QPoint from = vp(bEnd.x, bEnd.y);
    const QPoint to   = vp(400.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(200.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoModifier);

    // 3) 影子挂载路由 (§7.4): 释放到 C = 影子挂载 (Att1 = 影子→C) + Att2
    //    恢复位置钉点 —— 自定义基准/变量都挂在 Att2 上, 原样保留。
    QCOMPARE(doc.attachments().size(), size_t(2));
    const cad::param::Attachment* after = doc.findAttachment(att.id);
    QVERIFY(after);
    QVERIFY2(!after->angleOnly, "释放到目标后应恢复完整位置连接");
    const auto* shadowBlk = doc.blockById(after->toBlockId);
    QVERIFY2(shadowBlk && shadowBlk->isShadow, "Att2 基准仍为影子块");

    // 4) 有对齐点方向: 方向基准 = 对齐点 (A), 变量 LL 原样保留, 不烘成数值
    //    (影子挂载不动 Att2 的角度域 —— 自定义基准原样保留, R6 同源纪律)。
    QVERIFY2(after->angleRefBlockId == a.blockId,
             "用户自定义对齐点方向应原样保留");
    QVERIFY2(after->followerAngleFormula == QStringLiteral("LL"),
             "变量 LL 应原样保留, 不得被烘成数值 (旧实现 bug)");
    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a2 : doc.attachments()) {
        if (!a2.isPin && a2.fromBlockId == after->toBlockId) { att1 = &a2; break; }
    }
    QVERIFY2(att1 && att1->toBlockId == c.blockId,
             "影子应挂到新宿主 C (Att1)");

    // 5) 有对齐点方向: 不进入角度输入会话, 直接完成连接回 Idle。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);

    // 6) undo: 回到拖前仅角度态 (位置/角度/变量原样)。
    doc.undoStack()->undo();
    const cad::param::Attachment* undone = doc.findAttachment(att.id);
    QVERIFY(undone);
    QVERIFY2(undone->angleOnly, "undo 应回到仅角度态");
    QVERIFY2(undone->followerAngleFormula == QStringLiteral("LL"),
             "undo 后变量 LL 应原样保留");
}

// 二期 (CONTEXT_STRIP_DESIGN): 连接角度会话的输入走 条带 → 工具 → 手势 通道:
// 击键实时预览 (附件角度直写、无命令入栈)、°/⌒ 几何保持切换、Enter 确认收尾
// 为整步 undo 宏。旧浮动 AngleHud 已整体退场。
void TestSelectWKey::angleSessionStripInputDrivesConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 三个独立 对 (leader/follower), 每会话一对 —— 避免 follower 落点与
    // 已连接端点重叠触发 ConfirmTarget, 也免 undo 位移干扰。
    auto a = makeLine(doc, 100.0);                      // A: (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});    // B: (0,-50)-(100,-50)
    auto c = makeLine(doc, 100.0, Vec2{0.0, -100.0});   // C: (0,-100)-(100,-100)
    auto d = makeLine(doc, 100.0, Vec2{0.0, -150.0});   // D: (0,-150)-(100,-150)
    auto e = makeLine(doc, 100.0, Vec2{0.0, -200.0});   // E: (0,-200)-(100,-200)
    auto f = makeLine(doc, 100.0, Vec2{0.0, -250.0});   // F: (0,-250)-(100,-250)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);

    // ── 会话 1 (A→B): 击键预览 + °/⌒ 切换 + Enter 收尾 ──
    {
        // 选中 A 并拖 A.end (100,0) → B.end (100,-50)。
        const QPoint bodyA = vp(50.0, 0.0);
        sendMouse(QEvent::MouseButtonPress, bodyA, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyA, Qt::LeftButton, Qt::NoModifier);
        const QPoint fromA = vp(100.0, 0.0);
        sendMouse(QEvent::MouseButtonPress, fromA, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -15.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -40.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -50.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -50.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(1));

        // 1) 击键实时预览: 附件角度直写 (会话内不 push 命令)。
        const int undoIndex = doc.undoStack()->index();
        ts->connectAngleTextChanged(QStringLiteral("45"));
        QCOMPARE(doc.attachments().front().followerAngle, 45.0);
        QCOMPARE(doc.undoStack()->index(), undoIndex);

        // 2) °/⌒ 切换: 几何保持换算到弧长模式。
        ts->connectAngleModeChanged(cad::param::RotationMode::ArcLength);
        QCOMPARE(doc.attachments().front().rotationMode,
                 cad::param::RotationMode::ArcLength);

        // 3) Enter 确认: 收尾为整步 undo 宏 (连接 + 角度调整一步撤销)。
        ts->connectAngleCommitted();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(1));
        QCOMPARE(doc.attachments().front().rotationMode,
                 cad::param::RotationMode::ArcLength);
        QVERIFY(doc.undoStack()->index() > undoIndex);
    }

    // ── 会话 2 (C→D): 无效公式 → Enter 被拒; Esc 收尾 (留下 无效/弧长 残留态) ──
    {
        const QPoint bodyC = vp(50.0, -100.0);
        sendMouse(QEvent::MouseButtonPress, bodyC, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyC, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, vp(100.0, -100.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -125.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -150.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -150.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(2));

        ts->connectAngleTextChanged(QStringLiteral("abc"));   // 无效公式
        ts->connectAngleCommitted();
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);   // 无效 Enter 忽略
        ts->connectAngleCancelled();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(2));
    }

    // ── 会话 3 (E→F): 新会话必须复位 残留无效标记 + 残留弧长模式 ──
    //    (回归: 旧 showAngleHud 每次显示复位 m_angleValid/m_angleMode,
    //    beginAngleSession 同款 —— 否则 Enter 永远被拒 / 数值写错存储域。)
    {
        const QPoint bodyE = vp(50.0, -200.0);
        sendMouse(QEvent::MouseButtonPress, bodyE, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyE, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, vp(100.0, -200.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -225.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -250.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -250.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(3));

        // 模式已复位为角度: 数值输入写 followerAngle 而非弧长 (检查本会话
        // 新增的附件 = 最后一个)。
        ts->connectAngleTextChanged(QStringLiteral("30"));
        QCOMPARE(doc.attachments().back().followerAngle, 30.0);
        QCOMPARE(doc.attachments().back().rotationMode,
                 cad::param::RotationMode::Angle);

        // 合法性已复位: Enter 立即收尾 (不因上一会话的无效输入被拒)。
        ts->connectAngleCommitted();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(3));
        QCOMPARE(doc.attachments().back().rotationMode,
                 cad::param::RotationMode::Angle);
    }
}


// ---------------------------------------------------------------------------
// 重叠线段消歧 (2026-10): 悬停提示 → 点选+W 循环 → 右键候选菜单 (共享候选集合)
// ---------------------------------------------------------------------------

// C 方案: 完全重合的两条线段, 悬停时 HUD 提示候选名单 (名称可分辨).
void TestSelectWKey::overlapHoverShowsClusterHint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 两条几何完全重合的线段: A (后建, 堆叠在上) / B.
    auto b = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto move = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent m(QEvent::MouseMove, vpPos, global,
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &m);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 悬停重合线身 → 提示列出两条候选.
    move(50.0, 0.0);
    QVERIFY2(sel->overlapHintText().contains(QString::fromUtf8("重叠 2 条")),
             qPrintable(QStringLiteral("hint='%1'").arg(sel->overlapHintText())));
    QVERIFY(sel->overlapHintText().contains(QLatin1Char('A')));
    QVERIFY(sel->overlapHintText().contains(QLatin1Char('B')));

    // 移开 → 提示隐藏.
    move(500.0, 500.0);
    QVERIFY(sel->overlapHintText().isEmpty());
}

// A 方案: 点选集群激活 W 循环上下文; W 逐位回绕; 点别的线退出后 W 恢复模式切换.
void TestSelectWKey::overlapClickCyclesWithWKey()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 完全重合: A (后建, 堆叠上) / B 建在同一起点; 另加一条分离线 C 用于退出验证.
    auto b = makeLine(doc, 100.0);
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    auto c = makeLine(doc, 100.0, Vec2{0.0, -300.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto pressW = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 点击重合处 → 选中堆叠最上的 A, 激活循环上下文 (候选 2 条, 第 1/2).
    click(50.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QVERIFY(sel->overlapIndex() == 0);
    QCOMPARE(sel->overlapCandidates().size(), 2);
    QVERIFY(sel->overlapHintText().contains(QString::fromUtf8("第 1/2")));

    // 2) W → 循环到 B (第 2/2).
    pressW();
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(sel->overlapIndex(), 1);
    QVERIFY(sel->overlapHintText().contains(QString::fromUtf8("第 2/2")));

    // 3) W → 回绕到 A (第 1/2).
    pressW();
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(sel->overlapIndex(), 0);

    // 4) 点分离线 C → 循环上下文退出; 再按 W 恢复为模式切换 (单选→多选).
    click(50.0, -300.0);
    QCOMPARE(sel->overlapIndex(), -1);
    QVERIFY(sel->overlapHintText().isEmpty());
    pressW();   // 非循环上下文: W = 切换多选/单选
    click(50.0, 0.0);            // 多选: 加上 A
    QVERIFY(scene.findBlockItem(c.blockId)->toolSelected());
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
}

// B' 方案共用的命令式点名选中: 循环上下文中按索引换选 (右键菜单走同一入口).
void TestSelectWKey::overlapPickCandidateByIndex()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto b = makeLine(doc, 100.0);
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 点重合 → 激活; 命令式点名 2 号候选 (B) —— 与右键「重叠候选」菜单同一入口.
    click(50.0, 0.0);
    QVERIFY(sel->overlapIndex() == 0);
    sel->pickOverlapCandidate(1);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(sel->overlapIndex(), 1);
}

// ---------------------------------------------------------------------------
// 连接卡片两维独立 (2026-xx 用户拍板): 「连接线段」「独立角度」「连接保护」
// 复选框与「清除」按钮全删 —— 连接语义 = 两个正交维度的开关:
//   · 连接点按钮 (位置维度): 拆开 = 位置自由 (angleOnly, 角度仍跟随);
//     重连 = 位置重新吸附回原宿主 + 重新焊接。
//   · [独立] 按钮 (角度维度, SegmentRefCard, PANEL_REDESIGN §3/§6.4):
//     勾选 = 角度不跟随 (世界角度, 独立角); 再点 = 还原上次基准 (反算零跳变)。
//   · 位置拆开 + [独立] = 自由线 (Resolver angleOnly 无条件放行位置、
//     angleIndependent 保持自身旋转 —— 两维不再互斥)。
// 四态矩阵: 全连接 / 独立角 (位置跟·角度拆) / 仅角度 (位置拆·角度跟) / 自由。
// ---------------------------------------------------------------------------
void TestSelectWKey::connectionCardNewSemantics()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    auto line   = makeLine(doc, 60.0);
    doc.resolveAll();

    // 建立连接 (用户拍板 2026-08 复旧: 新建连接默认焊接).
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    QVERIFY2(doc.attachments().front().isLocked, "新建连接默认焊接");

    cad::ui::SegmentConnectionCard card(&doc, &scene);
    card.setTarget(line.blockId, line.segId);
    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);

    auto findBtn = [&](QWidget& w, const QString& text) -> QPushButton* {
        for (auto* b : w.findChildren<QPushButton*>())
            if (b->text() == text) return b;
        return nullptr;
    };
    auto worldDir = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        const Vec2 p1 = b->transform.toWorld(b->findPoint(line.startId)->resolvedPos);
        const Vec2 p2 = b->transform.toWorld(b->findPoint(line.endId)->resolvedPos);
        return std::atan2(p2.y - p1.y, p2.x - p1.x);
    };
    auto angleDelta = [](double a, double b) {
        double d = std::abs(a - b);
        d = std::fmod(d, 2.0 * M_PI);
        return d > M_PI ? 2.0 * M_PI - d : d;
    };
    auto fromPointWorld = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        return b->transform.toWorld(b->findPoint(line.startId)->resolvedPos);
    };

    // 0) 控件骨架: 连接卡无复选框、无「清除」按钮; 角度维度 = [独立] 勾选钮。
    QVERIFY2(card.findChildren<QCheckBox*>().isEmpty(),
             "连接卡不得再有复选框");
    QVERIFY2(!findBtn(card, QString::fromUtf8("清除")),
             "连接卡「清除」按钮已删 (拆开/重连 覆盖断开语义)");
    QPushButton* btnAngleBase = refCard.findChild<QPushButton*>(
        QStringLiteral("angleBaseToggleBtn"));
    QVERIFY2(btnAngleBase, "角度维度按钮 [独立] (objectName 沿用旧契约)");
    QCOMPARE(btnAngleBase->text(), QString::fromUtf8("独立"));
    QVERIFY2(!btnAngleBase->isChecked(), "初始非独立 (角度跟随基准)");
    QVERIFY2(btnAngleBase->isCheckable(), "[独立] 必须是 checkable 勾选钮");

    // 1) 连接点「拆开」(位置维度): 仅角度 —— 位置自由、角度仍跟随。
    //    影子换代 (DETACH_SHADOW_DESIGN.md §7.1, 2026-xx 翻案「位置记忆保留
    //    在附件 toBlockId」): 拆开后基准 = 影子块 (master = 原宿主) —— 重连
    //    回宿主的记忆由影子 master 链接承载 (⑤ 复原锚点 = 角色 1:1 映射)。
    QPushButton* btnDetach = findBtn(card, QString::fromUtf8("拆开"));
    QVERIFY(btnDetach);
    QVERIFY(btnDetach->isEnabled());
    btnDetach->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleOnly, "连接点拆开 = 位置维度拆开 (仅角度)");
        QVERIFY2(!a.isLocked, "拆开自动解除焊接");
        QVERIFY2(!a.angleIndependent, "位置维度拆开不碰角度维度");
        const auto* shadow = doc.blockById(a.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "拆开 = 影子换代 (基准 → 影子)");
        QVERIFY2(shadow->shadowMasterBlockId == leader.blockId,
                 "重连回宿主的记忆 = 影子 master (原宿主)");
        QVERIFY2(a.toPointId != leader.endId, "锚点 = 影子克隆点 (与本体无引用)");
        QCOMPARE(a.followerAngle, 180.0);   // offset 原样保留 (R2)
    }
    card.refresh();
    // 起点行按钮 (connDetachBtn) 翻面为「重连」; 终点行恒有禁用「拆开」,
    // 故不能再用全局 findBtn("拆开") 断言。
    QPushButton* connDetach = card.findChild<QPushButton*>(
        QStringLiteral("connDetachBtn"));
    QVERIFY2(connDetach && connDetach->text() == QString::fromUtf8("重连"),
             "位置拆开态连接点按钮翻面为「重连」");
    QVERIFY2(!btnAngleBase->isChecked(), "角度维度未动 ([独立] 未勾选)");
    const double dirDetached = worldDir(line.blockId);

    // 2) 连接点「重连」(位置维度): 位置回原宿主 + 重新焊接, 方向零跳变。
    QPushButton* btnReconnect = findBtn(card, QString::fromUtf8("重连"));
    QVERIFY2(btnReconnect, "位置拆开态应显示「重连」");
    QVERIFY(btnReconnect->isEnabled());
    btnReconnect->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly, "重连 = 位置维度恢复");
        QVERIFY2(a.isLocked, "重连必须重新焊接");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "重连后 from-point 必须重新吸附回宿主点");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirDetached) < 1e-9,
                 "重连后方向不得跳变");
    }

    // 3) [独立] 勾选 (角度维度): 独立角 —— 位置保持吸附、角度不再跟随。
    refCard.refresh();
    QCOMPARE(btnAngleBase->text(), QString::fromUtf8("独立"));
    QVERIFY2(!btnAngleBase->isChecked(), "跟随态 [独立] 未勾选");
    QVERIFY(btnAngleBase->isEnabled());
    btnAngleBase->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleIndependent, "基准点拆开 = 角度维度拆开 (独立角)");
        QVERIFY2(!a.angleOnly, "角度维度拆开不碰位置维度");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "独立角位置必须仍吸附在宿主点");
    }
    refCard.refresh();
    QVERIFY2(btnAngleBase->isChecked(), "独立角态 [独立] 已勾选");
    // 独立角态: 连接点按钮仍可用 (位置维度独立, 可再拆 → 自由)。
    card.refresh();
    QVERIFY2(findBtn(card, QString::fromUtf8("拆开"))->isEnabled(),
             "独立角态连接点按钮应可用 (两维独立)");
    const double dirIndep = worldDir(line.blockId);

    // 4) [独立] 取消勾选 (角度维度): 恢复角度跟随 (还原上次基准), 方向零跳变。
    btnAngleBase->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleIndependent, "[独立] 取消 = 角度维度恢复");
        QVERIFY2(!a.angleOnly, "重连角度不碰位置维度 (仍是全连接)");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirIndep) < 1e-9,
                 "恢复角度跟随不得跳线");
    }

    // 5) 双拆开 = 自由线: 位置自由 + 角度自管 —— 基准线旋转, 本线不动。
    card.refresh();
    btnDetach = findBtn(card, QString::fromUtf8("拆开"));
    btnDetach->click();               // 位置拆开
    refCard.refresh();
    btnAngleBase->click();            // [独立] 勾选 (角度拆开)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleOnly && a.angleIndependent,
                 "双拆开 = 位置自由 + 角度自管 (自由线)");
    }
    const auto* blk = doc.findBlock(line.blockId);
    const Vec2 originBefore = blk->transform.origin;
    const double rotBefore = blk->transform.rotation;
    // 基准线旋转 30°: 自由线不得跟随 (位置与角度都不被驱动)。
    auto* leaderMut = doc.findBlock(leader.blockId);
    leaderMut->transform.rotation += 30.0 * M_PI / 180.0;
    doc.resolveAll();
    QVERIFY2(blk->transform.origin.distanceTo(originBefore) < 1e-9,
             "双拆开自由线不得跟随基准线移动");
    QVERIFY2(std::abs(blk->transform.rotation - rotBefore) < 1e-9,
             "双拆开自由线角度不得被基准线驱动");

    // 6) 从自由逐步重连 → 全连接 (位置「重连」+ [独立] 取消, 顺序无关, 零跳变)。
    card.refresh();
    const double dirFree = worldDir(line.blockId);
    findBtn(card, QString::fromUtf8("重连"))->click();      // 位置维度
    QVERIFY2(!doc.attachments().front().angleOnly,
             "重连位置维度 → 独立角");
    refCard.refresh();
    btnAngleBase->click();                                  // [独立] 取消 (角度维度)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly && !a.angleIndependent, "双重重连 = 全连接");
        QVERIFY2(a.isLocked, "位置重连重新焊接");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "全连接 from-point 回宿主点");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirFree) < 1e-9,
                 "自由→全连接不得跳线");
    }

    // 7) 全新自由线 (无 attachment): 两个 拆开 按钮禁用; 输入 P# 回车建立连接。
    auto line2 = makeLine(doc, 50.0);
    doc.resolveAll();
    cad::ui::SegmentConnectionCard card2(&doc, &scene);
    card2.setTarget(line2.blockId, line2.segId);
    card2.refresh();
    QPushButton* btnDetach2 = findBtn(card2, QString::fromUtf8("拆开"));
    QVERIFY2(btnDetach2 && !btnDetach2->isEnabled(),
             "无连接时位置拆开按钮禁用");
    const auto* ldr7 = doc.findBlock(leader.blockId);
    const auto* hp7 = ldr7->findPoint(leader.endId);
    // 2026-08 「连接线段」框也是 PointRefEdit 了 —— 必须按 objectName 定位
    // 连接点框; 无差 findChild 会命中先创建的 connSegEdit (线段框只预填不建连)。
    auto* refConn2 = card2.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("connPointEdit"));
    QVERIFY(refConn2);
    refConn2->setText(hp7->serial);
    QTest::keyClick(refConn2, Qt::Key_Return);
    QCOMPARE(doc.attachments().size(), size_t(2));
    {
        const auto& a = doc.attachments().back();
        QCOMPARE(a.fromBlockId, line2.blockId);
        QCOMPARE(a.toBlockId, leader.blockId);
        QCOMPARE(a.toPointId, leader.endId);
        QVERIFY2(a.isLocked, "输入 P# 建立连接默认焊接");
    }
}

// ---------------------------------------------------------------------------
// 角度基准两点化 — 点2 输入框 (2026-08-31 修复「点2 完全无效」):
//   · 自动态 (默认跟随) 填点2 → 自动点1 (所连点) 固化为显式点1, 与点2 一起
//     六参落库 —— 此前 ref2 单独落库被 Resolver 两点分支 (angleRefBlockId
//     非空门控) 忽略, 输入随即被 refresh 清空 = 完全无效;
//   · 点2 == 有效点1 → 拒绝 (零向量方向); 设置本身反算零跳变;
//   · 点2 宿主块移动 → 跟随线转向 点1→点2 连线方向 (引擎消费);
//   · undo 单步回自动态 (ref1/ref2 全空);
//   · 自由线只预填点2 → 连入后第一次 refresh 落库 (此前被静默丢弃)。
// ---------------------------------------------------------------------------
void TestSelectWKey::angleRefPoint2TwoPointBasis()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));   // 宿主 A
    const auto line   = makeLine(doc, 60.0);                      // 本线
    const auto hostB  = makeLine(doc, 80.0, Vec2(120.0, 90.0));   // 点2 宿主 B
    doc.resolveAll();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* p1Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY2(p1Edit && p2Edit,
             "点1/点2 输入框 (angleRefPointEdit/angleRefPoint2Edit) 必须存在");

    // 挂 view 才能收到 CanvasScene::showToast (无视口时早退) —— 点2 拒绝
    // 路径的 toast 断言依赖 (2026-09, E:\4.gcad L2 报告: 用户填点2 "无法
    // 填入", 根因 = 静默拒绝零反馈)。
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto angDiff = [](double a, double b) {
        double d = std::abs(a - b);
        d = std::fmod(d, 2.0 * M_PI);
        return d > M_PI ? 2.0 * M_PI - d : d;
    };
    auto worldDir = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        const Vec2 w1 = b->transform.toWorld(
            b->findPoint(line.startId)->resolvedPos);
        const Vec2 w2 = b->transform.toWorld(
            b->findPoint(line.endId)->resolvedPos);
        return std::atan2(w2.y - w1.y, w2.x - w1.x);
    };
    const double dirBefore = worldDir(line.blockId);

    // 1) 自动态填点2 = 有效点1 (所连点) → 零向量方向, 拒绝, 模型不变。
    //    (裸指针一次一取: 不跨 addBlock 持有 findBlock 结果)
    p2Edit->setText(doc.findBlock(leader.blockId)->findPoint(leader.endId)->serial);
    QTest::keyClick(p2Edit, Qt::Key_Return);
    QVERIFY2(doc.attachments().front().angleRefBlockId.isNull(),
             "点2 == 所连点 拒绝, 仍为自动态");
    // 拒绝必须有反馈 —— 此前静默刷回 = "点2 填不进"假象 (E:\4.gcad L2 报告,
    // 2026-09): toast 文本明示零长度方向; 锚点 tag 红闪由 rejectRefInput 负责。
    // HudItem 非 QObject (无 Q_OBJECT), 不能 findChild —— 遍历场景 items
    // dynamic_cast 定位 (toast 是本用例场景里唯一的 HudItem)。
    {
        bool toastOk = false;
        const auto items = scene.items();
        for (auto* it : items) {
            auto* hud = dynamic_cast<HudItem*>(it);
            if (!hud) continue;
            if (hud->isVisible()
                && hud->text().contains(QString::fromUtf8("零长度")))
                { toastOk = true; break; }
        }
        QVERIFY2(toastOk,
                 "点2 == 点1 拒绝后 toast 应可见且说明零长度方向");
    }

    // 2) 自动态填点2 (hostB 起点): 点1 固化为所连点 + ref2 落库, 零跳变。
    p2Edit->setText(doc.findBlock(hostB.blockId)->findPoint(hostB.startId)->serial);
    QTest::keyClick(p2Edit, Qt::Key_Return);
    {
        const auto& a = doc.attachments().front();
        QCOMPARE(a.angleRefBlockId, leader.blockId);
        QCOMPARE(a.angleRefPointId, leader.endId);
        QVERIFY2(!a.angleRefSegmentId.isNull(), "固化点1 携带出口线段");
        QCOMPARE(a.angleRef2BlockId, hostB.blockId);
        QCOMPARE(a.angleRef2PointId, hostB.startId);
        QVERIFY2(!a.angleIndependent, "填点2 = 自定义意图, 退出自动态");
        QVERIFY2(angDiff(worldDir(line.blockId), dirBefore) < 1e-9,
                 "设置两点基准 = 反算零跳变");
    }
    refCard.refresh();
    QVERIFY2(!p1Edit->text().isEmpty(), "点1 回显固化后的所连点");
    QVERIFY2(!p2Edit->text().isEmpty(), "点2 回显已解析点");

    // 3) 引擎消费: 移动点2 宿主 → 两点方向变化 → 跟随线转向 点1→点2 方向。
    {
        auto* c = doc.blockById(hostB.blockId);
        c->transform.origin = c->transform.origin + Vec2(60.0, -40.0);
    }
    doc.resolveAll();
    {
        const Vec2 w1 = doc.findBlock(leader.blockId)->worldPos(leader.endId);
        const Vec2 w2 = doc.findBlock(hostB.blockId)->worldPos(hostB.startId);
        const double refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
        const double fA = doc.attachments().front().followerAngle * M_PI / 180.0;
        QVERIFY2(angDiff(worldDir(line.blockId), refWorld + M_PI - fA) < 1e-9,
                 "跟随线世界方向 = 点1→点2 连线方向 (闭合基准)");
    }

    // 4) undo 单步回自动态。
    doc.undoStack()->undo();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleRefBlockId.isNull() && a.angleRef2BlockId.isNull(),
                 "undo 恢复自动跟随 (ref1/ref2 全空)");
    }

    // 5) 自由线只预填点2 → 连入后第一次 refresh 落库 (此前被静默丢弃)。
    auto line2 = makeLine(doc, 50.0);
    doc.resolveAll();
    cad::ui::SegmentRefCard refCard2(&doc, &scene);
    refCard2.setTarget(line2.blockId, line2.segId);
    auto* p2Edit2 = refCard2.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY(p2Edit2);
    p2Edit2->setText(doc.findBlock(hostB.blockId)->findPoint(hostB.startId)->serial);
    QTest::keyClick(p2Edit2, Qt::Key_Return);
    QVERIFY2(doc.attachments().size() == 1, "自由态只预填, 不写模型");
    {
        cad::param::Attachment att2;
        att2.fromBlockId = line2.blockId;
        att2.fromPointId = line2.startId;
        att2.toBlockId   = leader.blockId;
        att2.toPointId   = leader.endId;
        att2.toSegmentId = leader.segId;
        att2.followerAngle = 180.0;
        QVERIFY(doc.addAttachment(att2));
    }
    refCard2.refresh();   // 连入后第一次 refresh → 预填自动落库
    {
        const auto& a = doc.attachments().back();
        QCOMPARE(a.angleRefBlockId, leader.blockId);
        QCOMPARE(a.angleRefPointId, leader.endId);
        QCOMPARE(a.angleRef2BlockId, hostB.blockId);
        QCOMPARE(a.angleRef2PointId, hostB.startId);
    }

    // 6) 真实操作路径 (2026-09 E:\4.gcad L2 报告): 点2 输入后**未按回车**
    //    (点走 / Tab) —— focusOutEvent 自动提交合法单解, 不再静默清空。
    //    此前一律 revertDisplay: "点进输入框→输入→点走" 的输入被无声丢掉,
    //    点2 看起来"永远填不进", 而点1 (自动回显) 恒有内容。
    {
        const auto& a = doc.attachments().front();
        QVERIFY(a.angleRefBlockId.isNull() && a.angleRef2BlockId.isNull());
    }
    refCard.refresh();
    p2Edit->setText(doc.findBlock(hostB.blockId)->findPoint(hostB.startId)->serial);
    QFocusEvent fe(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(p2Edit, &fe);   // 不按回车直接失焦 (点走/Tab)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleRefBlockId.isNull() && !a.angleRef2BlockId.isNull(),
                 "失焦自动提交合法单解: 自动态点1 固化 + 点2 落库 (不再静默清空)");
        QCOMPARE(a.angleRef2BlockId, hostB.blockId);
        QCOMPARE(a.angleRef2PointId, hostB.startId);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 对齐点可输入 + 自动态两点回填 (2026-09 设计修正, 用户拍板):
//   · 对齐点 = 本线段的哪个端点钉在目标点上 (Attachment::fromPointId) ——
//     旧实现是绑 startPointId 的只读 tag, 换向后乱跳且不可选对端。
//   · 自动态 (跟随所连的线): 点1 回显目标点 P2, 点2 回显宿主线段另一端 P1
//     —— 方向 = "P3 对齐 P2, 以 P2→P1 为基准" 全量可见, 且 autoEcho 灰显
//     不落库 (回填不算用户意图)。
//   · 对齐点与换向 (进出身份) 无关: 换向只翻箭头, fromPointId 不动。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::alignPointEditableAndAutoStateTwoPointBackfill()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // L1: 宿主 (P1 起点, P2 终点); L2: 本线 (P3 起点, P4 终点), 起点吸附 L1 终点。
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const auto line   = makeLine(doc, 60.0);   // 本线 (0,0)→(60,0)
    doc.resolveAll();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;            // P3 对齐 P2
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    CanvasScene scene(&doc);
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* alignEdit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    auto* p1Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY2(alignEdit && p1Edit && p2Edit, "对齐点/点1/点2 输入框必须存在");

    // 1) 自动态全量回填: 对齐点=P3, 点1=L1·P2, 点2=L1·P1 (三点可见)。
    {
        const auto blk = doc.findBlock(line.blockId);
        QCOMPARE(alignEdit->resolvedPointId(), line.startId);
        QCOMPARE(alignEdit->text(),
                 cad::param::Serial::tag(doc.findBlock(line.blockId)
                                             ->findPoint(line.startId)->serial));
        QCOMPARE(p1Edit->resolvedBlockId(), leader.blockId);
        QCOMPARE(p1Edit->resolvedPointId(), leader.endId);
        QCOMPARE(p2Edit->resolvedPointId(), leader.startId);
        QVERIFY2(p2Edit->isAutoEcho(), "自动回填 = 灰显 autoEcho, 不算用户意图");
        // autoEcho 回填不得触发预填落库 (refresh 幂等, 模型仍自动态)。
        refCard.refresh();
        QVERIFY2(doc.attachments().front().angleRefBlockId.isNull(),
                 "自动态回填不得把自动态误固化为自定义");
    }

    // 2) 对齐点可输入: 填 P4 → fromPointId 换成 line.endId, 角度零跳变。
    {
        const auto blk = doc.findBlock(line.blockId);
        const auto* endPt = blk->findPoint(line.endId);
        const auto dirBefore = [&]() {
            const Vec2 w1 = blk->transform.toWorld(
                blk->findPoint(line.startId)->resolvedPos);
            const Vec2 w2 = blk->transform.toWorld(
                blk->findPoint(line.endId)->resolvedPos);
            return std::atan2(w2.y - w1.y, w2.x - w1.x);
        }();
        alignEdit->setText(cad::param::Serial::tag(endPt->serial));
        QTest::keyClick(alignEdit, Qt::Key_Return);
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
        const auto blk2 = doc.findBlock(line.blockId);
        const Vec2 w1 = blk2->transform.toWorld(
            blk2->findPoint(line.startId)->resolvedPos);
        const Vec2 w2 = blk2->transform.toWorld(
            blk2->findPoint(line.endId)->resolvedPos);
        const double dirAfter = std::atan2(w2.y - w1.y, w2.x - w1.x);
        QVERIFY2(std::abs(cad::geo::normalizeRad(dirAfter - dirBefore)) < 1e-9,
                 "切换对齐点 P3→P4: 本线方向零跳变");
        QCOMPARE(alignEdit->resolvedPointId(), line.endId);
    }

    // 3) 对齐点输入限制: 只接受本线端点 (P3/P4), 宿主点 L1·P2 无匹配拒绝。
    {
        alignEdit->setText(doc.findBlock(leader.blockId)
                               ->findPoint(leader.endId)->serial);
        QTest::keyClick(alignEdit, Qt::Key_Return);
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
    }
    // 4) 与换向无关: ReverseSegmentCommand 换向 (start/end 互换) 后
    //    fromPointId 保持 (对齐点仍为 P4), 不随身份翻转; 撤换向也不动它。
    {
        doc.undoStack()->push(new cad::cmd::ReverseSegmentCommand(
            &doc, line.blockId, line.segId));
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
        refCard.refresh();
        QCOMPARE(alignEdit->resolvedPointId(), line.endId);
        doc.undoStack()->undo();   // 撤换向 (undo 不影响 fromPointId)
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
    }

    // 5) undo 单步: SetAlignPointCommand 撤销 → fromPointId 回 P3。
    {
        doc.undoStack()->undo();
        QCOMPARE(doc.attachments().front().fromPointId, line.startId);
        refCard.refresh();
        QCOMPARE(alignEdit->resolvedPointId(), line.startId);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// 终点连接行 (2026-xx 每端完整连接: 起点连接 = Attachment (位置+角度),
// 终点连接 = endTarget 终点指向 (Resolver Step 7)。验证:
//   · 桥接落点 (缺省): 输入 P# → endTarget 写入 + 自动发布 M_xxx 驱动终点
//     距离公式 → 终点精确落在目标点; undo 一步全回滚 (指向/公式/测量)。
//   · 仅指向: 只写 endTarget, 不碰长度/距离公式与测量。
//   · 双端连接 = 桥接线: SegmentRefCard (基准线行) 隐藏 —— 互斥 (无基准线)。
//   · 终点拆开/重连: 拆开清指向 (测量保留), 重连恢复记忆目标。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::connectionCardEndConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));   // 宿主 A
    const auto line   = makeLine(doc, 60.0);                       // 本线 (0,0)→(60,0)
    const auto hostB  = makeLine(doc, 80.0, Vec2(160.0, 60.0));    // 终点目标宿主 B
    doc.resolveAll();

    cad::ui::SegmentConnectionCard card(&doc, &scene);
    card.setTarget(line.blockId, line.segId);

    auto* endPointEdit = card.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("endConnPointEdit"));
    QVERIFY2(endPointEdit, "终点连接点输入框 (endConnPointEdit) 必须存在");
    auto* endDetach = card.findChild<QPushButton*>(
        QStringLiteral("endConnDetachBtn"));
    QVERIFY(endDetach);

    const auto* hb = doc.findBlock(hostB.blockId);
    const auto* bp = hb->findPoint(hostB.endId);
    QVERIFY(bp);

    // ── ① 长度模式「自动」= 桥接落点: 输入 P# 回车 → 终点精确落点 ──
    doc.findBlock(line.blockId)->lengthAuto = true;
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* blk = doc.findBlock(line.blockId);
        QVERIFY2(blk->endTargetPointId == hostB.endId, "终点连接写入 endTarget");
        QCOMPARE(blk->endTargetBlockId, hostB.blockId);
        const auto* seg = blk->findSegment(line.segId);
        QVERIFY2(!seg->lengthFormula.isEmpty(), "桥接落点自动驱动长度公式 (测量线标记)");
        const auto* ep = blk->findPoint(line.endId);
        QVERIFY2(!ep->distanceFormula.isEmpty(), "桥接落点自动驱动终点距离公式 (几何)");
        QCOMPARE(ep->distanceFormula, seg->lengthFormula);
        QCOMPARE(doc.measureVars().size(), size_t(1));
        // 终点精确落在目标点 (Step 7 指向 + 测量距离)。
        doc.resolveAll();
        const Vec2 endWorld = blk->transform.toWorld(ep->resolvedPos);
        const Vec2 targetWorld = hb->transform.toWorld(bp->resolvedPos);
        QVERIFY2(endWorld.distanceTo(targetWorld) < 1e-6,
                 "桥接落点: 终点必须精确落在目标点上");
        // undo 一步全回滚 (指向/公式/测量)。
        doc.undoStack()->undo();
        const auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId.isNull(), "undo 清除终点指向");
        QVERIFY2(b2->findSegment(line.segId)->lengthFormula.isEmpty(),
                 "undo 恢复线段长度公式");
        QVERIFY2(b2->findPoint(line.endId)->distanceFormula.isEmpty(),
                 "undo 恢复终点距离公式");
        QVERIFY2(doc.measureVars().empty(), "undo 删除自动发布的测量");
    }

    // ── ② 长度模式「指定」= 仅指向: 只写 endTarget, 不碰长度/距离公式与测量 ──
    doc.findBlock(line.blockId)->lengthAuto = false;
    card.refresh();
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(!b2->endTargetPointId.isNull(), "仅指向也写 endTarget");
        QCOMPARE(b2->endTargetBlockId, hostB.blockId);
        QVERIFY2(b2->findSegment(line.segId)->lengthFormula.isEmpty(),
                 "仅指向不碰线段长度公式");
        QVERIFY2(b2->findPoint(line.endId)->distanceFormula.isEmpty(),
                 "仅指向不碰终点距离公式");
        QVERIFY2(doc.measureVars().empty(), "仅指向不发布测量");
    }
    doc.undoStack()->undo();
    QVERIFY2(doc.findBlock(line.blockId)->endTargetPointId.isNull(),
             "仅指向 undo 清除指向");

    // ── ③ 双端连接 = 桥接线: 起点 Attachment + 终点 endTarget → 方向段隐藏,
    //    对齐点段保留 (显示默认进点 + 禁用, 2026-09 规则表 ④) ──
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    QVERIFY2(!refCard.isHidden(), "单端连接时基准线行可见");

    doc.findBlock(line.blockId)->lengthAuto = true;   // 回到桥接落点
    card.refresh();
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    refCard.refresh();
    {
        const auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(!b2->endTargetPointId.isNull(), "双端连接 = 桥接线");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 桥接落点自动测量
    }
    // 桥接线: 方向段 (点1/点2/[独立]) 隐藏, 对齐点段保留 (默认进点 + 禁用)。
    auto* alignEdit3 = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    auto* p1Edit3 = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    QVERIFY2(alignEdit3 && p1Edit3, "对齐点/点1 输入框必须存在");
    QVERIFY2(!refCard.isHidden(), "桥接线: 对齐点段保留 (整卡不隐藏)");
    QVERIFY2(!alignEdit3->isEnabled(), "桥接线: 无进点语义 → 对齐点禁用");
    QCOMPARE(alignEdit3->resolvedPointId(), line.startId);
    QVERIFY2(!p1Edit3->isVisible(), "桥接线: 方向段 (点1) 隐藏");

    // ── ④ 终点拆开: 清指向 (测量保留) → 方向段恢复; 重连恢复记忆目标 ──
    card.refresh();
    QVERIFY2(endDetach->isEnabled(), "有指向时终点拆开可用");
    endDetach->click();
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId.isNull(), "终点拆开清除指向");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 测量保留 (长度公式仍在)
    }
    refCard.refresh();
    QVERIFY2(!refCard.isHidden(), "拆开终点后基准线行恢复显示");
    QVERIFY2(p1Edit3->isVisible(), "拆开终点后方向段恢复显示");
    card.refresh();
    QCOMPARE(endDetach->text(), QString::fromUtf8("重连"));
    QVERIFY2(endDetach->isEnabled(), "终点拆开记忆可用 → 重连可用");
    endDetach->click();
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId == hostB.endId, "终点重连恢复记忆目标");
        QCOMPARE(b2->endTargetBlockId, hostB.blockId);
    }
    refCard.refresh();
    QVERIFY2(!refCard.isHidden(), "重连后回到桥接线: 对齐点段保留");
    QVERIFY2(!p1Edit3->isVisible(), "重连后方向段再隐藏");

    // ── ⑤ 重定向: 输入新目标点 → endTarget 与归属测量目标点一并更新 ──
    const auto* bs = hb->findPoint(hostB.startId);
    card.refresh();
    endPointEdit->setText(bs->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId == hostB.startId, "重定向更新 endTarget");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 不重复创建测量
        QCOMPARE(doc.measureVars().front().blockB, hostB.blockId);
        QCOMPARE(doc.measureVars().front().pointB, hostB.startId);
        // undo 恢复旧目标 (endTarget + 测量 B 点)。
        doc.undoStack()->undo();
        QVERIFY2(doc.findBlock(line.blockId)->endTargetPointId == hostB.endId,
                 "重定向 undo 恢复旧 endTarget");
        QCOMPARE(doc.measureVars().front().pointB, hostB.endId);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 终点指向超出延伸 (2026-09 规则表 ⑤, 用户拍板): 起点连接 + 终点指向 +
// 指定长度 —— 长度 > 目标距离时终点**越过目标点**、方向保持指向角继续延伸
// (Step 7 只驱动旋转, 长度由 Polar 距离独立决定, 不截断)。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::endTargetOvershootExtendsAlongAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // 宿主 A (L1): 起点 (200,0) → 终点 (300,0); 本线 L2: 起点 (0,0) → 终点 (60,0)。
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const auto line   = makeLine(doc, 60.0);
    doc.resolveAll();

    // 起点连接 (进点 = P3 钉在 L1 终点 P2)。
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));

    // 终点指向: 目标 = 宿主 B 起点 (位于 (120,90), 距本线起点 ~146mm)。
    const auto hostB = makeLine(doc, 80.0, Vec2(120.0, 90.0));
    doc.resolveAll();
    auto* blk = doc.findBlock(line.blockId);
    blk->endTargetBlockId = hostB.blockId;
    blk->endTargetPointId = hostB.startId;
    blk->endTargetOffset = 0.0;
    // 指定长度 200mm > 起点→目标距离 (~146mm): 终点应越过目标点。
    auto* ep = blk->findPoint(line.endId);
    ep->constraint = cad::param::PointConstraint::Polar;
    ep->refPointId = line.startId;
    ep->distance = 200.0;
    doc.resolveAll();

    const Vec2 startWorld = blk->transform.toWorld(
        blk->findPoint(line.startId)->resolvedPos);
    const Vec2 endWorld = blk->transform.toWorld(ep->resolvedPos);
    const Vec2 targetWorld = doc.findBlock(hostB.blockId)->worldPos(hostB.startId);

    // 1) 终点越过目标点 (沿指向方向继续延伸)。
    QVERIFY2(endWorld.distanceTo(startWorld) > 199.0,
             "指定长度 200mm 生效 (终点距起点 = 长度)");
    QVERIFY2(endWorld.distanceTo(targetWorld) > 1.0,
             "长度 > 目标距离: 终点越过目标点, 不截断");

    // 2) 方向保持指向角: 终点在 起点→目标 的射线上 (共线且同向)。
    const Vec2 aimDir = targetWorld - startWorld;
    const Vec2 endDir = endWorld - startWorld;
    const double cross = aimDir.x * endDir.y - aimDir.y * endDir.x;
    const double dot = aimDir.x * endDir.x + aimDir.y * endDir.y;
    QVERIFY2(std::abs(cross) < 1e-6 && dot > 0.0,
             "终点沿 起点→目标 射线方向延伸 (方向保持指向角)");

    // 3) 对齐点锁定: 有 endTarget 时进点锁定 start 端 (方案 A) —— 面板对齐
    //    点输入框禁用且显示 start 端。
    CanvasScene scene(&doc);
    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* alignEdit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    QVERIFY2(alignEdit, "对齐点输入框必须存在");
    QVERIFY2(!alignEdit->isEnabled(), "有 endTarget: 进点锁定 start 端 (禁用)");
    QCOMPARE(alignEdit->resolvedPointId(), line.startId);
}

// ─────────────────────────────────────────────────────────────────────────
// 双击打开线条属性面板: 交叉点处活跃层优先
// ─────────────────────────────────────────────────────────────────────────
// 用户报告 (2026-11): 辅助层的线段有时候很难双击打开线条属性面板, 有时候
// 弹出, 更多的时候双击没有反应。根因 = ToolSelect::mouseDoubleClick 的命中
// 与 press/hitBlock 不一致: 旧代码取 hits 列表里第一个 BlockItem, 若其层级
// 不是活跃层就直接 return —— 在辅助线/工作线交叉处, 堆叠在上的灰显块
// (另一层) 往往抢走第一次命中, 双击静默死亡; 而单击选中 (hitBlock 逐项
// 扫描到活跃层) 却正常。修复 = 双击改用 hitBlock 同规 (首个活跃层块胜出,
// 灰显层跳过而非否决)。以下用例锁定: 无论交叉处谁在上, 双击恒打开活跃层
// 线段的面板; 灰显层 (非活跃) 线段仍不可从画布双击编辑 (设计规则不变)。
void TestSelectWKey::doubleClickCrossingPrefersActiveLayer()
{
    struct Scenario {
        bool  auxActive;     ///< 活跃层 = 辅助层 (true) 或工作层 (false).
        bool  auxAddedFirst; ///< 辅助线先建 (交叉处被后建的工作线堆叠覆盖).
        Vec2  click;         ///< 双击位置 (用户坐标).
        bool  expectAuxDialog; ///< 期望面板目标 = 辅助线 (false = 工作线).
        bool  expectNoDialog;  ///< 期望不弹面板 (灰显层不可编辑).
        const char* what;
    };
    const Vec2 kCross{50.0, 100.0};  // (50,0)→(50,200) 辅助线 × (0,100)→(200,100) 工作线
    const Vec2 kBody{50.0, 30.0};    // 辅助线身上远离交叉点的位置

    const Scenario scenarios[] = {
        {true,  false, kCross, true,  false, "aux active, aux on top:  crossing must open AUX panel"},
        {true,  true,  kCross, true,  false, "aux active, work on top: crossing must open AUX panel"},
        {false, false, kCross, false, false, "work active, aux on top: crossing must open WORK panel"},
        {false, true,  kCross, false, false, "work active, work on top: crossing must open WORK panel"},
        {false, true,  kBody,  false, true,  "work active, aux body: grayed aux stays non-editable"},
    };

    for (const Scenario& sc : scenarios) {
        ParamDocument doc;
        CanvasScene scene(&doc);
        const QUuid auxLayer  = layerIdAt(doc, 0);
        const QUuid workLayer = layerIdAt(doc, 1);

        QUuid auxId, workId;
        if (sc.auxAddedFirst) {
            auxId  = addLineOnLayer(doc, auxLayer,  Vec2(50.0, 0.0),   90.0, 200.0);
            workId = addLineOnLayer(doc, workLayer, Vec2(0.0, 100.0),  0.0,  200.0);
        } else {
            workId = addLineOnLayer(doc, workLayer, Vec2(0.0, 100.0),  0.0,  200.0);
            auxId  = addLineOnLayer(doc, auxLayer,  Vec2(50.0, 0.0),   90.0, 200.0);
        }
        doc.setActiveLayer(sc.auxActive ? auxLayer : workLayer);
        doc.resolveAll();

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        cad::tools::ToolManager tm(&scene);
        tm.setParamDocument(&doc);   // default active tool is Select
        view.setInputDispatcher(&tm);

        auto vp = [&](double x, double y) {
            return view.mapFromScene(QPointF(x, -y));
        };
        const QPoint hit = vp(sc.click.x, sc.click.y);
        const QPoint global = view.viewport()->mapToGlobal(hit);
        auto send = [&](QEvent::Type type, Qt::MouseButton btn) {
            QMouseEvent ev(type, hit, global, btn,
                           btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(view.viewport(), &ev);
            // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
            // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
            QTest::qWait(20);
        };
        // 完整双击序列: press → release → dblclick → release.
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton);
        send(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton);

        const auto dialogs = view.findChildren<cad::ui::LinePropertyDialog*>();
        if (sc.expectNoDialog) {
            QVERIFY2(dialogs.isEmpty(), sc.what);
        } else if (!dialogs.isEmpty()) {
            QCOMPARE(dialogs.size(), 1);
            const QUuid expect = sc.expectAuxDialog ? auxId : workId;
            QCOMPARE(dialogs.first()->targetBlockId(), expect);
        } else {
            QFAIL(sc.what);   // 修复前: 交叉处双击静默无面板
        }
        for (auto* d : dialogs) d->close();
    }
}

void TestSelectWKey::multiSelectMoveToLayer()
{
    ParamDocument doc;
    const QUuid work1 = layerIdAt(doc, 1);
    doc.setActiveLayer(work1);
    const QUuid work2 = doc.addLayer(QStringLiteral("图层 2"));

    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        QTest::qWait(20);
    };
    auto pressW = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        QTest::qWait(20);
    };

    // 切换到多选模式
    pressW();

    // 点选两根线
    click(50.0, 0.0);
    click(50.0, -50.0);
    QCOMPARE(sel->selection().size(), 2);
    QVERIFY(sel->selection().contains(a.blockId));
    QVERIFY(sel->selection().contains(b.blockId));

    // 执行移动到 work2
    sel->moveSelectionToLayer(work2);

    // 验证选择集已清空，回到 Idle
    QVERIFY(sel->selection().isEmpty());
    QCOMPARE(sel->state(), cad::tools::SelectState::Idle);

    // 验证两根线的图层属性已变为 work2
    const auto* blkA = doc.findBlock(a.blockId);
    const auto* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(blkB && blkB->layer == work2);

    // 验证原子撤销 (Ctrl+Z)
    QCOMPARE(stack.count(), 1);
    stack.undo();
    blkA = doc.findBlock(a.blockId);
    blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work1);
    QVERIFY(blkB && blkB->layer == work1);

    // 验证重做 (Ctrl+Y)
    stack.redo();
    blkA = doc.findBlock(a.blockId);
    blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(blkB && blkB->layer == work2);

    // 移动到辅助层
    const QUuid auxId = doc.layersView().auxLayerId();
    doc.setActiveLayer(work2);
    pressW();
    click(50.0, 0.0);
    QCOMPARE(sel->selection().size(), 1);
    sel->moveSelectionToLayer(auxId);
    blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == auxId);

    // 撤销移动到辅助层
    stack.undo();
    blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == work2);
}

void TestSelectWKey::multiSelectRightClickMoveToLayerMenu()
{
    ParamDocument doc;
    const QUuid work1 = layerIdAt(doc, 1);
    doc.setActiveLayer(work1);
    const QUuid work2 = doc.addLayer(QStringLiteral("工作层 2"));

    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };

    // 选中线 A
    const QPoint vpPos = vp(50.0, 0.0);
    const QPoint global = view.viewport()->mapToGlobal(vpPos);
    QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QTest::qWait(20);

    QCOMPARE(sel->selection().size(), 1);

    bool menuFound = false;
    bool actionTriggered = false;
    bool auxFound = false;

    // 定时器在菜单弹出后查找并触发“工作层 2”Action
    QTimer::singleShot(100, [&]() {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                if (m->isVisible()) {
                    menuFound = true;
                    for (QAction* act : m->actions()) {
                        if (act->menu()) {
                            QAction* targetAct = nullptr;
                            for (QAction* subAct : act->menu()->actions()) {
                                if (subAct->text() == QStringLiteral("辅助层")) {
                                    auxFound = true;
                                }
                                if (subAct->text() == QStringLiteral("工作层 2")) {
                                    targetAct = subAct;
                                }
                            }
                            if (targetAct) {
                                actionTriggered = true;
                                targetAct->trigger();
                                m->close();
                                return;
                            }
                        }
                    }
                    m->close();
                }
            }
        }
    });

    // 发送右键点击
    QMouseEvent rPress(QEvent::MouseButtonPress, vpPos, global,
                       Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &rPress);
    QTest::qWait(200);

    QVERIFY(menuFound);
    QVERIFY(auxFound);
    QVERIFY(actionTriggered);

    const auto* blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(sel->selection().isEmpty());
}

QTEST_MAIN(TestSelectWKey)
#include "test_select_wkey.moc"
