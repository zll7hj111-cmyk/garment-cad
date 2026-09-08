#include "test_select_wkey.h"

void TestSelectWKey::connectionCardNewSemantics()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    auto line   = makeLine(doc, 60.0);
    doc.resolveAll();

    // 建立连接 (用户拍板 2026-08 复旧: 新建连接默认焊接).
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    QVERIFY2(doc.attachments().front().isLocked, "新建连接默认焊接");

    cad::ui::SegmentConnectionCard card(&doc, &scene);
    card.setTarget(line.blockId, line.segId);
    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);

    auto findBtn = [&](QWidget& w, const QString& text) -> QPushButton* {
        for (auto* b : w.findChildren<QPushButton*>())
            if (b->text() == text) return b;
        return nullptr;
    };
    auto worldDir = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        const Vec2 p1 = b->transform.toWorld(b->findPoint(line.startId)->resolvedPos);
        const Vec2 p2 = b->transform.toWorld(b->findPoint(line.endId)->resolvedPos);
        return std::atan2(p2.y - p1.y, p2.x - p1.x);
    };
    auto angleDelta = [](double a, double b) {
        double d = std::abs(a - b);
        d = std::fmod(d, 2.0 * M_PI);
        return d > M_PI ? 2.0 * M_PI - d : d;
    };
    auto fromPointWorld = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        return b->transform.toWorld(b->findPoint(line.startId)->resolvedPos);
    };

    // 0) 控件骨架: 连接卡无复选框、无「清除」按钮; 角度维度 = [独立] 勾选钮。
    QVERIFY2(card.findChildren<QCheckBox*>().isEmpty(),
             "连接卡不得再有复选框");
    QVERIFY2(!findBtn(card, QString::fromUtf8("清除")),
             "连接卡「清除」按钮已删 (拆开/重连 覆盖断开语义)");
    QPushButton* btnAngleBase = refCard.findChild<QPushButton*>(
        QStringLiteral("angleBaseToggleBtn"));
    QVERIFY2(btnAngleBase, "角度维度按钮 [独立] (objectName 沿用旧契约)");
    QCOMPARE(btnAngleBase->text(), QString::fromUtf8("独立"));
    QVERIFY2(!btnAngleBase->isChecked(), "初始非独立 (角度跟随基准)");
    QVERIFY2(btnAngleBase->isCheckable(), "[独立] 必须是 checkable 勾选钮");

    // 1) 连接点「拆开」(位置维度): 仅角度 —— 位置自由、角度仍跟随。
    //    影子换代 (DETACH_SHADOW_DESIGN.md §7.1, 2026-xx 翻案「位置记忆保留
    //    在附件 toBlockId」): 拆开后基准 = 影子块 (master = 原宿主) —— 重连
    //    回宿主的记忆由影子 master 链接承载 (⑤ 复原锚点 = 角色 1:1 映射)。
    QPushButton* btnDetach = findBtn(card, QString::fromUtf8("拆开"));
    QVERIFY(btnDetach);
    QVERIFY(btnDetach->isEnabled());
    btnDetach->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleOnly, "连接点拆开 = 位置维度拆开 (仅角度)");
        QVERIFY2(!a.isLocked, "拆开自动解除焊接");
        QVERIFY2(!a.angleIndependent, "位置维度拆开不碰角度维度");
        const auto* shadow = doc.blockById(a.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "拆开 = 影子换代 (基准 → 影子)");
        QVERIFY2(shadow->shadowMasterBlockId == leader.blockId,
                 "重连回宿主的记忆 = 影子 master (原宿主)");
        QVERIFY2(a.toPointId != leader.endId, "锚点 = 影子克隆点 (与本体无引用)");
        QCOMPARE(a.followerAngle, 180.0);   // offset 原样保留 (R2)
    }
    card.refresh();
    // 起点行按钮 (connDetachBtn) 翻面为「重连」; 终点行恒有禁用「拆开」,
    // 故不能再用全局 findBtn("拆开") 断言。
    QPushButton* connDetach = card.findChild<QPushButton*>(
        QStringLiteral("connDetachBtn"));
    QVERIFY2(connDetach && connDetach->text() == QString::fromUtf8("重连"),
             "位置拆开态连接点按钮翻面为「重连」");
    QVERIFY2(!btnAngleBase->isChecked(), "角度维度未动 ([独立] 未勾选)");
    const double dirDetached = worldDir(line.blockId);

    // 2) 连接点「重连」(位置维度): 位置回原宿主 + 重新焊接, 方向零跳变。
    QPushButton* btnReconnect = findBtn(card, QString::fromUtf8("重连"));
    QVERIFY2(btnReconnect, "位置拆开态应显示「重连」");
    QVERIFY(btnReconnect->isEnabled());
    btnReconnect->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly, "重连 = 位置维度恢复");
        QVERIFY2(a.isLocked, "重连必须重新焊接");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "重连后 from-point 必须重新吸附回宿主点");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirDetached) < 1e-9,
                 "重连后方向不得跳变");
    }

    // 3) [独立] 勾选 (角度维度): 独立角 —— 位置保持吸附、角度不再跟随。
    refCard.refresh();
    QCOMPARE(btnAngleBase->text(), QString::fromUtf8("独立"));
    QVERIFY2(!btnAngleBase->isChecked(), "跟随态 [独立] 未勾选");
    QVERIFY(btnAngleBase->isEnabled());
    btnAngleBase->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleIndependent, "基准点拆开 = 角度维度拆开 (独立角)");
        QVERIFY2(!a.angleOnly, "角度维度拆开不碰位置维度");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "独立角位置必须仍吸附在宿主点");
    }
    refCard.refresh();
    QVERIFY2(btnAngleBase->isChecked(), "独立角态 [独立] 已勾选");
    // 独立角态: 连接点按钮仍可用 (位置维度独立, 可再拆 → 自由)。
    card.refresh();
    QVERIFY2(findBtn(card, QString::fromUtf8("拆开"))->isEnabled(),
             "独立角态连接点按钮应可用 (两维独立)");
    const double dirIndep = worldDir(line.blockId);

    // 4) [独立] 取消勾选 (角度维度): 恢复角度跟随 (还原上次基准), 方向零跳变。
    btnAngleBase->click();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleIndependent, "[独立] 取消 = 角度维度恢复");
        QVERIFY2(!a.angleOnly, "重连角度不碰位置维度 (仍是全连接)");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirIndep) < 1e-9,
                 "恢复角度跟随不得跳线");
    }

    // 5) 双拆开 = 自由线: 位置自由 + 角度自管 —— 基准线旋转, 本线不动。
    card.refresh();
    btnDetach = findBtn(card, QString::fromUtf8("拆开"));
    btnDetach->click();               // 位置拆开
    refCard.refresh();
    btnAngleBase->click();            // [独立] 勾选 (角度拆开)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleOnly && a.angleIndependent,
                 "双拆开 = 位置自由 + 角度自管 (自由线)");
    }
    const auto* blk = doc.findBlock(line.blockId);
    const Vec2 originBefore = blk->transform.origin;
    const double rotBefore = blk->transform.rotation;
    // 基准线旋转 30°: 自由线不得跟随 (位置与角度都不被驱动)。
    auto* leaderMut = doc.findBlock(leader.blockId);
    leaderMut->transform.rotation += 30.0 * M_PI / 180.0;
    doc.resolveAll();
    QVERIFY2(blk->transform.origin.distanceTo(originBefore) < 1e-9,
             "双拆开自由线不得跟随基准线移动");
    QVERIFY2(std::abs(blk->transform.rotation - rotBefore) < 1e-9,
             "双拆开自由线角度不得被基准线驱动");

    // 6) 从自由逐步重连 → 全连接 (位置「重连」+ [独立] 取消, 顺序无关, 零跳变)。
    card.refresh();
    const double dirFree = worldDir(line.blockId);
    findBtn(card, QString::fromUtf8("重连"))->click();      // 位置维度
    QVERIFY2(!doc.attachments().front().angleOnly,
             "重连位置维度 → 独立角");
    refCard.refresh();
    btnAngleBase->click();                                  // [独立] 取消 (角度维度)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly && !a.angleIndependent, "双重重连 = 全连接");
        QVERIFY2(a.isLocked, "位置重连重新焊接");
        const auto* ldr = doc.findBlock(leader.blockId);
        const Vec2 hostWorld = ldr->transform.toWorld(
            ldr->findPoint(leader.endId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromPointWorld(line.blockId)) < 1e-6,
                 "全连接 from-point 回宿主点");
        QVERIFY2(angleDelta(worldDir(line.blockId), dirFree) < 1e-9,
                 "自由→全连接不得跳线");
    }

    // 7) 全新自由线 (无 attachment): 两个 拆开 按钮禁用; 输入 P# 回车建立连接。
    auto line2 = makeLine(doc, 50.0);
    doc.resolveAll();
    cad::ui::SegmentConnectionCard card2(&doc, &scene);
    card2.setTarget(line2.blockId, line2.segId);
    card2.refresh();
    QPushButton* btnDetach2 = findBtn(card2, QString::fromUtf8("拆开"));
    QVERIFY2(btnDetach2 && !btnDetach2->isEnabled(),
             "无连接时位置拆开按钮禁用");
    const auto* ldr7 = doc.findBlock(leader.blockId);
    const auto* hp7 = ldr7->findPoint(leader.endId);
    // 2026-08 「连接线段」框也是 PointRefEdit 了 —— 必须按 objectName 定位
    // 连接点框; 无差 findChild 会命中先创建的 connSegEdit (线段框只预填不建连)。
    auto* refConn2 = card2.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("connPointEdit"));
    QVERIFY(refConn2);
    refConn2->setText(hp7->serial);
    QTest::keyClick(refConn2, Qt::Key_Return);
    QCOMPARE(doc.attachments().size(), size_t(2));
    {
        const auto& a = doc.attachments().back();
        QCOMPARE(a.fromBlockId, line2.blockId);
        QCOMPARE(a.toBlockId, leader.blockId);
        QCOMPARE(a.toPointId, leader.endId);
        QVERIFY2(a.isLocked, "输入 P# 建立连接默认焊接");
    }
}

