#include "test_select_wkey.h"

// ---------------------------------------------------------------------------
// 曲线点击选择 (2026-10 用户报告: 选择工具对曲线判定比较迷).
// 命中断言点必须取在“实际绘制的曲线”上, 并远离控制折线 — 旧实现命中区
// 用控制折线 (shapePath), 强弯曲曲线偏离控制折线十几毫米, 点在曲线上会被
// 漏掉 (或点在折线附近就算命中).
// ---------------------------------------------------------------------------
void TestSelectWKey::curveBodyClickSelects()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto [blockId, segId] = makeCurveBlock(doc);

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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 曲线参数 T=0.5 处 (第一段跨度的中点): 弧腹点在控制折线内腹, 距折线边
    // 数毫米~十几毫米, 远超 8px 命中带. 断言该前提, 再点击选中.
    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    const auto* entry = blk->curveSpanEntry(segId);
    QVERIFY(entry && entry->spans.size() == 2);
    double dPolyBest = -1.0;
    const Vec2 hitPt = curveWorstHitPoint(*blk, segId, &dPolyBest);
    qInfo().noquote() << QStringLiteral("curve hit point (%1,%2) dist-to-control-polygon=%3")
        .arg(hitPt.x, 0, 'f', 2).arg(hitPt.y, 0, 'f', 2).arg(dPolyBest, 0, 'f', 2);
    QVERIFY2(dPolyBest > 8.0,
             qPrintable(QStringLiteral("测试前提不成立: 曲线点距控制折线只有 %1 mm")
                        .arg(dPolyBest, 0, 'f', 2)));

    click(hitPt.x, hitPt.y);
    QVERIFY2(scene.findBlockItem(blockId)->toolSelected(),
             "点击曲线线身必须选中该块 (命中区 = 实际曲线, 非控制折线)");

    // 低缩放 (view 0.5x): 命中带 = hoverRadiusPx(8) ÷ 0.5 = 16 场景单位,
    // 点击距曲线 12 单位仍在带内; 旧 boundingRect ±10 边距会被场景空间索引
    // (BSP, 按 boundingRect 快速剔除) 直接拒掉 — 曲线在低缩放下更难点中.
    view.scale(0.5, 0.5);
    // scale 同步生效(视图变换即刻改变); 无统一可观测条件, 暂留 qWait 仅作事件排空。
    QTest::qWait(20);
    {
        const auto* e2 = blk->curveSpanEntry(segId);
        QVERIFY(e2 && !e2->spans.empty());
        const Vec2 p0 = curvePointAt(*blk, segId, 0.5);
        const Vec2 tan = cad::geo::evalCurveDerivative(e2->spans, 0.5);
        const double tlen = std::hypot(tan.x, tan.y);
        QVERIFY(tlen > 1e-9);
        const Vec2 off = p0 + Vec2(-tan.y / tlen, tan.x / tlen) * 12.0;
        click(2000.0, 2000.0);  // 空白处 → 清选
        QVERIFY(!scene.findBlockItem(blockId)->toolSelected());
        click(off.x, off.y);
        QVERIFY2(scene.findBlockItem(blockId)->toolSelected(),
                 "低缩放下命中带内的点击必须选中 (boundingRect 需覆盖 pick band)");
    }
}

// 曲线线身拖动: 点曲线中段按住拖动, 块整体平移 (选中即拖).
void TestSelectWKey::curveBodyDragMoves()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto [blockId, segId] = makeCurveBlock(doc);

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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const auto* blk = doc.findBlock(blockId);
    const Vec2 hitPt = curveWorstHitPoint(*blk, segId);

    // press on the curve body → move past the drag threshold → commit.
    const QPoint body = vp(hitPt.x, hitPt.y);
    sendMouse(QEvent::MouseButtonPress, body, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(hitPt.x + 30.0, hitPt.y), Qt::NoButton,
              Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(hitPt.x + 60.0, hitPt.y), Qt::NoButton,
              Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(hitPt.x + 60.0, hitPt.y),
              Qt::LeftButton, Qt::NoModifier);

    QVERIFY(doc.findBlock(blockId)->transform.origin
                .distanceTo(Vec2(60.0, 0.0)) < 1e-6);
}

