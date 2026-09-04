/// @file test_rotate_copy.cpp
/// 旋转复制 (Ctrl+drag in ToolRotate): 模型层与基础事件链测试。

#include "test_rotate_copy.h"

void TestRotateCopy::cloneAttachesToOriginalWithRelativeAngle()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup a = makeLine(doc, 100.0);   // horizontal at origin
    doc.resolveAll();

    // Single-block duplicate → clone overlapping the original.
    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    QCOMPARE(r.blocks.size(), size_t(1));
    const Block& clone = r.blocks.front();

    // Auto-published length link must exist before the clone resolves.
    for (const auto& lv : r.newLinked)
        doc.addLinked(lv);
    doc.addBlock(clone);
    QVERIFY(doc.addAttachment(
        attachCloneToOriginal(doc, clone, a.blockId, a, -30.0)));
    doc.resolveAll();

    // Stored −30° (闭合基准 2026-08: 0° = 折叠重叠, 180° = 直行延续; 起点
    // 出口反向) → clone is 30° CCW of the original (relative-angle semantics
    // of the rotate-copy gesture).
    const Block* orig = doc.findBlock(a.blockId);
    const Block* cln = doc.findBlock(clone.id);
    QVERIFY(orig && cln);
    QVERIFY(cln->worldPos(cln->points.front().id)
                .distanceTo(orig->worldPos(a.startId)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 30.0) < 1e-6);
}

void TestRotateCopy::cloneFollowsOriginalRotation()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();
    for (const auto& lv : r.newLinked)
        doc.addLinked(lv);
    doc.addBlock(clone);
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, -30.0));
    doc.resolveAll();

    // Relative angle = 180° − (−30°) − 180° = 30° before and 75° after the
    // 45° turn (闭合基准存储).
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 30.0) < 1e-6);

    // Rotate the ORIGINAL 45° about its start → the clone keeps its 30°
    // RELATIVE angle (75° world) and its pivot stays on the original start.
    Block* orig = doc.findBlock(a.blockId);
    const Vec2 pivot = orig->worldPos(a.startId);
    const Vec2 startLocal = orig->findPoint(a.startId)->resolvedPos;
    const double newRot = 45.0 * M_PI / 180.0;
    orig->transform.rotation = newRot;
    orig->transform.origin = pivot - startLocal.rotated(newRot);
    doc.invalidateAllLayers();
    doc.resolveAll();

    QVERIFY(doc.diagnostics().empty());
    const Block* cln = doc.findBlock(clone.id);
    QVERIFY(cln);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 75.0) < 1e-6);
    QVERIFY(cln->worldPos(cln->points.front().id)
                .distanceTo(orig->worldPos(a.startId)) < 1e-6);
}

void TestRotateCopy::rotateCopyCommandUndoRedo()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();

    QUndoStack stack;
    stack.push(new cad::cmd::RotateCopyCommand(
        &doc, std::move(r), a.blockId, a.startId,
        clone.points.front().id, a.segId, 120.0));
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    // Stored 120° (闭合基准存储值, 挂起点 → refWorld = 180°):
    // world = refWorld + 180° − 120° = 240° ≡ −120°.
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) + 120.0) < 1e-6);

    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QVERIFY(doc.findBlock(clone.id) == nullptr);
    QCOMPARE(doc.attachments().size(), size_t(0));

    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) + 120.0) < 1e-6);
}

void TestRotateCopy::formulaLockedOriginalCopyIsFree()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // Leader B + follower A whose follower angle is FORMULA-locked.
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 45.0;
    conn.followerAngleFormula = QStringLiteral("45");   // locked (evaluates)
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // Rotate-copy of A: the clone's own attachment carries NO formula.
    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();
    doc.addBlock(clone);
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, 170.0));
    doc.resolveAll();

    const Attachment* ca = cloneAttachment(doc, clone.id, a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());   // 副本自动去公式
    QVERIFY(std::abs(ca->followerAngle - 170.0) < 1e-9);
}

