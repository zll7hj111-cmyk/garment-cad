/// @file test_realdoc_full.cpp
/// FULL-WINDOW drag benchmark on a real user document (.gcad).
///
/// Unlike test_realdoc_perf (solver + bare scene only), this test builds the
/// COMPLETE MainWindow (side panel, variable panel, status bar, tool manager)
/// and drives a REAL select-tool drag through the true event chain:
///   QTest::mouseClick (select+confirm) → mousePress → mouseMove*N → release
/// Each move is timed INCLUDING event processing (i.e. real repaints), so any
/// per-frame panel/sync/paint cost anywhere in the app shows up here.
///
/// Env: GCAD_DOC=<path to .gcad>   Run: test_realdoc_full

#include <QtTest>
#include <QApplication>
#include <QUndoStack>
#include <QGraphicsView>
#include <QStatusBar>
#include <QLoggingCategory>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include "app/MainWindow.h"
#include "canvas/CanvasView.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "tools/ToolManager.h"
#include "document/DocumentFile.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

double medianUs(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double maxUs(const std::vector<double>& v)
{
    return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
}

} // namespace

class TestRealdocFull : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void teardownSmoke();
    void fullWindowDrag();
};

namespace {

/// Dump a stack trace on any qFatal/Q_ASSERT so the teardown assert can be
/// attributed to the exact emit site.
void stackDumpHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    fprintf(stderr, "[qtmsg] %s\n", qPrintable(msg));
    fflush(stderr);
    if (type == QtFatalMsg) {
        void* frames[64];
        const int n = CaptureStackBackTrace(0, 64, frames, nullptr);
        HANDLE proc = GetCurrentProcess();
        SymInitialize(proc, nullptr, TRUE);
        char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        for (int i = 0; i < n; ++i) {
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = MAX_SYM_NAME;
            DWORD64 disp = 0;
            if (SymFromAddr(proc, reinterpret_cast<DWORD64>(frames[i]), &disp, sym))
                fprintf(stderr, "  #%d %s +0x%llx\n", i, sym->Name, (unsigned long long)disp);
            else
                fprintf(stderr, "  #%d %p\n", i, frames[i]);
        }
        fflush(stderr);
        abort();
    }
    // Non-fatal: just echo (the harness also logs via -o).
    Q_UNUSED(ctx);
}

} // namespace

void TestRealdocFull::initTestCase()
{
    qInstallMessageHandler(stackDumpHandler);
}

void TestRealdocFull::teardownSmoke()
{
    // Construct + load + destroy with ZERO interaction: isolates whether the
    // teardown-time assert is caused by the drag itself or by plain teardown.
    const QString path = qEnvironmentVariable("GCAD_DOC");
    if (path.isEmpty() || !QFileInfo::exists(path))
        QSKIP("set GCAD_DOC to a .gcad file");
    {
        MainWindow win;
        win.resize(800, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));
        auto* view = qobject_cast<CanvasView*>(win.centralWidget());
        QVERIFY(view);
        auto* scene = qobject_cast<CanvasScene*>(view->scene());
        QVERIFY(scene);
        QString err;
        QStringList warnings;
        QVERIFY(cad::doc::DocumentFile::load(path, *scene->paramDocument(), &err, &warnings));
        QApplication::processEvents();
        QTest::qWait(100);
    } // win destroyed here — must not assert
    qInfo() << "teardown smoke ok";
}

