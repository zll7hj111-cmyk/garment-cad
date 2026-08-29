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
#include "parametric/MeasurementStore.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

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
    void curveFollowerDragStable();
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
        auto* view = qobject_cast<CanvasView*>(win.getCentralWidget(0));
        QVERIFY(view);
        auto* scene = qobject_cast<CanvasScene*>(view->scene());
        QVERIFY(scene);
        QString err;
        QStringList warnings;
        QVERIFY(cad::doc::DocumentFile::load(path, *scene->paramDocument(), &err, &warnings));
        QApplication::processEvents();
        // 手动跑档(GCAD_DOC)的 teardown 冒烟: 等 load 触发的异步清场(重绘/延删), 无可观测谓词, 暂留。
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

    auto* view = qobject_cast<CanvasView*>(win.getCentralWidget(0));
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
        if (b.layer == doc->auxLayerId()) continue;
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

/// Regression (用户报告 2026-09 "曲线跟随拖动的时候晃动"):
/// 曲线块 (Bezier + CurveAnchor) 作为线级 follower 焊在外部 leader 线段上;
/// 拖动 leader 时曲线块整体跟随附件求解重解. 若附件求解/曲线缓存/组件沉降
/// 的顺序不对, 曲线锚点世界位置会在逐帧拖动中抖动 (帧间位移跳变).
/// 不变式: 逐帧拖动 leader 时, 曲线锚点世界位置的变化应平滑 — 每帧位移
/// 方向一致、大小与 leader 位移同量级; 且停顿帧位移为 0 (无漂移).
void TestRealdocFull::curveFollowerDragStable()
{
    const QString path = qEnvironmentVariable("GCAD_DOC");
    if (path.isEmpty() || !QFileInfo::exists(path))
        QSKIP("set GCAD_DOC to a .gcad file");

    ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));
    doc.resolveAll();

    // 收集曲线块: Bezier 段 + CurveAnchor 锚点.
    struct CurveInfo { QUuid block; QUuid anchor; };
    QVector<CurveInfo> curves;
    for (const auto& b : doc.blocks()) {
        bool hasBezier = false;
        for (const auto& seg : b.segments)
            if (seg.type == SegmentType::Bezier) { hasBezier = true; break; }
        if (!hasBezier) continue;
        for (const auto& pt : b.points)
            if (pt.constraint == PointConstraint::CurveAnchor)
                curves.push_back({b.id, pt.id});
    }
    if (curves.isEmpty())
        QSKIP("document has no curve block with a CurveAnchor");

    // 曲线块的 leader: 附件 fromBlockId == 曲线块 → toBlockId (曲线块是 follower).
    QSet<QUuid> leaders;
    for (const auto& c : curves)
        for (const auto& a : doc.attachments())
            if (a.fromBlockId == c.block)
                leaders.insert(a.toBlockId);
    if (leaders.isEmpty())
        QSKIP("curve blocks have no line-level leader attachment");

    // 取工作层上的第一个"自由根" leader 作拖动目标: 排除自身也是 follower
    // 的中间块 (拖中间块会被它的 incoming 附件拉回 leader 位, 无法观测链尾),
    // 排除辅助层块 (拖辅助层不触发工作层后处理).
    QUuid leader;
    for (const QUuid& id : leaders) {
        const Block* b = doc.findBlock(id);
        if (!b || b->layer == doc.auxLayerId()) continue;
        bool hasIncoming = false;
        for (const auto& a : doc.attachments())
            if (a.fromBlockId == id) { hasIncoming = true; break; }
        if (hasIncoming) continue;
        leader = id;
        break;
    }
    if (leader.isNull()) leader = *leaders.constBegin();
    qInfo() << "drag leader" << leader.toString() << "curves:" << curves.size();

    // 基线: 逐帧拖动 leader, 采样所有曲线锚点世界位置.
    constexpr int kFrames = 20;
    const Vec2 step(0.5, 0.3);
    QVector<QVector<Vec2>> samples(curves.size());
    const QUuid leaderLayer = doc.findBlock(leader)->layer;  // 真实所在层
    for (int f = 0; f <= kFrames; ++f) {
        if (f > 0) {
            Block* l = doc.findBlock(leader);
            QVERIFY(l);
            l->transform.origin += step;
            doc.invalidateLayer(leaderLayer);
            doc.resolveForDrag({leader});
        }
        for (int i = 0; i < curves.size(); ++i) {
            const Block* b = doc.findBlock(curves[i].block);
            samples[i].push_back(b ? b->worldPos(curves[i].anchor) : Vec2());
        }
    }

    // 晃动检测: 帧间位移的平滑度. 逐帧 leader 位移恒定 (step),
    // 锚点位移应近似同方向 (点积 > 0) 且大小在合理范围; 停顿帧位移 ≈ 0.
    double maxJitter = 0.0;
    for (int i = 0; i < curves.size(); ++i) {
        double lastMag = -1.0;
        for (int f = 1; f <= kFrames; ++f) {
            const Vec2 d = samples[i][f] - samples[i][f - 1];
            const double mag = std::sqrt(d.x * d.x + d.y * d.y);
            // 位移量: 应约为 |step| 的同量级 (附件焊接 → 曲线块刚性跟随).
            if (mag > 10.0 * step.length()) {
                qWarning() << "curve" << curves[i].block
                           << "frame" << f << "jump magnitude" << mag;
                maxJitter = (std::max)(maxJitter, mag);
            }
            // 帧间位移方差: 恒定拖动下锚点位移应稳定 (|d_{f+1}-d_f| 小).
            if (lastMag > 0.0 && std::abs(mag - lastMag) > 0.05) {
                qWarning() << "curve" << curves[i].block
                           << "frame" << f << "mag swing" << mag << "prev" << lastMag;
                maxJitter = (std::max)(maxJitter, mag);
            }
            lastMag = mag;
        }
        // 停顿帧: 第 kFrames 后再 resolve 一次, 锚点应原地不动.
        doc.invalidateLayer(leaderLayer);
        doc.resolveForDrag({leader});
        const Vec2 paused = doc.findBlock(curves[i].block)
            ? doc.findBlock(curves[i].block)->worldPos(curves[i].anchor) : Vec2();
        const Vec2 drift = paused - samples[i][kFrames];
        const double dd = std::sqrt(drift.x * drift.x + drift.y * drift.y);
        if (dd > 1e-3) {
            qWarning() << "curve" << curves[i].block << "pause drift" << dd;
            maxJitter = (std::max)(maxJitter, dd);
        }
        // 连续停顿多帧: 若每帧仍在移动 → 慢漂移/晃动 (非单次跳变).
        for (int pk = 1; pk <= 4; ++pk) {
            doc.invalidateLayer(leaderLayer);
            doc.resolveForDrag({leader});
            const Vec2 pp = doc.findBlock(curves[i].block)
                ? doc.findBlock(curves[i].block)->worldPos(curves[i].anchor) : Vec2();
            const Vec2 pd = pp - samples[i][kFrames];
            const double pdd = std::sqrt(pd.x * pd.x + pd.y * pd.y);
            if (pdd > 1e-3) {
                qWarning() << "  curve" << curves[i].block << "pause" << pk << "drift" << pdd;
                maxJitter = (std::max)(maxJitter, pdd);
            }
        }
    }

    QVERIFY2(maxJitter < 1.0, "curve anchor jitters during leader drag");
}

QTEST_MAIN(TestRealdocFull)
#include "test_realdoc_full.moc"
