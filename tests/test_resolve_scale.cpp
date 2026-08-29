/// @file test_resolve_scale.cpp
/// Solve-cost scaling benchmark (ARCHITECTURE_REVIEW P2-6).
///
/// The review flagged "single-threaded solving blocks the UI" as a risk but
/// explicitly asked for PerfProbe quantification BEFORE anyone reaches for a
/// worker thread. This benchmark supplies that number: it measures the two
/// resolve entry points that actually run on the GUI thread
///
///   A) drag frame — `invalidateLayer(work)` + `resolveForDrag(affected)`
///      (the per-frame hot path while dragging: aux layer frozen)
///   B) full resolve — `invalidateAllLayers()` + `resolveAll()`
///      (variable edit: every layer dirty, worst realistic case)
///
/// Canvas sync + paint sit ON TOP of these numbers (they are not measurable
/// without a live widget here); the reported figure is the solve alone.
///
/// across a range of document sizes, and prints a table with the 60 fps frame
/// budget (16.67 ms) as the reference line.
///
/// Assertions are CORRECTNESS only (geometry settles, aux really is frozen).
/// Timing is reported, never asserted on — a timing assertion would make this
/// test load-dependent and flaky (see ARCHITECTURE_REVIEW P2-3).
///
/// Run with GCAD_PROFILE=1 to additionally get the per-phase PerfProbe
/// breakdown (resolve.aux / resolve.work / meas.* / r.settle / ...).

#include <QtTest>
#include <QUuid>
#include <QList>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// 60 fps frame budget — the line a GUI-thread solve must stay under.
constexpr double kFrameBudgetUs = 16667.0;

QUuid layerIdAt(const ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

struct ChainNode {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
};

/// A two-point line block whose length is driven by @p distFormula (cm domain).
ChainNode addChainBlock(ParamDocument& doc, int layer, const QString& distFormula,
                        Vec2 origin)
{
    Block block;
    block.layer = layerIdAt(doc, layer);
    block.transform.origin = origin;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 40.0;
    p2.angle = 0.0;
    p2.distanceFormula = distFormula;
    const QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId};
}

void chain(ParamDocument& doc, const ChainNode& follower, const ChainNode& leader)
{
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId = leader.blockId;
    att.toPointId = leader.endId;
    att.followerAngle = 180.0;   // 闭合基准: 180° = 沿 leader 直行延续
    doc.addAttachment(std::move(att));
}

double medianUs(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(v.size() / 2)];
}

/// Build one scaled document: an aux chain that publishes a measurement, and a
/// working chain that consumes it — the dependency shape of a real pattern.
/// ParamDocument is a QObject (non-copyable, non-movable), so everything is
/// filled through out-parameters.
void buildDoc(ParamDocument& doc, std::vector<ChainNode>& aux,
              std::vector<ChainNode>& work, int chainLen)
{
    doc.setParameter(QStringLiteral("b"), 84.0);  // cm

    for (int i = 0; i < chainLen; ++i) {
        aux.push_back(addChainBlock(doc, 0, QStringLiteral("b/4+1"),
                                    Vec2(0.0, 20.0 * i)));
        if (i > 0) chain(doc, aux[i], aux[i - 1]);
    }
    MeasureVariable mv;
    mv.refName = QStringLiteral("M_span");
    mv.blockA = aux.front().blockId;
    mv.pointA = aux.front().startId;
    mv.blockB = aux.back().blockId;
    mv.pointB = aux.back().endId;
    doc.addMeasure(std::move(mv));

    for (int i = 0; i < chainLen; ++i) {
        work.push_back(addChainBlock(doc, 1, QStringLiteral("M_span/4"),
                                     Vec2(300.0, 20.0 * i)));
        if (i > 0) chain(doc, work[i], work[i - 1]);
    }
    doc.resolveAll();
}

struct Row {
    int chainLen      = 0;
    int blocks        = 0;
    double dragUs     = 0.0;   // scoped: aux frozen
    double fullUs     = 0.0;   // every layer dirty
};