// ---------------------------------------------------------------------------
// 角度基准两点化 — 点2 输入框 (2026-08-31 修复「点2 完全无效」):
//   · 自动态 (默认跟随) 填点2 → 自动点1 (所连点) 固化为显式点1, 与点2 一起
//     六参落库 —— 此前 ref2 单独落库被 Resolver 两点分支 (angleRefBlockId
//     非空门控) 忽略, 输入随即被 refresh 清空 = 完全无效;
//   · 点2 == 有效点1 → 拒绝 (零向量方向); 设置本身反算零跳变;
//   · 点2 宿主块移动 → 跟随线转向 点1→点2 连线方向 (引擎消费);
//   · undo 单步回自动态 (ref1/ref2 全空);
//   · 自由线只预填点2 → 连入后第一次 refresh 落库 (此前被静默丢弃)。
// ---------------------------------------------------------------------------
void TestSelectWKey::angleRefPoint2TwoPointBasis()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));   // 宿主 A
    const auto line   = makeLine(doc, 60.0);                      // 本线
    const auto hostB  = makeLine(doc, 80.0, Vec2(120.0, 90.0));   // 点2 宿主 B
    doc.resolveAll();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* p1Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY2(p1Edit && p2Edit,
             "点1/点2 输入框 (angleRefPointEdit/angleRefPoint2Edit) 必须存在");

    // 挂 view 才能收到 CanvasScene::showToast (无视口时早退) —— 点2 拒绝
    // 路径的 toast 断言依赖 (2026-09, E:\4.gcad L2 报告: 用户填点2 "无法
    // 填入", 根因 = 静默拒绝零反馈)。
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto angDiff = [](double a, double b) {
        double d = std::abs(a - b);
        d = std::fmod(d, 2.0 * M_PI);
        return d > M_PI ? 2.0 * M_PI - d : d;
    };
    auto worldDir = [&](const QUuid& blockId) {
        const auto* b = doc.findBlock(blockId);
        const Vec2 w1 = b->transform.toWorld(
            b->findPoint(line.startId)->resolvedPos);
        const Vec2 w2 = b->transform.toWorld(
            b->findPoint(line.endId)->resolvedPos);
        return std::atan2(w2.y - w1.y, w2.x - w1.x);
    };
    const double dirBefore = worldDir(line.blockId);

    // 1) 自动态填点2 = 有效点1 (所连点) → 零向量方向, 拒绝, 模型不变。
    //    (裸指针一次一取: 不跨 addBlock 持有 findBlock 结果)
    p2Edit->setText(doc.findBlock(leader.blockId)->findPoint(leader.endId)->serial);
    QTest::keyClick(p2Edit, Qt::Key_Return);
    QVERIFY2(doc.attachments().front().angleRefBlockId.isNull(),
             "点2 == 所连点 拒绝, 仍为自动态");
    // 拒绝必须有反馈 —— 此前静默刷回 = "点2 填不进"假象 (E:\4.gcad L2 报告,
    // 2026-09): toast 文本明示零长度方向; 锚点 tag 红闪由 rejectRefInput 负责。
    // HudItem 非 QObject (无 Q_OBJECT), 不能 findChild —— 遍历场景 items
    // dynamic_cast 定位 (toast 是本用例场景里唯一的 HudItem)。
    {
        bool toastOk = false;
        const auto items = scene.items();
        for (auto* it : items) {
            auto* hud = dynamic_cast<HudItem*>(it);
            if (!hud) continue;
            if (hud->isVisible()
                && hud->text().contains(QString::fromUtf8("零长度")))
                { toastOk = true; break; }
        }
        QVERIFY2(toastOk,
                 "点2 == 点1 拒绝后 toast 应可见且说明零长度方向");
    }

    // 2) 自动态填点2 (hostB 起点): 点1 固化为所连点 + ref2 落库, 零跳变。
    p2Edit->setText(doc.findBlock(hostB.blockId)->findPoint(hostB.startId)->serial);
    QTest::keyClick(p2Edit, Qt::Key_Return);
    {
        const auto& a = doc.attachments().front();
        QCOMPARE(a.angleRefBlockId, leader.blockId);
        QCOMPARE(a.angleRefPointId, leader.endId);
        QVERIFY2(!a.angleRefSegmentId.isNull(), "固化点1 携带出口线段");
        QCOMPARE(a.angleRef2BlockId, hostB.blockId);
        QCOMPARE(a.angleRef2PointId, hostB.startId);
        QVERIFY2(!a.angleIndependent, "填点2 = 自定义意图, 退出自动态");
        QVERIFY2(angDiff(worldDir(line.blockId), dirBefore) < 1e-9,
                 "设置两点基准 = 反算零跳变");
    }
    refCard.refresh();
    QVERIFY2(!p1Edit->text().isEmpty(), "点1 回显固化后的所连点");
    QVERIFY2(!p2Edit->text().isEmpty(), "点2 回显已解析点");

    // 3) 引擎消费: 移动点2 宿主 → 两点方向变化 → 跟随线转向 点1→点2 方向。
    {
        auto* c = doc.blockById(hostB.blockId);
        c->transform.origin = c->transform.origin + Vec2(60.0, -40.0);
    }
    doc.resolveAll();
    {
        const Vec2 w1 = doc.findBlock(leader.blockId)->worldPos(leader.endId);
        const Vec2 w2 = doc.findBlock(hostB.blockId)->worldPos(hostB.startId);
        const double refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
        const double fA = doc.attachments().front().followerAngle * M_PI / 180.0;
        QVERIFY2(angDiff(worldDir(line.blockId), refWorld + M_PI - fA) < 1e-9,
                 "跟随线世界方向 = 点1→点2 连线方向 (闭合基准)");
    }

    // 4) undo 单步回自动态。
    doc.undoStack()->undo();
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleRefBlockId.isNull() && a.angleRef2BlockId.isNull(),
                 "undo 恢复自动跟随 (ref1/ref2 全空)");
    }

    // 5) 自由线基准显示
    auto line2 = makeLine(doc, 50.0);
    doc.resolveAll();
    cad::ui::SegmentRefCard refCard2(&doc, &scene);
    refCard2.setTarget(line2.blockId, line2.segId);
    refCard2.refresh();
    QVERIFY2(doc.attachments().size() == 1, "自由态只预填, 不写模型");

    // 6) 真实操作路径 (2026-09 E:\4.gcad L2 报告): 点2 输入后**未按回车**
    //    (点走 / Tab) —— focusOutEvent 自动提交合法单解, 不再静默清空。
    //    此前一律 revertDisplay: "点进输入框→输入→点走" 的输入被无声丢掉,
    //    点2 看起来"永远填不进", 而点1 (自动回显) 恒有内容。
    {
        const auto& a = doc.attachments().front();
        QVERIFY(a.angleRefBlockId.isNull() && a.angleRef2BlockId.isNull());
    }
    refCard.refresh();
    p2Edit->setText(doc.findBlock(hostB.blockId)->findPoint(hostB.startId)->serial);
    QFocusEvent fe(QEvent::FocusOut, Qt::TabFocusReason);
    QApplication::sendEvent(p2Edit, &fe);   // 不按回车直接失焦 (点走/Tab)
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleRefBlockId.isNull() && !a.angleRef2BlockId.isNull(),
                 "失焦自动提交合法单解: 自动态点1 固化 + 点2 落库 (不再静默清空)");
        QCOMPARE(a.angleRef2BlockId, hostB.blockId);
        QCOMPARE(a.angleRef2PointId, hostB.startId);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 对齐点可输入 + 自动态两点回填 (2026-09 设计修正, 用户拍板):
