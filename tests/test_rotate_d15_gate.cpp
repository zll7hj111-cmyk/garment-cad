#include "test_rotate_helpers.h"

class TestRotateD15Gate : public QObject
{
    Q_OBJECT

private slots:
    void d15GateRequiresConfirmBeforeDrag();
    void d15DragCommitDropsToSelected();
    void d15BlankClickClearsSelectedTarget();
    void d15AnchorFollowsClickedEnd();
    void d15ConfirmStateHasVisualAndHint();
};
void TestRotateD15Gate::d15GateRequiresConfirmBeforeDrag()
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

void TestRotateD15Gate::d15DragCommitDropsToSelected()
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

void TestRotateD15Gate::d15BlankClickClearsSelectedTarget()
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
void TestRotateD15Gate::d15AnchorFollowsClickedEnd()
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

void TestRotateD15Gate::d15ConfirmStateHasVisualAndHint()
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

QTEST_MAIN(TestRotateD15Gate)
#include "test_rotate_d15_gate.moc"