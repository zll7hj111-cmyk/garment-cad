#include "test_dialog_tabs_helpers.h"

class TestDialogTabsAngleConn : public QObject
{
    Q_OBJECT
private slots:
    void endpointCardsStacked();
    void connectCardUniformHeights();
    void independentAngleInputDoesNotJump();
    void followerAngleModeToggleToArc();
    void angleFormulaPreservedInDialog();
    void endConnectionRowAndBadge();
    void detachClearsConnectEditAndRetargetReconnects();
    void reattachPreservesAngleRef();
    void linkCurrentLineButtonClearsRef();
    void independentAngleKeepsRefEditsEnabled();
};

/// 回归：端点 起点|终点 双微卡上下堆叠 (2026-xx §3: 两个端点组之间夹朝向箭头)。
/// 起点卡在上、终点卡在下、等宽; 朝向箭头在两者之间。
void TestDialogTabsAngleConn::endpointCardsStacked()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();

    // 2026-08-31 重设计: 端点组去灰底卡框 → 容器为无样式 QWidget (objectName 契约不变)。
    auto* startCard = dlg->findChild<QWidget*>(QStringLiteral("startPointCard"));
    auto* endCard = dlg->findChild<QWidget*>(QStringLiteral("endPointCard"));
    QVERIFY(startCard);
    QVERIFY(endCard);
    QVERIFY(startCard->isVisibleTo(dlg));
    QVERIFY(endCard->isVisibleTo(dlg));
    // 布局落定 = 双微卡到达最终几何 (上下堆叠为主判据); waitUntil 等待。
    QVERIFY2(cad::test::waitUntil([&] {
        const QRect a(startCard->mapTo(dlg, QPoint(0, 0)), startCard->size());
        const QRect b(endCard->mapTo(dlg, QPoint(0, 0)), endCard->size());
        return std::abs(a.width() - b.width()) <= 1
               && a.bottom() < b.top();
    }), "端点双微卡布局未落定");

    const QRect gS(startCard->mapTo(dlg, QPoint(0, 0)), startCard->size());
    const QRect gE(endCard->mapTo(dlg, QPoint(0, 0)), endCard->size());

    // 上下堆叠 (QVBoxLayout 水平拉伸): 起点在上、终点在下、等宽。
    QVERIFY2(std::abs(gS.width() - gE.width()) <= 1,
             qPrintable(QStringLiteral(
                 "endpoint cards must have equal widths "
                 "(start %1, end %2)").arg(gS.width()).arg(gE.width())));
    QVERIFY2(gS.bottom() < gE.top(),
             qPrintable(QStringLiteral(
                 "start card must sit above end card "
                 "(start bottom %1, end top %2)")
                 .arg(gS.bottom()).arg(gE.top())));

    qInfo().noquote() << QStringLiteral(
        "[endpoint-cards] start geo (dlg) %1x%2 @%3,%4; end %5x%6 @%7,%8")
        .arg(gS.width()).arg(gS.height()).arg(gS.x()).arg(gS.y())
        .arg(gE.width()).arg(gE.height()).arg(gE.x()).arg(gE.y());

    // 延长量 (2026-xx, §6.2) 已并入端点双微卡: 起/终各一栏输入框仍可见。
    auto* startExt = dlg->findChild<QLineEdit*>(QStringLiteral("startExtendEdit"));
    auto* endExt = dlg->findChild<QLineEdit*>(QStringLiteral("endExtendEdit"));
    QVERIFY(startExt);
    QVERIFY(endExt);
    QVERIFY(startExt->isVisibleTo(dlg));
    QVERIFY(endExt->isVisibleTo(dlg));

    delete dlg;
}

/// 回归：连接卡「输入框大小不一」 (用户 2026-12 反馈) —— 行内控件统一高
/// 30px (2026-xx 紧凑化, ElaLineEdit/ElaComboBox 原生值, PointRefEdit 已对齐),
/// 点引用输入统一宽 140px (列对齐)。
void TestDialogTabsAngleConn::connectCardUniformHeights()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    doc.resolveAll();

    // 建一条跟随连接, 面板以「跟随 · 连接」态展示 (行最多)。
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = line.blockId == doc.blocks().front().id
        ? doc.blocks().at(1).id : doc.blocks().front().id;
    // 用真实 leader (第一条线 = setup 的 100mm 线)。
    const auto* leaderBlk = doc.findBlock(
        line.blockId == doc.blocks().at(0).id
            ? doc.blocks().at(1).id : doc.blocks().at(0).id);
    if (leaderBlk && !leaderBlk->segments.empty()) {
        att.toPointId = leaderBlk->segments.front().startPointId;
        att.toSegmentId = leaderBlk->segments.front().id;
        QVERIFY(doc.addAttachment(att));
    }
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    // P2-3: 等子控件出现而不是固定 sleep 120ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<cad::ui::SegmentConnectionCard*>() != nullptr; }),
             "timed out waiting for cad::ui::SegmentConnectionCard* to appear");
    auto* card = dlg->findChild<cad::ui::SegmentConnectionCard*>();
    QVERIFY(card);

    // ① 行内控件统一高度 30px (2026-xx 紧凑化, 与状态栏对齐)。
    int badH = 0;
    for (auto* w : card->findChildren<QWidget*>()) {
        const bool isInput =
            qobject_cast<ElaLineEdit*>(w) || qobject_cast<cad::ui::PointRefEdit*>(w)
            || qobject_cast<ElaPushButton*>(w) || qobject_cast<ElaComboBox*>(w);
        if (!isInput) continue;
        if (w->height() != 30 && w->y() >= 0) {   // 布局后高度应为 30
            ++badH;
            qInfo() << "[uniform] bad height:" << w->metaObject()->className()
                    << w->height() << w->geometry();
        }
    }
    QVERIFY2(badH == 0, qPrintable(QStringLiteral("连接卡行内控件高度不统一: %1 个")
                                       .arg(badH)));

    // ② 点引用输入 (PointRefEdit) 统一 140px 宽 (2026-12 版式规范)。
    {
        int refBad = 0;
        for (auto* p : card->findChildren<cad::ui::PointRefEdit*>()) {
            if (p->width() != 140) ++refBad;
        }
        QVERIFY2(refBad == 0,
                 qPrintable(QStringLiteral("点引用输入宽度不统一: %1 个")
                                .arg(refBad)));
    }

    qInfo() << "[uniform] conn card controls all 30px high; ref inputs 140px";
    delete dlg;
}