//   · 对齐点 = 本线段的哪个端点钉在目标点上 (Attachment::fromPointId) ——
//     旧实现是绑 startPointId 的只读 tag, 换向后乱跳且不可选对端。
//   · 自动态 (跟随所连的线): 点1 回显目标点 P2, 点2 回显宿主线段另一端 P1
//     —— 方向 = "P3 对齐 P2, 以 P2→P1 为基准" 全量可见, 且 autoEcho 灰显
//     不落库 (回填不算用户意图)。
//   · 对齐点与换向 (进出身份) 无关: 换向只翻箭头, fromPointId 不动。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::alignPointEditableAndAutoStateTwoPointBackfill()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // L1: 宿主 (P1 起点, P2 终点); L2: 本线 (P3 起点, P4 终点), 起点吸附 L1 终点。
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const auto line   = makeLine(doc, 60.0);   // 本线 (0,0)→(60,0)
    doc.resolveAll();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;            // P3 对齐 P2
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    CanvasScene scene(&doc);
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* alignEdit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    auto* p1Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY2(alignEdit && p1Edit && p2Edit, "对齐点/点1/点2 输入框必须存在");

    // 1) 自动态全量回填: 对齐点=P3, 点1=L1·P2, 点2=L1·P1 (三点可见)。
    {
        const auto blk = doc.findBlock(line.blockId);
        QCOMPARE(alignEdit->resolvedPointId(), line.startId);
        QCOMPARE(alignEdit->text(),
                 cad::param::Serial::tag(doc.findBlock(line.blockId)
                                             ->findPoint(line.startId)->serial));
        QCOMPARE(p1Edit->resolvedBlockId(), leader.blockId);
        QCOMPARE(p1Edit->resolvedPointId(), leader.endId);
        QCOMPARE(p2Edit->resolvedPointId(), leader.startId);
        QVERIFY2(p2Edit->isAutoEcho(), "自动回填 = 灰显 autoEcho, 不算用户意图");
        // autoEcho 回填不得触发预填落库 (refresh 幂等, 模型仍自动态)。
        refCard.refresh();
        QVERIFY2(doc.attachments().front().angleRefBlockId.isNull(),
                 "自动态回填不得把自动态误固化为自定义");
    }

    // 2) 对齐点可输入: 填 P4 → fromPointId 换成 line.endId, 角度零跳变。
    {
        const auto blk = doc.findBlock(line.blockId);
        const auto* endPt = blk->findPoint(line.endId);
        const auto dirBefore = [&]() {
            const Vec2 w1 = blk->transform.toWorld(
                blk->findPoint(line.startId)->resolvedPos);
            const Vec2 w2 = blk->transform.toWorld(
                blk->findPoint(line.endId)->resolvedPos);
            return std::atan2(w2.y - w1.y, w2.x - w1.x);
        }();
        alignEdit->setText(cad::param::Serial::tag(endPt->serial));
        QTest::keyClick(alignEdit, Qt::Key_Return);
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
        const auto blk2 = doc.findBlock(line.blockId);
        const Vec2 w1 = blk2->transform.toWorld(
            blk2->findPoint(line.startId)->resolvedPos);
        const Vec2 w2 = blk2->transform.toWorld(
            blk2->findPoint(line.endId)->resolvedPos);
        const double dirAfter = std::atan2(w2.y - w1.y, w2.x - w1.x);
        QVERIFY2(std::abs(cad::geo::normalizeRad(dirAfter - dirBefore)) < 1e-9,
                 "切换对齐点 P3→P4: 本线方向零跳变");
        QCOMPARE(alignEdit->resolvedPointId(), line.endId);
    }

    // 3) 对齐点输入限制: 只接受本线端点 (P3/P4), 宿主点 L1·P2 无匹配拒绝。
    {
        alignEdit->setText(doc.findBlock(leader.blockId)
                               ->findPoint(leader.endId)->serial);
        QTest::keyClick(alignEdit, Qt::Key_Return);
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
    }
    // 4) 与换向无关: ReverseSegmentCommand 换向 (start/end 互换) 后
    //    fromPointId 保持 (对齐点仍为 P4), 不随身份翻转; 撤换向也不动它。
    {
        doc.undoStack()->push(new cad::cmd::ReverseSegmentCommand(
            &doc, line.blockId, line.segId));
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
        refCard.refresh();
        QCOMPARE(alignEdit->resolvedPointId(), line.endId);
        doc.undoStack()->undo();   // 撤换向 (undo 不影响 fromPointId)
        QCOMPARE(doc.attachments().front().fromPointId, line.endId);
    }

    // 5) undo 单步: SetAlignPointCommand 撤销 → fromPointId 回 P3。
    {
        doc.undoStack()->undo();
        QCOMPARE(doc.attachments().front().fromPointId, line.startId);
        refCard.refresh();
        QCOMPARE(alignEdit->resolvedPointId(), line.startId);
    }
}


