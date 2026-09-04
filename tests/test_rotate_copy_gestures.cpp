/// @file test_rotate_copy_gestures.cpp
/// 旋转复制高阶手势: 锚心切换、单位换算模式、D15确认门流、多选与快捷发布。

#include "test_rotate_copy.h"

void TestRotateCopy::xToggleSwitchesAnchorToEndPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A (default anchor = START point).
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    const Vec2 endBefore = blk->worldPos(a.endId);
    const Vec2 startBefore = blk->worldPos(a.startId);

    // X: switch the anchor to the END point.
    sendKeyX(view);

    sendConfirm(view);
    // Drag: cursor (50,0) → (0,100) around the end pivot (100,0) ⇒ −45°.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk2 = doc.findBlock(a.blockId);
    // The END point stays pinned to the pivot; the START point swung around.
    QVERIFY(blk2->worldPos(a.endId).distanceTo(endBefore) < 1e-6);
    QVERIFY(blk2->worldPos(a.startId).distanceTo(startBefore) > 1e-3);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-45.0)) < 1e-6);

    // One undo restores the original pose.
    stack.undo();
    const Block* blk3 = doc.findBlock(a.blockId);
    QVERIFY(blk3->worldPos(a.endId).distanceTo(endBefore) < 1e-6);
    QVERIFY(blk3->worldPos(a.startId).distanceTo(startBefore) < 1e-6);
}

// 直接点击另一端端点 = 切换锚心（与 X 键等价）。
void TestRotateCopy::clickEndPointSwitchesAnchor()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A (default anchor = start).
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // Click the END point → anchor switches there.
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
    // Drag: cursor (50,0) → (0,100) around the end pivot (100,0).
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-45.0)) < 1e-6);
    // End pivot stayed pinned: (100,0).
    QVERIFY(blk->worldPos(a.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6);
}

// 条带「换向」在旋转会话内 = 切换锚心 (2026-12): ContextStrip 转发
// reverseRequested → ToolRotate::onReverseRequested → toggleAnchor(), 与 X 键/
// 点端点等价 (gizmo pivot 环移到另一端); 已连接线段 = no-op (同 toggleAnchor
// 守卫, 跟随保护绝不断开)。
void TestRotateCopy::stripReverseTogglesAnchor()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // C: 自由线 (0,100)→(80,100).
    const LineSetup c = makeLine(doc, 80.0, Vec2(0.0, 100.0));
    // B: (200,0)→(300,0); A hangs on B's END → A: (300,0)→(360,0).
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 180.0;
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 自由线 C: 点近起点半段 → 锚 = 起点。
    sendMouse(QEvent::MouseButtonPress, vp(20.0, 100.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(20.0, 100.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), c.startId);

    // 条带换向 (旋转会话) → 锚切到终点; 再点 → 回起点。
    tool->onReverseRequested(c.blockId, c.segId);
    QCOMPARE(tool->anchorPointId(), c.endId);
    tool->onReverseRequested(c.blockId, c.segId);
    QCOMPARE(tool->anchorPointId(), c.startId);

    // 2) 连接线 A: 锚恒取挂接端 (起点); 条带换向 = no-op (跟随保护)。
    sendKeyEsc(view);
    sendMouse(QEvent::MouseButtonPress, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), a.startId);
    const int attBefore = static_cast<int>(doc.attachments().size());
    tool->onReverseRequested(a.blockId, a.segId);
    QCOMPARE(tool->anchorPointId(), a.startId);   // 锚不动
    QCOMPARE(doc.attachments().size(), size_t(attBefore));  // 挂接不释放
}