void TestSelectWKey::quickDetachKeyD()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0}); // (0,-50)-(100,-50)
    doc.resolveAll();

    // 建立连接 (用户拍板 2026-08 复旧: 新建连接默认**焊接** → isLocked=true,
    // 拖动保护默认勾选; 拆散走 D 键快拆 / 面板取消勾选)。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    QVERIFY2(doc.attachments().front().isLocked,
             "新建连接默认勾选拖动保护 (焊接)");

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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 选中 follower B。注意: addAttachment 后 Resolver 已把 B 吸附/旋转
    //    到新位置 (起点吸到 A 的端点) — 取 B 当前解析后的中点点击保证命中。
    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 mid =
        (bb->worldPos(b.startId) + bb->worldPos(b.endId)) * 0.5;
    click(mid.x, mid.y);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) 拖动拆开: 按住 B 拖离 A 自动解除位置吸附、保留角度跟随并生成影子基准。
    auto drag = [&](double x0, double y0, double x1, double y1) {
        const QPoint vp0 = vp(x0, y0);
        const QPoint vp1 = vp(x1, y1);
        QMouseEvent press(QEvent::MouseButtonPress, vp0, view.viewport()->mapToGlobal(vp0),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, vp1, view.viewport()->mapToGlobal(vp1),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, vp1, view.viewport()->mapToGlobal(vp1),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        QTest::qWait(20);
    };
    drag(mid.x, mid.y, mid.x, mid.y + 50.0);

    // 3) 连接已转为仅角度: 位置自由 + 自动解锁 (位置自由 ↔ 拖动保护互斥)。
    //    影子基准 (DETACH_SHADOW_DESIGN.md §3/§7.1, 2026-xx 翻案活引用语义):
    //    基准换代为隐藏影子块 (master = 本体 A), offset 原样保留 (R2)。
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& after = doc.attachments().front();
    QVERIFY2(after.angleOnly, "拖拽应把连接转为仅角度 (拆开保留角度)");
    QVERIFY2(!after.isLocked, "拆开自动清除拖动保护");
    {
        const auto* shadow = doc.blockById(after.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "基准应为影子块 (拆开影子基准)");
        QVERIFY2(shadow->shadowMasterBlockId == a.blockId, "影子 master = 本体 A");
        QVERIFY2(after.followerAngle == 90.0, "offset 原样保留 (R2)");
        QVERIFY2(doc.findBlock(a.blockId) != shadow, "影子与本体无引用关系 (克隆=值拷贝)");
    }

    // 4) undo: 恢复完整连接 (焊接态原样还原 — SetAttachmentAngleOnlyCommand
    //    快照 isLocked=true, undo 回放); 影子随 undo 一并删除 (基准还原本体)。
    doc.undoStack()->undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto& undone = doc.attachments().front();
    QVERIFY2(!undone.angleOnly, "undo 应恢复完整连接");
    QVERIFY2(undone.isLocked,
             "undo 应恢复原来的焊接态 (默认焊, 快照还原不得丢锁)");
    QVERIFY2(undone.toBlockId == a.blockId, "基准还原为本体 (活引用恢复)");
    QVERIFY2(!doc.findShadowOfMaster(a.blockId), "undo 应删除影子块");
}