// 回归 (用户报告 2026-12): 独立角度线的角度输入"不断跳动" —— applyAngle
// 写模型后立即 populateAngleField 按 (尚未重解的) resolvedPos 读回世界角,
// 拿到旧值覆盖用户刚输入的内容。修复后输入保留, 世界角由 onDocResolved
// (重解广播) 刷新。
void TestDialogTabsAngleConn::independentAngleInputDoesNotJump()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup follower = makeLine(doc, 60.0);
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    QVERIFY(doc.addAttachment(att));
    doc.setAttachmentAngleIndependent(att.id, true);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        follower.blockId, follower.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentAngleCard*>() != nullptr;
             }),
             "timed out waiting for SegmentAngleCard* to appear");
    auto* card = dlg->findChild<cad::ui::SegmentAngleCard*>();
    QVERIFY(card);
    ElaLineEdit* edit = card->findChild<ElaLineEdit*>();
    QVERIFY(edit);

    // 用户输入 45 并回车: 输入必须保留 (不回跳), 模型角度确实改了。
    edit->setText(QStringLiteral("45"));
    emit edit->editingFinished();

    const QUuid fid = follower.blockId;
    QVERIFY2(cad::test::waitUntil([&] {
                 const auto* b = doc.findBlock(fid);
                 const auto* e = b ? b->findPoint(follower.endId) : nullptr;
                 const auto* s = b ? b->findPoint(b->segments.front().startPointId) : nullptr;
                 if (!b || !e || !s || !s->resolved || !e->resolved) return false;
                 const double rotDeg = b->transform.rotation * 180.0 / M_PI;
                 const double world = cad::geo::normalizeDeg360(e->angle + rotDeg);
                 return std::abs(world - 45.0) < 1e-9;
             }),
             "timed out: independent-angle world angle did not reach 45");
    // 输入不回跳: 编辑框仍是用户输入的值 (旧 bug: 被旧世界角覆盖)。
    QCOMPARE(edit->text(), QStringLiteral("45"));
    // 附件原样保留。
    QCOMPARE(doc.attachments().size(), size_t(1));
    delete dlg;
}

