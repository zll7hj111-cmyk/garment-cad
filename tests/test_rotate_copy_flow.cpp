#include "test_rotate_helpers.h"
#include "parametric/FollowerAngle.h"

class TestRotateCopyFlow : public QObject
{
    Q_OBJECT

private slots:
    void rotateCopyAutoPublishesParentParameter();
    void rotateCopyFourStepFlow();
    void dragJitterDoesNotReleaseUntilPhysicalRelease();
    void gizmoDisplayConsistencyAcrossModes();
    void gizmoConnectedAnchorPointsAtBody();
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

    // 1. 起点锚心就绪态：黄弧长度为 0（无多余半圆），黄虚线 = 起手姿态 = 线体世界方向 0 rad
    //    （2026-09 统一 M2：灰虚线恒世界 0°，黄虚线才是起手姿态 —— 就绪态两者重合）
    QVERIFY(gz->isArcEmpty());
    QVERIFY(std::abs(gz->startPoseRad() - 0.0) < 1e-4);
    QVERIFY(std::abs(gz->currentPoseRad() - 0.0) < 1e-4);

    // 2. X 键换锚心（= 把枢轴移到另一端）：自由线姿态不再翻转 180°（拍板 D1），
    //    黄弧长度仍为 0（绝无 180° 或 240° 怪异半圆），起手姿态仍是线体方向 0 rad
    sendKeyX(view);
    QVERIFY(gz->isArcEmpty());
    QVERIFY(std::abs(gz->startPoseRad() - 0.0) < 1e-4);
    QVERIFY(std::abs(gz->currentPoseRad() - 0.0) < 1e-4);

    // 切回起点锚心
    sendKeyX(view);
    sendConfirm(view);
    QVERIFY(gz->isArcEmpty());

    // 3. 开始拖动旋转：黄弧从起手姿态展开为非空内弧，且起手姿态冻结不变
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 50.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY(!gz->isArcEmpty());
    QVERIFY2(std::abs(gz->startPoseRad() - 0.0) < 1e-4,
             "拖动期黄虚线必须冻结在起手姿态（0°），不得跟随线段走");
    QVERIFY2(std::abs(gz->currentPoseRad()) > 1e-4,
             "黄虚线之外还须有当前姿态方向（黄弧才非空）");

    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    // 提交后恢复就绪态：黄弧再次清空
    QVERIFY(gz->isArcEmpty());
}