// 影子挂载路由 (DETACH_SHADOW_DESIGN.md §7.4, 2026-xx 翻案「双基准」语义):
// 线段处于拆开影子基准态 (angleOnly, 基准 = 影子块) 时, 拖动端点重新建立
// 位置连接 → 挂到非本体 = 影子挂载新宿主 (Att1 = 影子→宿主, Δ 反算保向;
// Att2 恢复位置钉点重新焊接) —— 形成 宿主→影子→本线 双连接链 (R3 链式随动)。
// 仅连接语义不变 (followerAngle/公式原样、不进角度会话); undo 整步回拆开态。
void TestSelectWKey::angleOnlyEndpointDragReconnects()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                    // (0,0)-(100,0) 旧基准线
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});  // (0,-50)-(100,-50) 跟随线
    auto c = makeLine(doc, 100.0, Vec2{300.0, -50.0}); // (300,-50)-(400,-50) 新宿主
    doc.resolveAll();

    // B 完整连接 A (B.start 吸到 A.end, 垂直 90°), 再 D 键快拆 → 仅角度
    // (位置自由、角度仍跟随 A = 使用引用线段但没有连接线段)。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QVERIFY(doc.findAttachment(att.id));
    doc.setAttachmentAngleOnly(att.id, true);
    doc.resolveAll();
    const auto* detached = doc.findAttachment(att.id);
    QVERIFY2(detached && detached->angleOnly, "预置: 仅角度 (拆开保留角度)");

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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // B 起点吸在 A.end (100,0), 垂直 90° → 终点 (100,100)。
    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 bEnd = bb->worldPos(b.endId);
    const cad::geo::Vec2 bMid = (bb->worldPos(b.startId) + bEnd) * 0.5;

    // 1) 选中 B。
    sendMouse(QEvent::MouseButtonPress, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) 从 B 端点 (100,100) 拖到 C 终点 (400,-50)。
    const QPoint from = vp(bEnd.x, bEnd.y);
    const QPoint to   = vp(400.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(200.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoModifier);

    // 3) 影子挂载路由 (DETACH_SHADOW_DESIGN.md §7.4, 2026-xx 翻案双基准):
    //    拖端点释放到 C = 影子挂载 (Att1 = 影子→C, Δ 反算保向) + Att2 恢复
    //    位置钉点重新焊接 —— 形成 C→影子→B 双连接链 (R3 链式随动)。
    //    followerAngle 原样保留 (仅连接, 不反算覆盖)。
    QCOMPARE(doc.attachments().size(), size_t(2));
    const cad::param::Attachment* after = doc.findAttachment(att.id);
    QVERIFY(after);
    QVERIFY2(!after->angleOnly, "释放到目标后应恢复完整位置连接 (Att2)");
    QVERIFY2(after->isLocked, "重挂恢复完整连接应重新焊接 (拖动保护默认勾选)");
    const auto* shadowBlk = doc.blockById(after->toBlockId);
    QVERIFY2(shadowBlk && shadowBlk->isShadow, "Att2 基准仍为影子块 (换代不回退)");
    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a2 : doc.attachments()) {
        if (!a2.isPin && a2.fromBlockId == after->toBlockId) { att1 = &a2; break; }
    }
    QVERIFY2(att1, "影子挂载应生成 Att1 (影子→新宿主)");
    QVERIFY2(att1->toBlockId == c.blockId, "影子应挂到新宿主线段");
    QVERIFY2(att1->toPointId == c.endId, "影子锚点应钉在新宿主端点");
    QVERIFY2(after->followerAngle == 90.0,
             "仅连接: followerAngle 应原样保留, 不得反算覆盖 (旧实现 bug)");
    const cad::param::Block* bb2 = doc.findBlock(b.blockId);
    const cad::param::Block* cc = doc.findBlock(c.blockId);
    QVERIFY(bb2 && cc);
    QVERIFY(bb2->worldPos(b.startId).distanceTo(cc->worldPos(c.endId)) < 1e-6);

    // 3b) R3 链式随动: 宿主 C 旋转 +30° → B 跟随旋转 +30° (位置+角度链式)。
    {
        const double rotB0 = bb2->transform.rotation;
        doc.blockById(c.blockId)->transform.rotation += 30.0 * M_PI / 180.0;
        doc.resolveAll();
        QVERIFY2(std::abs(doc.findBlock(b.blockId)->transform.rotation
                          - (rotB0 + 30.0 * M_PI / 180.0)) < 1e-9,
                 "L3 旋转 → 影子随动 → B 链式跟转 (R3)");
    }

    // 4) 仅连接: 直接完成连接回 Idle (不进入角度输入会话)。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(doc.attachments().size(), size_t(2));

    // 5) undo: 回到快拆后的仅角度态 (Att1 删除, 影子保留冻结当前方向,
    //    位置/角度原样) — 影子挂载整步可撤销。
    doc.undoStack()->undo();
    const cad::param::Attachment* undone2 = doc.findAttachment(att.id);
    QVERIFY(undone2);
    QVERIFY2(undone2->angleOnly, "undo 应回到仅角度态");
    QVERIFY2(doc.findAttachment(att.id)->toBlockId == shadowBlk->id,
             "undo 后 Att2 仍指向影子");
    bool att1Gone = true;
    for (const auto& a2 : doc.attachments())
        if (!a2.isPin && a2.fromBlockId == shadowBlk->id) att1Gone = false;
    QVERIFY2(att1Gone, "undo 应删除 Att1 (挂载关系)");
}