// REPRO (用户报告 2026-12): 「跟随角度」状态下点 ∠/⌒ 切换不到弧长模式。
// 逐步验证: 数值跟随角 → 弧长; 弧长 → 角度; 公式跟随角 → 弧长 (应被拒, 设计如此)。
void TestDialogTabsAngleConn::followerAngleModeToggleToArc()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup follower = makeLine(doc, 60.0);
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.followerAngle = 45.0;   // 非零跟随角: 0° 折叠重叠, 弧长换算恒为 0, 无法区分"切换成功"
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QCOMPARE(doc.attachments().front().rotationMode,
             cad::param::RotationMode::Angle);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        follower.blockId, follower.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentAngleCard*>() != nullptr;
             }),
             "timed out waiting for SegmentAngleCard* to appear");
    auto* card = dlg->findChild<cad::ui::SegmentAngleCard*>();
    QVERIFY(card);

    auto modeButton = [&]() -> QPushButton* {
        for (auto* b : card->findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("∠")
                || b->text() == QStringLiteral("⌒")
                || b->text() == QStringLiteral("↔"))
                return b;
        return nullptr;
    };

    // ① 数值跟随角 → 弧长: 点 ⌒ 按钮后必须切到 ArcLength, 且换算正确
    // (45°·(60mm 半径) = π/4·60 ≈ 47.12mm)。
    {
        QPushButton* btn = modeButton();
        QVERIFY2(btn, "mode button (∠/⌒/↔) not found");
        QVERIFY2(btn->isEnabled(), "mode button disabled in 跟随角 state");
        QCOMPARE(btn->text(), QStringLiteral("∠"));
        btn->click();
        const auto* a = &doc.attachments().front();
        QVERIFY2(a->rotationMode == cad::param::RotationMode::ArcLength,
                 qPrintable(QStringLiteral("跟随角→弧长切换失败, rotationMode=%1")
                                .arg(static_cast<int>(a->rotationMode))));
        QVERIFY2(std::abs(a->arcLength - 45.0 * M_PI / 180.0 * 60.0) < 1e-6,
                 "弧长换算错误");
    }

    // ② 弧长 → 开度 (弦长) → 角度: 循环三态切换
    {
        QPushButton* btn = modeButton();
        QVERIFY2(btn, "mode button lost after ①");
        QCOMPARE(btn->text(), QStringLiteral("⌒"));
        btn->click();
        const auto* a = &doc.attachments().front();
        QVERIFY2(a->rotationMode == cad::param::RotationMode::ChordLength,
                 "弧长→开度(弦长)切换失败");
        const double expectedChord = 2.0 * 60.0 * std::sin(22.5 * M_PI / 180.0);
        QVERIFY2(std::abs(a->chordLength - expectedChord) < 0.1, "弦长换算错误");

        btn->click();
        const auto* a2 = &doc.attachments().front();
        QVERIFY2(a2->rotationMode == cad::param::RotationMode::Angle,
                 "开度(弦长)→角度切换失败");
        QVERIFY2(std::abs(a2->followerAngle - 45.0) < 0.1, "角度反算错误");
    }

    // ③ 公式跟随角 → 弧长 → 开度 (2026-12 用户拍板: 公式驱动可切换, 且公式
    // **原样搬移不乘换算系数** —— "一个公式只会在一种模式下表达, 用户
    // 选择哪个模式, 公式就按哪个模式求值")。
    {
        ElaLineEdit* edit = card->findChild<ElaLineEdit*>();
        QVERIFY(edit);
        edit->setText(QStringLiteral("30+15"));
        emit edit->editingFinished();
        const auto* a = &doc.attachments().front();
        QVERIFY2(a->rotationMode == cad::param::RotationMode::Angle
                     && !a->followerAngleFormula.isEmpty(),
                 "formula not applied to follower angle");
        QPushButton* btn = modeButton();
        btn->click();
        const auto* a2 = &doc.attachments().front();
        QVERIFY2(a2->rotationMode == cad::param::RotationMode::ArcLength,
                 "公式驱动跟随角→弧长切换失败 (2026-12 起应可切)");
        QVERIFY2(a2->arcLengthFormula == QStringLiteral("30+15"),
                 "公式必须原样保留, 不得乘换算系数/烘焙成数值");
        QCOMPARE(btn->text(), QStringLiteral("⌒"));

        btn->click();
        const auto* a3 = &doc.attachments().front();
        QVERIFY2(a3->rotationMode == cad::param::RotationMode::ChordLength,
                 "公式驱动弧长→开度切换失败");
        QVERIFY2(a3->chordLengthFormula == QStringLiteral("30+15"),
                 "公式必须原样保留");
        QCOMPARE(btn->text(), QStringLiteral("↔"));
    }

    delete dlg;
}

void TestDialogTabsAngleConn::angleFormulaPreservedInDialog()
{
    // ① 自由线使用表达式角度 (如 1+1):
    // 打开面板必须显示 "1+1" 原文, 副标签显示 "= 2°", 绝不自动把表达式清空并直接显示 2.
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        doc.setActiveLayer(layerIdAt(doc, 1));
        const LineSetup line = makeLine(doc, 100.0);
        auto* b = doc.findBlock(line.blockId);
        auto* ep = b->findPoint(line.endId);
        ep->angleFormula = QStringLiteral("1+1");
        ep->angle = 2.0;
        doc.resolveAll();

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto* dlg = new cad::ui::LinePropertyDialog(
            line.blockId, line.segId, &doc, &scene, &view);
        dlg->show();
        QVERIFY2(cad::test::waitUntil([&] {
                     return dlg->findChild<cad::ui::SegmentAngleCard*>() != nullptr;
                 }),
                 "timed out waiting for SegmentAngleCard");
        auto* card = dlg->findChild<cad::ui::SegmentAngleCard*>();
        QVERIFY(card);
        auto* edit = card->findChild<ElaLineEdit*>();
        QVERIFY(edit);

        // 主输入框保留表达式原文
        QCOMPARE(edit->text(), QStringLiteral("1+1"));

        // 副标签显示求值结果 = 2°
        auto* followLbl = card->findChild<ElaText*>(QStringLiteral("followValueLabel"));
        QVERIFY(followLbl);
        QVERIFY(followLbl->isVisible());
        QCOMPARE(followLbl->text(), QString::fromUtf8("= 2°"));

        // 等待 debounce (200ms) 触发，确保不会自动将 1+1 覆盖回填为数值 2
        cad::test::settle(300);
        QCOMPARE(edit->text(), QStringLiteral("1+1"));

        // 模型里的公式依然完好
        b = doc.findBlock(line.blockId);
        ep = b->findPoint(line.endId);
        QCOMPARE(ep->angleFormula, QStringLiteral("1+1"));

        dlg->accept();
        delete dlg;

        // 对话框关闭后，模型里的公式依然完好
        b = doc.findBlock(line.blockId);
        ep = b->findPoint(line.endId);
        QCOMPARE(ep->angleFormula, QStringLiteral("1+1"));
    }

    // ② 跟随线使用表达式角度:
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        doc.setActiveLayer(layerIdAt(doc, 1));
        const LineSetup leader = makeLine(doc, 100.0, Vec2(200.0, 0.0));
        const LineSetup follower = makeLine(doc, 60.0);
        Attachment att;
        att.fromBlockId = follower.blockId;
        att.fromPointId = follower.startId;
        att.toBlockId   = leader.blockId;
        att.toPointId   = leader.endId;
        att.followerAngle = 2.0;
        att.followerAngleFormula = QStringLiteral("1+1");
        QVERIFY(doc.addAttachment(att));
        doc.resolveAll();

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        auto* dlg = new cad::ui::LinePropertyDialog(
            follower.blockId, follower.segId, &doc, &scene, &view);
        dlg->show();
        QVERIFY2(cad::test::waitUntil([&] {
                     return dlg->findChild<cad::ui::SegmentAngleCard*>() != nullptr;
                 }),
                 "timed out waiting for SegmentAngleCard");
        auto* card = dlg->findChild<cad::ui::SegmentAngleCard*>();
        QVERIFY(card);
        auto* edit = card->findChild<ElaLineEdit*>();
        QVERIFY(edit);

        QCOMPARE(edit->text(), QStringLiteral("1+1"));

        auto* followLbl = card->findChild<ElaText*>(QStringLiteral("followValueLabel"));
        QVERIFY(followLbl);
        QVERIFY(followLbl->isVisible());
        QCOMPARE(followLbl->text(), QString::fromUtf8("= 2°"));

        cad::test::settle(300);
        QCOMPARE(edit->text(), QStringLiteral("1+1"));

        dlg->accept();
        delete dlg;

        const auto* a = &doc.attachments().front();
        QCOMPARE(a->followerAngleFormula, QStringLiteral("1+1"));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 终点连接行 + 桥接线 badge + 端点微卡摘要 (2026-xx 每端完整连接):