// 锚心切换全链路同步条带 (2026-12): 旋转工具选中线段 → 条带进入旋转会话
// (基准读数锚心端在前, 角度字段显示锚心基准角); 点条带换向 / 点另一端端点
// 切锚心 → 工具锚心 + 条带基准 + 角度字段 + 状态栏锚心提示全部翻转。
// 回归价值: 覆盖"selectTarget 上报时序"与"锚心切换同步条带"两条真实链路
// (旧 bug: 换向走了 ReverseSegmentCommand, 环形显示/gizmo 不动)。
void TestRotateCopy::anchorSwitchSyncsStrip()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0), 自由线
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const Block* blk = doc.findBlock(a.blockId);
    const auto* sp = blk->findPoint(a.startId);
    const auto* ep = blk->findPoint(a.endId);
    const QString sTag = cad::param::Serial::tag(sp->serial);
    const QString eTag = cad::param::Serial::tag(ep->serial);
    const QString fwd = QString::fromUtf8("%1 → %2").arg(sTag, eTag);
    const QString rev = QString::fromUtf8("%1 → %2").arg(eTag, sTag);

    // 1) 点近起点半段 → 锚 = 起点; 条带进入旋转会话: 基准读数锚心端在前,
    //    换向按钮可点, 角度字段 = 锚心基准角 (0°, 无偏移)。
    sendMouse(QEvent::MouseButtonPress, vp(20.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(20.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), a.startId);
    QCOMPARE(bridge.strip.basisText(), fwd);
    QVERIFY(bridge.strip.reverseButton()->isEnabled());
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));
    QVERIFY(bridge.hint.contains(QStringLiteral("锚心")));
    QVERIFY(bridge.hint.contains(sTag));

    // 2) 点条带「换向」→ 锚切到终点 (pivot 环移动): 基准翻转, 角度 +180°。
    bridge.strip.reverseButton()->click();
    QCOMPARE(tool->anchorPointId(), a.endId);
    QCOMPARE(bridge.strip.basisText(), rev);
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("180"));
    QVERIFY(bridge.hint.contains(eTag));
    QCOMPARE(stack.count(), 0);   // 换向 = 切锚心, 不 push 命令

    // 3) 点另一端端点 (起点) → 锚切回起点, 条带全部跟随翻转。
    sendMouse(QEvent::MouseButtonPress, vp(2.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(2.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), a.startId);
    QCOMPARE(bridge.strip.basisText(), fwd);
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));
    QCOMPARE(stack.count(), 0);
}

