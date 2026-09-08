#include "test_rotate_helpers.h"

class TestRotateAnchor : public QObject
{
    Q_OBJECT

private slots:
    void xToggleSwitchesAnchorToEndPoint();
    void clickEndPointSwitchesAnchor();
    void stripReverseTogglesAnchor();
    void anchorSwitchSyncsStrip();
    void connectedLineXAnchorSwitchBlocked();
    void independentAngleLineRotatesBlockKeepsPin();
    void endAnchorRotateCopyAttachesToEnd();
    void endAnchorLineFollowsCursor();
};
void TestRotateAnchor::xToggleSwitchesAnchorToEndPoint()
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
void TestRotateAnchor::clickEndPointSwitchesAnchor()
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
void TestRotateAnchor::stripReverseTogglesAnchor()
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
void TestRotateAnchor::anchorSwitchSyncsStrip()
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
void TestRotateAnchor::connectedLineXAnchorSwitchBlocked()
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
void TestRotateAnchor::independentAngleLineRotatesBlockKeepsPin()
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
void TestRotateAnchor::endAnchorRotateCopyAttachesToEnd()
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
void TestRotateAnchor::endAnchorLineFollowsCursor()
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
QTEST_MAIN(TestRotateAnchor)
#include "test_rotate_anchor.moc"