//   · 方案 A: 端点双微卡的只读连接行存在 (startPointConn/endPointConn)。
//   · 终点连接 (桥接落点缺省): 输入目标 P# → badge「终点指向」+ 终点卡「指向」。
//   · 双端连接 = 桥接线: 起点连接行输入 leader P# → badge「桥接线」+
//     基准线卡 (SegmentRefCard) 隐藏 + 起点卡「跟随」。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabsAngleConn::endConnectionRowAndBadge()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);                       // leader (100mm @200,0) + line (60mm 自由)
    const auto hostB = makeLine(doc, 80.0, Vec2(160.0, 60.0));   // 终点目标宿主
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentConnectionCard*>() != nullptr;
             }),
             "timed out waiting for SegmentConnectionCard");
    auto* card = dlg->findChild<cad::ui::SegmentConnectionCard*>();

    // 方案 A: 端点双微卡的只读连接行存在 (双卡同构 → 等高契约保持)。
    auto* startConn = dlg->findChild<ElaText*>(QStringLiteral("startPointConn"));
    auto* endConn = dlg->findChild<ElaText*>(QStringLiteral("endPointConn"));
    QVERIFY(startConn && endConn);

    auto hasHint = [&](const QString& prefix) {
        for (auto* t : dlg->findChildren<ElaText*>())
            if (t->text().startsWith(prefix)) return true;
        return false;
    };

    // ① 终点连接 (桥接落点缺省): 输入目标 P# 回车 → badge 终点指向 + 终点卡摘要。
    auto* endPointEdit = card->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("endConnPointEdit"));
    QVERIFY2(endPointEdit, "终点连接点输入框 (endConnPointEdit) 必须存在");
    const auto* hb = doc.findBlock(hostB.blockId);
    const auto* bp = hb->findPoint(hostB.endId);
    endPointEdit->setText(bp->serial);
    QTest::keyClick(endPointEdit, Qt::Key_Return);
    QVERIFY2(cad::test::waitUntil([&] {
                 return hasHint(QString::fromUtf8("终点指向"));
             }),
             "自由线 + 终点指向 → badge 应为「终点指向」");
    QVERIFY2(cad::test::waitUntil([&] {
                 return endConn->text().contains(QString::fromUtf8("指向"));
             }),
             "终点微卡应显示「指向」摘要");
    QVERIFY2(doc.findBlock(line.blockId)->endTargetPointId == hostB.endId,
             "终点连接写入 endTarget");

    // ② 双端连接 = 桥接线: 起点连接行输入 leader P# → badge 桥接线 + 基准线
    //    卡隐藏 + 起点卡「跟随」。
    auto* startPointEdit = card->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("connPointEdit"));
    QVERIFY2(startPointEdit, "起点连接点输入框 (connPointEdit) 必须存在");
    const auto* ldrBlk = doc.findBlock(doc.blocks().at(0).id);   // 第一条 = leader
    const auto& ldrSeg = ldrBlk->segments.front();
    const auto* lp = ldrBlk->findPoint(ldrSeg.endPointId);
    startPointEdit->setText(lp->serial);
    QTest::keyClick(startPointEdit, Qt::Key_Return);
    QVERIFY2(cad::test::waitUntil([&] {
                 return hasHint(QString::fromUtf8("桥接线"));
             }),
             "双端连接 → badge 应为「桥接线」");
    auto* refCard = dlg->findChild<cad::ui::SegmentRefCard*>();
    QVERIFY(refCard);
    // 2026-09 规则表: 桥接线方向段 (点1/点2/[独立]) 隐藏, 对齐点段保留
    // (显示默认进点 + 禁用, 无进点语义)。
    auto* alignEdit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("alignPointEdit"));
    auto* p1Edit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    QVERIFY2(alignEdit && p1Edit, "对齐点/点1 输入框必须存在");
    QVERIFY2(cad::test::waitUntil([&] { return !p1Edit->isVisible(); }),
             "双端连接 → 方向段 (点1) 隐藏 (角度由两点决定)");
    QVERIFY2(!refCard->isHidden(), "桥接线: 对齐点段保留 (整卡不隐藏)");
    QVERIFY2(!alignEdit->isEnabled(), "桥接线: 无进点语义 → 对齐点禁用");
    QVERIFY2(cad::test::waitUntil([&] {
                 return startConn->text().contains(QString::fromUtf8("跟随"));
             }),
             "起点微卡应显示「跟随」摘要");
    QVERIFY2(startConn->height() >= startConn->fontMetrics().height(),
             "跟随摘要高度必须 >= 字体行高, 避免上下截断");

    delete dlg;

    // ③ 宿主线 (leader): 拥有吸附于其上的子线 → 端点微卡应显示「挂载」摘要, 且高度不截断
    auto* leaderDlg = new cad::ui::LinePropertyDialog(
        ldrBlk->id, ldrSeg.id, &doc, &scene, &view);
    leaderDlg->show();
    auto* ldrStartConn = leaderDlg->findChild<ElaText*>(QStringLiteral("startPointConn"));
    auto* ldrEndConn = leaderDlg->findChild<ElaText*>(QStringLiteral("endPointConn"));
    QVERIFY(ldrStartConn && ldrEndConn);
    QVERIFY2(cad::test::waitUntil([&] {
                 return ldrEndConn->text().contains(QString::fromUtf8("挂载"));
             }),
             "宿主线端点微卡应显示「挂载」摘要");
    QVERIFY2(ldrEndConn->height() >= ldrEndConn->fontMetrics().height(),
             "挂载摘要高度必须 >= 字体行高, 避免上下截断");

    delete leaderDlg;
}