// ── 影子角度通道 (拆开影子基准 R6/R8, DETACH_SHADOW_DESIGN.md §7.2) ────────
// offset 公式锁定 + 基准 = 影子块 (拆开态): 旋转手势不再拒绝 —— 改写影子
// 角度 (公式原样存活, R6), 跟随线绕 p3 原地转 (p3 世界位置不变, R8);
// undo 整步回到拖前 (影子角度 + 跟随线姿态)。
void TestRotateCopy::shadowChannel_formulaLockRotatesShadowKeepsP3()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // 本体 A + 跟随线 B (offset 公式 "90" 锁定), B.start 吸到 A.end。
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 50.0);
    Attachment conn;
    conn.fromBlockId = b.blockId;
    conn.fromPointId = b.startId;
    conn.toBlockId = a.blockId;
    conn.toPointId = a.endId;
    conn.toSegmentId = a.segId;
    conn.followerAngle = 90.0;
    conn.followerAngleFormula = QStringLiteral("90");   // locked (R6 前提)
    QVERIFY(doc.addAttachment(conn));
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // 拆开 (影子换代, 门面影子路由): Att2 基准 = 影子块。
    doc.setAttachmentAngleOnly(conn.id, true);
    doc.resolveAll();
    const QUuid shadowId = doc.findAttachment(conn.id)->toBlockId;
    QVERIFY(doc.blockById(shadowId) && doc.blockById(shadowId)->isShadow);
    const double shadowRot0 = doc.blockById(shadowId)->transform.rotation;
    const double rotB0 = doc.findBlock(b.blockId)->transform.rotation;
    const Vec2 p3World0 = doc.findBlock(b.blockId)->worldPos(b.startId);

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
    tm.switchTool(cad::tools::ToolType::Rotate);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // B: 竖直线, start=(100,0) (接点 = 锚心) → end=(100,50)。选中 B (中点) + 确认。
    const Vec2 bMidW = (doc.findBlock(b.blockId)->worldPos(b.startId)
                        + doc.findBlock(b.blockId)->worldPos(b.endId)) * 0.5;
    const QPoint mid = vp(bMidW.x, bMidW.y);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);

    // 旋转: 锚心 = 挂连接端 (B.start = p3, 接点 (100,0))。按在 B 中点
    // (光标角 90°), 拖到 (150,0) (光标角 0°) → 光标增量 −90° → 影子通道
    // δ = −90° (拆开态 = 写影子 rotation, B 方向随影子转)。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 0.0), Qt::LeftButton,
              Qt::NoModifier);
    doc.resolveAll();

    // R6: offset 公式原样存活; 影子角度被改写 (−90°); B 方向随影子转。
    QVERIFY2(doc.findAttachment(conn.id)->followerAngleFormula
                 == QStringLiteral("90"),
             "R6: 旋转影子通道不碰公式");
    QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                      - (shadowRot0 - M_PI / 2.0)) < 1e-6,
             "R6: 旋转改写影子角度 (−90°)");
    QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation
                      - (rotB0 - M_PI / 2.0)) < 1e-6,
             "R6: B 方向随影子基准转动");

    // R8: 拆开态旋转 = 绕 p3 原地转 —— p3 世界位置保持不变。
    QVERIFY2(doc.findBlock(b.blockId)->worldPos(b.startId).distanceTo(p3World0)
                 < 1e-6,
             "R8: p3 世界位置不变 (原地转)");

    // undo: 影子角度 + B 姿态一起回拖前值 (ShadowRotateCommand 快照)。
    stack.undo();
    doc.resolveAll();
    QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation - shadowRot0)
                 < 1e-9, "undo: 影子角度回拖前值");
    QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation - rotB0)
                 < 1e-9, "undo: B 姿态回拖前值");
    QVERIFY2(doc.findBlock(b.blockId)->worldPos(b.startId).distanceTo(p3World0)
                 < 1e-6, "undo: p3 仍在原位");
}

// ═══════════════════════════════════════════════════════════════════
// UI-layer: full event chain through CanvasView (QTest mouse events →
// dispatch → ToolRotate).
// ═══════════════════════════════════════════════════════════════════