// ─────────────────────────────────────────────────────────────────────────
// 终点连接行 (2026-xx 每端完整连接: 起点连接 = Attachment (位置+角度),
// 终点连接 = endTarget 终点指向 (Resolver Step 7)。验证:
//   · 桥接落点 (缺省): 输入 P# → endTarget 写入 + 自动发布 M_xxx 驱动终点
//     距离公式 → 终点精确落在目标点; undo 一步全回滚 (指向/公式/测量)。
//   · 仅指向: 只写 endTarget, 不碰长度/距离公式与测量。
//   · 双端连接 = 桥接线: SegmentRefCard (基准线行) 隐藏 —— 互斥 (无基准线)。
//   · 终点拆开/重连: 拆开清指向 (测量保留), 重连恢复记忆目标。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::connectionCardEndConnection()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));   // 宿主 A
    const auto line   = makeLine(doc, 60.0);                       // 本线 (0,0)→(60,0)
    const auto hostB  = makeLine(doc, 80.0, Vec2(160.0, 60.0));    // 终点目标宿主 B
    doc.resolveAll();

    cad::ui::SegmentConnectionCard card(&doc, &scene);
    card.setTarget(line.blockId, line.segId);

    auto* endPointEdit = card.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("endConnPointEdit"));
    QVERIFY2(endPointEdit, "终点连接点输入框 (endConnPointEdit) 必须存在");
    auto* endDetach = card.findChild<QPushButton*>(
        QStringLiteral("endConnDetachBtn"));
    QVERIFY(endDetach);

    const auto* hb = doc.findBlock(hostB.blockId);
    const auto* bp = hb->findPoint(hostB.endId);
    QVERIFY(bp);

    // ── ① 长度模式「自动」= 桥接落点: 输入 P# 回车 → 终点精确落点 ──
    doc.findBlock(line.blockId)->lengthAuto = true;
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* blk = doc.findBlock(line.blockId);
        QVERIFY2(blk->endTargetPointId == hostB.endId, "终点连接写入 endTarget");
        QCOMPARE(blk->endTargetBlockId, hostB.blockId);
        const auto* seg = blk->findSegment(line.segId);
        QVERIFY2(!seg->lengthFormula.isEmpty(), "桥接落点自动驱动长度公式 (测量线标记)");
        const auto* ep = blk->findPoint(line.endId);
        QVERIFY2(!ep->distanceFormula.isEmpty(), "桥接落点自动驱动终点距离公式 (几何)");
        QCOMPARE(ep->distanceFormula, seg->lengthFormula);
        QCOMPARE(doc.measureVars().size(), size_t(1));
        // 终点精确落在目标点 (Step 7 指向 + 测量距离)。
        doc.resolveAll();
        const Vec2 endWorld = blk->transform.toWorld(ep->resolvedPos);
        const Vec2 targetWorld = hb->transform.toWorld(bp->resolvedPos);
        QVERIFY2(endWorld.distanceTo(targetWorld) < 1e-6,
                 "桥接落点: 终点必须精确落在目标点上");
        // undo 一步全回滚 (指向/公式/测量)。
        doc.undoStack()->undo();
        const auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId.isNull(), "undo 清除终点指向");
        QVERIFY2(b2->findSegment(line.segId)->lengthFormula.isEmpty(),
                 "undo 恢复线段长度公式");
        QVERIFY2(b2->findPoint(line.endId)->distanceFormula.isEmpty(),
                 "undo 恢复终点距离公式");
        QVERIFY2(doc.measureVars().empty(), "undo 删除自动发布的测量");
    }

    // ── ② 长度模式「指定」= 仅指向: 只写 endTarget, 不碰长度/距离公式与测量 ──
    doc.findBlock(line.blockId)->lengthAuto = false;
    card.refresh();
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(!b2->endTargetPointId.isNull(), "仅指向也写 endTarget");
        QCOMPARE(b2->endTargetBlockId, hostB.blockId);
        QVERIFY2(b2->findSegment(line.segId)->lengthFormula.isEmpty(),
                 "仅指向不碰线段长度公式");
        QVERIFY2(b2->findPoint(line.endId)->distanceFormula.isEmpty(),
                 "仅指向不碰终点距离公式");
        QVERIFY2(doc.measureVars().empty(), "仅指向不发布测量");
    }
    doc.undoStack()->undo();
    QVERIFY2(doc.findBlock(line.blockId)->endTargetPointId.isNull(),
             "仅指向 undo 清除指向");

    // ── ③ 双端连接 = 桥接线: 起点 Attachment + 终点 endTarget → 方向段隐藏,
    //    对齐点段保留 (显示默认进点 + 禁用, 2026-09 规则表 ④) ──
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    QVERIFY2(!refCard.isHidden(), "单端连接时基准线行可见");

    doc.findBlock(line.blockId)->lengthAuto = true;   // 回到桥接落点
    card.refresh();
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    refCard.refresh();
    {
        const auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(!b2->endTargetPointId.isNull(), "双端连接 = 桥接线");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 桥接落点自动测量
    }
    // 桥接线: 方向段 (点1/点2/[独立]) 隐藏, 对齐点段保留 (默认进点 + 禁用)。
    auto* alignEdit3 = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    auto* p1Edit3 = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    QVERIFY2(alignEdit3 && p1Edit3, "对齐点/点1 输入框必须存在");
    QVERIFY2(!refCard.isHidden(), "桥接线: 对齐点段保留 (整卡不隐藏)");
    QVERIFY2(!alignEdit3->isEnabled(), "桥接线: 无进点语义 → 对齐点禁用");
    QCOMPARE(alignEdit3->resolvedPointId(), line.startId);
    QVERIFY2(!p1Edit3->isVisible(), "桥接线: 方向段 (点1) 隐藏");

    // ── ④ 终点拆开: 清指向 (测量保留) → 方向段恢复; 重连恢复记忆目标 ──
    card.refresh();
    QVERIFY2(endDetach->isEnabled(), "有指向时终点拆开可用");
    endDetach->click();
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId.isNull(), "终点拆开清除指向");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 测量保留 (长度公式仍在)
    }
    refCard.refresh();
    QVERIFY2(!refCard.isHidden(), "拆开终点后基准线行恢复显示");
    card.refresh();
    QCOMPARE(endDetach->text(), QString::fromUtf8("重连"));
    QVERIFY2(endDetach->isEnabled(), "终点拆开记忆可用 → 重连可用");
    endDetach->click();
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId == hostB.endId, "终点重连恢复记忆目标");
        QCOMPARE(b2->endTargetBlockId, hostB.blockId);
    }
    refCard.refresh();
    QVERIFY2(!refCard.isHidden(), "重连后回到桥接线: 对齐点段保留");

    // ── ⑤ 重定向: 输入新目标点 → endTarget 与归属测量目标点一并更新 ──
    const auto* bs = hb->findPoint(hostB.startId);
    card.refresh();
    endPointEdit->setText(bs->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    {
        auto* b2 = doc.findBlock(line.blockId);
        QVERIFY2(b2->endTargetPointId == hostB.startId, "重定向更新 endTarget");
        QCOMPARE(doc.measureVars().size(), size_t(1));   // 不重复创建测量
        QCOMPARE(doc.measureVars().front().blockB, hostB.blockId);
        QCOMPARE(doc.measureVars().front().pointB, hostB.startId);
        // undo 恢复旧目标 (endTarget + 测量 B 点)。
        doc.undoStack()->undo();
        QVERIFY2(doc.findBlock(line.blockId)->endTargetPointId == hostB.endId,
                 "重定向 undo 恢复旧 endTarget");
        QCOMPARE(doc.measureVars().front().pointB, hostB.endId);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 终点指向超出延伸 (2026-09 规则表 ⑤, 用户拍板): 起点连接 + 终点指向 +
// 指定长度 —— 长度 > 目标距离时终点**越过目标点**、方向保持指向角继续延伸
// (Step 7 只驱动旋转, 长度由 Polar 距离独立决定, 不截断)。
// ─────────────────────────────────────────────────────────────────────────
void TestSelectWKey::endTargetOvershootExtendsAlongAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // 宿主 A (L1): 起点 (200,0) → 终点 (300,0); 本线 L2: 起点 (0,0) → 终点 (60,0)。
    const auto leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const auto line   = makeLine(doc, 60.0);
    doc.resolveAll();

    // 起点连接 (进点 = P3 钉在 L1 终点 P2)。
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));

    // 终点指向: 目标 = 宿主 B 起点 (位于 (120,90), 距本线起点 ~146mm)。
    const auto hostB = makeLine(doc, 80.0, Vec2(120.0, 90.0));
    doc.resolveAll();
    auto* blk = doc.findBlock(line.blockId);
    blk->endTargetBlockId = hostB.blockId;
    blk->endTargetPointId = hostB.startId;
    blk->endTargetOffset = 0.0;
    // 指定长度 200mm > 起点→目标距离 (~146mm): 终点应越过目标点。
    auto* ep = blk->findPoint(line.endId);
    ep->constraint = cad::param::PointConstraint::Polar;
    ep->refPointId = line.startId;
    ep->distance = 200.0;
    doc.resolveAll();

    const Vec2 startWorld = blk->transform.toWorld(
        blk->findPoint(line.startId)->resolvedPos);
    const Vec2 endWorld = blk->transform.toWorld(ep->resolvedPos);
    const Vec2 targetWorld = doc.findBlock(hostB.blockId)->worldPos(hostB.startId);

    // 1) 终点越过目标点 (沿指向方向继续延伸)。
    QVERIFY2(endWorld.distanceTo(startWorld) > 199.0,
             "指定长度 200mm 生效 (终点距起点 = 长度)");
    QVERIFY2(endWorld.distanceTo(targetWorld) > 1.0,
             "长度 > 目标距离: 终点越过目标点, 不截断");

    // 2) 方向保持指向角: 终点在 起点→目标 的射线上 (共线且同向)。
    const Vec2 aimDir = targetWorld - startWorld;
    const Vec2 endDir = endWorld - startWorld;
    const double cross = aimDir.x * endDir.y - aimDir.y * endDir.x;
    const double dot = aimDir.x * endDir.x + aimDir.y * endDir.y;
    QVERIFY2(std::abs(cross) < 1e-6 && dot > 0.0,
             "终点沿 起点→目标 射线方向延伸 (方向保持指向角)");

    // 3) 对齐点锁定: 有 endTarget 时进点锁定 start 端 (方案 A) —— 面板对齐
    //    点输入框禁用且显示 start 端。
    CanvasScene scene(&doc);
    cad::ui::SegmentRefCard refCard(&doc, &scene);
    refCard.setTarget(line.blockId, line.segId);
    auto* alignEdit = refCard.findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    QVERIFY2(alignEdit, "对齐点输入框必须存在");
    QVERIFY2(!alignEdit->isEnabled(), "有 endTarget: 进点锁定 start 端 (禁用)");
    QCOMPARE(alignEdit->resolvedPointId(), line.startId);
}