/// NOTE: no QVERIFY in here — QVERIFY expands to `return;` and this helper
/// returns a value. Correctness is asserted by the calling test slots.
Row measure(int chainLen, int iters)
{
    ParamDocument doc;
    std::vector<ChainNode> aux;
    std::vector<ChainNode> work;
    buildDoc(doc, aux, work, chainLen);

    Row row;
    row.chainLen = chainLen;
    row.blocks = static_cast<int>(doc.blocks().size());

    const QUuid workLayer = layerIdAt(doc, 1);
    if (workLayer.isNull() || work.empty())
        return row;

    // ── A) drag frame (per-frame hot path, aux frozen) ──────────────────────
    // resolveForDrag is what a real drag calls every frame; affectedBlockIds
    // is the dragged closure (here: the whole working chain).
    QList<QUuid> affected;
    for (const ChainNode& n : work)
        affected.append(n.blockId);

    Block* dragBlk = doc.findBlock(work[static_cast<size_t>(chainLen) / 2].blockId);
    if (!dragBlk) return row;
    const Vec2 dragOrigin = dragBlk->transform.origin;
    std::vector<double> dragUs;
    dragUs.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        dragBlk->transform.origin = dragOrigin + Vec2(0.5 * (i % 40), 0.3 * (i % 40));
        doc.invalidateLayer(workLayer);
        const auto t0 = std::chrono::steady_clock::now();
        doc.resolveForDrag(affected);
        const auto t1 = std::chrono::steady_clock::now();
        dragUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    row.dragUs = medianUs(std::move(dragUs));

    // ── B) full resolve (variable edit — every layer dirty) ─────────────────
    std::vector<double> fullUs;
    fullUs.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        doc.setParameter(QStringLiteral("b"), 84.0 + (i % 5) * 0.1);
        doc.invalidateAllLayers();
        const auto t0 = std::chrono::steady_clock::now();
        doc.resolveAll();
        const auto t1 = std::chrono::steady_clock::now();
        fullUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    row.fullUs = medianUs(std::move(fullUs));
    return row;
}

} // namespace

class TestResolveScale : public QObject
{
    Q_OBJECT

private slots:
    void resolveCostScaling();
    void auxStaysFrozenDuringWorkingDrag();
};

void TestResolveScale::resolveCostScaling()
{
    // Chain length per layer; total blocks = 2x. 400/layer is already far
    // beyond a hand-drafted pattern (a real bodice front is well under 100).
    const int sizes[] = {10, 50, 200, 400};
    const int itersFor[] = {200, 150, 80, 30};

    std::vector<Row> rows;
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        rows.push_back(measure(sizes[s], itersFor[s]));

    QString table = QStringLiteral(
        "\n[resolve-scale] median us per resolve (60fps budget = 16667 us)\n"
        "  chain/layer | blocks | drag (aux frozen) | full (all dirty) | drag %budget");
    for (const Row& r : rows) {
        table += QStringLiteral("\n  %1 | %2 | %3 us | %4 us | %5%")
                     .arg(r.chainLen, 11)
                     .arg(r.blocks, 6)
                     .arg(r.dragUs, 17, 'f', 1)
                     .arg(r.fullUs, 16, 'f', 1)
                     .arg(100.0 * r.dragUs / kFrameBudgetUs, 0, 'f', 2);
    }
    qInfo().noquote() << table;

    // ── Correctness guard only ─────────────────────────────────────────────
    // A pathological (>100x) blow-up between successive sizes would mean the
    // solve stopped being near-linear and someone should look. Deliberately
    // loose: this must never fail because a CI machine was busy.
    for (size_t i = 1; i < rows.size(); ++i) {
        const double sizeRatio = static_cast<double>(rows[i].blocks)
                               / static_cast<double>(rows[i - 1].blocks);
        const double okCeiling = rows[i - 1].fullUs * sizeRatio * sizeRatio * 4.0 + 500.0;
        QVERIFY2(rows[i].fullUs < okCeiling,
                 qPrintable(QStringLiteral(
                     "full resolve at %1 blocks (%2 us) blew far past the "
                     "near-linear expectation from %3 blocks (%4 us)")
                     .arg(rows[i].blocks).arg(rows[i].fullUs, 0, 'f', 1)
                     .arg(rows[i - 1].blocks).arg(rows[i - 1].fullUs, 0, 'f', 1)));
    }
}

void TestResolveScale::auxStaysFrozenDuringWorkingDrag()
{
    // The layered dirty-marking cache is the reason a drag is cheaper than a
    // full resolve. If it ever regresses, scenario A silently degrades into
    // scenario B — assert the freeze still holds at a non-trivial size.
    ParamDocument doc;
    std::vector<ChainNode> aux;
    std::vector<ChainNode> work;
    buildDoc(doc, aux, work, 80);

    Block* auxBlk = doc.findBlock(aux.back().blockId);
    QVERIFY(auxBlk != nullptr);
    const Vec2 auxEndBefore = auxBlk->transform.toWorld(
        auxBlk->findPoint(aux.back().endId)->resolvedPos);

    Block* dragBlk = doc.findBlock(work.back().blockId);
    QVERIFY(dragBlk != nullptr);
    const Vec2 dragOrigin = dragBlk->transform.origin;
    for (int i = 0; i < 30; ++i) {
        dragBlk->transform.origin = dragOrigin + Vec2(2.0 * i, i);
        doc.invalidateLayer(layerIdAt(doc, 1));
        doc.resolveForDrag({dragBlk->id});
    }

    const Vec2 auxEndAfter = auxBlk->transform.toWorld(
        auxBlk->findPoint(aux.back().endId)->resolvedPos);
    QVERIFY2(auxEndAfter.distanceTo(auxEndBefore) < 1e-6,
             "aux layer must stay frozen during a working-layer drag");
    QVERIFY(doc.diagnostics().empty());
}

QTEST_GUILESS_MAIN(TestResolveScale)
#include "test_resolve_scale.moc"