void TestRotateCopy::ctrlDragRotateCopyCommits()
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

    // 1) Click selects the line (Idle → Ready).
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(doc.blocks().size(), size_t(1));
    sendConfirm(view);

    // 2) Ctrl+press → drag up 90° → release commits the rotate-copy.
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cloneAttachment(doc, cln->id, a.blockId) != nullptr);
    // Dragging 90° CCW around the start point → clone at 90° CCW of the
    // original (复制基准 2026-08: 0° 相对角 = 与原线重叠，与锚心无关).
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);

    // 3) Undo removes the copy in ONE step.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

// 对角自由线的旋转复制：副本 0° 必须精确重叠原线（复制基准 2026-08 定稿；
// 旧基准"锚心偏移 0/180°"只对水平线碰巧正确，对角/连接线差 180°−α —
// 用户报告"复制以 180° 创建"）。
void TestRotateCopy::diagonalFreeLineCopyOverlapsOriginal()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    if (auto* blk = doc.findBlock(a.blockId))
        blk->transform.rotation = M_PI / 4.0;   // 45° 对角自由线（绕原点）
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

    // 1) Click selects the 45° line (Idle → Ready); anchor = start point (0,0).
    const QPoint hit = vp(35.0, 35.0);   // scene (35,35) — on the diagonal
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(doc.blocks().size(), size_t(1));
    sendConfirm(view);

    // 2) Ctrl+press ONLY (no drag yet): the clone must overlap the original
    //    exactly — 45°, not 45°−180° = −135° (the reported bug).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* pre = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { pre = &b; break; }
    QVERIFY(pre);
    QVERIFY(std::abs(worldAngleDeg(doc, pre->id) - 45.0) < 1e-6);

    // 3) Drag +90° CCW (θ0 = 45° at press, θ1 = 135° at (−50,50)) → release.
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { cln = &b; break; }
    QVERIFY(cln);
    // 副本 = 原线朝向 45° + 相对角 90° = 135°（旧代码 45 − 180 + 90 = −45）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);

    // 4) Undo removes the copy in ONE step.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::ctrlDragZeroAngleDiscards()
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

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+press → drag away → drag BACK to the exact start angle → release:
    // zero relative angle = copy discarded (转回原位 = 不复制).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::escCancelsCopy()
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

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+press → drag → Esc mid-gesture: preview clone dropped, original
    // untouched, tool back to Ready (target kept).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    // Esc 同步取消手势; 随后是"什么都没提交"的负向断言, 无可等状态 → settle 排空。
    cad::test::settle();

    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::consecutiveCopiesAllAttachToOriginal()
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
    auto ctrlRotateTo = [&](const QPoint& from, double x, double y) {
        sendConfirm(view);
        sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::ControlModifier);
        sendMouse(QEvent::MouseMove, vp(x, y), Qt::NoButton, Qt::ControlModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(x, y), Qt::LeftButton,
                  Qt::ControlModifier);
    };

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Copy #1 → 90°; copy #2 → 180°. Both attach to the ORIGINAL and the
    // tool stays Ready (连续复制).
    ctrlRotateTo(hit, 0.0, 50.0);
    QCOMPARE(doc.blocks().size(), size_t(2));
    ctrlRotateTo(hit, -50.0, 0.0);
    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(2));

    QList<double> relAngles;
    for (const auto& b : doc.blocks()) {
        if (b.id == a.blockId) continue;
        QVERIFY(cloneAttachment(doc, b.id, a.blockId) != nullptr);
        relAngles << worldAngleDeg(doc, b.id);
    }
    QCOMPARE(relAngles.size(), 2);
    // 复制基准 2026-08: 副本绝对角 = 原线朝向 + 相对角 → 90° 与 180°
    // （相对 180° = 沿原线正向延伸）。
    QVERIFY(relAngles.contains(90.0));
    QVERIFY(relAngles.contains(180.0));
}