// ── 有对齐点方向 + 变量时接入新线段不覆盖变量 (2026-xx 用户报告) ─────────
// 线段处于拆开影子基准态 (angleOnly) 且使用了自定义对齐点方向
// (angleRefBlockId 非空) 与变量 (followerAngleFormula, 如 LL=90) 时, 拖端点
// 接入新线段 (影子挂载路由, §7.4):
//   · 影子挂载只动位置维度 (Att1 + Att2 焊接) —— 自定义对齐点方向与变量
//     原样保留, 不反算覆盖、不清变量 (旧实现把变量烘成数值)。
//   · 不进入角度输入会话 (回车 = 确定连接而非确定角度), 直接完成连接。
//   · undo 单步回到拖前仅角度态。
void TestSelectWKey::angleRefWithFormulaReattachKeepsFormula()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto a = makeLine(doc, 100.0);                     // (0,0)-(100,0) 旧基准线
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});   // (0,-50)-(100,-50) 跟随线
    auto c = makeLine(doc, 100.0, Vec2{300.0, -50.0});  // (300,-50)-(400,-50) 新宿主
    doc.resolveAll();

    // 变量 LL = 90 (度)。
    FormulaVariable ll;
    ll.name = QStringLiteral("LL");
    ll.expression = QStringLiteral("90");
    doc.addFormula(ll);

    // B 完整连接 A (B.start 吸到 A.end), 再 D 键快拆 → 仅角度。
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    doc.setAttachmentAngleOnly(att.id, true);
    doc.resolveAll();
    const auto* detached = doc.findAttachment(att.id);
    QVERIFY2(detached && detached->angleOnly, "预置: 仅角度 (拆开保留角度)");

    // 用户自定义对齐点方向 = A (旧基准线), 并设变量 LL=90 驱动跟随角。
    auto* mut = doc.findAttachment(att.id);
    QVERIFY(mut);
    mut->angleRefBlockId = a.blockId;
    mut->angleRefSegmentId = a.segId;
    mut->angleRefPointId = a.endId;
    mut->followerAngleFormula = QStringLiteral("LL");
    doc.resolveAll();
    QVERIFY2(!mut->angleRefBlockId.isNull(), "预置: 用户自定义对齐点方向");
    QVERIFY2(mut->followerAngleFormula == QStringLiteral("LL"),
             "预置: 变量驱动跟随角");

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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const cad::param::Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    const cad::geo::Vec2 bEnd = bb->worldPos(b.endId);
    const cad::geo::Vec2 bMid = (bb->worldPos(b.startId) + bEnd) * 0.5;

    // 1) 选中 B。
    sendMouse(QEvent::MouseButtonPress, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(bMid.x, bMid.y), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());

    // 2) 从 B 端点拖到 C 终点 (400,-50)。
    const QPoint from = vp(bEnd.x, bEnd.y);
    const QPoint to   = vp(400.0, -50.0);
    sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(200.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, to, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoModifier);

    // 3) 影子挂载路由 (§7.4): 释放到 C = 影子挂载 (Att1 = 影子→C) + Att2
    //    恢复位置钉点 —— 自定义基准/变量都挂在 Att2 上, 原样保留。
    QCOMPARE(doc.attachments().size(), size_t(2));
    const cad::param::Attachment* after = doc.findAttachment(att.id);
    QVERIFY(after);
    QVERIFY2(!after->angleOnly, "释放到目标后应恢复完整位置连接");
    const auto* shadowBlk = doc.blockById(after->toBlockId);
    QVERIFY2(shadowBlk && shadowBlk->isShadow, "Att2 基准仍为影子块");

    // 4) 有对齐点方向: 方向基准 = 对齐点 (A), 变量 LL 原样保留, 不烘成数值
    //    (影子挂载不动 Att2 的角度域 —— 自定义基准原样保留, R6 同源纪律)。
    QVERIFY2(after->angleRefBlockId == a.blockId,
             "用户自定义对齐点方向应原样保留");
    QVERIFY2(after->followerAngleFormula == QStringLiteral("LL"),
             "变量 LL 应原样保留, 不得被烘成数值 (旧实现 bug)");
    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a2 : doc.attachments()) {
        if (!a2.isPin && a2.fromBlockId == after->toBlockId) { att1 = &a2; break; }
    }
    QVERIFY2(att1 && att1->toBlockId == c.blockId,
             "影子应挂到新宿主 C (Att1)");

    // 5) 有对齐点方向: 不进入角度输入会话, 直接完成连接回 Idle。
    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);

    // 6) undo: 回到拖前仅角度态 (位置/角度/变量原样)。
    doc.undoStack()->undo();
    const cad::param::Attachment* undone = doc.findAttachment(att.id);
    QVERIFY(undone);
    QVERIFY2(undone->angleOnly, "undo 应回到仅角度态");
    QVERIFY2(undone->followerAngleFormula == QStringLiteral("LL"),
             "undo 后变量 LL 应原样保留");
}

