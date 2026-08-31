#include <QtTest>
#include <QApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QTabBar>
#include <QTabWidget>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "ui/LinePropertyDialog.h"
#include "ui/SegmentAngleCard.h"
#include "ui/SegmentRefCard.h"
#include "ui/PointRefEdit.h"
#include "ui/SegmentAuxTab.h"
#include "ui/SegmentConnectionCard.h"
#include "ElaComboBox.h"
#include "ElaPushButton.h"
#include "ElaLineEdit.h"
#include "ui/AuxPointForm.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "geometry/Angle.h"
#include "TestHelpers.h"

using namespace cad::param;
using namespace cad::test;

/// Reproduce the reported UX bug: switching back to the 属性 tab inside the
/// line-property dialog requires repeated clicks ("狂点才生效"), while other
/// tab switches are snappy. Hypothesis: when the aux tab hides, its focused
/// QLineEdit loses focus → editingFinished → SegmentAuxTab::onLiveUpdate →
/// applyTo + resolveAll + refreshAllBlockItems run SYNCHRONOUSLY inside the
/// tab-bar mouse-press handler, freezing the UI; repeated clicks then bounce
/// the page back and forth until an odd click count lands on 属性.
class TestDialogTabs : public QObject
{
    Q_OBJECT
private slots:
    void switchBackAfterTyping();      ///< full user sequence (typed value)
    void switchBackWithoutEditing();   ///< control: no editing, no focus
    void switchBackLargeDoc();         ///< heavy document: freeze magnitude
    void probeTabHitArea();            ///< which widget owns the tab-bar pixels
    void probeCardTitleColor();        ///< why section titles look washed out (像素探针)
    void endpointCardsStacked();    ///< 端点 起点|终点 双微卡上下堆叠 (2026-xx §3)
    void connectCardUniformHeights();  ///< 连接卡行内控件统一高度/宽度 (用户 2026-12 反馈)
    void independentAngleInputDoesNotJump();  ///< 独立角输入不回跳 (用户 2026-12 反馈)
    void followerAngleModeToggleToArc();  ///< REPRO: 跟随角 → 弧长 切换 (用户报告切换不了)
    void endConnectionRowAndBadge();  ///< 终点连接行 + 桥接线 badge + 端点微卡摘要 (2026-xx 每端完整连接)
};

namespace {

QUuid addAuxPoint(ParamDocument& doc, const QUuid& blockId, const QUuid& segId)
{
    Block* blk = doc.findBlock(blockId);
    ParamPoint pt;
    pt.constraint = PointConstraint::Interpolated;
    pt.hostSegmentId = segId;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.interpPercent = 0.5;
    pt.interpConstant = 0.0;
    pt.interpOffsetAngle = 0.0;
    pt.interpOffsetDist = 0.0;
    pt.serial = doc.newPointSerial();
    const QUuid id = blk->addPoint(pt);
    blk->findSegment(segId)->auxPointIds.push_back(id);
    return id;
}

/// Open the dialog over a fresh scene with one leader + one line (+aux point).
void setup(ParamDocument& doc, CanvasScene& scene, LineSetup& line)
{
    doc.setActiveLayer(layerIdAt(doc, 1));
    makeLine(doc, 100.0, Vec2(200.0, 0.0));
    line = makeLine(doc, 60.0);
}

QLineEdit* percentEditOf(cad::ui::AuxPointForm* form)
{
    for (auto* e : form->findChildren<QLineEdit*>())
        if (e->placeholderText().contains(QLatin1String("0.5")))
            return e;
    return nullptr;
}

} // namespace

void TestDialogTabs::switchBackAfterTyping()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    LineSetup line;
    setup(doc, scene, line);
    addAuxPoint(doc, line.blockId, line.segId);
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
    // 2026-08: 「点连接」只读 tab 已删 (属性页连接分区覆盖其内容), 3 枚 =
    // 属性 / 锚点 / 辅助点。
    QCOMPARE(tabs->count(), 3);

    // ── 0→2 (属性 → 辅助点): baseline ──
    QElapsedTimer t0;
    t0.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    const qint64 msToAux = t0.elapsed();
    QCOMPARE(tabs->currentIndex(), 2);

    // Select the aux list item → edit form becomes visible. The list lives in
    // the aux tab PAGE, which QTabWidget reparents into its internal
    // QStackedWidget — locate it via the page (widget(2)) ancestor chain.
    auto* auxTab = dlg->findChild<cad::ui::SegmentAuxTab*>();
    QVERIFY(auxTab);
    QListWidget* list = nullptr;
    for (auto* w : dlg->findChildren<QListWidget*>())
        if (tabs->widget(2) && tabs->widget(2)->isAncestorOf(w)) { list = w; break; }
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

    // ── 2→0 (辅助点 → 属性): the suspected freeze point ──
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

    qInfo() << "[dialog-tabs] latency 0->2:" << msToAux
            << "ms; 2->0 after typing:" << msBack << "ms";

    // Simulate frantic re-clicking (odd count must land on 属性)
    QElapsedTimer t2;
    t2.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msRapid = t2.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);
    qInfo() << "[dialog-tabs] 6 rapid re-clicks:" << msRapid << "ms";

    delete dlg;
}