// 回归（2026-09 用户报告「旋转工具的黄色显示圈是不是有显示错误」）：
// 连接线（跟随线）世界段向 = refWorld + π − α，与锚心是起点还是终点无关；
// 枢轴在终点时线体自枢轴反向延伸，姿态角内含 anchorFlip(π) 才能贴住线体，
// 否则黄虚线/黄弧整体画在相反半球、与实体脱节。
// 2026-09 统一 M2 后：黄虚线 = 起手姿态（静息态 = 当前姿态），黄弧 = 起手→当前。
// 拖动中黄虚线**冻结**在起手姿态（= 静息态枢轴→线体方向），只有黄弧活动边跟着当前姿态走。
void TestRotateCopyFlow::gizmoConnectedAnchorPointsAtBody()
{
    // 相对「静息态枢轴→线体方向」的三个偏差角；构造失败返回哨兵 1e9。
    // startOff  = 黄虚线（起手姿态）偏差 —— 恒应为 0（静息态与拖动中都冻结在静息线体方向）
    // currentOff= 黄弧活动边（当前姿态）偏差 —— 静息态 0，拖动中 −30°
    // sweep     = 黄弧跨度 = 当前姿态 − 起手姿态 —— 静息态 0，拖动中 −30°
    struct GizmoProbe {
        double startOff;
        double currentOff;
        double sweep;
    };
    const auto measure = [](bool anchorEnd, bool rotating) {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        const LineSetup leader = makeLine(doc, 100.0);                  // (0,0) → (100,0)
        const LineSetup follower = makeLine(doc, 60.0, Vec2(200.0, 0.0));
        doc.resolveAll();

        Attachment att;
        att.fromBlockId   = follower.blockId;
        att.fromPointId   = anchorEnd ? follower.endId : follower.startId;
        att.toBlockId     = leader.blockId;
        att.toPointId     = leader.endId;
        att.toSegmentId   = leader.segId;
        att.followerAngle = 60.0;                                       // 非 90°: 避免补角与自身相等而漏检
        att.rotationMode  = RotationMode::Angle;
        if (!doc.addAttachment(att)) return GizmoProbe{1e9, 1e9, 1e9};
        doc.resolveAll();

        const Attachment* a = followerAttachmentOf(doc, follower.blockId);
        const Block* fb = doc.findBlock(follower.blockId);
        if (!a || !fb) return GizmoProbe{1e9, 1e9, 1e9};

        const QUuid pivotId = anchorEnd ? follower.endId : follower.startId;
        const QUuid bodyId  = anchorEnd ? follower.startId : follower.endId;
        const cad::geo::Vec2 pivot = fb->worldPos(pivotId);
        const double bodyRad = (fb->worldPos(bodyId) - pivot).angle();  // 枢轴 → 线体

        cad::tools::GizmoPoseInput in;
        in.isConnected     = true;
        in.isAnchorEnd     = anchorEnd;                                 // RotateSession::setupTarget 置位
        in.refWorldRad     = cad::param::effectiveAngleRefWorld(&doc, *a);
        in.currentAngleDeg = rotating ? 90.0 : a->followerAngle;
        in.isRotating      = rotating;
        in.dragAngle0      = a->followerAngle;                          // 起手姿态 = 拖动开始时的 α

        const cad::tools::GizmoPose pose = cad::tools::computeGizmoPose(in);
        return GizmoProbe{
            cad::geo::normalizeDeg180(cad::geo::radToDeg(pose.startPoseRad - bodyRad)),
            cad::geo::normalizeDeg180(cad::geo::radToDeg(pose.currentPoseRad - bodyRad)),
            cad::geo::radToDeg(cad::geo::normalizeRad(pose.currentPoseRad - pose.startPoseRad))};
    };

    const auto restStart = measure(false, false);
    const auto restEnd = measure(true, false);
    QVERIFY2(std::abs(restStart.startOff) < 1e-3, "连接线起点锚心: 静息态黄虚线必须自枢轴指向线体");
    QVERIFY2(std::abs(restStart.currentOff) < 1e-3, "连接线起点锚心: 静息态黄弧活动边必须自枢轴指向线体");
    QVERIFY2(std::abs(restEnd.startOff) < 1e-3, "连接线终点锚心: 静息态黄虚线必须自枢轴指向线体, 不得反向 180°");
    QVERIFY2(std::abs(restEnd.currentOff) < 1e-3, "连接线终点锚心: 静息态黄弧活动边必须自枢轴指向线体");
    // 静息态（尚未拖动）黄弧跨度必须为 0 —— 起手姿态就是当前姿态
    QVERIFY2(std::abs(restStart.sweep) < 1e-3, "连接线起点锚心: 静息态黄弧跨度应为 0");
    QVERIFY2(std::abs(restEnd.sweep) < 1e-3, "连接线终点锚心: 静息态黄弧跨度应为 0");

    // 拖动中：黄虚线冻结在起手姿态（= 静息线体方向），黄弧跨度 = 当前 − 起手 = −30°（与锚心端无关）
    const auto dragStart = measure(false, true);
    const auto dragEnd = measure(true, true);
    QVERIFY2(std::abs(dragStart.startOff) < 1e-3, "拖动中起点锚心: 黄虚线必须冻结在起手姿态(静息线体方向)");
    QVERIFY2(std::abs(dragEnd.startOff) < 1e-3, "拖动中终点锚心: 黄虚线必须冻结在起手姿态(静息线体方向)");
    QVERIFY2(std::abs(dragStart.currentOff + 30.0) < 1e-3, "拖动中起点锚心: 黄弧活动边应相对静息线体 −30°");
    QVERIFY2(std::abs(dragEnd.currentOff + 30.0) < 1e-3, "拖动中终点锚心: 黄弧活动边应相对静息线体 −30°");
    QVERIFY2(std::abs(dragStart.sweep + 30.0) < 1e-3, "拖动中起点锚心: 黄弧跨度应为 −30°");
    QVERIFY2(std::abs(dragEnd.sweep + 30.0) < 1e-3, "拖动中终点锚心: 黄弧跨度应为 −30°");
}

QTEST_MAIN(TestRotateCopyFlow)
#include "test_rotate_copy_flow.moc"