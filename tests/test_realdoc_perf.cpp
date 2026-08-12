/// @file test_realdoc_perf.cpp
/// Per-frame drag-cost breakdown on a REAL user document (.gcad).
///
/// Loads the document given in env var GCAD_DOC (or argv[1]) and simulates
/// the exact per-frame paths the tools use while dragging:
///   A) block drag  (ToolSelect::updateDrag)     — translate + resolveForDrag
///   B) anchor drag (ToolSelect::updateAnchorDrag) — freePos + resolveForDrag
///   C) same frames through the live CanvasScene (+ full-viewport repaint)
/// and prints a µs breakdown so the bottleneck (solver / scene sync / repaint)
/// can be attributed. Run: test_realdoc_perf
///
/// Env: GCAD_DOC=<path to .gcad>

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QPainter>
#include <QPixmap>
#include <chrono>
#include <algorithm>

#include "document/DocumentFile.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "canvas/CanvasScene.h"
#include "geometry/Vec2.h"

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
    if (v.empty()) return 0.0;
    return *std::max_element(v.begin(), v.end());
}

void printDocStats(const ParamDocument& doc)
{
    int pts = 0, segs = 0, curves = 0, aux = 0, freePts = 0;
    int workBlocks = 0, auxBlocks = 0;
    for (const auto& b : doc.blocks()) {
        pts += static_cast<int>(b.points.size());
        for (const auto& p : b.points)
            if (p.isAuxiliary) ++aux;
            else if (p.constraint == PointConstraint::Free) ++freePts;
        for (const auto& s : b.segments) {
            if (s.type == SegmentType::Line) ++segs;
            else ++curves;
        }
        if (b.layer == doc.auxLayerId()) ++auxBlocks; else ++workBlocks;
    }
    qInfo().noquote() << QStringLiteral(
        "[realdoc] blocks=%1 (work=%2 aux=%3) points=%4 (aux=%5 free=%6) "
        "lines=%7 curves=%8 attachments=%9 measures=%10 linked=%11 formulas=%12 vars=%13")
        .arg(doc.blocks().size()).arg(workBlocks).arg(auxBlocks)
        .arg(pts).arg(aux).arg(freePts)
        .arg(segs).arg(curves)
        .arg(doc.attachments().size())
        .arg(doc.measureVars().size())
        .arg(doc.linkedVars().size())
        .arg(doc.formulas().size())
        .arg(doc.variables().size());
}

/// Count attachments that would cascade from @p seed (followers reachable).
int followerCount(const ParamDocument& doc, const QUuid& seed)
{
    int n = 0;
    for (const auto& a : doc.attachments())
        if (a.toBlockId == seed && !a.isPin) ++n;
    return n;
}

} // namespace

class TestRealdocPerf : public QObject
{
    Q_OBJECT

private slots:
    void dragFrameBreakdown();

private:
    bool loadDoc(ParamDocument& doc, QString& path);
};

bool TestRealdocPerf::loadDoc(ParamDocument& doc, QString& path)
{
    path = qEnvironmentVariable("GCAD_DOC");
    if (path.isEmpty())
        path = QStringLiteral("1.gcad");
    QString err;
    QStringList warnings;
    if (!cad::doc::DocumentFile::load(path, doc, &err, &warnings)) {
        qWarning().noquote() << "load failed:" << err;
        return false;
    }
    for (const auto& w : warnings)
        qWarning().noquote() << "load warning:" << w;
    return true;
}

