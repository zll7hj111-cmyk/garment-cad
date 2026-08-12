#include <QtTest>
#include <QApplication>
#include <QElapsedTimer>
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
#include "tools/LinePropertyDialog.h"
#include "tools/SegmentAuxTab.h"
#include "tools/AuxPointForm.h"
#include "ElaScrollPageArea.h"
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

QLineEdit* percentEditOf(cad::tools::AuxPointForm* form)
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(80);
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 4);

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
    auto* auxTab = dlg->findChild<cad::tools::SegmentAuxTab*>();
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
    QTest::qWait(20);

    auto* form = dlg->findChild<cad::tools::AuxPointForm*>();
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
    QCOMPARE(tabs->currentIndex(), 0);

    // The typed value must have been applied on focus loss
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        target.blockId, target.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(80);
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
    QTest::qWait(20);
    auto* form = dlg->findChild<cad::tools::AuxPointForm*>();
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(80);
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs);
    auto* bar = tabs->tabBar();
    QVERIFY(bar);

    qInfo() << "[hit] dlg geo:" << dlg->geometry()
            << "tabs geo:" << tabs->geometry()
            << "tabbar geo:" << bar->geometry();
    auto* auxTab = dlg->findChild<cad::tools::SegmentAuxTab*>();
    QVERIFY(auxTab);

    auto widgetAt = [&](int tabIdx, int yFrac) {
        const QRect r = bar->tabRect(tabIdx);
        const QPoint p = bar->mapToGlobal(
            r.topLeft() + QPoint(r.width() / 2, qMax(1, r.height() * yFrac / 4)));
        auto* w = QApplication::widgetAt(p);
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(80);
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

QTEST_MAIN(TestDialogTabs)
#include "test_dialog_tabs.moc"