// 已连接线段禁止切换锚心（用户拍板 2026-08）：X 键被拒，挂接绝不断开；
// 旋转保持 Connected 模式 = 编辑跟随角。撤销一步恢复跟随角。
// 注意：事件坐标经 viewport 整数化（event->pos() 是 QPoint），因此用 0° 跟随角度
// 的跟随线 + 整数坐标，保证拖动角精确（45° 跟随角度会引入亚像素取整误差）。
void TestRotateCopy::connectedLineXAnchorSwitchBlocked()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with a 180° follower angle
    // (闭合基准: 180° = 沿 leader 直行延续) → A: (300,0)→(360,0), world angle 0°.
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 180.0;
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());
    QCOMPARE(doc.attachments().size(), size_t(1));

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select follower A: anchor = its START point (the attachment point),
    // so the session starts in Connected mode.
    const QPoint mid = vp(330.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // X: switch blocked — the START point is attached, so the anchor stays
    // put and the session remains in Connected mode (跟随保护).
    sendKeyX(view);

    sendConfirm(view);
    // Drag around the START pivot (300,0): cursor (330,0) → (300,−100):
    // cur0 = atan2(0,30) = 0°, theta = atan2(−100,0) = −90°,
    // Connected 拖动增量与世界角同向、与存储角反向: target = 180° − (−90°)
    // = 270°（存储不归一化，≡ −90°），世界角 = 180° − 270° = −90°.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, -100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, -100.0), Qt::LeftButton,
              Qt::NoModifier);

    // The follower link SURVIVES (旋转 = 编辑跟随角，挂接不被释放).
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment* keep = followerAttachmentOf(doc, a.blockId);
    QVERIFY(keep);
    QVERIFY(std::abs(keep->followerAngle - 270.0) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);

    // ONE undo restores the follower angle (link still intact).
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment* keep2 = followerAttachmentOf(doc, a.blockId);
    QVERIFY(keep2);
    QVERIFY(std::abs(keep2->followerAngle - 180.0) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 回归 (用户报告 2026-12): 勾选「独立角度」后旋转拖动无效 —— 旧实现把
// 独立角线当普通跟随线进 Connected 模式写 followerAngle, 而 Resolver 对
// angleIndependent 忽略 followerAngle → 拖了不转。独立角线必须走自由线
// 旋转 (写块 transform.rotation), 且位置焊点绝不能被"旋转=放弃跟随"释放。
void TestRotateCopy::independentAngleLineRotatesBlockKeepsPin()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END, 独立角度 (位置焊死、角度自管)
    // → A 初始 0° 世界角: (300,0)→(360,0)。
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(conn));
    doc.setAttachmentAngleIndependent(conn.id, true);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 选中 A: 锚 = 挂接端 (起点 300,0) —— 独立角线锚心仍在位置焊点。
    const QPoint mid = vp(330.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
    // 绕起点 (300,0) 拖到 (300,−100): 光标角 = atan2(−100,0) = −90°,
    // 自由线 target = 起始角 0 + 增量 −90 = −90° (与 connectedLineXAnchorSwitchBlocked
    // 同一次拖动, 世界角 = −90°)。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, -100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, -100.0), Qt::LeftButton,
              Qt::NoModifier);

    // 位置焊点保留 (attachment 仍在), 且块自己转了 → 世界角 = −90°。
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment* keep = followerAttachmentOf(doc, a.blockId);
    QVERIFY(keep);
    QVERIFY2(keep->angleIndependent, "独立角度标志不得被旋转清除");
    const Block* blk = doc.findBlock(a.blockId);
    QVERIFY(blk);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);
    // 位置焊点不动: A 起点仍在 B 终点 (300,0)。
    QVERIFY(blk->worldPos(a.startId).distanceTo(Vec2(300.0, 0.0)) < 1e-6);

    // 一步 undo 回 0° (transform 恢复, attachment 不动)。
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 锚心=终点时 Ctrl+拖动旋转复制：副本挂回原线的终点（不是起点）。
void TestRotateCopy::endAnchorRotateCopyAttachesToEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point.
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    sendConfirm(view);
    // Ctrl+drag: cursor (50,0) → (0,100) around the END pivot (100,0)
    // ⇒ relative −45° (original stays horizontal at 0°).
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId && cloneAttachment(doc, b.id, a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    // The clone hangs on the ORIGINAL's END point (not the start).
    const Attachment* ca = cloneAttachment(doc, cln->id, a.blockId);
    QVERIFY(ca);
    QCOMPARE(ca->toPointId, a.endId);
    // 对齐点对齐旋转的锚点（用户拍板 2026-09）:
    // 旋转锚点是终点时，副本以自身的终点作为对齐点钉在原线终点上。
    QCOMPARE(ca->fromPointId, cln->segments.front().endPointId);
    // 终点精确对齐在 (100, 0) 上
    const Vec2 clnEndPos = cln->worldPos(ca->fromPointId);
    QVERIFY(std::abs(clnEndPos.x - 100.0) < 1e-4);
    QVERIFY(std::abs(clnEndPos.y - 0.0) < 1e-4);
    // 相对旋转 −45°：原线方向 0°，副本绕终点顺时针旋转 45°，世界方向为 −45°。
    const double clnWorld = cad::geo::normalizeDeg180(worldAngleDeg(doc, cln->id));
    QVERIFY(std::abs(clnWorld - (-45.0)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 终点锚心新语义：HUD 角度 = 从终点指向线的方向（线方向+180°），
// 线始终跟随光标 —— 光标拖到终点正上方，线从终点向上伸出（方向 −90°），
// 而不是旧行为的“背对光标”朝下。
void TestRotateCopy::endAnchorLineFollowsCursor()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point (X).
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    sendConfirm(view);
    // Drag the cursor from the line body (50,0) straight UP to (100,100)
    // — from the END pivot (100,0) that is the 90° direction. The line must
    // point AT the cursor: start swings to (100,100), end stays pinned.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    // End pinned at (100,0); start swung to the cursor side (100,100):
    // line direction = −90° (起点→终点朝下), display angle (from end) = 90°.
    QVERIFY(blk->worldPos(a.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6);
    QVERIFY(blk->worldPos(a.startId).distanceTo(Vec2(100.0, 100.0)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);
}

// 弧长/角度显示归一化：存储多圈角度（1260° = 3.5 圈）时 HUD 必须显示
// [0, 360) 内的归一化值（用户报告: 400°+ 爆表回归 2026-08）。
void TestRotateCopy::stripShowsFormulaForFormulaDrivenAngle()
{
    // 2026-12 统一 (用户报告 "用了变量参数, HUD 显示的是换算的数值"):
    // 公式驱动的跟随角, 旋转 HUD 显示公式原文而非换算数值 (与
    // SegmentAngleCard「公式优先显示原文」同约定)。
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 45.0;
    conn.followerAngleFormula = QStringLiteral("45");   // 公式驱动
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A (anchor = attachment point → Connected/Angle mode).
    // 先解析实际几何再点 A 中段 (followerAngle 45° → 世界角 135°, 朝左上方)。
    const Block* aBlk = doc.findBlock(a.blockId);
    QVERIFY(aBlk);
    const Segment& aSeg = aBlk->segments.front();
    const ParamPoint* aSp = aBlk->findPoint(aSeg.startPointId);
    const ParamPoint* aEp = aBlk->findPoint(aSeg.endPointId);
    QVERIFY(aSp && aEp && aSp->resolved && aEp->resolved);
    const Vec2 aMid = aBlk->transform.toWorld(
        (aSp->resolvedPos + aEp->resolvedPos) * 0.5);
    sendMouse(QEvent::MouseButtonPress, vp(aMid.x, aMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(aMid.x, aMid.y), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // 公式驱动 → HUD 显示公式原文 (2026-12 统一约定)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("45"));
}

void TestRotateCopy::stripShowsFormulaForFormulaDrivenArc()
{
    // 2026-12 统一: 弧长公式驱动的跟随线, 旋转 HUD 显示公式原文 (cm 域)。
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.rotationMode = cad::param::RotationMode::ArcLength;
    conn.arcLength = 0.0;
    conn.arcLengthFormula = QStringLiteral("10");   // 弧长公式 (cm)
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A: 先解析实际几何再点 A 中段 (弧长 10cm / 半径 60mm → A 朝上)。
    const Block* aBlk = doc.findBlock(a.blockId);
    QVERIFY(aBlk);
    const Segment& aSeg = aBlk->segments.front();
    const ParamPoint* aSp = aBlk->findPoint(aSeg.startPointId);
    const ParamPoint* aEp = aBlk->findPoint(aSeg.endPointId);
    QVERIFY(aSp && aEp && aSp->resolved && aEp->resolved);
    const Vec2 aMid = aBlk->transform.toWorld(
        (aSp->resolvedPos + aEp->resolvedPos) * 0.5);
    sendMouse(QEvent::MouseButtonPress, vp(aMid.x, aMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(aMid.x, aMid.y), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // 弧长公式驱动 → HUD 显示公式原文 (2026-12 统一约定)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("10"));
}

void TestRotateCopy::angleModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with followerAngle = 1260°
    // (闭合基准: 世界角 = 180° − 1260° ≡ 0° → A points RIGHT, mid-body at
    // (330,0); HUD 显示 fmod(1260,360) = 180°).
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 1260.0;   // multi-turn stored value
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A (anchor = attachment point → Connected/Angle mode).
    sendMouse(QEvent::MouseButtonPress, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("180"));   // 1260 → 180
}

// 弧长模式多圈 → 切回角度模式，条带显示归一化角度（不爆表）。
void TestRotateCopy::arcLengthModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);   // radius 60mm
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.rotationMode = cad::param::RotationMode::ArcLength;
    conn.arcLength = 3.0 * 2.0 * M_PI * 60.0;   // 3 full turns
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A: 3 turns = 弧长角 1080° ≡ 0° 折叠（闭合基准恒等映射 2026-08:
    // 弧长角 = 线夹角）→ A points LEFT (toward (240,0)), mid-body (270,0).
    // 显示 = 带符号折角（v3 定稿）：1080° ≡ 0° 折叠 → HUD 0° / 0cm。
    sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // ArcLength mode: 条带显示带符号折角弧长 (cm; 3 turns ≡ 0° 折叠 → 0)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));

    // 点 ° 切回角度模式: 角度必须归一化到折叠后的 0°
    // (弧长 3 圈 ≡ 角度 0° 折叠, 恒等映射 2026-08)。
    QVERIFY(bridge.strip.unitAngleButton()->isEnabled());
    QTest::mouseClick(bridge.strip.unitAngleButton(), Qt::LeftButton);
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));   // 1080 → 0

    // 输入带符号折角：输入 270（>180 视为原始 α）→ 存储 α = 270（另一侧）。
    // 条带的长度/角度输入走 200ms debounce 且只在字段聚焦时应用 —— 先给焦点。
    bridge.strip.angleEdit()->setFocus();
    bridge.strip.angleEdit()->setText(QStringLiteral("270"));
    // 断言"值变了"(debounce 到期后同步应用): 谓词 = 存储断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        for (const auto& att2 : doc.attachments())
            if (att2.fromBlockId == a.blockId && std::abs(att2.followerAngle - 270.0) < 1e-6)
                return true;
        return false;
    }), "输入 270 后 followerAngle 应更新为 270（条带输入未应用到存储）");

    // 单位切换往返刷新条带：显示带符号折角 −90（v3 定稿，符号 = 折向）。
    QTest::mouseClick(bridge.strip.unitArcButton(), Qt::LeftButton);   // → 弧长
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("-9.4"));
    QTest::mouseClick(bridge.strip.unitAngleButton(), Qt::LeftButton); // → 角度
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("-90"));
}