void TestRealdocPerf::dragFrameBreakdown()
{
    ParamDocument doc;
    QString path;
    if (!loadDoc(doc, path))
        QSKIP("document not found (set GCAD_DOC)");
    qInfo().noquote() << "[realdoc] loaded:" << path;
    printDocStats(doc);

    constexpr int kIters = 120;

    // ── Pick the working-layer block with the most direct followers ──
    QUuid dragId;
    int bestFollowers = -1;
    for (const auto& b : doc.blocks()) {
        if (b.layer == doc.auxLayerId()) continue;
        const int f = followerCount(doc, b.id);
        if (f > bestFollowers) { bestFollowers = f; dragId = b.id; }
    }
    QVERIFY(!dragId.isNull());
    qInfo().noquote() << QStringLiteral(
        "[realdoc] drag seed block has %1 direct followers").arg(bestFollowers);

    // ══ A) block drag: pure solver cost (no scene) ══
    {
        Block* blk = doc.findBlock(dragId);
        const Vec2 origin = blk->transform.origin;
        std::vector<double> us;
        us.reserve(kIters);
        for (int i = 0; i < kIters; ++i) {
            blk->transform.origin = origin + Vec2(0.5 * i, 0.3 * i);
            doc.invalidateLayer(blk->layer);
            const auto t0 = std::chrono::steady_clock::now();
            doc.resolveForDrag({dragId});
            const auto t1 = std::chrono::steady_clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        blk->transform.origin = origin;
        doc.invalidateAllLayers();
        doc.resolveAll();
        qInfo().noquote() << QStringLiteral(
            "[A] block drag resolveForDrag: median %1 us, max %2 us")
            .arg(medianUs(us), 0, 'f', 1).arg(maxUs(us), 0, 'f', 1);
    }

    // ══ A2) every working block once — worst-case drag target ══
    {
        std::vector<double> medians;
        for (const auto& b : doc.blocks()) {
            if (b.layer == doc.auxLayerId()) continue;
            Block* blk = doc.findBlock(b.id);
            const Vec2 origin = blk->transform.origin;
            std::vector<double> us;
            for (int i = 0; i < 40; ++i) {
                blk->transform.origin = origin + Vec2(0.5 * i, 0.3 * i);
                doc.invalidateLayer(blk->layer);
                const auto t0 = std::chrono::steady_clock::now();
                doc.resolveForDrag({b.id});
                const auto t1 = std::chrono::steady_clock::now();
                us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            blk->transform.origin = origin;
            doc.invalidateAllLayers();
            doc.resolveAll();
            medians.push_back(medianUs(us));
        }
        std::sort(medians.begin(), medians.end());
        qInfo().noquote() << QStringLiteral(
            "[A2] per-block drag medians: min %1 us / mid %2 us / worst %3 us")
            .arg(medians.front(), 0, 'f', 1)
            .arg(medians[medians.size() / 2], 0, 'f', 1)
            .arg(medians.back(), 0, 'f', 1);
    }

    // ══ B) anchor (point) drag: pure solver cost ══
    {
        // Pick the first non-aux point of the seed block (updateAnchorDrag
        // converts whatever it grabs to Free — same semantics here).
        Block* blk = doc.findBlock(dragId);
        QUuid ptId;
        for (const auto& p : blk->points) {
            if (!p.isAuxiliary && p.constraint != PointConstraint::CurveAnchor) {
                ptId = p.id; break;
            }
        }
        QVERIFY(!ptId.isNull());
        ParamPoint* pt = blk->findPoint(ptId);
        const Vec2 origPos = pt->freePos;
        const auto origConstraint = pt->constraint;

        std::vector<double> us;
        us.reserve(kIters);
        for (int i = 0; i < kIters; ++i) {
            pt->freePos = origPos + Vec2(0.5 * i, 0.3 * i);
            pt->constraint = PointConstraint::Free;
            doc.invalidateLayer(blk->layer);
            const auto t0 = std::chrono::steady_clock::now();
            doc.resolveForDrag({dragId});
            const auto t1 = std::chrono::steady_clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        pt->freePos = origPos;
        pt->constraint = origConstraint;
        doc.invalidateAllLayers();
        doc.resolveAll();
        qInfo().noquote() << QStringLiteral(
            "[B] anchor drag resolveForDrag: median %1 us, max %2 us")
            .arg(medianUs(us), 0, 'f', 1).arg(maxUs(us), 0, 'f', 1);
    }

    // ══ A-full) full resolve comparison (what a measurement-driven
    //    upgrade-to-full costs) ══
    {
        Block* blk = doc.findBlock(dragId);
        const Vec2 origin = blk->transform.origin;
        std::vector<double> us;
        for (int i = 0; i < 40; ++i) {
            blk->transform.origin = origin + Vec2(0.5 * i, 0.3 * i);
            doc.invalidateAllLayers();
            const auto t0 = std::chrono::steady_clock::now();
            doc.resolveAll();
            const auto t1 = std::chrono::steady_clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        blk->transform.origin = origin;
        doc.invalidateAllLayers();
        doc.resolveAll();
        qInfo().noquote() << QStringLiteral(
            "[A-full] full resolveAll (all layers dirty): median %1 us")
            .arg(medianUs(us), 0, 'f', 1);
    }

    // ══ C) full pipeline: scene live-sync + software repaint ══
    {
        CanvasScene scene(&doc);
        // Items are created by blockAdded; after load the doc already has all
        // blocks, so recreate them explicitly.
        if (scene.findBlockItem(dragId) == nullptr) {
            for (const auto& b : doc.blocks())
                scene.addBlockItem(b.id);
        }
        scene.refreshAllBlockItems();

        QGraphicsView view(&scene);
        view.resize(1600, 1000);
        QPixmap pm(view.viewport()->size());

        Block* blk = doc.findBlock(dragId);
        const Vec2 origin = blk->transform.origin;

        std::vector<double> solveUs, syncUs, paintUs, frameUs;
        for (int i = 0; i < kIters; ++i) {
            blk->transform.origin = origin + Vec2(0.5 * i, 0.3 * i);
            doc.invalidateLayer(blk->layer);

            const auto f0 = std::chrono::steady_clock::now();
            doc.resolveForDrag({dragId});   // emits resolved -> scene sync
            const auto f1 = std::chrono::steady_clock::now();

            QPainter p(&pm);
            view.render(&p);                // full-viewport software repaint
            p.end();
            const auto f2 = std::chrono::steady_clock::now();

            const double solveAndSync =
                std::chrono::duration<double, std::micro>(f1 - f0).count();
            const double paint =
                std::chrono::duration<double, std::micro>(f2 - f1).count();
            solveUs.push_back(solveAndSync);
            paintUs.push_back(paint);
            frameUs.push_back(solveAndSync + paint);
        }
        blk->transform.origin = origin;

        qInfo().noquote() << QStringLiteral(
            "[C] live pipeline (scene sync + 1600x1000 software repaint)\n"
            "    resolve+sync: median %1 us (max %2)\n"
            "    repaint     : median %3 us (max %4)\n"
            "    frame total : median %5 us -> ~%6 fps")
            .arg(medianUs(solveUs), 0, 'f', 1).arg(maxUs(solveUs), 0, 'f', 1)
            .arg(medianUs(paintUs), 0, 'f', 1).arg(maxUs(paintUs), 0, 'f', 1)
            .arg(medianUs(frameUs), 0, 'f', 1)
            .arg(medianUs(frameUs) > 0 ? 1e6 / medianUs(frameUs) : 0.0, 0, 'f', 0);
    }

    qInfo().noquote() << QStringLiteral("[realdoc] done");
}

QTEST_MAIN(TestRealdocPerf)
#include "test_realdoc_perf.moc"
