#include "test_dialog_tabs_helpers.h"

class TestDialogTabsSwitch : public QObject
{
    Q_OBJECT
private slots:
    void switchBackAfterTyping();
    void switchBackLargeDoc();
    void probeTabHitArea();
    void switchBackWithoutEditing();
    void probeCardTitleColor();
};

void TestDialogTabsSwitch::switchBackAfterTyping()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    addAuxPoint(doc, line.blockId, line.segId);
    addAnchorPoint(doc, line.blockId, line.segId);
    doc.resolveAll();
    qInfo() << "[dialog-tabs] seg aux after add:"
            << doc.findBlock(line.blockId)->findSegment(line.segId)->auxPointIds.size()
            << "block id:" << line.blockId.toString() << "seg id:" << line.segId.toString();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    // P2-3: 等子控件出现而不是固定 sleep 80ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget* to appear");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);
    // 2026-08: 「点连接」只读 tab 已删 (属性页连接分区覆盖其内容);
    // 2026-09: 辅助点下沉为属性页【线上点】分区, 现为 2 枚 = 属性 / 锚点。
    QCOMPARE(tabs->count(), 2);

    // 辅助点已作为分区嵌入在属性页 (tab 0) 内，无需切换 tab 即可找到
    auto* auxTab = dlg->findChild<cad::ui::SegmentAuxTab*>();
    QVERIFY(auxTab);
    auto* list = auxTab->findChild<QListWidget*>();
    QVERIFY(list);
    qInfo() << "[dialog-tabs] list count after dialog open:"
            << list->count()
            << "seg aux:"
            << doc.findBlock(line.blockId)->findSegment(line.segId)->auxPointIds.size();
    QCOMPARE(list->count(), 1);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    // P2-3: 等子控件出现而不是固定 sleep 20ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<cad::ui::AuxPointForm*>() != nullptr; }),
             "timed out waiting for cad::ui::AuxPointForm* to appear");
    auto* form = dlg->findChild<cad::ui::AuxPointForm*>();
    QVERIFY(form);
    QVERIFY(form->isVisible());
    QLineEdit* percentEdit = percentEditOf(form);
    QVERIFY(percentEdit);

    // Type into the percent field: focus + keystrokes (debounce not yet fired)
    QTest::mouseClick(percentEdit, Qt::LeftButton, Qt::NoModifier, QPoint(12, 6));
    percentEdit->selectAll();
    QTest::keyClicks(percentEdit, QStringLiteral("0.6"));
    QCOMPARE(percentEdit->text(), QStringLiteral("0.6"));

    // ── 0→1 (属性 → 锚点): the suspected freeze point with focus loss ──
    QElapsedTimer t0;
    t0.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    const qint64 msToAnchor = t0.elapsed();
    QVERIFY2(cad::test::waitUntil([&] { return tabs->currentIndex() == 1; }),
             "timed out waiting for the tab switch to 锚点");
    QCOMPARE(tabs->currentIndex(), 1);

    // ── 1→0 (锚点 → 属性) ──
    QElapsedTimer t1;
    t1.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msBack = t1.elapsed();

    // P2-3: this is where the ctest flake lived. The typed value is applied on
    // FOCUS LOSS and the apply path is debounced (SegmentAuxTab's live-update
    // timer) -- it is NOT guaranteed to have run by the time mouseClick()
    // returns. Asserting immediately made the test measure how busy the machine
    // was instead of whether the feature works: on a loaded box the debounce
    // had not fired yet and a healthy dialog looked broken.
    QVERIFY2(cad::test::waitUntil([&] { return tabs->currentIndex() == 0; }),
             "timed out waiting for the tab switch back to 属性");
    QCOMPARE(tabs->currentIndex(), 0);

    const QUuid blockId = line.blockId;
    const QUuid segId   = line.segId;
    QVERIFY2(cad::test::waitUntil([&] {
                 auto* b = doc.findBlock(blockId);
                 if (!b) return false;
                 auto* s = b->findSegment(segId);
                 if (!s || s->auxPointIds.empty()) return false;
                 auto* p = b->findPoint(s->auxPointIds.front());
                 return p && std::abs(p->interpPercent - 0.6) < 1e-9;
             }),
             "timed out waiting for the typed interpPercent to be applied");

    auto* blk = doc.findBlock(line.blockId);
    auto* seg = blk->findSegment(line.segId);
    auto* pt = blk->findPoint(seg->auxPointIds.front());
    QVERIFY(pt);
    QVERIFY(std::abs(pt->interpPercent - 0.6) < 1e-9);

    qInfo() << "[dialog-tabs] latency 0->1:" << msToAnchor
            << "ms; 1->0 after typing:" << msBack << "ms";

    // Simulate frantic re-clicking (odd count must land on 属性)
    QElapsedTimer t2;
    t2.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msRapid = t2.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);
    qInfo() << "[dialog-tabs] 6 rapid re-clicks:" << msRapid << "ms";

    delete dlg;
}

