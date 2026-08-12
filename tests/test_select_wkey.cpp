#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "parametric/ParamDocument.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::layerIdAt;

/// End-to-end regression test for the select tool's W key (多选 ↔ 单选 toggle):
/// a REAL QKeyEvent travels CanvasView::keyPressEvent → ToolManager::dispatch
/// → ToolSelect::keyPress, and the resulting mode change is verified through
/// the observable click behaviour (Single replaces, Multi adds).
class TestSelectWKey : public QObject
{
    Q_OBJECT

private slots:
    void wTogglesMultiSelectionThroughFullEventChain();
    void ctrlDragCopiesAfterConfirm();
    void multiSelectMarqueeSelectsBothLines();
};

void TestSelectWKey::wTogglesMultiSelectionThroughFullEventChain()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));  // makeLine blocks land on the first working layer
    CanvasScene scene(&doc);

    // Two horizontal lines, stacked vertically: A at y=0, B at y=-50.
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);        // default active tool is Select
    view.setToolManager(&tm);

    // Map user coords (+Y up) → viewport pixels via the real view transform.
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

    auto* itemA = scene.findBlockItem(a.blockId);
    auto* itemB = scene.findBlockItem(b.blockId);
    QVERIFY(itemA);
    QVERIFY(itemB);

    // Diagnostics: the clicked viewport point must actually hit a block item.
    {
        const QPointF scenePt = view.mapToScene(vp(50.0, 0.0));
        const auto hits = scene.items(scenePt);
        qInfo("click(50,0) -> scene(%g,%g), %d items", scenePt.x(), scenePt.y(),
              int(hits.size()));
        for (QGraphicsItem* it : hits)
            qInfo("  item: %s", it->type() == BlockItem::Type ? "BlockItem" : "other");
    }

    // Default mode is Single: clicking B replaces the selection of A.
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(!itemA->toolSelected());   // Single replaced A

    // W toggles to Multi: the current selection SURVIVES the toggle, so the
    // user can keep adding to it (regression: it used to be cleared, which
    // made W look broken — picking one block then W then another lost the
    // first one).
    pressW();
    QVERIFY(itemB->toolSelected());    // preserved across the toggle
    click(50.0, 0.0);
    QVERIFY(itemA->toolSelected());    // added
    QVERIFY(itemB->toolSelected());    // B still selected

    // W again toggles back to Single: the multi-set is dropped (single-click
    // replace semantics) and clicking B replaces the selection.
    pressW();
    QVERIFY(!itemA->toolSelected());
    QVERIFY(!itemB->toolSelected());   // Multi → Single clears the set
    click(50.0, -50.0);
    QVERIFY(itemB->toolSelected());
    QVERIFY(!itemA->toolSelected());   // Single replaced again
}

// Ctrl+drag must copy from a CONFIRMED selection (the documented 确认流程:
// 选中 → 右键确认 → Ctrl+拖动). Regression: the gesture used to require the
// Confirm state; the whole chain must work end-to-end.
void TestSelectWKey::ctrlDragCopiesAfterConfirm()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    tm.setUndoStack(&stack);   // endCopyDrag commits via the undo stack
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 1) Click to select the line (Selecting state).
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(scene.findBlockItem(a.blockId)->toolSelected());
    QCOMPARE(doc.blocks().size(), size_t(1));

    // 2) Right-click confirms the selection (确认流程).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::RightButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::RightButton, Qt::NoModifier);

    // 3) Ctrl+press on the confirmed line and drag +120 mm right → copy fires.
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(80.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(170.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(170.0, 0.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    const Block* orig = doc.findBlock(a.blockId);
    const Block* clone = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { clone = &b; break; }
    QVERIFY(clone);
    // Clone landed at origin + (120, 0): same shape, new position.
    QVERIFY(clone->transform.origin.distanceTo(Vec2(120.0, 0.0)) < 1e-6);
    QVERIFY(orig->segments.size() == clone->segments.size());
}

// 多选模式下空白处拖拽必须显示并应用框选（回归：手势提取时丢失了
// setState(Marquee)，导致 mouseMove/mouseRelease 不进 Marquee 分支——
// 框选框不更新、释放不应用，表现为“W 进多选后框选消失”）。
void TestSelectWKey::multiSelectMarqueeSelectsBothLines()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton
                                                                 : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    auto* itemA = scene.findBlockItem(a.blockId);
    auto* itemB = scene.findBlockItem(b.blockId);
    QVERIFY(itemA);
    QVERIFY(itemB);

    // W → 多选模式，然后从空白处（左上方）拖框到右下，覆盖两条线。
    QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    QApplication::sendEvent(&view, &key);
    QTest::qWait(20);

    const QPoint start = vp(-150.0, 120.0);   // empty space above both lines
    const QPoint end   = vp(220.0, -180.0);    // covers A (y=0) and B (y=-50)
    sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, end, Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoModifier);

    // Both lines are selected by the marquee toggle.
    QVERIFY(itemA->toolSelected());
    QVERIFY(itemB->toolSelected());
}

QTEST_MAIN(TestSelectWKey)
#include "test_select_wkey.moc"