void TestDialogTabs::switchBackLargeDoc()
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

    // Go to 辅助点, select the aux item, type into percent (focus!),
    // then switch back to 属性 — the reported freeze point.
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    QListWidget* list = nullptr;
    for (auto* w : dlg->findChildren<QListWidget*>())
        if (tabs->widget(2) && tabs->widget(2)->isAncestorOf(w)) { list = w; break; }
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

    QElapsedTimer t1;
    t1.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msBack = t1.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);

    qInfo() << "[dialog-tabs] large doc: resolveAll" << resolveMs
            << "ms; 2->0 after typing:" << msBack << "ms";
    delete dlg;
}

void TestDialogTabs::probeTabHitArea()
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
            << "| tab2 mid:" << widgetAt(2, 2);

    // Regression: the aux-tab container must never sit on top of the tab bar.
    // It used to be a visible orphan QWidget at (0,0) that swallowed clicks on
    // the 属性/锚点 tabs (reported: "只能点击靠下才能切换").
    // ElaTabWidget uses an ElaTabBar (className "ElaTabBar") — a QTabBar
    // subclass — so the hit-test asserts on that name.
    QVERIFY(!auxTab->isVisible());
    QCOMPARE(widgetAt(0, 1), QStringLiteral("ElaTabBar"));
    QCOMPARE(widgetAt(0, 2), QStringLiteral("ElaTabBar"));
    QCOMPARE(widgetAt(2, 2), QStringLiteral("ElaTabBar"));

    // Sanity: a click on the 属性 tab now actually switches to it.
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                      bar->tabRect(2).center());
    QCOMPARE(tabs->currentIndex(), 2);
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

void TestDialogTabs::switchBackWithoutEditing()
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
    // P2-3: 等子控件出现而不是固定 sleep 80ms（负载下会让下面
    // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
    QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QTabWidget*>() != nullptr; }),
             "timed out waiting for QTabWidget* to appear");
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);

    QElapsedTimer t0;
    t0.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(2).center());
    const qint64 msToAux = t0.elapsed();

    QElapsedTimer t1;
    t1.start();
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                      tabs->tabBar()->tabRect(0).center());
    const qint64 msBack = t1.elapsed();
    QCOMPARE(tabs->currentIndex(), 0);

    qInfo() << "[dialog-tabs] control (no editing): 0->2:" << msToAux
            << "ms; 2->0:" << msBack << "ms";
    delete dlg;
}

/// 像素探针: 分区标题为什么"掉色" (用户报告 2026-?)。对比 几何 分区标题
/// 与 长度 标签的真实渲染 (grab 最暗像素) + QSS 解析后的调色板。
void TestDialogTabs::probeCardTitleColor()
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

/// 回归：端点 起点|终点 双微卡上下堆叠 (2026-xx §3: 两个端点组之间夹朝向箭头)。
/// 起点卡在上、终点卡在下、等宽; 朝向箭头在两者之间。
void TestDialogTabs::endpointCardsStacked()
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
void TestDialogTabs::connectCardUniformHeights()
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
void TestDialogTabs::independentAngleInputDoesNotJump()
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
void TestDialogTabs::followerAngleModeToggleToArc()
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
                || b->text() == QStringLiteral("⌒"))
                return b;
        return nullptr;
    };

    // ① 数值跟随角 → 弧长: 点 ⌒ 按钮后必须切到 ArcLength, 且换算正确
    // (45°·(60mm 半径) = π/4·60 ≈ 47.12mm)。
    {
        QPushButton* btn = modeButton();
        QVERIFY2(btn, "mode button (∠/⌒) not found");
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

    // ② 弧长 → 角度: 反向切换必须同样工作, 反算回 45°。
    {
        QPushButton* btn = modeButton();
        QVERIFY2(btn, "mode button lost after ①");
        QCOMPARE(btn->text(), QStringLiteral("⌒"));
        btn->click();
        const auto* a = &doc.attachments().front();
        QVERIFY2(a->rotationMode == cad::param::RotationMode::Angle,
                 "弧长→角度切换失败");
        // 反算回 45°: 容差 0.1° —— 输入框回显是 2 位小数的 cm 值 ("4.71"),
        // 经显示截断往返会损失 ~0.02° (45°→4.71cm→44.98°), 属既有显示精度。
        QVERIFY2(std::abs(a->followerAngle - 45.0) < 0.1, "角度反算错误");
    }

    // ③ 公式跟随角 → 弧长 (2026-12 用户拍板: 公式驱动可切换, 且公式
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
    }

    delete dlg;
}

// ─────────────────────────────────────────────────────────────────────────
// 终点连接行 + 桥接线 badge + 端点微卡摘要 (2026-xx 每端完整连接):
//   · 方案 A: 端点双微卡的只读连接行存在 (startPointConn/endPointConn)。
//   · 终点连接 (桥接落点缺省): 输入目标 P# → badge「终点指向」+ 终点卡「指向」。
//   · 双端连接 = 桥接线: 起点连接行输入 leader P# → badge「桥接线」+
//     基准线卡 (SegmentRefCard) 隐藏 + 起点卡「跟随」。
// ─────────────────────────────────────────────────────────────────────────
void TestDialogTabs::endConnectionRowAndBadge()
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

    delete dlg;
}

QTEST_MAIN(TestDialogTabs)
#include "test_dialog_tabs.moc"