// 旋转复制提交后 HUD 必须隐藏：输入框目标会悄然从“副本相对角度”切回
// “原线角度”，用户继续输入表达式/数值会作用到原线段（用户回归：复制
// 后输入表达式作用在原线上）。连续复制时 HUD 恢复显示副本编辑。
void TestRotateCopy::rotateCopyCommitRestoresStripTarget()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0)→(100,0)
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

    const QPoint mid = vp(50.0, 0.0);
    // 选中 A → 条带锁定到 A。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(bridge.pinnedBlock, a.blockId);
    QVERIFY(!bridge.strip.blockId().isNull());
    sendConfirm(view);

    // Ctrl+按下 → 进入复制态: 条带**解除锁定** (拍板 —— 复制显示的是绕锚心
    // 相对角, 与跟随角/绝对角三套语义不共用一个框; 相对角读数走状态栏)。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    QVERIFY(bridge.pinnedBlock.isNull());
    QVERIFY(!bridge.hint.isEmpty());          // 状态栏报相对角
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(2));
    // 提交后条带锁定回**原线 A** —— 复制期间输入框若还指着副本, 用户接着
    // 敲数字就会改到副本上 (原 HUD 用"提交即隐藏"防这一手)。
    QCOMPARE(bridge.strip.blockId(), a.blockId);

    // 连续复制: 再次进入复制态 → 再次解除锁定, 提交后再锁回原线。
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    QVERIFY(bridge.pinnedBlock.isNull());
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(bridge.strip.blockId(), a.blockId);
}

// ═══════════════════════════════════════════════════════════════════
// Formula-locked follower (角度公式锁定的跟随线)
// ═══════════════════════════════════════════════════════════════════

namespace {

/// Build: horizontal leader B (200,0)→(300,0) + follower A attached at its
/// end with a FORMULA-locked 45° follower angle. A's start = (300,0),
/// A points 45° for 60 mm (mid-body ≈ (321.2, 21.2)).
struct LockedFollowerSetup {
    LineSetup b;
    LineSetup a;
};

LockedFollowerSetup makeLockedFollower(ParamDocument& doc)
{
    LockedFollowerSetup s;
    s.b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    s.a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = s.a.blockId;
    conn.fromPointId = s.a.startId;
    conn.toBlockId = s.b.blockId;
    conn.toPointId = s.b.endId;
    conn.toSegmentId = s.b.segId;
    conn.followerAngle = 135.0;   // 闭合基准: 世界角保持 45°（180°−45°）
    conn.followerAngleFormula = QStringLiteral("135");   // 公式锁定
    doc.addAttachment(conn);
    doc.resolveAll();
    return s;
}

} // namespace

// 旋转锁定跟随线：变量/公式驱动角度 = 锁定，旋转工具拒绝旋转 —— 不烘焙
// 公式、不覆盖变量、角度不变（用户拍板 2026-12）。
void TestRotateCopy::lockedFollowerRotationBakesFormula()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LockedFollowerSetup s = makeLockedFollower(doc);
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

    // Click the follower's mid-body to select it (Ready, formula locked).
    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
    // Ordinary drag (NO Ctrl): rotation is REFUSED — the formula stays intact,
    // the variable is not overwritten, and the angle does not change.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, 40.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, 40.0), Qt::LeftButton,
              Qt::NoModifier);

    const Attachment* ca = followerAttachmentOf(doc, s.a.blockId);
    QVERIFY(ca);
    QCOMPARE(ca->followerAngleFormula, QStringLiteral("135")); // 公式原样保留
    QVERIFY(std::abs(ca->followerAngle - 135.0) < 1e-6);       // 数值未被覆盖
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 45.0) < 1e-6); // 角度不变
}