// 二期 (CONTEXT_STRIP_DESIGN): 连接角度会话的输入走 条带 → 工具 → 手势 通道:
// 击键实时预览 (附件角度直写、无命令入栈)、°/⌒ 几何保持切换、Enter 确认收尾
// 为整步 undo 宏。旧浮动 AngleHud 已整体退场。
void TestSelectWKey::angleSessionStripInputDrivesConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 三个独立 对 (leader/follower), 每会话一对 —— 避免 follower 落点与
    // 已连接端点重叠触发 ConfirmTarget, 也免 undo 位移干扰。
    auto a = makeLine(doc, 100.0);                      // A: (0,0)-(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});    // B: (0,-50)-(100,-50)
    auto c = makeLine(doc, 100.0, Vec2{0.0, -100.0});   // C: (0,-100)-(100,-100)
    auto d = makeLine(doc, 100.0, Vec2{0.0, -150.0});   // D: (0,-150)-(100,-150)
    auto e = makeLine(doc, 100.0, Vec2{0.0, -200.0});   // E: (0,-200)-(100,-200)
    auto f = makeLine(doc, 100.0, Vec2{0.0, -250.0});   // F: (0,-250)-(100,-250)
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

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);

    // ── 会话 1 (A→B): 击键预览 + °/⌒ 切换 + Enter 收尾 ──
    {
        // 选中 A 并拖 A.end (100,0) → B.end (100,-50)。
        const QPoint bodyA = vp(50.0, 0.0);
        sendMouse(QEvent::MouseButtonPress, bodyA, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyA, Qt::LeftButton, Qt::NoModifier);
        const QPoint fromA = vp(100.0, 0.0);
        sendMouse(QEvent::MouseButtonPress, fromA, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -15.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -40.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -50.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -50.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(1));

        // 1) 击键实时预览: 附件角度直写 (会话内不 push 命令)。
        const int undoIndex = doc.undoStack()->index();
        ts->connectAngleTextChanged(QStringLiteral("45"));
        QCOMPARE(doc.attachments().front().followerAngle, 45.0);
        QCOMPARE(doc.undoStack()->index(), undoIndex);

        // 2) °/⌒ 切换: 几何保持换算到弧长模式。
        ts->connectAngleModeChanged(cad::param::RotationMode::ArcLength);
        QCOMPARE(doc.attachments().front().rotationMode,
                 cad::param::RotationMode::ArcLength);

        // 3) Enter 确认: 收尾为整步 undo 宏 (连接 + 角度调整一步撤销)。
        ts->connectAngleCommitted();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(1));
        QCOMPARE(doc.attachments().front().rotationMode,
                 cad::param::RotationMode::ArcLength);
        QVERIFY(doc.undoStack()->index() > undoIndex);
    }

    // ── 会话 2 (C→D): 无效公式 → Enter 被拒; Esc 收尾 (留下 无效/弧长 残留态) ──
    {
        const QPoint bodyC = vp(50.0, -100.0);
        sendMouse(QEvent::MouseButtonPress, bodyC, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyC, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, vp(100.0, -100.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -125.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -150.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -150.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(2));

        ts->connectAngleTextChanged(QStringLiteral("abc"));   // 无效公式
        ts->connectAngleCommitted();
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);   // 无效 Enter 忽略
        ts->connectAngleCancelled();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(2));
    }

    // ── 会话 3 (E→F): 新会话必须复位 残留无效标记 + 残留弧长模式 ──
    //    (回归: 旧 showAngleHud 每次显示复位 m_angleValid/m_angleMode,
    //    beginAngleSession 同款 —— 否则 Enter 永远被拒 / 数值写错存储域。)
    {
        const QPoint bodyE = vp(50.0, -200.0);
        sendMouse(QEvent::MouseButtonPress, bodyE, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, bodyE, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, vp(100.0, -200.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(105.0, -225.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(100.0, -250.0), Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(100.0, -250.0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
        QCOMPARE(doc.attachments().size(), size_t(3));

        // 模式已复位为角度: 数值输入写 followerAngle 而非弧长 (检查本会话
        // 新增的附件 = 最后一个)。
        ts->connectAngleTextChanged(QStringLiteral("30"));
        QCOMPARE(doc.attachments().back().followerAngle, 30.0);
        QCOMPARE(doc.attachments().back().rotationMode,
                 cad::param::RotationMode::Angle);

        // 合法性已复位: Enter 立即收尾 (不因上一会话的无效输入被拒)。
        ts->connectAngleCommitted();
        QTest::qWait(30);
        QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
        QCOMPARE(doc.attachments().size(), size_t(3));
        QCOMPARE(doc.attachments().back().rotationMode,
                 cad::param::RotationMode::Angle);
    }
}


// ---------------------------------------------------------------------------
// 重叠线段消歧 (2026-10): 悬停提示 → 点选+W 循环 → 右键候选菜单 (共享候选集合)
// ---------------------------------------------------------------------------

// C 方案: 完全重合的两条线段, 悬停时 HUD 提示候选名单 (名称可分辨).
void TestSelectWKey::overlapHoverShowsClusterHint()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 两条几何完全重合的线段: A (后建, 堆叠在上) / B.
    auto b = makeLine(doc, 100.0);                   // (0,0)-(100,0)
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto move = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent m(QEvent::MouseMove, vpPos, global,
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &m);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 悬停重合线身 → 悬停提示已被去除，不干扰视野.
    move(50.0, 0.0);
    QVERIFY(sel->overlapHintText().isEmpty());

    // 移开 → 保持干净.
    move(500.0, 500.0);
    QVERIFY(sel->overlapHintText().isEmpty());
}

// 方案: 点击重合点后，按 W 键打开电池组; 点选电池卡片切换对象; 点分离线后 W 恢复单选/多选切换.
void TestSelectWKey::overlapClickCyclesWithWKey()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 完全重合: A (后建, 堆叠上) / B 建在同一起点; 另加一条分离线 C 用于退出验证.
    auto b = makeLine(doc, 100.0);
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    auto c = makeLine(doc, 100.0, Vec2{0.0, -300.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        QTest::qWait(20);
    };
    auto pressW = [&]() {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QApplication::sendEvent(&view, &key);
        QTest::qWait(20);
    };

    // 1) 点击重合处 → 选中堆叠最上的 A.
    click(50.0, 0.0);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());

    // 2) 按 W 键 → 触发打开电池组 (快捷键触发)
    pressW();

    // 3) 点选候选 1 (B) → 切换到 B
    sel->pickOverlapCandidate(1);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());

    // 4) 点分离线 C → 选中 C; 再按 W 恢复为模式切换 (单选→多选).
    click(50.0, -300.0);
    pressW();   // 非重叠点: W = 切换多选/单选
    click(50.0, 0.0);            // 多选: 加上 A
    QVERIFY(scene.findBlockItem(c.blockId)->toolSelected());
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
}

