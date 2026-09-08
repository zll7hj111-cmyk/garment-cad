#include "test_rotate_helpers.h"

class TestRotateStrip : public QObject
{
    Q_OBJECT

private slots:
    void stripShowsFormulaForFormulaDrivenAngle();
    void stripShowsFormulaForFormulaDrivenArc();
    void angleModeOverflowNormalized();
    void arcLengthModeOverflowNormalized();
};
// 弧长/角度显示归一化：存储多圈角度（1260° = 3.5 圈）时 HUD 必须显示
// [0, 360) 内的归一化值（用户报告: 400°+ 爆表回归 2026-08）。
void TestRotateStrip::stripShowsFormulaForFormulaDrivenAngle()
{
    // 2026-12 统一 (用户报告 "用了变量参数, HUD 显示的是换算的数值"):
    // 公式驱动的跟随角, 旋转 HUD 显示公式原文而非换算数值 (与
    // SegmentAngleCard「公式优先显示原文」同约定)。
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
    conn.followerAngleFormula = QStringLiteral("45");   // 公式驱动
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
        QTest::qWait(20);
    };

    // Select A (anchor = attachment point → Connected/Angle mode).
    // 先解析实际几何再点 A 中段 (followerAngle 45° → 世界角 135°, 朝左上方)。
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

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // 公式驱动 → HUD 显示公式原文 (2026-12 统一约定)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("45"));
}

void TestRotateStrip::stripShowsFormulaForFormulaDrivenArc()
{
    // 2026-12 统一: 弧长公式驱动的跟随线, 旋转 HUD 显示公式原文 (cm 域)。
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
    conn.arcLengthFormula = QStringLiteral("10");   // 弧长公式 (cm)
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
        QTest::qWait(20);
    };

    // Select A: 先解析实际几何再点 A 中段 (弧长 10cm / 半径 60mm → A 朝上)。
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

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // 弧长公式驱动 → HUD 显示公式原文 (2026-12 统一约定)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("10"));
}

void TestRotateStrip::angleModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with followerAngle = 1260°
    // (闭合基准: 世界角 = 180° − 1260° ≡ 0° → A points RIGHT, mid-body at
    // (330,0); HUD 显示 fmod(1260,360) = 180°).
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 1260.0;   // multi-turn stored value
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

    // Select A (anchor = attachment point → Connected/Angle mode).
    sendMouse(QEvent::MouseButtonPress, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("180"));   // 1260 → 180
}

// 弧长模式多圈 → 切回角度模式，条带显示归一化角度（不爆表）。
void TestRotateStrip::arcLengthModeOverflowNormalized()
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
    conn.arcLength = 3.0 * 2.0 * M_PI * 60.0;   // 3 full turns
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

    // Select A: 3 turns = 弧长角 1080° ≡ 0° 折叠（闭合基准恒等映射 2026-08:
    // 弧长角 = 线夹角）→ A points LEFT (toward (240,0)), mid-body (270,0).
    // 显示 = 带符号折角（v3 定稿）：1080° ≡ 0° 折叠 → HUD 0° / 0cm。
    sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // 条带锁定到该线段 (旋转工具不再持有 HUD —— 读数与输入都落这里)。
    QVERIFY(!bridge.strip.blockId().isNull());
    // ArcLength mode: 条带显示带符号折角弧长 (cm; 3 turns ≡ 0° 折叠 → 0)。
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));

    // 点 ° 切回角度模式: 角度必须归一化到折叠后的 0°
    // (弧长 3 圈 ≡ 角度 0° 折叠, 恒等映射 2026-08)。
    QVERIFY(bridge.strip.unitAngleButton()->isEnabled());
    QTest::mouseClick(bridge.strip.unitAngleButton(), Qt::LeftButton);
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("0"));   // 1080 → 0

    // 输入带符号折角：输入 270（>180 视为原始 α）→ 存储 α = 270（另一侧）。
    // 条带的长度/角度输入走 200ms debounce 且只在字段聚焦时应用 —— 先给焦点。
    bridge.strip.angleEdit()->setFocus();
    bridge.strip.angleEdit()->setText(QStringLiteral("270"));
    // 断言"值变了"(debounce 到期后同步应用): 谓词 = 存储断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        for (const auto& att2 : doc.attachments())
            if (att2.fromBlockId == a.blockId && std::abs(att2.followerAngle - 270.0) < 1e-6)
                return true;
        return false;
    }), "输入 270 后 followerAngle 应更新为 270（条带输入未应用到存储）");

    // 单位切换往返刷新条带：显示带符号折角 −90（v3 定稿，符号 = 折向）。
    QTest::mouseClick(bridge.strip.unitArcButton(), Qt::LeftButton);   // → 弧长
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("-9.4"));
    QTest::mouseClick(bridge.strip.unitAngleButton(), Qt::LeftButton); // → 角度
    QCOMPARE(bridge.strip.angleEdit()->text(), QStringLiteral("-90"));
}

QTEST_MAIN(TestRotateStrip)
#include "test_rotate_strip.moc"