// 锁定跟随线的旋转复制：Ctrl+press 拖动 → 副本出现，副本自身无公式。
void TestRotateCopy::lockedFollowerRotateCopyWorks()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LockedFollowerSetup s = makeLockedFollower(doc);
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

    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
    // Ctrl+press on the LOCKED follower → drag 45° CCW → release.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, 40.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, 40.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // B + A + clone
    QCOMPARE(doc.attachments().size(), size_t(2));     // A→B + clone→A
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && b.id != s.b.blockId) { cln = &b; break; }
    QVERIFY(cln);
    const Attachment* ca = cloneAttachment(doc, cln->id, s.a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());         // 副本无公式
    // 复制基准 2026-08 定稿: 副本 0° = 与原线精确重叠（A 世界朝向 45°），
    // 拖动 +45° CCW → 副本 = 45° + 45° = 90°（旧基准差 180°−α = 135°:
    // 副本落在 −90°，相对角语义全部错位 —— 用户报告"复制以 180° 创建"）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);
}

// 弧长/角度表达式锁定：HUD 切换角度↔弧长模式只是显示单位变化，绝不把
// 表达式换算烘焙成数值（用户要求：弧长用表达式时不能自己换算成数值）。
void TestRotateCopy::modeSwitchKeepsFormula()
{
    // ── 场景 1：角度表达式锁定，切到弧长模式 → 拒绝且公式保留 ──
    {
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
        conn.followerAngleFormula = QStringLiteral("45");   // 角度表达式锁定
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

        // 选中 A（锚心=挂接点 → Connected/Angle 模式）。先解析实际几何再点
        // A 中段 —— followerAngle 45° → 世界角 135°（朝左上方），(270,0) 落在
        // 基准线 B 的线身上（B 跨 x∈[200,300]），点那里会锁到 B 而非 A。
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
        // 条带锁定到 A (角度格可编辑, 单位段可用)。
        QVERIFY(bridge.strip.unitArcButton()->isEnabled());

        // 点 ⌒ 切到弧长模式 (2026-12 用户拍板: 公式驱动可切换, 且公式
        // **原样搬移不乘换算系数**): rotationMode 翻转, arcLengthFormula
        // 保持 "45" 原样; 数值字段仍做几何保持换算 (45° → 47.12mm)。
        bridge.strip.unitArcButton()->click();
        const Attachment* att = followerAttachmentOf(doc, a.blockId);
        QVERIFY(att);
        QCOMPARE(att->rotationMode, cad::param::RotationMode::ArcLength);
        QCOMPARE(att->arcLengthFormula, QStringLiteral("45"));   // 原样保留
        QVERIFY2(std::abs(att->arcLength - 45.0 * M_PI / 180.0 * 60.0) < 1e-6,
                 "数值字段仍按几何保持换算");
    }

    // ── 场景 2：弧长表达式锁定，切到角度模式 → 公式原样搬移 ──
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
        conn.arcLength = 47.1238898038469;   // ≈ 45° 的弧长（弧度 × 半径）
        conn.arcLengthFormula = QStringLiteral("10");   // 弧长表达式锁定 (cm)
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

        // 选中 A：先解析实际几何再点 A 中段（弧长公式 10cm / 半径 60mm →
        // 弧长角 ≈95.5° → 世界角 ≈84.5°，朝上偏右）。
        const Block* aBlk2 = doc.findBlock(a.blockId);
        QVERIFY(aBlk2);
        const Segment& aSeg2 = aBlk2->segments.front();
        const ParamPoint* aSp2 = aBlk2->findPoint(aSeg2.startPointId);
        const ParamPoint* aEp2 = aBlk2->findPoint(aSeg2.endPointId);
        QVERIFY(aSp2 && aEp2 && aSp2->resolved && aEp2->resolved);
        const Vec2 aMid2 = aBlk2->transform.toWorld(
            (aSp2->resolvedPos + aEp2->resolvedPos) * 0.5);
        sendMouse(QEvent::MouseButtonPress, vp(aMid2.x, aMid2.y), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(aMid2.x, aMid2.y), Qt::LeftButton, Qt::NoModifier);
        // 点 ° 切到角度模式 (2026-12 用户拍板: 公式原样搬移不乘系数):
        // rotationMode 翻转, followerAngleFormula 保持 "10" 原样;
        // 数值字段仍做几何保持换算 (10cm 弧长 → 95.5°)。
        QVERIFY(bridge.strip.unitAngleButton()->isEnabled());
        bridge.strip.unitAngleButton()->click();
        const Attachment* att = followerAttachmentOf(doc, a.blockId);
        QVERIFY(att);
        QCOMPARE(att->rotationMode, cad::param::RotationMode::Angle);
        QCOMPARE(att->followerAngleFormula, QStringLiteral("10"));   // 原样保留
        QVERIFY2(std::abs(att->followerAngle - 10.0 * 1800.0 / (M_PI * 60.0)) < 1e-6,
                 "数值字段仍按几何保持换算");
    }
}

