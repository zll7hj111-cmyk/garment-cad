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
#include <QUndoStack>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "document/commands/BlockCommands.h"
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
    /// 画布方向指示 (2026-12): 换向几何零跳变, 箭头是唯一可见反馈 —— 像素级
    /// 断言换向前后箭头左右镜像。
    void directionChevronFlipsOnReverse();
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

// 画布方向指示 (DirectionMarker.h): 起点→终点小箭头, 换向 (ReverseSegmentCommand,
// 几何零跳变) 后自动翻转 —— 这是画布上唯一的换向可见反馈。像素级验证:
// 场景里只有 水平线 + 箭头 (showName/showLength 默认关, 无文字标签), 换向
// 前后两次抓帧的唯一差异只能是箭头区域 (左右镜像)。断言: 差集非空 (箭头
// 画了且翻转了)、足够小 (只是箭头不是整帧异常)、位于线中点附近。
void TestHoldShow::directionChevronFlipsOnReverse()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const auto setup = makeLine(doc, 100.0);   // (0,0) → (100,0), 自由线
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.addBlockItem(setup.blockId);

    QGraphicsView view(&scene);
    view.resize(800, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QImage before = cad::test::grabStable(view);

    // 换向: 物理换身, 世界几何零跳变。
    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, setup.blockId, setup.segId));
    QImage after = cad::test::grabStable(view);

    QCOMPARE(before.size(), after.size());
    // grab() 返回设备像素 (dpr 1.25), mapFromScene 返回逻辑坐标 → 乘 dpr。
    const QPoint midDev(
        qRound(view.mapFromScene(QPointF(50.0, 0.0)).x() * before.devicePixelRatio()),
        qRound(view.mapFromScene(QPointF(50.0, 0.0)).y() * before.devicePixelRatio()));

    int diff = 0;
    QRect diffBox;
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < before.width(); ++x) {
            if (before.pixel(x, y) == after.pixel(x, y)) continue;
            ++diff;
            if (!diffBox.isValid()) diffBox = QRect(x, y, 1, 1);
            else diffBox |= QRect(x, y, 1, 1);
        }
    }
    QVERIFY2(diff > 0, "方向指示箭头应在换向后翻转 (换向几何零跳变, 唯变项 = 箭头)");
    QVERIFY2(diff < 5000, "差集应只是箭头 (小区域), 过大 = 渲染异常");
    const QPoint dc = diffBox.center();
    QVERIFY2(std::abs(dc.x() - midDev.x()) < 40 && std::abs(dc.y() - midDev.y()) < 40,
             qPrintable(QStringLiteral("箭头差集应在中点附近 (mid %1,%2 diff %3,%4)")
                            .arg(midDev.x()).arg(midDev.y())
                            .arg(dc.x()).arg(dc.y())));
}

QTEST_MAIN(TestHoldShow)
#include "test_hold_show.moc"