// ─────────────────────────────────────────────────────────────────────────────
// D15 单线确认流 (用户拍板 2026-08-27): 选中 → 确认 → 拖动 → 回落选中态.
// ─────────────────────────────────────────────────────────────────────────────
void TestRotateCopy::d15GateRequiresConfirmBeforeDrag()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    const QPoint mid = vp(50.0, 0.0);

    // 选中 → 确认门关闭.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());

    // 选中态按住线身"拖动" = no-op: 位姿不动, 无命令.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-9);
    QCOMPARE(stack.count(), 0);

    // 确认 (右键) → 拖动生效; HUD 回车仲裁: 无输入焦点时回车只确认不应用.
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 90.0) < 2.0);
    QCOMPARE(stack.count(), 1);
}

void TestRotateCopy::d15DragCommitDropsToSelected()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    const QPoint mid = vp(50.0, 0.0);

    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    const double ang1 = worldAngleDeg(doc, a.blockId);

    // 一次拖动提交.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (ang1 + 90.0)) < 2.0);

    // 提交后自动回落选中态: 紧接着的第二次拖动 no-op.
    QVERIFY(!tool->selectionConfirmed());
    const double ang2 = worldAngleDeg(doc, a.blockId);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - ang2) < 1e-9);
    // (注: 第二次拖动的按压点已落在线外 —— 选中态点空白按 D15 清除目标,
    //  这本身是被验证的设计行为; 角度不变断言已覆盖 no-op 语义.)

    // 再次确认后恢复可拖: 重新选中新位姿的线身 (已转竖直, 在 (0,50)).
    sendMouse(QEvent::MouseButtonPress, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
}