// 属性对话框：跟随角度/弧长表达式线必须在输入框旁显示当前计算值
// （表达式不直观，用户要求：看到公式也要看到值）。
void TestRotateCopy::propertyDialogShowsFollowValue()
{
    // ── 场景 1：跟随角度表达式 → 显示 = 45° ──
    {
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
        conn.followerAngleFormula = QStringLiteral("45");   // 表达式
        doc.addAttachment(conn);
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());                       // 公式线显示当前值
        QCOMPARE(val->text(), QStringLiteral("= 45°"));
        delete dlg;
    }

    // ── 场景 2：弧长表达式 → 显示 = 10.00 cm ──
    {
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
        conn.arcLengthFormula = QStringLiteral("10");   // 弧长表达式 (cm)
        doc.addAttachment(conn);
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());
        QCOMPARE(val->text(), QStringLiteral("= 10 cm"));
        delete dlg;
    }

    // ── 场景 3：自由线绝对角度表达式（智能笔创建态同路径）
    // → 显示 = 45°（逆时针为正 2026-08 v3 定稿：绝对角度 = 世界角，无镜像） ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        CanvasScene scene(&doc);
        const LineSetup a = makeLine(doc, 100.0);   // (0,0)→(100,0) Polar
        auto* blk = doc.findBlock(a.blockId);
        QVERIFY(blk);
        auto* ep = blk->findPoint(blk->segments.front().endPointId);
        QVERIFY(ep);
        ep->angleFormula = QStringLiteral("45");   // 绝对角度表达式
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());
        QCOMPARE(val->text(), QStringLiteral("= 45°"));
        delete dlg;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Endpoint-aim (终点指向) — resolver Step 7 would pull the direction back
// to the target point on every frame, so rotation must RELEASE the aim.
// ═══════════════════════════════════════════════════════════════════

namespace {

/// Build: free line A from (0,0) to (100,0) with endTarget aiming at point P
/// at (100, 100) — the resolver keeps A pointing 45° at P.
struct AimLineSetup {
    LineSetup a;
    QUuid targetPointId;
};

AimLineSetup makeAimLine(ParamDocument& doc)
{
    AimLineSetup s;
    s.a = makeLine(doc, 100.0);
    // Target block T: a vertical line so its start point sits at (100,100).
    const LineSetup t = makeLine(doc, 50.0, Vec2(100.0, 100.0));
    (void)t;
    Block* blk = doc.findBlock(s.a.blockId);
    Q_ASSERT(blk);
    blk->endTargetBlockId = doc.blocks().back().id;
    blk->endTargetPointId = doc.blocks().back().points.front().id;
    s.targetPointId = blk->endTargetPointId;
    doc.resolveAll();
    return s;
}

} // namespace

// 旋转带终点指向的自由线：拖动即解除指向（否则 Resolver 每帧拉回），
// 角度自由变化；撤销一步恢复指向约束。
void TestRotateCopy::endTargetRotationReleasesAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
    QVERIFY(doc.diagnostics().empty());
    // A aims at P before the gesture.
    const Block* blk0 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk0->endTargetBlockId.isNull());
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 45.0) < 1e-6);

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

    // Select A at its mid-body (the aim keeps A at 45° from (0,0) to
    // (70.7,70.7), so the mid point is (35.4, 35.4)).
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Drag 90° (cursor from (35.4,35.4) → (0,50) around pivot (0,0)): aim released.
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(s.a.blockId);
    QVERIFY(blk->endTargetBlockId.isNull());           // 指向已解除
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 90.0) < 1e-6);

    // Undo restores the aim constraint AND the old direction.
    stack.undo();
    const Block* blk2 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk2->endTargetBlockId.isNull());
    QCOMPARE(blk2->endTargetPointId, s.targetPointId);
}