void TestDialogTabsSwitch::switchBackLargeDoc()
{
    // Emulate a real garment pattern: 20 work-layer lines, 4 aux points each,
    // plus a bridge — the freeze (if any) comes from the full resolveAll +
    // refreshAllBlockItems triggered on focus loss when switching back.
    ParamDocument doc;
    CanvasScene scene(&doc);
    doc.setActiveLayer(layerIdAt(doc, 1));

    LineSetup target;
    QVector<LineSetup> others;
    for (int i = 0; i < 20; ++i) {
        LineSetup line = makeLine(doc, 60.0 + i * 3.0, Vec2(i * 30.0, i * 20.0));
        if (i == 10) target = line;
        others.append(line);
    }
    for (auto& line : others) {
        for (int k = 0; k < 4; ++k) {
            ParamDocument& d = doc;
            Block* blk = d.findBlock(line.blockId);
            ParamPoint pt;
            pt.constraint = PointConstraint::Interpolated;
            pt.hostSegmentId = line.segId;
            pt.isAuxiliary = true;
            pt.visible = true;
            pt.interpPercent = 0.2 + 0.2 * k;
            pt.interpConstant = 0.0;
            pt.interpOffsetAngle = 30.0;
            pt.interpOffsetDist = 5.0;
            pt.serial = d.newPointSerial();
            const QUuid id = blk->addPoint(pt);
            blk->findSegment(line.segId)->auxPointIds.push_back(id);
        }
    }
    {
        ParamDocument& d = doc;
        const auto a = d.findBlock(target.blockId);
        ParamPoint pt;
        pt.constraint = PointConstraint::Interpolated;
        pt.hostSegmentId = target.segId;
        pt.isAuxiliary = true;
        pt.visible = true;
        pt.interpPercent = 0.5;
        pt.serial = d.newPointSerial();
        const QUuid id = a->addPoint(pt);
        a->findSegment(target.segId)->auxPointIds.push_back(id);
    }
    addAnchorPoint(doc, target.blockId, target.segId);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        target.blockId, target.segId, &doc, &scene, &view);
    dlg->show();
    // P2-3: 等子控件出现而不是固定 sleep 80ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget* to appear");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);

    QElapsedTimer tr;
    tr.start();
    doc.resolveAll();
    const qint64 resolveMs = tr.elapsed();

    // Aux point list is in 属性 tab (index 0).
    auto* auxTab = dlg->findChild<cad::ui::SegmentAuxTab*>();
    QVERIFY(auxTab);
    auto* list = auxTab->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 5);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    // P2-3: 等子控件出现而不是固定 sleep 20ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<cad::ui::AuxPointForm*>() != nullptr; }),
             "timed out waiting for cad::ui::AuxPointForm* to appear");
    auto* form = dlg->findChild<cad::ui::AuxPointForm*>();
    QVERIFY(form && form->isVisible());
    QLineEdit* percentEdit = percentEditOf(form);
    QVERIFY(percentEdit);
    QTest::mouseClick(percentEdit, Qt::LeftButton, Qt::NoModifier, QPoint(12, 6));
    percentEdit->selectAll();
    QTest::keyClicks(percentEdit, QStringLiteral("0.6"));
    QCOMPARE(percentEdit->text(), QStringLiteral("0.6"));

    // Switch to 锚点 (1) then back to 属性 (0)
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    QCOMPARE(tabs->currentIndex(), 1);

    QElapsedTimer t1;
    t1.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msBack = t1.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);

    qInfo() << "[dialog-tabs] large doc: resolveAll" << resolveMs
            << "ms; 1->0 after typing:" << msBack << "ms";
    delete dlg;
}

