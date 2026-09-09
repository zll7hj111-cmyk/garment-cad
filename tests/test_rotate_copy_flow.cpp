#include "test_rotate_helpers.h"
#include "parametric/FollowerAngle.h"
#include "tools/CircleFactory.h"

class TestRotateCopyFlow : public QObject
{
    Q_OBJECT

private slots:
    void rotateCopyAutoPublishesParentParameter();
    void rotateCopyFourStepFlow();
    void dragJitterDoesNotReleaseUntilPhysicalRelease();
    void gizmoDisplayConsistencyAcrossModes();
    void gizmoConnectedAnchorPointsAtBody();
    void gizmoCircleUsesRadiusDirection();
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

// m01404「圆的旋转辅助显示好像没有做单独的适配。比如旋转的黄色虚线、灰色虚线，
// 黄圈效果」：整圆两端点同为接缝点 ⇒ start→end 弦向恒 (0,0) ⇒ 姿态角恒 0° ⇒
// 黄虚线压住灰虚线（世界 0°）、黄弧跨度为 0、徽标恒 0°。圆段姿态必须取
// 「圆心→接缝」半径方向（与圆心虚线标注同源），旋转量才只算一次。
void TestRotateCopyFlow::gizmoCircleUsesRadiusDirection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    QUndoStack stack;
    cad::tools::CircleFactory factory(&doc, &stack);
    // 接缝（起点）落在 (0, 50) ⇒ 半径方向 = 世界 90°，与弦向退化值 0° 可区分。
    const QUuid circleId = factory.createCircle({0.0, 0.0}, 50.0, 90.0);
    QVERIFY(!circleId.isNull());
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
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

    // 弧上 315° 处（避开 interpPercent 0.25/0.5/0.75 的三个粉色锚点）。
    const QPoint onArc = vp(35.355, -35.355);
    sendMouse(QEvent::MouseButtonPress, onArc, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, onArc, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);

    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    const auto* gz = tool->gizmo();
    QVERIFY(gz);

    // 1. 就绪态：起手姿态 = 圆心→接缝半径方向 = 90°（旧实现恒 0°）
    QVERIFY(gz->isArcEmpty());
    QVERIFY2(std::abs(gz->startPoseRad() - cad::geo::kPi / 2.0) < 1e-4,
             "就绪态黄虚线必须是圆的半径方向 90°，不是退化的 0°");
    QVERIFY(std::abs(gz->currentPoseRad() - cad::geo::kPi / 2.0) < 1e-4);

    // 2. 拖动 −45°：黄虚线冻结在起手姿态 90°，黄弧活动边 = 当前姿态 45°
    sendMouse(QEvent::MouseButtonPress, onArc, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(-35.355, -35.355), Qt::NoButton, Qt::NoModifier);
    QVERIFY2(!gz->isArcEmpty(), "圆拖动中黄弧必须非空（旧实现起手 = 当前 = 0°）");
    QVERIFY2(std::abs(gz->startPoseRad() - cad::geo::kPi / 2.0) < 1e-4,
             "拖动中黄虚线必须冻结在起手半径方向 90°");
    QVERIFY2(std::abs(gz->currentPoseRad() - cad::geo::kPi / 4.0) < 0.02,
             "黄弧活动边 = 当前半径方向 45°（90° − 45°）");
    // 徽标文本与状态提示同源（updateGizmo → updateStatusHint），此处读状态提示校验
    // 显示读数：基准 = 圆的半径方向 90°（旧实现为退化的 0°），当前 = 45°。
    auto hintDeg = [&](const QString& label) {
        const int i = bridge.hint.indexOf(label);
        if (i < 0) return -1e9;
        return bridge.hint.mid(i + label.size()).section(QChar(0x00B0), 0, 0).toDouble();
    };
    QVERIFY2(std::abs(hintDeg(QString::fromUtf8("基准: ")) - 90.0) < 0.5,
             qPrintable(QStringLiteral("状态提示基准角应为半径方向 90°, 实得: %1").arg(bridge.hint)));
    QVERIFY2(std::abs(hintDeg(QString::fromUtf8("角度: ")) - 45.0) < 1.0,
             qPrintable(QStringLiteral("状态提示当前角应为 45°, 实得: %1").arg(bridge.hint)));

    sendMouse(QEvent::MouseButtonRelease, vp(-35.355, -35.355), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(gz->isArcEmpty());

    // 3. 提交后：块旋转 −45°，块内 a₀ 保持 90° ⇒ 世界半径方向 = 45°
    const Block* blk = doc.findBlock(circleId);
    QVERIFY(blk);
    QVERIFY(!blk->segments.empty());
    const Segment& seg = blk->segments.front();
    QVERIFY(seg.fitKind == cad::param::FitKind::Circle);
    QVERIFY2(std::abs(cad::geo::radToDeg(blk->transform.rotation) + 45.0) < 1.0,
             "圆块旋转量应为 −45°");
    QVERIFY2(std::abs(blk->circleStartAngleDeg(seg) - 90.0) < 1e-6,
             "块内 a₀ 不随旋转改变");
}

QTEST_MAIN(TestRotateCopyFlow)
#include "test_rotate_copy_flow.moc"