#include "test_rotate_helpers.h"

class TestRotateCopyEndtarget : public QObject
{
    Q_OBJECT
private slots:
    void modeSwitchKeepsFormula();
    void propertyDialogShowsFollowValue();
    void endTargetRotationReleasesAim();
    void endTargetRotateCopyDropsAim();
    void endTargetRotateCopyKeepsOriginalAim();
    void endTargetRotateCopyIdleGestureKeepsOriginalAim();
    void endTargetRotateCopyUndoRedoKeepsOriginalAim();
    void midGestureCtrlConvertsToCopy();
};

// 弧长/角度表达式锁定：HUD 切换角度↔弧长模式只是显示单位变化，绝不把
// 表达式换算烘焙成数值（用户要求：弧长用表达式时不能自己换算成数值）。
void TestRotateCopyEndtarget::modeSwitchKeepsFormula()
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
void TestRotateCopyEndtarget::propertyDialogShowsFollowValue()
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
void TestRotateCopyEndtarget::endTargetRotationReleasesAim()
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
void TestRotateCopyEndtarget::endTargetRotateCopyDropsAim()
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
void TestRotateCopyEndtarget::endTargetRotateCopyKeepsOriginalAim()
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
void TestRotateCopyEndtarget::endTargetRotateCopyIdleGestureKeepsOriginalAim()
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
void TestRotateCopyEndtarget::endTargetRotateCopyUndoRedoKeepsOriginalAim()
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
void TestRotateCopyEndtarget::midGestureCtrlConvertsToCopy()
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


QTEST_MAIN(TestRotateCopyEndtarget)
#include "test_rotate_copy_endtarget.moc"