// ─────────────────────────────────────────────────────────────────────────
// 双击打开线条属性面板: 交叉点处活跃层优先
// ─────────────────────────────────────────────────────────────────────────
// 用户报告 (2026-11): 辅助层的线段有时候很难双击打开线条属性面板, 有时候
// 弹出, 更多的时候双击没有反应。根因 = ToolSelect::mouseDoubleClick 的命中
// 与 press/hitBlock 不一致: 旧代码取 hits 列表里第一个 BlockItem, 若其层级
// 不是活跃层就直接 return —— 在辅助线/工作线交叉处, 堆叠在上的灰显块
// (另一层) 往往抢走第一次命中, 双击静默死亡; 而单击选中 (hitBlock 逐项
// 扫描到活跃层) 却正常。修复 = 双击改用 hitBlock 同规 (首个活跃层块胜出,
// 灰显层跳过而非否决)。以下用例锁定: 无论交叉处谁在上, 双击恒打开活跃层
// 线段的面板; 灰显层 (非活跃) 线段仍不可从画布双击编辑 (设计规则不变)。
void TestSelectWKey::doubleClickCrossingPrefersActiveLayer()
{
    struct Scenario {
        bool  auxActive;     ///< 活跃层 = 辅助层 (true) 或工作层 (false).
        bool  auxAddedFirst; ///< 辅助线先建 (交叉处被后建的工作线堆叠覆盖).
        Vec2  click;         ///< 双击位置 (用户坐标).
        bool  expectAuxDialog; ///< 期望面板目标 = 辅助线 (false = 工作线).
        bool  expectNoDialog;  ///< 期望不弹面板 (灰显层不可编辑).
        const char* what;
    };
    const Vec2 kCross{50.0, 100.0};  // (50,0)→(50,200) 辅助线 × (0,100)→(200,100) 工作线
    const Vec2 kBody{50.0, 30.0};    // 辅助线身上远离交叉点的位置

    const Scenario scenarios[] = {
        {true,  false, kCross, true,  false, "aux active, aux on top:  crossing must open AUX panel"},
        {true,  true,  kCross, true,  false, "aux active, work on top: crossing must open AUX panel"},
        {false, false, kCross, false, false, "work active, aux on top: crossing must open WORK panel"},
        {false, true,  kCross, false, false, "work active, work on top: crossing must open WORK panel"},
        {false, true,  kBody,  false, true,  "work active, aux body: grayed aux stays non-editable"},
    };

    for (const Scenario& sc : scenarios) {
        ParamDocument doc;
        CanvasScene scene(&doc);
        const QUuid auxLayer  = layerIdAt(doc, 0);
        const QUuid workLayer = layerIdAt(doc, 1);

        QUuid auxId, workId;
        if (sc.auxAddedFirst) {
            auxId  = addLineOnLayer(doc, auxLayer,  Vec2(50.0, 0.0),   90.0, 200.0);
            workId = addLineOnLayer(doc, workLayer, Vec2(0.0, 100.0),  0.0,  200.0);
        } else {
            workId = addLineOnLayer(doc, workLayer, Vec2(0.0, 100.0),  0.0,  200.0);
            auxId  = addLineOnLayer(doc, auxLayer,  Vec2(50.0, 0.0),   90.0, 200.0);
        }
        doc.setActiveLayer(sc.auxActive ? auxLayer : workLayer);
        doc.resolveAll();

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        cad::tools::ToolManager tm(&scene);
        tm.setParamDocument(&doc);   // default active tool is Select
        view.setInputDispatcher(&tm);

        auto vp = [&](double x, double y) {
            return view.mapFromScene(QPointF(x, -y));
        };
        const QPoint hit = vp(sc.click.x, sc.click.y);
        const QPoint global = view.viewport()->mapToGlobal(hit);
        auto send = [&](QEvent::Type type, Qt::MouseButton btn) {
            QMouseEvent ev(type, hit, global, btn,
                           btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(view.viewport(), &ev);
            // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
            // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
            QTest::qWait(20);
        };
        // 完整双击序列: press → release → dblclick → release.
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton);
        send(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton);

        const auto dialogs = view.findChildren<cad::ui::LinePropertyDialog*>();
        if (sc.expectNoDialog) {
            QVERIFY2(dialogs.isEmpty(), sc.what);
        } else if (!dialogs.isEmpty()) {
            QCOMPARE(dialogs.size(), 1);
            const QUuid expect = sc.expectAuxDialog ? auxId : workId;
            QCOMPARE(dialogs.first()->targetBlockId(), expect);
        } else {
            QFAIL(sc.what);   // 修复前: 交叉处双击静默无面板
        }
        for (auto* d : dialogs) d->close();
    }
}

