#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QStandardItemModel>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "ElaText.h"
#include "ui/LinePropertyDialog.h"
#include "ui/SegmentConnectionCard.h"
#include "document/commands/AttachmentCommands.h"
#include "parametric/ParamDocument.h"
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
    // ── 连接卡片新语义 (2026-10 用户拍板) ──
    void connectionCardNewSemantics();
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
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& after = doc.attachments().front();
    QVERIFY2(after.angleOnly, "D 键应把连接转为仅角度 (拆开保留角度)");
    QVERIFY2(!after.isLocked, "拆开自动清除拖动保护");

    // 4) undo: 恢复完整连接 (焊接态原样还原 — SetAttachmentAngleOnlyCommand
    //    快照 isLocked=true, undo 回放)。
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& undone = doc.attachments().front();
    QVERIFY2(!undone.angleOnly, "undo 应恢复完整连接");
    QVERIFY2(undone.isLocked,
             "undo 应恢复原来的焊接态 (默认焊, 快照还原不得丢锁)");
}

// 回归 (用户报告): 线段使用了引用线段 (角度跟随基准线) 但没有连接线段
// (仅角度 angleOnly = 位置自由) 时, 拖动端点必须能重新建立位置连接 —
// 吸附反应 + 原附件重挂到新目标 (旧角度基准保留为独立角度基准 = 双基准,
// 恢复完整连接并重新焊接)。
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

    // 3) 原附件重挂: 同一 id, 恢复完整位置连接 (angleOnly=false)、重新焊接、
    //    位置吸到 C.end、旧 A 保留为独立角度基准 (双基准)。
    QCOMPARE(doc.attachments().size(), size_t(1));
    const cad::param::Attachment* after = doc.findAttachment(att.id);
    QVERIFY(after);
    QVERIFY2(!after->angleOnly, "释放到目标后应恢复完整位置连接");
    QVERIFY2(after->isLocked, "重挂恢复完整连接应重新焊接 (拖动保护默认勾选)");
    QVERIFY2(after->toBlockId == c.blockId, "位置应重挂到新宿主线段");
    QVERIFY2(after->toPointId == c.endId, "位置应重挂到新宿主端点");
    QVERIFY2(after->angleRefBlockId == a.blockId,
             "旧角度基准应保留为独立角度基准 (双基准)");
    const cad::param::Block* bb2 = doc.findBlock(b.blockId);
    const cad::param::Block* cc = doc.findBlock(c.blockId);
    QVERIFY(bb2 && cc);
    QVERIFY(bb2->worldPos(b.startId).distanceTo(cc->worldPos(c.endId)) < 1e-6);

    // 4) 手势进入角度输入, Esc 收尾回 Idle。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    // 二期: Esc 经条带 → 工具 → 手势 (旧浮动 AngleHud 已退场)。
    ts->connectAngleCancelled();
    QTest::qWait(30);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(doc.attachments().size(), size_t(1));

    // 5) undo: 回到快拆前的仅角度态 (位置/角度原样) — 重挂整步可撤销。
    //    (连接手势的 undo 栈 = ParamDocument 的内部栈, 与卡片一致.)
    doc.undoStack()->undo();
    const cad::param::Attachment* undone2 = doc.findAttachment(att.id);
    QVERIFY(undone2);
    QVERIFY2(undone2->angleOnly, "undo 应回到仅角度态");
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
// 连接卡片语义 (用户拍板 2026-08 复旧; SegmentConnectionCard):
//   · 新建连接默认**焊接** (isLocked=true) — 拖动保护默认勾选, 拖任一端整对
//     移动不拆; 拆散走 D 键快拆 / 面板取消「拖动保护」。
//   · 「位置吸附」✗ = 彻底断开 (删 attachment, 位置+角度跟随一并解除);
//   · 「拖动保护」✓/✗ = 焊接/解焊开关 (checked = isLocked): 取消 = 解除焊接
//     (连接保持, 拖跟随线可拆散 — 不是仅角度);
//     仅角度态勾选 = 恢复完整连接 + 重新焊接;
//   · 断开后记忆最近宿主, 重新勾选「位置吸附」一键恢复 (角度反算无跳变,
//     重建连接默认焊接).
// ---------------------------------------------------------------------------
void TestSelectWKey::connectionCardNewSemantics()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    auto line   = makeLine(doc, 60.0);
    doc.resolveAll();

    // 建立连接 (用户拍板 2026-08 复旧: 默认勾选拖动保护 = 焊接).
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

    auto findChk = [&](const QString& prefix) -> QCheckBox* {
        for (auto* c : card.findChildren<QCheckBox*>())
            if (c->text().startsWith(prefix)) return c;
        return nullptr;
    };
    QCheckBox* chkHost = findChk(QString::fromUtf8("\u8fde\u63a5\u7ebf\u6bb5"));  // 连接线段开关
    QCheckBox* chkLock = findChk(QString::fromUtf8("\u8fde\u63a5\u4fdd\u62a4"));   // 连接保护
    QVERIFY(chkHost);
    QVERIFY(chkLock);
    // 初始: 完整连接且已焊 → 连接线段 ✓ / 拖动保护 ✓.
    QVERIFY(chkHost->isChecked());
    QVERIFY(chkLock->isChecked());

    // 记录断开前跟随线的世界朝向 (重连后必须保持).
    const auto* blk = doc.findBlock(line.blockId);
    const Vec2 w1 = blk->transform.toWorld(blk->findPoint(line.startId)->resolvedPos);
    const Vec2 w2 = blk->transform.toWorld(blk->findPoint(line.endId)->resolvedPos);
    const double dirBefore = std::atan2(w2.y - w1.y, w2.x - w1.x);

    // 1) 取消「拖动保护」 = 解除焊接 (连接保持完整, 拖跟随线可拆散 —
    //    不得切换为仅角度).
    chkLock->setChecked(false);
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.isLocked, "取消拖动保护 = 解除焊接");
        QVERIFY2(!a.angleOnly, "解焊后仍是完整连接 (不是仅角度)");
    }

    // 2) 重新勾选「拖动保护」 = 焊接 (isLocked=true, 拖任一端整对移动).
    chkLock->setChecked(true);
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.isLocked, "勾选拖动保护 = 焊接");
        QVERIFY2(!a.angleOnly, "焊接态仍是完整连接");
    }

    // 3) 仅角度态 (D 键/拖拆的模型结果) 勾选「拖动保护」 = 恢复完整连接 +
    //    重新焊接 (命令路径修复: 不得沿用 m_oldLocked=false 而显示
    //    "✓ 拖动保护但可拖拆").
    {
        cad::cmd::SetAttachmentAngleOnlyCommand cmd(&doc,
            doc.attachments().front().id, /*angleOnly=*/true);
        cmd.redo();
        QVERIFY(doc.attachments().front().angleOnly);
    }
    card.refresh();   // 直接命令改模型后卡片需同步 (复选框状态回未勾选)
    QVERIFY(!chkLock->isChecked());
    chkLock->setChecked(true);
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly, "仅角度态勾选拖动保护 = 恢复完整连接");
        QVERIFY2(a.isLocked, "恢复完整连接必须重新焊接");
    }

    // 统一状态模型 (用户 2026-12-15 拍板): 无「模式」下拉 —— 跟随/独立线段
    // 由 Attachment 存在性推导 (标题 + 行形态), 连接开关 = 「位置吸附」复选框。
    auto hasRowLabel = [&](const QString& text) {
        for (auto* t : card.findChildren<ElaText*>())
            if (t->text() == text) return true;
        return false;
    };

    // 4) 取消「连接线段」复选框 = 彻底断开 (attachment 删除)。
    chkHost->setChecked(false);
    QVERIFY2(doc.attachments().empty(),
             "取消连接线段 = 彻底断开");
    // 统一表单: 恒显「连接线段」行 (自由线 = 连接字段为空; 2026-12 去卡框
    // 化后标签为短词无冒号).
    QVERIFY2(hasRowLabel(QString::fromUtf8("连接线段")),
             "连接行标签恒为「连接线段」");
    // 2026-12 去卡框化: 卡内不再自持标题 —— 恒定「连接」标题由属性页「连接」
    // 分区标题提供 (LinePropertyDialog), 不做 独立线段/跟随 状态区分。
    QVERIFY2(!hasRowLabel(QString::fromUtf8("独立线段")),
             "不得再出现「独立线段」状态标题");

    // 5) 再勾选「连接线段」 = 记忆宿主一键恢复: attachment 重建、目标不变、
    //    世界朝向反算保持 (零跳变)、默认焊接.
    chkHost->setChecked(true);
    QCOMPARE(doc.attachments().size(), size_t(1));
    {
        const auto& a = doc.attachments().front();
        QCOMPARE(a.toBlockId, leader.blockId);
        QCOMPARE(a.toPointId, leader.endId);
        QVERIFY2(a.isLocked, "重建连接默认焊接");
        const auto* blk2 = doc.findBlock(line.blockId);
        const Vec2 v1 = blk2->transform.toWorld(blk2->findPoint(line.startId)->resolvedPos);
        const Vec2 v2 = blk2->transform.toWorld(blk2->findPoint(line.endId)->resolvedPos);
        const double dirAfter = std::atan2(v2.y - v1.y, v2.x - v1.x);
        double dAng = std::abs(dirAfter - dirBefore);
        dAng = std::fmod(dAng, 2.0 * M_PI);
        if (dAng > M_PI) dAng = 2.0 * M_PI - dAng;
        QVERIFY2(dAng < 1e-9,
                 qPrintable(QStringLiteral("一键恢复后方向跳变 %1 度")
                            .arg(dAng * 180.0 / M_PI, 0, 'f', 6)));
    }

    // 6) undo 栈覆盖: 断开 (remove) 后 Ctrl+Z 恢复连接.
    card.setTarget(line.blockId, line.segId);
    QVERIFY(chkHost->isChecked());
    chkHost->setChecked(false);
    QVERIFY(doc.attachments().empty());
    doc.undoStack()->undo();   // RemoveAttachmentCommand.undo → 连接恢复
    QCOMPARE(doc.attachments().size(), size_t(1));

    // 7) 断开记忆快照 (用户 2026-12-15 拍板: 模式下拉已删, 快照恢复并入
    //    「位置吸附」复选框): 取消勾选 = 快照完整连接配置后拆除; 重新勾选 =
    //    优先原样恢复 (宿主/角度/焊接/公式/角度基准), 且零跳变。
    card.setTarget(line.blockId, line.segId);
    QVERIFY(chkHost->isChecked());
    const auto* blk7 = doc.findBlock(line.blockId);
    const Vec2 w7a = blk7->transform.toWorld(blk7->findPoint(line.startId)->resolvedPos);
    const Vec2 w7b = blk7->transform.toWorld(blk7->findPoint(line.endId)->resolvedPos);
    const double dir7 = std::atan2(w7b.y - w7a.y, w7b.x - w7a.x);
    const double followerAngle7 = doc.attachments().front().followerAngle;

    chkHost->setChecked(false);      // 断开 → 完整配置进快照
    QVERIFY(doc.attachments().empty());
    QVERIFY(!chkHost->isChecked());
    chkHost->setChecked(true);       // 恢复 → 快照原样回来
    QCOMPARE(doc.attachments().size(), size_t(1));
    {
        const auto& a = doc.attachments().front();
        QCOMPARE(a.toBlockId, leader.blockId);
        QCOMPARE(a.toPointId, leader.endId);
        QCOMPARE(a.followerAngle, followerAngle7);
        QVERIFY2(a.isLocked, "快照恢复必须原样保持焊接");
        QVERIFY2(!a.angleOnly, "快照恢复不得退化为仅角度");
        const auto* blk = doc.findBlock(line.blockId);
        const Vec2 v1 = blk->transform.toWorld(blk->findPoint(line.startId)->resolvedPos);
        const Vec2 v2 = blk->transform.toWorld(blk->findPoint(line.endId)->resolvedPos);
        const double dir = std::atan2(v2.y - v1.y, v2.x - v1.x);
        double dAng = std::abs(dir - dir7);
        dAng = std::fmod(dAng, 2.0 * M_PI);
        if (dAng > M_PI) dAng = 2.0 * M_PI - dAng;
        QVERIFY2(dAng < 1e-9,
                 qPrintable(QStringLiteral("断开快照恢复后方向跳变 %1 度")
                            .arg(dAng * 180.0 / M_PI, 0, 'f', 6)));
    }
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

QTEST_MAIN(TestSelectWKey)
#include "test_select_wkey.moc"