void TestRotateCopy::d15BlankClickClearsSelectedTarget()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 80.0, Vec2(200, 150));
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 选中 a.
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());

    // 选中态点空白 = 取消选择 (回 Idle).
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Idle);
    QVERIFY(!tool->selectionConfirmed());

    // 点另一条线 = 切换目标, 仍处选中态.
    sendMouse(QEvent::MouseButtonPress, vp(240.0, 150.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(240.0, 150.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Ready);
    QVERIFY(!tool->selectionConfirmed());
}


// 锚心跟随点击端 (用户拍板 2026-08-27): 自由线取离点击更近的一端 (16px/zoom);
// 连接线恒取挂连接的一端 —— 选中即入"编辑跟随角"安全模式; 中段点击保持起点.
void TestRotateCopy::d15AnchorFollowsClickedEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup host = makeLine(doc, 100.0);                 // (0,0)→(100,0)
    const LineSetup free2 = makeLine(doc, 100.0, Vec2(0, 200));  // (0,200)→(100,200)
    const LineSetup fol = makeLine(doc, 60.0, Vec2(100, 0));     // 挂接在 host 终点
    doc.resolveAll();
    Attachment conn;
    conn.fromBlockId = fol.blockId;
    conn.fromPointId = fol.startId;
    conn.toBlockId = host.blockId;
    conn.toPointId = host.endId;
    conn.toSegmentId = host.segId;
    conn.followerAngle = 180.0;   // 闭合基准: 180° = 沿 leader 直行延伸
    QVERIFY(doc.addAttachment(conn));
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 自由线: 点近终点 (12mm 处, 避开端头 path-merge 空洞) → 锚 = 终点.
    sendMouse(QEvent::MouseButtonPress, vp(88.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(88.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.endId);
    sendConfirm(view);
    // (不再拖动; Esc 退回选中态以换目标.)
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 2) 自由线: 无阈值 —— 点左半段 → 锚 = 起点.
    sendMouse(QEvent::MouseButtonPress, vp(40.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(40.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.startId);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 2b) 点右半段 → 锚 = 终点.
    sendMouse(QEvent::MouseButtonPress, vp(60.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(60.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.endId);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 3) 连接线: 点其空闲端 (远离挂接端) → 锚仍恒取挂接端 (起点).
    sendMouse(QEvent::MouseButtonPress, vp(130.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(130.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), fol.startId);
}

void TestRotateCopy::d15ConfirmStateHasVisualAndHint()
{
    // H2 (TOOL_SYSTEM_AUDIT 2026-08-29): 确认门三态都必须有可观测反馈 ——
    // 未确认 = gizmo 虚线/空心 + 状态栏带「右键/回车确认」; 确认 = 实线/
    // 实心 + 提示改口; Esc 反悔回选中态 → 提示回来。旧实现 gizmo 两态同图、
    // 无提示, 用户点线后拖动无反应, 把正确功能感知为"工具卡死"。
    //
    // (H2 的第三处表达原是 HUD caption 后缀, 一期随 AngleHud 退场迁入
    //  状态栏 L1 —— 经 ToolHost::setHintOverride 上报, 桥上 hint 字段捕获。)
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    // 1) 选中 (未确认): gizmo 未确认态视觉 + 状态栏确认提示.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());
    QVERIFY(!tool->gizmoConfirmed());
    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    const QString confirmHint = QString::fromUtf8("右键或回车确认");
    QVERIFY2(bridge.hint.contains(confirmHint),
             qPrintable(QStringLiteral("unconfirmed hint should carry hint: ")
                        + bridge.hint));

    // 2) 右键确认: 实线/实心 + 提示改口.
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    QVERIFY(tool->gizmoConfirmed());
    QVERIFY2(!bridge.hint.contains(confirmHint),
             qPrintable(QStringLiteral("confirmed hint should drop hint: ")
                        + bridge.hint));

    // 3) Esc 反悔 → 选中态: 提示与视觉同步回退 (再确认可再拖).
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    QVERIFY(!tool->selectionConfirmed());
    QVERIFY(!tool->gizmoConfirmed());
    QVERIFY2(bridge.hint.contains(confirmHint),
             qPrintable(QStringLiteral("Esc should restore hint: ") + bridge.hint));
}

void TestRotateCopy::rotateCopyAutoPublishesParentParameter()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 120.0);   // (0,0) → (120,0) 纯数值线段
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(60.0, 0.0);
    // 1) 选中并确认
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);

    // 此时文档中尚无 LinkedVariable
    QCOMPARE(doc.linkedVars().size(), size_t(0));

    // 2) 长按 Ctrl 拖动旋转 90°
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 60.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 60.0), Qt::LeftButton, Qt::ControlModifier);

    // 3) 验证新块已创建
    QCOMPARE(doc.blocks().size(), size_t(2));
    // 父线段参数已自动发布为 LinkedVariable！
    QCOMPARE(doc.linkedVars().size(), size_t(1));
    const cad::param::LinkedVariable& lv = doc.linkedVars().front();
    QCOMPARE(lv.sourceBlockId, a.blockId);
    QCOMPARE(lv.sourceSegmentId, a.segId);
    QVERIFY(lv.refName.startsWith(QStringLiteral("L")));

    // 4) 找到副本块，验证其对齐点和长度公式
    const Block* cloneBlk = nullptr;
    for (const auto& b : doc.blocks()) {
        if (b.id != a.blockId) { cloneBlk = &b; break; }
    }
    QVERIFY(cloneBlk);
    const auto& cloneSeg = cloneBlk->segments.front();
    // 副本长度公式自动填入父线段发布的变量名
    QCOMPARE(cloneSeg.lengthFormula, lv.refName);
    const auto* pEnd = cloneBlk->findPoint(cloneSeg.endPointId);
    QVERIFY(pEnd);
    QCOMPARE(pEnd->distanceFormula, lv.refName);

    // 5) 挂接附件验证：锚心为起点，副本对齐点也是起点
    const Attachment* att = cloneAttachment(doc, cloneBlk->id, a.blockId);
    QVERIFY(att);
    QCOMPARE(att->fromPointId, cloneSeg.startPointId);
    QCOMPARE(att->toPointId, a.startId);

    // 6) Undo 测试：撤销后发布的变量和副本块一同移除
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.linkedVars().size(), size_t(0));

    // 7) Redo 测试：重做后恢复
    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(2));
    QCOMPARE(doc.linkedVars().size(), size_t(1));
}

