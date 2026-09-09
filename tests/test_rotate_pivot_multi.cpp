#include "test_rotate_helpers.h"

#include "tools/CircleFactory.h"
#include "tools/RotateInputTracker.h"

class TestRotatePivotMulti : public QObject
{
    Q_OBJECT

private slots:
    void marqueeSelectionAndPivotSnapRotate();
    void adoptSelectionFromSelectToolAndRotate();
    void singleLinePickPivotAndRotateCadFlow();
    void pivotSnapFindsCircleCenter();
};
void TestRotatePivotMulti::marqueeSelectionAndPivotSnapRotate()
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

void TestRotatePivotMulti::adoptSelectionFromSelectToolAndRotate()
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

void TestRotatePivotMulti::singleLinePickPivotAndRotateCadFlow()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) -> (100,0)
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

    // 1. 单选线段：点击线身
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Ready);
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::Selecting);
    QVERIFY(!tool->selectionConfirmed());
    QVERIFY(!tool->pivotPicked());
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), false);

    // 2. 右键确定选区
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::PickingPivot);
    QVERIFY(!tool->pivotPicked());
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), false);

    // 3. 点击空白处 (50, 50) 确定旋转中心，松开
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(tool->pivotPicked());
    QVERIFY(tool->pivot().distanceTo({50.0, 50.0}) < 1e-4);
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::ReadyToRotate);
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), true);

    // 右键可退回定中心阶段，重新指定中心
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, vp(50.0, 50.0));
    QTest::qWait(20);
    QVERIFY(!tool->pivotPicked());
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::PickingPivot);
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), false);

    // 点击端点 (100, 0) 并松开确定中心
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(tool->pivotPicked());
    QVERIFY(tool->pivot().distanceTo({100.0, 0.0}) < 1e-4);
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::ReadyToRotate);
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), true);

    // 4. 再次按下并拖拽旋转：从 (100, 50) 拖拽到 (50, 0)（绕 (100, 0) 逆时针 90°）
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->phase(), cad::tools::RotatePhase::Rotating);
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), true);

    // 松开提交并退出状态
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Idle);
    QVERIFY(!tool->selectionConfirmed());
    QCOMPARE(scene.overlay()->isRotateGizmoVisible(), false);

    // 验证旋转结果：以 (100, 0) 为中心
    const auto* blk = doc.findBlock(a.blockId);
    QVERIFY(blk);
    QVERIFY(blk->worldPos(a.endId).distanceTo({100.0, 0.0}) < 1e-4);
    const double ang = worldAngleDeg(doc, a.blockId);
    QVERIFY(std::abs(ang - 90.0) < 1e-4 || std::abs(ang - (-90.0)) < 1e-4);

    // 撤销验证
    QVERIFY(stack.canUndo());
    stack.undo();
    blk = doc.findBlock(a.blockId);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-4);
}

// m01094 ⑤: 旋转枢轴必须能吸附到圆心。圆心既不是段的起点也不是终点 (段端点
// 是绕它的 Polar 接缝点), 旧实现只遍历 {seg.startPointId, seg.endPointId},
// 圆心永远进不了候选 —— 用户报告「选择旋转中心也捕捉不到圆心」。
void TestRotatePivotMulti::pivotSnapFindsCircleCenter()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    QUndoStack stack;
    cad::tools::CircleFactory factory(&doc, &stack);
    const QUuid circleId = factory.createCircle({0.0, 0.0}, 50.0);
    QVERIFY(!circleId.isNull());
    doc.resolveAll();

    cad::tools::RotateInputTracker tracker;
    // 圆心附近 (偏 2mm): 必须吸附到圆心。
    tracker.updateHoverSnap(&scene, &doc, Vec2{2.0, 0.0});
    QVERIFY2(tracker.hoverSnapped(), "圆心必须进入旋转枢轴吸附候选");
    QVERIFY(tracker.hoverSnapPoint().distanceTo(Vec2{0.0, 0.0}) < 1e-6);

    // 既有的「吸附到端点」不能丢: 接缝起点 (50, 0)。
    tracker.updateHoverSnap(&scene, &doc, Vec2{50.0, 1.0});
    QVERIFY(tracker.hoverSnapped());
    QVERIFY(tracker.hoverSnapPoint().distanceTo(Vec2{50.0, 0.0}) < 1e-6);

    // 远离任何点 → 不吸附。
    tracker.updateHoverSnap(&scene, &doc, Vec2{25.0, 25.0});
    QVERIFY(!tracker.hoverSnapped());
    tracker.teardown();
}

QTEST_MAIN(TestRotatePivotMulti)
#include "test_rotate_pivot_multi.moc"