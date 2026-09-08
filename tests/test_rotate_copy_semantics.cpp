#include "test_rotate_helpers.h"

class TestRotateCopySemantics : public QObject
{
    Q_OBJECT
private slots:
    void cloneAttachesToOriginalWithRelativeAngle();
    void cloneFollowsOriginalRotation();
    void rotateCopyCommandUndoRedo();
    void formulaLockedOriginalCopyIsFree();
    void ctrlDragRotateCopyCommits();
    void diagonalFreeLineCopyOverlapsOriginal();
    void ctrlDragZeroAngleDiscards();
    void escCancelsCopy();
    void consecutiveCopiesAllAttachToOriginal();
    void rotateCopyCommitRestoresStripTarget();
    void lockedFollowerRotationBakesFormula();
    void lockedFollowerRotateCopyWorks();
};

void TestRotateCopySemantics::cloneAttachesToOriginalWithRelativeAngle()
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
        attachCloneToOriginal(doc, clone, a.blockId, a, 150.0)));
    doc.resolveAll();

    // 闭合基准存储 150° (母线直线向量基准 2026-09: refWorld = 0°):
    // world = refWorld + 180° − followerAngle = 0° + 180° − 150° = 30°
    // clone is 30° CCW of the original (relative-angle semantics
    // of the rotate-copy gesture).
    const Block* orig = doc.findBlock(a.blockId);
    const Block* cln = doc.findBlock(clone.id);
    QVERIFY(orig && cln);
    QVERIFY(cln->worldPos(cln->points.front().id)
                .distanceTo(orig->worldPos(a.startId)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 30.0) < 1e-6);
}

void TestRotateCopySemantics::cloneFollowsOriginalRotation()
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
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, 150.0));
    doc.resolveAll();

    // Relative angle = 180° − 150° = 30° before and 75° after the
    // 45° turn (母线直线向量基准存储 2026-09).
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

void TestRotateCopySemantics::rotateCopyCommandUndoRedo()
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
    // Stored 120° (闭合基准存储值, 母线直线向量 refWorld = 0°):
    // world = refWorld + 180° − 120° = 60°.
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 60.0) < 1e-6);

    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QVERIFY(doc.findBlock(clone.id) == nullptr);
    QCOMPARE(doc.attachments().size(), size_t(0));

    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 60.0) < 1e-6);
}

void TestRotateCopySemantics::formulaLockedOriginalCopyIsFree()
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

void TestRotateCopySemantics::ctrlDragRotateCopyCommits()
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
void TestRotateCopySemantics::diagonalFreeLineCopyOverlapsOriginal()
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

void TestRotateCopySemantics::ctrlDragZeroAngleDiscards()
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

void TestRotateCopySemantics::escCancelsCopy()
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

void TestRotateCopySemantics::consecutiveCopiesAllAttachToOriginal()
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
void TestRotateCopySemantics::rotateCopyCommitRestoresStripTarget()
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
void TestRotateCopySemantics::lockedFollowerRotationBakesFormula()
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
void TestRotateCopySemantics::lockedFollowerRotateCopyWorks()
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


QTEST_MAIN(TestRotateCopySemantics)
#include "test_rotate_copy_semantics.moc"