void TestSelectWKey::multiSelectMoveToLayer()
{
    ParamDocument doc;
    const QUuid work1 = layerIdAt(doc, 1);
    doc.setActiveLayer(work1);
    const QUuid work2 = doc.addLayer(QStringLiteral("图层 2"));

    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
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

    // 切换到多选模式
    pressW();

    // 点选两根线
    click(50.0, 0.0);
    click(50.0, -50.0);
    QCOMPARE(sel->selection().size(), 2);
    QVERIFY(sel->selection().contains(a.blockId));
    QVERIFY(sel->selection().contains(b.blockId));

    // 执行移动到 work2
    sel->moveSelectionToLayer(work2);

    // 验证选择集已清空，回到 Idle
    QVERIFY(sel->selection().isEmpty());
    QCOMPARE(sel->state(), cad::tools::SelectState::Idle);

    // 验证两根线的图层属性已变为 work2
    const auto* blkA = doc.findBlock(a.blockId);
    const auto* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(blkB && blkB->layer == work2);

    // 验证原子撤销 (Ctrl+Z)
    QCOMPARE(stack.count(), 1);
    stack.undo();
    blkA = doc.findBlock(a.blockId);
    blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work1);
    QVERIFY(blkB && blkB->layer == work1);

    // 验证重做 (Ctrl+Y)
    stack.redo();
    blkA = doc.findBlock(a.blockId);
    blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(blkB && blkB->layer == work2);

    // 移动到辅助层
    const QUuid auxId = doc.layersView().auxLayerId();
    doc.setActiveLayer(work2);
    pressW();
    click(50.0, 0.0);
    QCOMPARE(sel->selection().size(), 1);
    sel->moveSelectionToLayer(auxId);
    blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == auxId);

    // 撤销移动到辅助层
    stack.undo();
    blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == work2);
}