void TestDialogTabsSwitch::probeTabHitArea()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    addAnchorPoint(doc, line.blockId, line.segId);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    // P2-3: 等子控件出现而不是固定 sleep 80ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget* to appear");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);
    auto* bar = tabs->tabBar();
    QVERIFY(bar);

    qInfo() << "[hit] dlg geo:" << dlg->geometry()
            << "tabs geo:" << tabs->geometry()
            << "tabbar geo:" << bar->geometry();
    auto* auxTab = dlg->findChild<cad::ui::SegmentAuxTab*>();
    QVERIFY(auxTab);

    auto widgetAt = [&](int tabIdx, int yFrac) {
        const QRect r = bar->tabRect(tabIdx);
        const QPoint p = bar->mapToGlobal(
            r.topLeft() + QPoint(r.width() / 2, qMax(1, r.height() * yFrac / 4)));
        // widget-tree hit test: QApplication::widgetAt() 是窗口像素级命中，
        // 对 ElaDialog 的透明阴影 margin 区返回 null（Windows GetWindowFromPoint
        // 不命中全透明像素），与字体/阴影环境耦合；childAt 走纯 widget 几何，
        // 同时仍能检出 aux 容器盖住 tabbar 的回归。
        auto* w = dlg->childAt(dlg->mapFromGlobal(p));
        return w ? QString::fromLatin1(w->metaObject()->className()) : QStringLiteral("null");
    };
    qInfo() << "[hit] auxTab visible:" << auxTab->isVisible();
    qInfo() << "[hit] tab0 top/mid/bot:"
            << widgetAt(0, 1) << widgetAt(0, 2) << widgetAt(0, 3)
            << "| tab1 mid:" << widgetAt(1, 2);

    // Regression: the aux-tab container must never sit on top of the tab bar.
    // ElaTabWidget uses an ElaTabBar (className "ElaTabBar") — a QTabBar
    // subclass — so the hit-test asserts on that name.
    QCOMPARE(widgetAt(0, 1), QStringLiteral("ElaTabBar"));
    QCOMPARE(widgetAt(0, 2), QStringLiteral("ElaTabBar"));
    QCOMPARE(widgetAt(1, 2), QStringLiteral("ElaTabBar"));

    // Sanity: a click on the 锚点 tab now actually switches to it.
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                      bar->tabRect(1).center());
    QCOMPARE(tabs->currentIndex(), 1);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                      bar->tabRect(0).topLeft() + QPoint(24, 4));
    QCOMPARE(tabs->currentIndex(), 0);

    // Exhaustive sweep: any VISIBLE widget (dialog-owned or nested) whose
    // global rect intersects the tab bar would swallow clicks on some tabs.
    // Widgets inside the tab bar's own subtree (Ela tab close buttons etc.)
    // are tab-bar chrome by definition — only OUTSIDE widgets must not
    // overlap the bar.
    const QPoint barTL = bar->mapToGlobal(QPoint(0, 0));
    const QRect barGlobal(barTL, bar->size());
    int overlaps = 0;
    for (auto* w : dlg->findChildren<QWidget*>()) {
        if (w == bar || w == tabs) continue;
        if (bar->isAncestorOf(w)) continue;  // tab-bar-internal chrome.
        if (!w->isVisible() && !w->isVisibleTo(dlg)) continue;
        const QRect g(w->mapToGlobal(QPoint(0, 0)), w->size());
        if (!g.intersects(barGlobal)) continue;
        if (QString::fromLatin1(w->metaObject()->className())
                .startsWith(QLatin1String("Ela")))
            continue;  // ElaTabWidget internal chrome — allowed.
        ++overlaps;
        qInfo() << "[hit] OVERLAP on tabbar:" << w->metaObject()->className()
                << "visible:" << w->isVisible()
                << "geo:" << g;
    }
    qInfo() << "[hit] overlap count:" << overlaps;
    QCOMPARE(overlaps, 0);

    // Regression: cards are ElaScrollPageArea subclasses whose constructor
    // hard-codes setFixedHeight(75) — that crushed every card's content and
    // made the dialog unusable. The dialog lifts the constraint; assert no
    // card is still pinned to the 75px fixed height.
    const auto cards = dlg->findChildren<ElaScrollPageArea*>();
    int pinned = 0;
    for (auto* card : cards) {
        if (card->minimumHeight() == card->maximumHeight()
            && card->maximumHeight() <= 80) {
            ++pinned;
            qInfo() << "[hit] PINNED card:" << card->metaObject()->className()
                    << "h:" << card->height();
        } else {
            qInfo() << "[hit] card:" << card->metaObject()->className()
                    << "h:" << card->height();
        }
    }
    qInfo() << "[hit] pinned card count:" << pinned;
    QCOMPARE(pinned, 0);
    for (int i = 0; i < tabs->count(); ++i) {
        const QRect r = bar->tabRect(i);
        const QPoint topC = bar->mapToGlobal(
            r.topLeft() + QPoint(r.width() / 2, qMax(1, r.height() / 4)));
        const QPoint midC = bar->mapToGlobal(r.center());
        const QPoint botC = bar->mapToGlobal(
            r.topLeft() + QPoint(r.width() / 2, r.height() * 3 / 4));
        auto* wTop = QApplication::widgetAt(topC);
        auto* wMid = QApplication::widgetAt(midC);
        auto* wBot = QApplication::widgetAt(botC);
        qInfo() << "[hit] tab" << i << tabs->tabText(i)
                << "rect:" << r
                << "top:" << (wTop ? wTop->metaObject()->className() : "null")
                << "mid:" << (wMid ? wMid->metaObject()->className() : "null")
                << "bot:" << (wBot ? wBot->metaObject()->className() : "null");
    }
    delete dlg;
}

