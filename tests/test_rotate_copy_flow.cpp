#include "test_rotate_helpers.h"

class TestRotateCopyFlow : public QObject
{
    Q_OBJECT

private slots:
    void rotateCopyAutoPublishesParentParameter();
    void rotateCopyFourStepFlow();
    void dragJitterDoesNotReleaseUntilPhysicalRelease();
    void gizmoDisplayConsistencyAcrossModes();
};
void TestRotateCopyFlow::rotateCopyAutoPublishesParentParameter()
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

void TestRotateCopyFlow::rotateCopyFourStepFlow()
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
void TestRotateCopyFlow::dragJitterDoesNotReleaseUntilPhysicalRelease()
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
void TestRotateCopyFlow::gizmoDisplayConsistencyAcrossModes()
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

QTEST_MAIN(TestRotateCopyFlow)
#include "test_rotate_copy_flow.moc"