void TestSelectWKey::multiSelectRightClickMoveToLayerMenu()
{
    ParamDocument doc;
    const QUuid work1 = layerIdAt(doc, 1);
    doc.setActiveLayer(work1);
    const QUuid work2 = doc.addLayer(QStringLiteral("工作层 2"));

    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
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
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };

    // 选中线 A
    const QPoint vpPos = vp(50.0, 0.0);
    const QPoint global = view.viewport()->mapToGlobal(vpPos);
    QMouseEvent press(QEvent::MouseButtonPress, vpPos, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, vpPos, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QTest::qWait(20);

    QCOMPARE(sel->selection().size(), 1);

    bool menuFound = false;
    bool actionTriggered = false;
    bool auxFound = false;

    // 定时器在菜单弹出后查找并触发“工作层 2”Action
    QTimer::singleShot(100, [&]() {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* m = qobject_cast<QMenu*>(w)) {
                if (m->isVisible()) {
                    menuFound = true;
                    for (QAction* act : m->actions()) {
                        if (act->menu()) {
                            QAction* targetAct = nullptr;
                            for (QAction* subAct : act->menu()->actions()) {
                                if (subAct->text() == QStringLiteral("辅助层")) {
                                    auxFound = true;
                                }
                                if (subAct->text() == QStringLiteral("工作层 2")) {
                                    targetAct = subAct;
                                }
                            }
                            if (targetAct) {
                                actionTriggered = true;
                                targetAct->trigger();
                                m->close();
                                return;
                            }
                        }
                    }
                    m->close();
                }
            }
        }
    });

    // 发送右键点击
    QMouseEvent rPress(QEvent::MouseButtonPress, vpPos, global,
                       Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &rPress);
    QTest::qWait(200);

    QVERIFY(menuFound);
    QVERIFY(auxFound);
    QVERIFY(actionTriggered);

    const auto* blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA && blkA->layer == work2);
    QVERIFY(sel->selection().isEmpty());
}