// ─────────────────────────────────────────────────────────────────────────
// 拆开/重连 与「连接到」输入框 (用户 2026-09 报告):
//   · 拆开后「连接到」框必须清空 —— 位置已自由, 回显原目标会误导
//     (旧实现: 框里仍显示原内容, 虽然按钮已翻面为「重连」)。
//   · 拆开后输入目标 P# 回车 = 重连意图: 位置恢复吸附 + 重新焊接
//     (旧实现: 只改目标点, angleOnly 保持, 位置维度仍自由 —— 画布上
//     只有线段特效, 位置不跟随)。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabsAngleConn::detachClearsConnectEditAndRetargetReconnects()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);                       // leader (100mm @200,0) + line (60mm 自由)
    const auto hostB = makeLine(doc, 80.0, Vec2(160.0, 60.0));   // 重定向目标宿主
    doc.resolveAll();

    // 建立跟随连接 (line → leader 终点)。
    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = doc.blocks().at(0).id;       // 第一条 = leader
    att.toPointId   = doc.findBlock(doc.blocks().at(0).id)->segments.front().endPointId;
    att.toSegmentId = doc.findBlock(doc.blocks().at(0).id)->segments.front().id;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentConnectionCard*>() != nullptr;
             }),
             "timed out waiting for SegmentConnectionCard");

    // 端点组内「连接到」框 (startConnectEdit) + 拆开按钮 (startDetachBtn)。
    auto* connEdit = dlg->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("startConnectEdit"));
    auto* detachBtn = dlg->findChild<QPushButton*>(
        QStringLiteral("startDetachBtn"));
    QVERIFY2(connEdit && detachBtn, "起点「连接到」框与拆开按钮必须存在");

    // ① 初始: 已连接 → 框回显目标点, 按钮「拆开」。
    QVERIFY2(cad::test::waitUntil([&] { return !connEdit->text().isEmpty(); }),
             "已连接态「连接到」框应回显目标点");
    QCOMPARE(detachBtn->text(), QString::fromUtf8("拆开"));

    // ② 拆开 (位置维度): 框清空 + 按钮翻面「重连」+ 模型 angleOnly。
    detachBtn->click();
    QVERIFY2(cad::test::waitUntil([&] { return connEdit->text().isEmpty(); }),
             "拆开后「连接到」框必须清空 (位置已自由, 回显原目标会误导)");
    QCOMPARE(detachBtn->text(), QString::fromUtf8("重连"));
    QVERIFY2(doc.attachments().front().angleOnly, "拆开 = 位置维度拆开 (仅角度)");

    // ③ 拆开后输入目标 P# 回车 = 重定向 (影子挂载路由, DETACH_SHADOW_DESIGN.md
    //    §7.4, 2026-xx 翻案「直接重挂到新目标」): 影子挂到新宿主 (Att1 反算
    //    保向) + Att2 恢复位置吸附并重新焊接 —— 位置确实吸附回新宿主点。
    const auto* hb = doc.findBlock(hostB.blockId);
    const auto* bp = hb->findPoint(hostB.startId);
    connEdit->setText(bp->serial);
    QTest::keyClick(connEdit, Qt::Key_Return);
    QVERIFY2(cad::test::waitUntil([&] {
                 if (doc.attachments().size() != 2) return false;
                 const auto& a = doc.attachments().front();
                 if (a.angleOnly || !a.isLocked) return false;
                 const auto* sh = doc.blockById(a.toBlockId);
                 if (!sh || !sh->isShadow) return false;
                 for (const auto& a1 : doc.attachments())
                     if (!a1.isPin && a1.fromBlockId == sh->id
                         && a1.toBlockId == hostB.blockId
                         && a1.toPointId == hostB.startId)
                         return true;
                 return false;
             }),
             "拆开后输入 P# = 影子挂载到新宿主 (Att1→hostB.start) + Att2 焊接");
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(!a.angleOnly, "重定向必须恢复位置维度 (angleOnly=false)");
        QVERIFY2(a.isLocked, "重定向必须重新焊接");
        const auto* shadow = doc.blockById(a.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "Att2 基准 = 影子块");
        QCOMPARE(a.followerAngle, 180.0);   // offset 原样保留 (R2)
        // 位置确实吸附回新宿主点 (Resolver 链式生效: L3→影子→本线)。
        const Vec2 hostWorld = hb->transform.toWorld(
            hb->findPoint(hostB.startId)->resolvedPos);
        const auto* fb = doc.findBlock(line.blockId);
        const Vec2 fromWorld = fb->transform.toWorld(
            fb->findPoint(line.startId)->resolvedPos);
        QVERIFY2(hostWorld.distanceTo(fromWorld) < 1e-6,
                 "重连后 from-point 必须重新吸附回新宿主点");
    }
    QVERIFY2(cad::test::waitUntil([&] {
                 return connEdit->text().contains(
                     cad::param::Serial::tag(bp->serial));
             }),
             "重连后「连接到」框应回显新目标点");
    QCOMPARE(detachBtn->text(), QString::fromUtf8("拆开"));

    delete dlg;
}