void TestDialogTabsSwitch::switchBackWithoutEditing()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    addAnchorPoint(doc, line.blockId, line.segId);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto* dlg = new cad::ui::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    // P2-3: 等子控件出现而不是固定 sleep 80ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget* to appear");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);

    QElapsedTimer t0;
    t0.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(1).center());
    const qint64 msToAnchor = t0.elapsed();
    QCOMPARE(tabs->currentIndex(), 1);

    QElapsedTimer t1;
    t1.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msBack = t1.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);

    qInfo() << "[dialog-tabs] control (no editing): 0->1:" << msToAnchor
            << "ms; 1->0:" << msBack << "ms";
    delete dlg;
}

/// 像素探针: 分区标题为什么"掉色" (用户报告 2026-?)。对比 几何 分区标题
/// 与 长度 标签的真实渲染 (grab 最暗像素) + QSS 解析后的调色板。
void TestDialogTabsSwitch::probeCardTitleColor()
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
    cad::test::grabStable(*dlg);   // 等待 polish + 首次 paint: 两帧一致 = 已绘制 (替代 150ms 墙钟)

    auto dump = [](const QString& tag, ElaText* t) {
        if (!t) { qInfo().noquote() << tag << "= null"; return; }
        const QImage img = t->grab().toImage();
        // 文字像素 = alpha>0 (透明底); 统计 最暗 / 平均明度 / 实心占比。
        qint64 sumL = 0, solid = 0, n = 0;
        QColor darkest(255, 255, 255);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.alpha() == 0) continue;
                if (c.lightness() < darkest.lightness()) darkest = c;
                sumL += c.lightness();
                if (c.lightness() < 90) ++solid;
                ++n;
            }
        qInfo().noquote()
            << tag
            << "| text=" << t->text()
            << "| font=" << t->font().family() << "w" << t->font().weight()
            << "px" << t->font().pixelSize()
            << "| darkest=" << darkest.name()
            << QStringLiteral("| textPx=%1 avgL=%2 solid(%3%)")
                   .arg(n).arg(n ? sumL / n : 0).arg(n ? 100 * solid / n : 0);
    };

    for (auto* t : dlg->findChildren<ElaText*>()) {
        if (t->text() == QString::fromUtf8("几何"))
            dump(QStringLiteral("TITLE-几何"), t);
        if (t->text() == QString::fromUtf8("连接"))
            dump(QStringLiteral("TITLE-连接"), t);
        if (t->text() == QString::fromUtf8("长度"))
            dump(QStringLiteral("LABEL-长度"), t);
        if (t->text() == QString::fromUtf8("连接线段"))
            dump(QStringLiteral("LABEL-连接线段"), t);
    }
    delete dlg;
}


QTEST_MAIN(TestDialogTabsSwitch)
#include "test_dialog_tabs_switch.moc"