void TestSelectWKey::quickDetachAuxPointMountWithDKey()
{
    ParamDocument doc;
    QUndoStack stack;
    CanvasScene scene(&doc);
    CanvasView view(&scene);
    view.show();

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    auto* sel = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(sel);

    // L1: 宿主线 (0,0) -> (100,0)
    auto l1 = makeLine(doc, 100.0);
    // 在 L1 上添加辅助点 (percent=0.5)
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = l1.segId;
    aux.interpPercent = 0.5;
    aux.isAuxiliary = true;
    const QUuid auxId = aux.id;
    doc.blockById(l1.blockId)->addPoint(std::move(aux));
    doc.blockById(l1.blockId)->findSegment(l1.segId)->auxPointIds.push_back(auxId);
    doc.resolveAll();

    // L2: 跟随线，起点挂在 L1 的辅助点上
    auto l2 = makeLine(doc, 50.0, Vec2{50.0, 0.0});
    Attachment att;
    att.fromBlockId = l2.blockId;
    att.fromPointId = l2.startId;
    att.toBlockId = l1.blockId;
    att.toPointId = auxId;
    att.toSegmentId = l1.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    scene.refreshAllBlockItems();

    view.resize(900, 600);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
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

    // 拖动跟随线 L2 拆散辅助点挂载
    drag(50.0, 25.0, 50.0, 75.0);
    doc.resolveAll();

    // 验证辅助点上的连接已被彻底释放（数据上完全移除连接）
    QVERIFY(doc.findAttachment(att.id) == nullptr);
    QVERIFY(doc.diagnostics().empty());

    // 验证被拆开的跟随线 L2 可以自由重新连接到其他线段 (如 L3)
    auto l3 = makeLine(doc, 80.0, Vec2{100.0, 100.0});
    Attachment attNew;
    attNew.fromBlockId = l2.blockId;
    attNew.fromPointId = l2.startId;
    attNew.toBlockId = l3.blockId;
    attNew.toPointId = l3.startId;
    attNew.toSegmentId = l3.segId;
    QVERIFY2(doc.addAttachment(attNew), "辅助点挂载拆开后，跟随线必须能自由连接到其他线段");
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // 移除 attNew 并验证 undo / redo
    doc.removeAttachment(attNew.id);
    doc.resolveAll();

    // 验证 undo 恢复原辅助点挂载
    doc.undoStack()->undo();
    doc.resolveAll();
    QVERIFY(doc.findAttachment(att.id) != nullptr);

    // 验证 redo 再次彻底释放
    doc.undoStack()->redo();
    doc.resolveAll();
    QVERIFY(doc.findAttachment(att.id) == nullptr);
}

