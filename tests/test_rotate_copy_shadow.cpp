#include "test_rotate_helpers.h"

class TestRotateCopyShadow : public QObject
{
    Q_OBJECT
private slots:
    void shadowChannel_formulaLockRotatesShadowKeepsP3();
    void shadowChannel_noFormulaLockRotatesShadowKeepsFollowerAngle();
};

// ── 影子角度通道 (拆开影子基准 R6/R8, DETACH_SHADOW_DESIGN.md §7.2) ────────
// offset 公式锁定 + 基准 = 影子块 (拆开态): 旋转手势不再拒绝 —— 改写影子
// 角度 (公式原样存活, R6), 跟随线绕 p3 原地转 (p3 世界位置不变, R8);
// undo 整步回到拖前 (影子角度 + 跟随线姿态)。
void TestRotateCopyShadow::shadowChannel_formulaLockRotatesShadowKeepsP3()
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

void TestRotateCopyShadow::shadowChannel_noFormulaLockRotatesShadowKeepsFollowerAngle()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // 本体 A + 跟随线 B (无公式锁定, 纯数值夹角 90°), B.start 吸到 A.end。
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 50.0);
    Attachment conn;
    conn.fromBlockId = b.blockId;
    conn.fromPointId = b.startId;
    conn.toBlockId = a.blockId;
    conn.toPointId = a.endId;
    conn.toSegmentId = a.segId;
    conn.followerAngle = 90.0;
    conn.followerAngleFormula.clear();   // 无公式锁定! (验证无公式锁定下依然旋转影子保持夹角)
    QVERIFY(doc.addAttachment(conn));
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // 拆开 (影子换代): Att2 基准 = 影子块。
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

    // 旋转: 按在 B 中点 (光标角 90°), 拖到 (150,0) (光标角 0°) → 光标增量 −90°
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    doc.resolveAll();

    // 核心断言: 即使没有公式锁定, 也绝不篡改 followerAngle (恒定为 90°)!
    // 旋转的是基准角度 (影子旋转 −90°), B 方向随影子转动。
    QCOMPARE(doc.findAttachment(conn.id)->followerAngle, 90.0);
    QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                      - (shadowRot0 - M_PI / 2.0)) < 1e-6,
             "无公式锁定下旋转依然改写影子角度 (−90°)");
    QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation
                      - (rotB0 - M_PI / 2.0)) < 1e-6,
             "B 方向随影子基准转动");
    QVERIFY2(doc.findBlock(b.blockId)->worldPos(b.startId).distanceTo(p3World0) < 1e-6,
             "原地转: p3 世界位置保持不变");

    // undo 验证
    stack.undo();
    doc.resolveAll();
    QCOMPARE(doc.findAttachment(conn.id)->followerAngle, 90.0);
    QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation - shadowRot0) < 1e-9,
             "undo: 影子角度恢复");
    QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation - rotB0) < 1e-9,
             "undo: B 姿态恢复");
}

// ═══════════════════════════════════════════════════════════════════
// UI-layer: full event chain through CanvasView (QTest mouse events →
// dispatch → ToolRotate).
// ═══════════════════════════════════════════════════════════════════


QTEST_MAIN(TestRotateCopyShadow)
#include "test_rotate_copy_shadow.moc"