void TestRealdocFull::fullWindowDrag()
{
    const QString path = qEnvironmentVariable("GCAD_DOC");
    if (path.isEmpty() || !QFileInfo::exists(path))
        QSKIP("set GCAD_DOC to a .gcad file");

    MainWindow win;
    win.resize(1600, 1000);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    auto* view = qobject_cast<CanvasView*>(win.centralWidget());
    QVERIFY(view);
    auto* scene = qobject_cast<CanvasScene*>(view->scene());
    QVERIFY(scene);
    ParamDocument* doc = scene->paramDocument();
    QVERIFY(doc);

    // Load the real document through the same loader the app uses.
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, *doc, &err, &warnings));
    QApplication::processEvents();

    // Pick a working-layer block with a resolved segment midpoint to grab.
    QUuid targetBlock;
    Vec2 grabWorld;
    for (const auto& b : doc->blocks()) {
        if (b.layer == 0) continue;
        for (const auto& seg : b.segments) {
            const auto* sp = b.findPoint(seg.startPointId);
            const auto* ep = b.findPoint(seg.endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
            grabWorld = b.transform.toWorld(
                (sp->resolvedPos + ep->resolvedPos) * 0.5);
            targetBlock = b.id;
            break;
        }
        if (!targetBlock.isNull()) break;
    }
    QVERIFY(!targetBlock.isNull());

    // Make the select tool active (default) and center the view on the target.
    const QPointF scenePt = cad::geo::Coord::toScene(grabWorld.x, grabWorld.y);
    view->centerOn(scenePt);
    QApplication::processEvents();
    const QPoint vp = view->mapFromScene(scenePt);
    QVERIFY(view->viewport()->rect().contains(vp));

    // Sanity: the grab point really hits the target block's item.
    const auto hits = scene->items(scenePt);
    bool hitTarget = false;
    for (QGraphicsItem* it : hits) {
        if (auto* bi = BlockItem::containingItem(it)) {
            if (bi->blockId() == targetBlock) { hitTarget = true; break; }
        }
    }
    QVERIFY2(hitTarget, "grab point does not hit the target block");

    // ── Click 1: select + confirm (单选即确认) ──
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
    QApplication::processEvents();

    // ── Click 2 press: begins the drag ──
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
    QApplication::processEvents();

    // ── Drag frames ──
    constexpr int kFrames = 150;
    std::vector<double> moveUs, dispatchUs, flushUs;
    moveUs.reserve(kFrames);
    dispatchUs.reserve(kFrames);
    flushUs.reserve(kFrames);
    for (int i = 1; i <= kFrames; ++i) {
        const QPoint to = vp + QPoint(i, i / 2);
        const auto t0 = std::chrono::steady_clock::now();
        QTest::mouseMove(view->viewport(), to);
        const auto t1 = std::chrono::steady_clock::now();
        QApplication::processEvents();   // flush paints — the real frame cost
        const auto t2 = std::chrono::steady_clock::now();
        dispatchUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        flushUs.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
        moveUs.push_back(std::chrono::duration<double, std::micro>(t2 - t0).count());
    }
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                        vp + QPoint(kFrames, kFrames / 2));
    QApplication::processEvents();

    const double med = medianUs(moveUs);
    qInfo().noquote() << QStringLiteral(
        "\n[full-drag] real MainWindow, select-tool block drag (%1 frames)\n"
        "  dispatch (tool+resolve+sync): median %2 us / max %3 us\n"
        "  paint flush (processEvents) : median %4 us / max %5 us\n"
        "  per move event total        : median %6 us / p95 %7 us / max %8 us\n"
        "  -> effective ceiling: ~%9 fps")
        .arg(kFrames)
        .arg(medianUs(dispatchUs), 0, 'f', 1).arg(maxUs(dispatchUs), 0, 'f', 1)
        .arg(medianUs(flushUs), 0, 'f', 1).arg(maxUs(flushUs), 0, 'f', 1)
        .arg(med, 0, 'f', 1)
        .arg(moveUs[static_cast<size_t>(kFrames * 0.95)], 0, 'f', 1)
        .arg(maxUs(moveUs), 0, 'f', 1)
        .arg(med > 0 ? 1e6 / med : 0.0, 0, 'f', 0);

    // Guard: a tiny document must drag smoothly even in Debug builds.
    QVERIFY2(med < 16000.0,
             "median drag frame exceeds 16 ms on a tiny document — real lag");
}

QTEST_MAIN(TestRealdocFull)
#include "test_realdoc_full.moc"