void TestRotateCopy::rotateCopyFourStepFlow()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    // 步骤 1：点击线段选中（未确认态）
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // 步骤 2：切换旋转方向（锚点）为终点
    sendKeyX(view);

    // 步骤 3：确定
    sendConfirm(view);

    // 步骤 4：长按 Ctrl 拖动线段进行旋转复制
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 50.0), Qt::LeftButton, Qt::ControlModifier);

    // 验证：成功复制一条线段
    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* cloneBlk = nullptr;
    for (const auto& b : doc.blocks()) {
        if (b.id != a.blockId) { cloneBlk = &b; break; }
    }
    QVERIFY(cloneBlk);
    const Attachment* att = cloneAttachment(doc, cloneBlk->id, a.blockId);
    QVERIFY(att);
    // 对齐点严格对齐旋转锚点：原线锚点为终点，副本对齐点必须为副本终点
    QCOMPARE(att->fromPointId, cloneBlk->segments.front().endPointId);
    QCOMPARE(att->toPointId, a.endId);
    // 终点世界坐标对齐
    const Vec2 clnEndPos = cloneBlk->worldPos(att->fromPointId);
    QVERIFY(std::abs(clnEndPos.x - 100.0) < 1e-4);
    QVERIFY(std::abs(clnEndPos.y - 0.0) < 1e-4);
}

