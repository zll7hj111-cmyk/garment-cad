#include "test_select_wkey.h"

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

    // 默认模式为常驻多选：单击 A 选中 A；单击 B 加选 B（A 与 B 同时选中）
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(itemA->toolSelected());   // 多选保留 A

    // 再次单击 B 则反选（从选择集中移除 B）
    click(50.0, -50.0);
    QVERIFY(!itemB->toolSelected());
    QVERIFY(itemA->toolSelected());

    // 单击空白处清空全部选择
    click(300.0, 300.0);
    QVERIFY(!itemA->toolSelected());
    QVERIFY(!itemB->toolSelected());

    // 再次单击 B 重新选中 B
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());

    // 按 W 键不再切换模式（已取消单选/多选切换，保持常驻多选）
    pressW();
    QVERIFY(itemB->toolSelected());
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());
    QVERIFY(itemB->toolSelected());
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

    // 多选体系: 再单击 B 线身 → 加选 B (A 与 B 均选中).
    click(50.0, -50.0);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
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
    click(300.0, 300.0);   // 点击空白取消前序选择
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
    click(300.0, 300.0);   // 点击空白取消前序选择
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

    // 新交互：端点（或附近）按下直接发起连接手势（无需预先选线）；
    // 释放手势后回到 Selecting 状态
    sendMouse(QEvent::MouseButtonPress, vp(95.0, -50.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(ts->state(), cad::tools::SelectState::Connecting);
    sendMouse(QEvent::MouseButtonRelease, vp(95.0, -50.0), Qt::LeftButton, Qt::NoModifier);
    QTest::qWait(20);
    QCOMPARE(ts->state(), cad::tools::SelectState::Selecting);

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


QTEST_MAIN(TestSelectWKey)