// ─────────────────────────────────────────────────────────────────────────
// 拆开重连 = 保持角度基准 (用户 2026-09 拍板: 重连时角度基准保持不变):
//   · 自动态拆开 (angleOnly) 后重连到新宿主 = 仅连接, 旧所连线段被固化为
//     两点基准 (点1 = 旧线段另一端、点2 = 旧目标点, preserveAngleRefOnReattach),
//     方向基准不随新宿主漂移 —— 拆开保留的角度继续由旧的活基准线驱动。
//   · 自定义基准重连时原样保留。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabsAngleConn::reattachPreservesAngleRef()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);                       // leader (100mm @200,0) + line (60mm 自由)
    const auto hostB = makeLine(doc, 80.0, Vec2(160.0, 60.0));   // 重定向目标宿主
    const auto hostC = makeLine(doc, 70.0, Vec2(300.0, 120.0));  // 二次重定向目标宿主
    doc.resolveAll();

    // master id 提前复制值 (leader 块裸指针会随后续 addBlock 扩容悬垂 —
    // 一次一取, 禁止跨变更持有; 2026-xx 影子换代 addBlock 触发的回归)。
    const QUuid leaderId = doc.blocks().at(0).id;
    const QUuid leaderEnd = doc.blockById(leaderId)->segments.front().endPointId;
    const QUuid leaderStart = doc.blockById(leaderId)->segments.front().startPointId;

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leaderId;
    att.toPointId   = leaderEnd;
    att.toSegmentId = doc.blockById(leaderId)->segments.front().id;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QVERIFY2(doc.attachments().front().angleRefBlockId.isNull(),
             "初始连接 = 自动态 (无自定义角度基准)");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::PointRefEdit*>(
                            QStringLiteral("startConnectEdit")) != nullptr;
             }),
             "timed out waiting for startConnectEdit");

    auto* connEdit = dlg->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("startConnectEdit"));
    auto* detachBtn = dlg->findChild<QPushButton*>(
        QStringLiteral("startDetachBtn"));
    QVERIFY2(connEdit && detachBtn, "起点「连接到」框与拆开按钮必须存在");

    // 影子挂载判定 (DETACH_SHADOW_DESIGN.md §7.4, 2026-xx 翻案「两点基准
    // 固化」语义): 拆开 = 影子换代 (基准 = 隐藏影子块); 重定向到非本体 =
    // 影子挂载 (Att1 = 影子→宿主, Δ 反算保向) + Att2 恢复位置钉点。
    auto findAtt1Of = [&](const QUuid& shadowId) -> const cad::param::Attachment* {
        for (const auto& a : doc.attachments())
            if (!a.isPin && a.fromBlockId == shadowId) return &a;
        return nullptr;
    };
    auto retargetTo = [&](const LineSetup& host) {
        detachBtn->click();   // 拆开 (影子换代)
        QVERIFY2(cad::test::waitUntil([&] { return connEdit->text().isEmpty(); }),
                 "拆开后「连接到」框应清空");
        const auto* hb = doc.findBlock(host.blockId);
        const auto* hp = hb->findPoint(host.startId);
        connEdit->setText(hp->serial);
        QTest::keyClick(connEdit, Qt::Key_Return);
        QVERIFY2(cad::test::waitUntil([&] {
                     if (doc.attachments().size() != 2) return false;
                     const auto& a = doc.attachments().front();
                     if (a.angleOnly || !a.isLocked) return false;
                     const auto* sh = doc.blockById(a.toBlockId);
                     if (!sh || !sh->isShadow) return false;
                     const auto* a1 = findAtt1Of(sh->id);
                     return a1 && a1->toBlockId == host.blockId;
                 }),
                 "重定向 = 影子挂载到新宿主 (Att1) + Att2 恢复位置焊接");
    };

    // ① 拆开重定向到 hostB: 基准 = 影子块 (master = leader), offset (180°)
    //    原样保留 (R2) —— 影子替代旧两点基准固化。
    retargetTo(hostB);
    {
        const auto& a = doc.attachments().front();
        const auto* sh = doc.blockById(a.toBlockId);
        QVERIFY2(sh && sh->isShadow, "拆开重定向: Att2 基准 = 影子块");
        QVERIFY2(sh->shadowMasterBlockId == leaderId,
                 "影子 master = 原基准线 leader (旧基准的载体化, R2)");
        QVERIFY2(a.followerAngle == 180.0, "offset 原样保留 (R2)");
        QVERIFY2(a.angleRefBlockId.isNull(),
                 "影子替代两点基准固化: angleRef 保持自动态 (翻案 2026-xx)");
        const auto* a1 = findAtt1Of(sh->id);
        QVERIFY2(a1 && a1->toBlockId == hostB.blockId,
                 "影子挂载到 hostB (Att1)");
        QVERIFY2(a1->isLocked, "新建挂载连接默认焊接 (与 addAttachment 同约定)");
    }

    // ② 二次拆开重定向到 hostC: 影子换宿主 (原子替换旧 Att1, Δ 重新反算),
    //    Att2 的 offset 与影子块保持不变 (基准不漂移 —— 影子是稳定中间实体)。
    retargetTo(hostC);
    {
        const auto& a = doc.attachments().front();
        const auto* sh = doc.blockById(a.toBlockId);
        QVERIFY2(sh && sh->isShadow, "二次重定向: Att2 基准 = 同一影子块");
        QVERIFY2(a.followerAngle == 180.0, "二次重定向: offset 原样保留");
        const auto* a1 = findAtt1Of(sh->id);
        QVERIFY2(a1 && a1->toBlockId == hostC.blockId,
                 "影子换宿主到 hostC (旧 Att1 原子替换)");
        QVERIFY2(doc.attachments().size() == 2, "换宿主不残留旧 Att1");
    }

    delete dlg;
}