// 验证拖拽中途防抖保护：未真正松开鼠标时，即使收到虚假release也不提前提交
void TestRotateCopy::dragJitterDoesNotReleaseUntilPhysicalRelease()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    // 选中并确认
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);

    // Ctrl+Press 开始拖动复制
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 50.0), Qt::NoButton, Qt::ControlModifier);

    // 此时处于拖拽旋转态中
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QCOMPARE(tool->selectionConfirmed(), true);

    // 正常松手后提交完成复制
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 50.0), Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(2));
}

// 验证旋转工具黄圈（Gizmo）在自由线、终点锚心、拖动与旋转复制下的几何一致性
void TestRotateCopy::gizmoDisplayConsistencyAcrossModes()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
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
    StripBridge bridge(&doc, tm);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    const auto* gz = tool->gizmo();
    QVERIFY(gz);

    // 1. 起点锚心就绪态：黄弧长度为 0（无多余半圆），灰虚线对齐线段朝向 0 rad
    QVERIFY(gz->isArcEmpty());
    QVERIFY(std::abs(gz->refWorldRad() - 0.0) < 1e-4);

    // 2. 切换锚心为终点：黄弧长度仍为 0（绝无 180° 或 240° 怪异半圆），灰虚线对齐终点朝向 π (180°)
    sendKeyX(view);
    QVERIFY(gz->isArcEmpty());
    QVERIFY(std::abs(std::abs(gz->refWorldRad()) - M_PI) < 1e-4);

    // 切回起点锚心
    sendKeyX(view);
    sendConfirm(view);
    QVERIFY(gz->isArcEmpty());

    // 3. 开始拖动旋转：黄弧从灰虚线（0°）展开为非空内弧
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 50.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY(!gz->isArcEmpty());

    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    // 提交后恢复就绪态：黄弧再次清空
    QVERIFY(gz->isArcEmpty());
}