// B' 方案共用的命令式点名选中: 循环上下文中按索引换选 (右键菜单走同一入口).
void TestSelectWKey::overlapPickCandidateByIndex()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto b = makeLine(doc, 100.0);
    auto a = makeLine(doc, 100.0, Vec2{0.0, 0.0});
    doc.findBlock(a.blockId)->name = QString::fromUtf8("A");
    doc.findBlock(b.blockId)->name = QString::fromUtf8("B");
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto click = [&](double x, double y) {
        const QPoint vpPos = vp(x, y);
        const QPoint global = view.viewport()->mapToGlobal(vpPos);
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 点重合 → 激活; 命令式点名 2 号候选 (B) —— 与右键「重叠候选」菜单同一入口.
    click(50.0, 0.0);
    QVERIFY(sel->overlapIndex() == 0);
    sel->pickOverlapCandidate(1);
    QVERIFY(scene.findBlockItem(b.blockId)->toolSelected());
    QVERIFY(!scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(sel->overlapIndex(), 1);
}

// ---------------------------------------------------------------------------
// 连接卡片两维独立 (2026-xx 用户拍板): 「连接线段」「独立角度」「连接保护」
// 复选框与「清除」按钮全删 —— 连接语义 = 两个正交维度的开关:
//   · 连接点按钮 (位置维度): 拆开 = 位置自由 (angleOnly, 角度仍跟随);
//     重连 = 位置重新吸附回原宿主 + 重新焊接。
//   · [独立] 按钮 (角度维度, SegmentRefCard, PANEL_REDESIGN §3/§6.4):
//     勾选 = 角度不跟随 (世界角度, 独立角); 再点 = 还原上次基准 (反算零跳变)。
//   · 位置拆开 + [独立] = 自由线 (Resolver angleOnly 无条件放行位置、
//     angleIndependent 保持自身旋转 —— 两维不再互斥)。
// 四态矩阵: 全连接 / 独立角 (位置跟·角度拆) / 仅角度 (位置拆·角度跟) / 自由。
// ---------------------------------------------------------------------------