// ─────────────────────────────────────────────────────────────────────────
// [链接当前线] 按钮 (用户 2026-09 拍板): 清空自定义角度基准回自动态 ——
// 方向基准 = 当前所连线段出口方向 (方向行灰显回显当前线段两点)。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabsAngleConn::linkCurrentLineButtonClearsRef()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);                       // leader (100mm @200,0) + line (60mm 自由)
    const auto hostB = makeLine(doc, 80.0, Vec2(160.0, 60.0));   // 自定义基准目标
    doc.resolveAll();

    const auto* leaderBlk = doc.findBlock(doc.blocks().at(0).id);
    const auto& leaderSeg = leaderBlk->segments.front();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leaderBlk->id;
    att.toPointId   = leaderSeg.endPointId;
    att.toSegmentId = leaderSeg.id;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    // 自定义基准 = hostB 起点 (与所连线段不同)。
    const auto* hb = doc.findBlock(hostB.blockId);
    doc.setAttachmentAngleRef(att.id, hostB.blockId, hostB.segId, hostB.startId);
    doc.resolveAll();
    QVERIFY2(!doc.attachments().front().angleRefBlockId.isNull(),
             "前置: 自定义角度基准已设置");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentRefCard*>() != nullptr;
             }),
             "timed out waiting for SegmentRefCard");

    auto* refCard = dlg->findChild<cad::ui::SegmentRefCard*>();
    auto* linkBtn = refCard->findChild<QPushButton*>(
        QStringLiteral("linkCurrentLineBtn"));
    auto* p1Edit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    QVERIFY2(linkBtn && p1Edit && p2Edit, "链接当前线按钮与点1/点2 输入框必须存在");

    // 自定义态: 按钮可用, 点1/点2 回显自定义基准。
    QVERIFY2(cad::test::waitUntil([&] { return linkBtn->isEnabled(); }),
             "自定义基准态: [链接当前线] 应可用");
    QVERIFY2(p1Edit->text().contains(cad::param::Serial::tag(hb->findPoint(hostB.startId)->serial)),
             "自定义态: 点1 回显自定义基准点");

    // 点击 → 清空自定义基准回自动态。
    linkBtn->click();
    QVERIFY2(cad::test::waitUntil([&] {
                 return doc.attachments().front().angleRefBlockId.isNull();
             }),
             "点击 [链接当前线] 应清空自定义角度基准");
    {
        const auto& a = doc.attachments().front();
        QVERIFY2(a.angleRef2BlockId.isNull() && a.angleRef2PointId.isNull(),
                 "点击 [链接当前线] 应同时清空点2");
        QVERIFY2(!a.angleIndependent, "链接当前线 = 角度跟随, 不进入独立角");
    }
    // 自动态: 方向行灰显回显当前所连线段两点 (点1 = 宿主目标点, 点2 = 另一端)。
    const auto* ldrEndPt = leaderBlk->findPoint(leaderSeg.endPointId);
    const auto* ldrStartPt = leaderBlk->findPoint(leaderSeg.startPointId);
    QVERIFY2(cad::test::waitUntil([&] {
                 return p1Edit->text().contains(
                            cad::param::Serial::tag(ldrEndPt->serial))
                     && p2Edit->text().contains(
                            cad::param::Serial::tag(ldrStartPt->serial));
             }),
             "自动态: 点1/点2 灰显回显当前所连线段两点");
    QVERIFY2(!linkBtn->isEnabled(), "自动态: [链接当前线] 禁用 (基准本就是当前线段)");

    delete dlg;
}