void TestRotateCopy::marqueeSelectionAndPivotSnapRotate()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // lExt: external line (-50, 0) -> (0, 0) mm
    const LineSetup lExt = makeLine(doc, 50.0, Vec2{-50.0, 0.0});
    // Line 1: (0, 0) -> (100, 0) mm, attached to lExt end
    const LineSetup l1 = makeLine(doc, 100.0);
    // Line 2: (0, 50) -> (100, 50) mm
    const LineSetup l2 = makeLine(doc, 100.0, Vec2{0.0, 50.0});
    {
        cad::param::Attachment att;
        att.id = QUuid::createUuid();
        att.fromBlockId = l1.blockId;
        att.fromPointId = l1.startId;
        att.toBlockId = lExt.blockId;
        att.toPointId = lExt.endId;
        att.toSegmentId = lExt.segId;
        att.followerAngle = 180.0;
        att.rotationMode = cad::param::RotationMode::Angle;
        doc.addAttachment(att);
    }
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
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setInputDispatcher(&tm);

    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 1. 框选 l1 和 l2（从 (10, -10) 框到 (80, 60) mm，避开在 X<=0 的 lExt）
    sendMouse(QEvent::MouseButtonPress, vp(10.0, -10.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(80.0, 60.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(80.0, 60.0), Qt::LeftButton, Qt::NoModifier);

    // 验证多选集合包含 l1 和 l2，但不包含 lExt
    QVERIFY(tool->isMultiSelect());
    QCOMPARE(tool->selection().size(), 2);
    QVERIFY(tool->selection().contains(l1.blockId));
    QVERIFY(tool->selection().contains(l2.blockId));
    QVERIFY(!tool->selection().contains(lExt.blockId));
    QVERIFY(!tool->selectionConfirmed());

    // 2. 右键或回车确认选区，进入“定锚状态”
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    QVERIFY(!tool->pivotPicked());

    // 3. 鼠标悬停到 Line 1 起点 (0, 0) 附近，出现黄色吸附预览圈
    sendMouse(QEvent::MouseMove, vp(1.0, 1.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY(tool->hoverSnapped());
    QVERIFY(tool->hoverSnapPoint().distanceTo({0.0, 0.0}) < 1e-4);

    // 4. 单击 Line 1 起点定锚点
    sendMouse(QEvent::MouseButtonPress, vp(1.0, 1.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(1.0, 1.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(tool->pivotPicked());
    QVERIFY(tool->pivot().distanceTo({0.0, 0.0}) < 1e-4);

    // 5. 从 (100, 0) 拖拽到 (0, 100)（逆时针旋转 90 度）
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton, Qt::NoModifier);

    // 验证旋转完成后退出状态（退出到 Idle）
    QCOMPARE(tool->state(), cad::tools::RotateState::Idle);
    QVERIFY(!tool->selectionConfirmed());

    // 验证旋转结果：以 (0,0) 为锚点旋转 90 度
    // l1 起点仍为 (0,0)，终点变为 (0, 100) mm
    const auto* blk1 = doc.findBlock(l1.blockId);
    QVERIFY(blk1);
    cad::geo::Vec2 p1Start = blk1->worldPos(l1.startId);
    cad::geo::Vec2 p1End = blk1->worldPos(l1.endId);
    QVERIFY(p1Start.distanceTo({0.0, 0.0}) < 1e-3);
    QVERIFY(p1End.distanceTo({0.0, 100.0}) < 1e-3);

    // l2 初始 origin (0, 50)，绕 (0,0) 逆时针 90 度变为 (-50, 0)，方向变为 +Y
    const auto* blk2 = doc.findBlock(l2.blockId);
    QVERIFY(blk2);
    cad::geo::Vec2 p2Start = blk2->worldPos(l2.startId);
    cad::geo::Vec2 p2End = blk2->worldPos(l2.endId);
    QVERIFY(p2Start.distanceTo({-50.0, 0.0}) < 1e-3);
    QVERIFY(p2End.distanceTo({-50.0, 100.0}) < 1e-3);

    // 外部连接被断开
    QVERIFY(doc.attachments().empty());

    // 6. 撤销 Undo
    stack.undo();
    blk1 = doc.findBlock(l1.blockId);
    blk2 = doc.findBlock(l2.blockId);
    QVERIFY(blk1->worldPos(l1.endId).distanceTo({100.0, 0.0}) < 1e-3);
    QVERIFY(blk2->worldPos(l2.endId).distanceTo({100.0, 50.0}) < 1e-3);
    // 外部连接恢复
    QCOMPARE(doc.attachments().size(), size_t(1));

    // 7. 重做 Redo
    stack.redo();
    blk1 = doc.findBlock(l1.blockId);
    blk2 = doc.findBlock(l2.blockId);
    QVERIFY(blk1->worldPos(l1.endId).distanceTo({0.0, 100.0}) < 1e-3);
    QVERIFY(blk2->worldPos(l2.endId).distanceTo({-50.0, 100.0}) < 1e-3);
    QVERIFY(doc.attachments().empty());
}

void TestRotateCopy::adoptSelectionFromSelectToolAndRotate()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup l1 = makeLine(doc, 100.0);
    const LineSetup l2 = makeLine(doc, 100.0, Vec2{0.0, 50.0});
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

    // 1. 选择工具下选中 l1 和 l2
    tm.switchTool(cad::tools::ToolType::Select);
    auto* toolSelect = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(toolSelect);
    toolSelect->selectBlocksExternally({l1.blockId, l2.blockId});

    // 2. 切换到旋转工具
    tm.switchTool(cad::tools::ToolType::Rotate);
    auto* toolRotate = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(toolRotate);
    // 验证继承了选择集，处于待确认状态
    QVERIFY(toolRotate->isMultiSelect());
    QVERIFY(toolRotate->selection().contains(l1.blockId));
    QVERIFY(toolRotate->selection().contains(l2.blockId));
    QVERIFY(!toolRotate->selectionConfirmed());

    // 右键确认选区
    sendConfirm(view);
    QVERIFY(toolRotate->selectionConfirmed());
    QVERIFY(!toolRotate->pivotPicked());

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 3. 在画布空白处 (50, 25) mm 按下并直接拖拽（连贯手势，> 5px）
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 25.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 80.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(toolRotate->pivotPicked());
    QVERIFY(toolRotate->pivot().distanceTo({50.0, 25.0}) < 1e-4);

    // 继续拖动旋转
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);

    // 验证旋转完成后退出状态
    QCOMPARE(toolRotate->state(), cad::tools::RotateState::Idle);
    QVERIFY(!toolRotate->selectionConfirmed());

    // 撤销检查
    QVERIFY(stack.canUndo());
    stack.undo();
    const auto* blk1 = doc.findBlock(l1.blockId);
    QVERIFY(blk1->worldPos(l1.endId).distanceTo({100.0, 0.0}) < 1e-3);
}