// 终点指向线的旋转复制：副本不继承指向，可自由转动。
void TestRotateCopy::endTargetRotateCopyDropsAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
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

    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
    // Ctrl+drag on the AIMING line → clone appears with NO aim constraint
    // and rotates freely (原线仍指向 P，副本相对转 90°).
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
    // 锚心语义: 副本 0° = 与原线重叠，相对 +90° → 副本绝对角度 =
    // 45° + 90° = 135°（复制基准 2026-08）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);
}

// 旋转复制只清副本的终点指向，原块的指向必须保留（用户回归: 辅助层 L246
// 旋转复制后原线指向被误删）。
void TestRotateCopy::endTargetRotateCopyKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
    QVERIFY(doc.diagnostics().empty());
    const Block* blk0 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk0->endTargetBlockId.isNull());   // 前提: A 带指向

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

    // Select A, then Ctrl+drag a rotate-copy 90° CCW.
    sendConfirm(view);
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    // 原块指向必须保留（只副本被清）。
    QVERIFY(!orig->endTargetBlockId.isNull());
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    bool targetAlive = false;
    for (const auto& b : doc.blocks())
        if (b.id == orig->endTargetBlockId) { targetAlive = true; break; }
    QVERIFY(targetAlive);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
}

// Idle 状态下一次手势 Ctrl+press（选中+复制同一步）也不许删原块指向。
void TestRotateCopy::endTargetRotateCopyIdleGestureKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
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

    // 直接 Ctrl+press（Idle → select + beginRotateCopy 一步）。
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    QVERIFY(!orig->endTargetBlockId.isNull());          // 原块指向保留
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());            // 副本无指向
}

// 旋转复制 → undo → redo 全程，原块指向保持（undo 只删副本）。
void TestRotateCopy::endTargetRotateCopyUndoRedoKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
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

    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(3));

    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(2));           // 副本已删
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig && !orig->endTargetBlockId.isNull());  // 原块指向仍在
    QCOMPARE(orig->endTargetPointId, s.targetPointId);

    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(3));
    const Block* orig2 = doc.findBlock(s.a.blockId);
    QVERIFY(orig2 && !orig2->endTargetBlockId.isNull()); // redo 后仍在
    QCOMPARE(orig2->endTargetPointId, s.targetPointId);
}

// 普通旋转拖动中途按下 Ctrl → 转为旋转复制：已转角度转移到副本，原块
// 回弹旋转前姿态（普通旋转已清除的终点指向一并恢复）。这是用户报告的
// “旋转复制删掉 L246 终点指向”的典型时序：先按下鼠标再按 Ctrl。
void TestRotateCopy::midGestureCtrlConvertsToCopy()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
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

    const QPoint mid = vp(35.36, 35.36);   // A 的中点（A 指向 T 后为 45° 斜线）
    // 选中 A。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    // 普通旋转：press 无 Ctrl → beginRotation 清除终点指向（设计如此）。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(doc.findBlock(s.a.blockId)->endTargetBlockId.isNull());
    // 已转 45°（A 方向 45° → 90°）。
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 90.0) < 1e-6);
    // 中途按住 Ctrl 继续拖动 → 转复制：原块回弹 + 指向恢复。
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    // 终点指向已恢复（普通旋转的清除被回滚）。
    QVERIFY(!orig->endTargetBlockId.isNull());
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    // 原块回弹到旋转前姿态（45°）。
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 45.0) < 1e-6);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
    // 已转角度转移到副本：副本 = 原线朝向 45° + 相对角 45° = 90°
    // （复制基准 2026-08: 0° = 与原线重叠）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);
}

QTEST_MAIN(TestRotateCopy)
