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
#include "tools/PointRefEdit.h"
#include "tools/SegmentAuxTab.h"
#include "tools/SegmentExtendCard.h"
#include "tools/SegmentConnectionCard.h"
#include "ElaComboBox.h"
#include "ElaPushButton.h"
#include "ElaLineEdit.h"
#include "tools/AuxPointForm.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
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
    void probeCardTitleColor();        ///< why card titles look washed out (像素探针)
    void extendAppearSideBySide();     ///< 延长|外观 双气泡并排 (用户 2026-12 要求)
    void connectCardUniformHeights();  ///< 连接卡行内控件统一高度/宽度 (用户 2026-12 反馈)
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

/// 像素探针: 卡片标题为什么"掉色" (用户报告 2026-?)。对比 基本信息 标题
/// 与 名称: 标签的真实渲染 (grab 最暗像素) + QSS 解析后的调色板。
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(150);   // 等待 polish + 首次 paint (ElaText::paintEvent 重置 palette)

    auto dump = [](const QString& tag, ElaText* t) {
        if (!t) { qInfo().noquote() << tag << "= null"; return; }
        const QImage img = t->grab().toImage();
        const double dpr = img.devicePixelRatio();
        QColor darkest(255, 255, 255);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.lightness() < darkest.lightness()) darkest = c;
            }
        qInfo().noquote()
            << tag
            << "| text=" << t->text()
            << "| obj=" << t->objectName()
            << "| sheet=" << t->styleSheet().replace('\n', ' ')
            << "| palWin=" << t->palette().color(QPalette::WindowText).name()
            << "| palTxt=" << t->palette().color(QPalette::Text).name()
            << QStringLiteral("| grab(%1x%2 dpr=%3) darkest=%4")
                   .arg(img.width()).arg(img.height()).arg(dpr).arg(darkest.name());
    };

    for (auto* t : dlg->findChildren<ElaText*>()) {
        if (t->text() == QString::fromUtf8("基本信息"))
            dump(QStringLiteral("TITLE-基本信息"), t);
        if (t->text() == QString::fromUtf8("名称:"))
            dump(QStringLiteral("LABEL-名称"), t);
    }
    delete dlg;
}

/// 回归：延长|外观 双气泡并排 (用户 2026-12: "外观和延长能不能像下面那个
/// 起点和终点一样，左右各一个气泡")。两卡必须在同一行、等高、左右相邻
/// (延长在左、外观在右)。
void TestDialogTabs::extendAppearSideBySide()
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
    QTest::qWait(120);   // 等布局/滚动区几何落定

    auto* extCard = dlg->findChild<cad::tools::SegmentExtendCard*>(
        QStringLiteral("extendCard"));
    auto* appearCard = dlg->findChild<ElaScrollPageArea*>(
        QStringLiteral("appearCard"));
    QVERIFY(extCard);
    QVERIFY(appearCard);
    QVERIFY(extCard->isVisibleTo(dlg));
    QVERIFY(appearCard->isVisibleTo(dlg));

    const QRect gE(extCard->mapTo(dlg, QPoint(0, 0)), extCard->size());
    const QRect gA(appearCard->mapTo(dlg, QPoint(0, 0)), appearCard->size());

    // 同一行、等高 (QHBoxLayout 垂直拉伸), 1px 舍入容差。
    QVERIFY2(std::abs(gE.top() - gA.top()) <= 1,
             qPrintable(QStringLiteral(
                 "extend/appear cards must share the same row "
                 "(ext top %1, appear top %2)").arg(gE.top()).arg(gA.top())));
    QVERIFY2(std::abs(gE.height() - gA.height()) <= 1,
             qPrintable(QStringLiteral(
                 "extend/appear cards must have equal heights "
                 "(ext %1, appear %2)").arg(gE.height()).arg(gA.height())));
    // 左右相邻: 延长在左、外观在右, 不重叠。
    QVERIFY2(gE.right() < gA.left(),
             qPrintable(QStringLiteral(
                 "extend card must sit left of appear card "
                 "(ext right %1, appear left %2)")
                 .arg(gE.right()).arg(gA.left())));

    qInfo().noquote() << QStringLiteral(
        "[ext-appear] ext geo (dlg) %1x%2 @%3,%4; appear %5x%6 @%7,%8")
        .arg(gE.width()).arg(gE.height()).arg(gE.x()).arg(gE.y())
        .arg(gA.width()).arg(gA.height()).arg(gA.x()).arg(gA.y());

    delete dlg;
}

/// 回归：连接卡「输入框大小不一」 (用户 2026-12 反馈) —— 行内控件统一高
/// 35px (ElaLineEdit/ElaComboBox 原生值, ElaPushButton/PointRefEdit 已对齐),
/// 点引用输入统一宽 150px (列对齐)。
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

    auto* dlg = new cad::tools::LinePropertyDialog(
        line.blockId, line.segId, &doc, &scene, &view);
    dlg->show();
    QTest::qWait(120);

    auto* card = dlg->findChild<cad::tools::SegmentConnectionCard*>();
    QVERIFY(card);

    // ① 行内控件统一高度 35px。
    int badH = 0;
    for (auto* w : card->findChildren<QWidget*>()) {
        const bool isInput =
            qobject_cast<ElaLineEdit*>(w) || qobject_cast<cad::tools::PointRefEdit*>(w)
            || qobject_cast<ElaPushButton*>(w) || qobject_cast<ElaComboBox*>(w);
        if (!isInput) continue;
        if (w->height() != 35 && w->y() >= 0) {   // 布局后高度应为 35
            ++badH;
            qInfo() << "[uniform] bad height:" << w->metaObject()->className()
                    << w->height() << w->geometry();
        }
    }
    QVERIFY2(badH == 0, qPrintable(QStringLiteral("连接卡行内控件高度不统一: %1 个")
                                       .arg(badH)));

    // ② 点引用输入 (PointRefEdit) 统一 150px 宽。
    {
        int refBad = 0;
        for (auto* p : card->findChildren<cad::tools::PointRefEdit*>()) {
            if (p->width() != 150) ++refBad;
        }
        QVERIFY2(refBad == 0,
                 qPrintable(QStringLiteral("点引用输入宽度不统一: %1 个")
                                .arg(refBad)));
    }

    qInfo() << "[uniform] conn card controls all 35px high; ref inputs 150px";
    delete dlg;
}

QTEST_MAIN(TestDialogTabs)
#include "test_dialog_tabs.moc"
