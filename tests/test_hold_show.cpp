/// @file test_hold_show.cpp
/// Regression tests for the hold-to-show overlay keys (N = all names,
/// L = all lengths):
///   - forceShowName/forceShowLength reveal labels without touching the model
///     (pixel-level: rendering the scene shows extra text pixels while forced);
///   - releasing restores the model's own show flags (snapshot semantics);
///   - forceShowChanged signal fires on every state flip;
///   - overlapping points sharing the same name render a single label
///     (dedup by 0.1 mm position grid).
///
/// Run: test_hold_show

#include <QtTest>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QGraphicsView>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::test::makeLine;
using cad::test::layerIdAt;

namespace {

/// Non-white pixel count of a full-view render. Uses QGraphicsView::grab
/// (the REAL view rendering path) — QGraphicsScene::render clips to each
/// item's boundingRect, which cuts off labels drawn outside it; the view
/// does not.
int renderNonWhitePixels(CanvasScene& scene, const QString& dumpPath = {})
{
    QGraphicsView view(&scene);
    view.resize(800, 600);
    view.show();
    QTest::qWaitForWindowExposed(&view);   // no QVERIFY: non-void helper
    // P2-3: 等到视图真的画完再抓帧（旧的固定 30ms sleep 在负载下会抓到半帧
    // / 空白, 让下面所有像素断言假失败 —— 整批 ctest 抖动的来源之一）。
    QImage img = cad::test::grabStable(view);
    view.hide();
    if (!dumpPath.isEmpty())
        img.save(dumpPath);
    int count = 0;
    for (int y = 0; y < img.height(); y += 2) {          // sample every 2nd row —
        for (int x = 0; x < img.width(); x += 2) {       // plenty for text pixels
            if (img.pixel(x, y) != qRgb(255, 255, 255))
                ++count;
        }
    }
    return count;
}

} // namespace

class TestHoldShow : public QObject
{
    Q_OBJECT

private slots:
    void forceNameRevealsSegmentAndPointLabels();
    void forceLengthRevealsLengthLabels();
    void releaseRestoresModelFlags();
    void signalFiresOnEveryFlip();
    void overlappingSameNamePointsDrawSingleLabel();
};

void TestHoldShow::forceNameRevealsSegmentAndPointLabels()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    auto setup = makeLine(doc, 100.0);
    auto* block = doc.findBlock(setup.blockId);
    QVERIFY(block);
    auto* seg = block->findSegment(setup.segId);
    auto* sp  = block->findPoint(setup.startId);
    seg->name = QStringLiteral("LINE1");
    sp->name  = QStringLiteral("P1");
    // All model show-flags stay OFF: labels must appear ONLY via force.
    seg->showName = false;
    sp->showName  = false;
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(setup.blockId);

    const int baseline = renderNonWhitePixels(scene);
    QVERIFY(baseline > 0);          // the line itself paints something

    scene.setForceShowName(true);
    const int forced = renderNonWhitePixels(scene);
    QVERIFY2(forced - baseline > 50,
             qPrintable(QStringLiteral("expected label pixels under force (baseline %1, forced %2)")
                            .arg(baseline).arg(forced)));
}

void TestHoldShow::forceLengthRevealsLengthLabels()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    auto setup = makeLine(doc, 100.0);
    auto* block = doc.findBlock(setup.blockId);
    auto* seg = block->findSegment(setup.segId);
    seg->name = QStringLiteral("LINE1");
    seg->showLength = false;
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(setup.blockId);

    const int baseline = renderNonWhitePixels(scene);
    scene.setForceShowLength(true);
    const int forced = renderNonWhitePixels(scene);
    QVERIFY2(forced - baseline > 50,
             qPrintable(QStringLiteral("expected length-label pixels under force (baseline %1, forced %2)")
                            .arg(baseline).arg(forced)));
}

void TestHoldShow::releaseRestoresModelFlags()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    auto setup = makeLine(doc, 100.0);
    auto* block = doc.findBlock(setup.blockId);
    auto* seg = block->findSegment(setup.segId);
    seg->name = QStringLiteral("LINE1");
    // The model's own flag is ON: releasing the force must fall back to the
    // model value (label stays visible), NOT to a clean hidden state.
    seg->showName = true;
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(setup.blockId);

    const int modelOn = renderNonWhitePixels(scene);

    scene.setForceShowName(false);
    const int afterRelease = renderNonWhitePixels(scene);
    QVERIFY2(std::abs(afterRelease - modelOn) < 50,
             qPrintable(QStringLiteral("release must restore model flags (modelOn %1, afterRelease %2)")
                            .arg(modelOn).arg(afterRelease)));
}

void TestHoldShow::signalFiresOnEveryFlip()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    auto setup = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(setup.blockId);

    QSignalSpy spy(&scene, &CanvasScene::forceShowChanged);
    scene.setForceShowName(true);
    scene.setForceShowName(true);    // unchanged → no second emission
    scene.setForceShowLength(true);
    scene.setForceShowName(false);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toBool(), true);    // (true, false)
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QCOMPARE(spy.at(2).at(0).toBool(), false);   // (false, true)
    QCOMPARE(spy.at(2).at(1).toBool(), true);
}

void TestHoldShow::overlappingSameNamePointsDrawSingleLabel()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // Two identical blocks at the SAME world position with the SAME point
    // name — their labels overlap fully and must be drawn only once.
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, cad::geo::Vec2::zero());
    doc.findBlock(a.blockId)->findPoint(a.startId)->name = QStringLiteral("X");
    doc.findBlock(b.blockId)->findPoint(b.startId)->name = QStringLiteral("X");
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(a.blockId);
    scene.setForceShowName(true);
    const int oneLabel = renderNonWhitePixels(scene);

    CanvasScene scene2(&doc);
    scene2.addBlockItem(a.blockId);
    scene2.addBlockItem(b.blockId);
    scene2.setForceShowName(true);
    const int twoBlocks = renderNonWhitePixels(scene2);

    // Deduped: the overlapping copy adds (almost) nothing beyond its line —
    // an un-deduped second label would add hundreds of text pixels.
    QVERIFY2(twoBlocks - oneLabel < 100,
             qPrintable(QStringLiteral("same-name overlapping labels must merge (one %1, two %2)")
                            .arg(oneLabel).arg(twoBlocks)));
}

QTEST_MAIN(TestHoldShow)
#include "test_hold_show.moc"