// ─────────────────────────────────────────────────────────────────────────
// 独立角: 点1/点2 清空但**不禁用** (用户 2026-09 拍板) —— 独立角时方向行
// 无内容, 但输入框保持可编辑 (可直接填点 = 退出独立角并建立自定义基准)。
// 2026-09 修正: 独立角**不锁「链接当前线」按钮** —— 独立角 + 自定义基准
// (ref 字段 = 还原缓存) 时按钮可用, 点击 = 清空基准 + 退出独立角回自动态。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabsAngleConn::independentAngleKeepsRefEditsEnabled()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);                       // leader (100mm @200,0) + line (60mm 自由)
    const auto hostB = makeLine(doc, 80.0, Vec2(160.0, 60.0));   // 自定义基准目标
    doc.resolveAll();

    const auto* leaderBlk = doc.findBlock(doc.blocks().at(0).id);
    const auto& leaderSeg = leaderBlk->segments.front();

    cad::param::Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.startId;
    att.toBlockId   = leaderBlk->id;
    att.toPointId   = leaderSeg.endPointId;
    att.toSegmentId = leaderSeg.id;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    // 自定义基准 (hostB 起点) + 独立角: ref 字段保留为还原缓存。
    const auto* hb = doc.findBlock(hostB.blockId);
    doc.setAttachmentAngleRef(att.id, hostB.blockId, hostB.segId, hostB.startId);
    doc.setAttachmentAngleIndependent(att.id, true);
    doc.resolveAll();
    QVERIFY2(doc.attachments().front().angleIndependent, "前置: 独立角已设置");
    QVERIFY2(!doc.attachments().front().angleRefBlockId.isNull(),
             "前置: 独立角保留自定义基准 (还原缓存)");

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QVERIFY2(cad::test::waitUntil([&] {
                 return dlg->findChild<cad::ui::SegmentRefCard*>() != nullptr;
             }),
             "timed out waiting for SegmentRefCard");

    auto* refCard = dlg->findChild<cad::ui::SegmentRefCard*>();
    auto* p1Edit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPointEdit"));
    auto* p2Edit = refCard->findChild<cad::ui::PointRefEdit*>(
        QStringLiteral("angleRefPoint2Edit"));
    auto* indBtn = refCard->findChild<QPushButton*>(
        QStringLiteral("angleBaseToggleBtn"));
    auto* linkBtn = refCard->findChild<QPushButton*>(
        QStringLiteral("linkCurrentLineBtn"));
    QVERIFY2(p1Edit && p2Edit && indBtn && linkBtn,
             "点1/点2 输入框与 [独立]/[链接当前线] 按钮必须存在");

    // 独立角: 点1/点2 清空 (方向行无内容) 但保持可编辑。
    QVERIFY2(cad::test::waitUntil([&] { return indBtn->isChecked(); }),
             "[独立] 按钮应勾选");
    QVERIFY2(p1Edit->text().isEmpty() && p2Edit->text().isEmpty(),
             "独立角: 点1/点2 应清空 (方向行无内容)");
    QVERIFY2(p1Edit->isEnabled() && p2Edit->isEnabled(),
             "独立角: 点1/点2 应保持可编辑 (不禁用)");

    // 独立角 + 自定义基准: [链接当前线] 可用 (独立只是清空, 不锁按钮)。
    QVERIFY2(linkBtn->isEnabled(),
             "独立角 + 自定义基准: [链接当前线] 应可用 (独立不锁按钮)");

    // 点击 → 清空基准 + 退出独立角回自动态。
    linkBtn->click();
    QVERIFY2(cad::test::waitUntil([&] {
                 const auto& a = doc.attachments().front();
                 return a.angleRefBlockId.isNull() && !a.angleIndependent;
             }),
             "独立角点击 [链接当前线] 应清空基准并退出独立角");
    // 自动态: 方向行灰显回显当前所连线段两点。
    const auto* ldrEndPt = leaderBlk->findPoint(leaderSeg.endPointId);
    const auto* ldrStartPt = leaderBlk->findPoint(leaderSeg.startPointId);
    QVERIFY2(cad::test::waitUntil([&] {
                 return p1Edit->text().contains(
                            cad::param::Serial::tag(ldrEndPt->serial))
                     && p2Edit->text().contains(
                            cad::param::Serial::tag(ldrStartPt->serial));
             }),
             "退出独立角后: 点1/点2 灰显回显当前所连线段两点");
    QVERIFY2(!linkBtn->isEnabled(), "自动态: [链接当前线] 禁用 (基准本就是当前线段)");

    delete dlg;
}


QTEST_MAIN(TestDialogTabsAngleConn)
#include "test_dialog_tabs_angle_conn.moc"
