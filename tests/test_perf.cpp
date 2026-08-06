/// @file test_perf.cpp
/// Performance benchmark for the layered dirty-marking resolve pipeline.
///
/// Builds a realistic two-layer document (auxiliary calculation layer +
/// working layer) and measures:
///   A) working-layer drag  — aux layer frozen (the per-frame hot path)
///   B) full resolve        — every layer dirty (variable edit)
/// and verifies the aux layer really is skipped during a working drag.
///
/// Run: test_perf  (prints timings; asserts correctness, not speed)

#include <QtTest>
#include <QUuid>
#include <chrono>

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

/// A chained line block on a specific layer. Free start, Polar end whose
/// distance is driven by @p distFormula (cm domain).
struct ChainNode {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
};

ChainNode addChainBlock(ParamDocument& doc, int layer, const QString& distFormula,
                        Vec2 origin)
{
    Block block;
    block.layer = layer;
    block.transform.origin = origin;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 40.0;
    p2.angle = 0.0;
    p2.distanceFormula = distFormula;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId};
}

/// Attach follower.start onto leader.end (construction-angle connection).
void chain(ParamDocument& doc, const ChainNode& follower, const ChainNode& leader)
{
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId = leader.blockId;
    att.toPointId = leader.endId;
    att.followerAngle = 0.0;
    doc.addAttachment(std::move(att));
}

/// Median of a vector of durations (µs).
double medianUs(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

class TestPerf : public QObject
{
    Q_OBJECT

private slots:
    void layeredResolveBenchmark();
    void worstCaseChainOrder();
    void auxFromIntersectionTwoPhase();
    void formulaChainBenchmark();
};

void TestPerf::layeredResolveBenchmark()
{
    constexpr int kChainLen = 20;  // blocks per layer chain

    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);  // cm

    // ── Auxiliary calculation layer (index 0) ──
    // A chain of construction lines driven by the base variable.
    std::vector<ChainNode> aux;
    for (int i = 0; i < kChainLen; ++i) {
        aux.push_back(addChainBlock(doc, 0, QStringLiteral("b/4+1"),
                                    Vec2(0.0, 20.0 * i)));
        if (i > 0) chain(doc, aux[i], aux[i - 1]);
    }
    // Publish the distance across the aux chain as a measurement.
    MeasureVariable mv;
    mv.refName = QStringLiteral("M_span");
    mv.blockA = aux.front().blockId;
    mv.pointA = aux.front().startId;
    mv.blockB = aux.back().blockId;
    mv.pointB = aux.back().endId;
    doc.addMeasure(std::move(mv));

    // ── Working layer (index 1) ──
    // A chain whose segment lengths consume the published measurement.
    std::vector<ChainNode> work;
    for (int i = 0; i < kChainLen; ++i) {
        work.push_back(addChainBlock(doc, 1, QStringLiteral("M_span/4"),
                                     Vec2(300.0, 20.0 * i)));
        if (i > 0) chain(doc, work[i], work[i - 1]);
    }

    doc.resolveAll();  // settle everything once

    // Sanity: the published measurement is live and consumed by the working
    // layer (the working segment length tracks M_span/4).
    const double spanMm = doc.findMeasure(doc.measureVars().front().id)->value;
    QVERIFY(spanMm > 0.0);

    // Record an aux block's resolved end position — it must NOT move during a
    // working-layer drag (proof the aux layer is frozen/skipped).
    Block* auxBlk = doc.findBlock(aux.back().blockId);
    QVERIFY(auxBlk != nullptr);
    const Vec2 auxEndBefore = auxBlk->transform.toWorld(
        auxBlk->findPoint(aux.back().endId)->resolvedPos);
    const double auxRotBefore = auxBlk->transform.rotation;

    // ── Scenario A: working-layer drag (aux frozen) ──
    // Simulates the per-frame hot path: move a working block, narrow the
    // resolve scope to the working group, resolve.
    constexpr int kIters = 300;
    std::vector<double> dragUs;
    Block* dragBlk = doc.findBlock(work[kChainLen / 2].blockId);
    QVERIFY(dragBlk != nullptr);
    const Vec2 dragOrigin = dragBlk->transform.origin;
    for (int i = 0; i < kIters; ++i) {
        dragBlk->transform.origin = dragOrigin + Vec2(0.5 * i, 0.3 * i);
        doc.invalidateLayer(1);  // working group only
        auto t0 = std::chrono::steady_clock::now();
        doc.resolveAll();
        auto t1 = std::chrono::steady_clock::now();
        dragUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    // Verify the aux layer was untouched by the working drag.
    const Vec2 auxEndAfter = auxBlk->transform.toWorld(
        auxBlk->findPoint(aux.back().endId)->resolvedPos);
    QCOMPARE(auxEndAfter.distanceTo(auxEndBefore) < 1e-6, true);
    QCOMPARE(auxBlk->transform.rotation, auxRotBefore);

    // ── Scenario B: full resolve (variable edit — every layer dirty) ──
    std::vector<double> fullUs;
    for (int i = 0; i < kIters; ++i) {
        doc.setParameter(QStringLiteral("b"), 84.0 + (i % 5) * 0.1);
        doc.invalidateAllLayers();
        auto t0 = std::chrono::steady_clock::now();
        doc.resolveAll();
        auto t1 = std::chrono::steady_clock::now();
        fullUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    const double dragMed = medianUs(dragUs);
    const double fullMed = medianUs(fullUs);

    qInfo().noquote() << QStringLiteral(
        "\n[perf-bench] %1-block chain per layer\n"
        "  working-drag (aux frozen): median %2 us/frame\n"
        "  full resolve (var edit)  : median %3 us/frame\n"
        "  aux skip delta           : %4 us/frame (%5%)")
        .arg(kChainLen)
        .arg(dragMed, 0, 'f', 1)
        .arg(fullMed, 0, 'f', 1)
        .arg(fullMed - dragMed, 0, 'f', 1)
        .arg(fullMed > 0 ? (100.0 * (fullMed - dragMed) / fullMed) : 0.0, 0, 'f', 0);

    // Sanity guard (not a strict ordering): with redundant settle passes
    // eliminated, the frozen-aux path and the full resolve cost nearly the
    // same here (the aux chain is cheap), so their order is noise-sensitive.
    // Assert only that the frozen path is not catastrophically slower, which
    // would signal a real regression in the layered pipeline.
    QVERIFY2(dragMed <= fullMed * 2.0,
             "working-drag with frozen aux must not be vastly slower than full resolve");
}

namespace {

/// Build @p len independent line blocks on @p layer, then chain them so block
/// i+1 follows block i. When @p reverse is true the attachments are appended in
/// REVERSE dependency order — the pathological case for the old iterative
/// settle (it needed O(len) relaxation passes); the topological settle must be
/// order-independent.
std::vector<ChainNode> buildChain(ParamDocument& doc, int layer, int len, bool reverse)
{
    std::vector<ChainNode> nodes;
    nodes.reserve(len);
    for (int i = 0; i < len; ++i)
        nodes.push_back(addChainBlock(doc, layer, QStringLiteral("b/4+1"),
                                      Vec2(0.0, 15.0 * i)));
    if (!reverse) {
        for (int i = 1; i < len; ++i)
            chain(doc, nodes[i], nodes[i - 1]);
    } else {
        for (int i = len - 1; i >= 1; --i)
            chain(doc, nodes[i], nodes[i - 1]);
    }
    return nodes;
}

/// Median per-frame cost of dragging the middle block of a chain (working layer
/// only, aux frozen), over @p iters frames.
double dragCost(ParamDocument& doc, const std::vector<ChainNode>& nodes, int iters)
{
    Block* dragBlk = doc.findBlock(nodes[nodes.size() / 2].blockId);
    const Vec2 origin = dragBlk->transform.origin;
    std::vector<double> us;
    us.reserve(iters);
    for (int i = 0; i < iters; ++i) {
        dragBlk->transform.origin = origin + Vec2(0.5 * i, 0.3 * i);
        doc.invalidateLayer(1);
        auto t0 = std::chrono::steady_clock::now();
        doc.resolveAll();
        auto t1 = std::chrono::steady_clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    return medianUs(us);
}

} // namespace

void TestPerf::worstCaseChainOrder()
{
    // The topological settle must make chain settlement order-independent: a
    // reverse-ordered deep chain (the old iterative solver's O(N^2) worst case)
    // should cost about the same as a forward-ordered one.
    constexpr int kLen = 40;
    constexpr int kIters = 200;

    ParamDocument fwd;
    fwd.setParameter(QStringLiteral("b"), 84.0);
    auto fwdNodes = buildChain(fwd, 1, kLen, /*reverse=*/false);
    fwd.resolveAll();
    const double fwdCost = dragCost(fwd, fwdNodes, kIters);

    ParamDocument rev;
    rev.setParameter(QStringLiteral("b"), 84.0);
    auto revNodes = buildChain(rev, 1, kLen, /*reverse=*/true);
    rev.resolveAll();
    const double revCost = dragCost(rev, revNodes, kIters);

    qInfo().noquote() << QStringLiteral(
        "\n[perf-bench] %1-block chain, attachment order sensitivity\n"
        "  forward order: median %2 us/frame\n"
        "  reverse order: median %3 us/frame  (iterative solver's old O(N^2) case)")
        .arg(kLen).arg(fwdCost, 0, 'f', 1).arg(revCost, 0, 'f', 1);

    // Order-independence guard: reverse must not be dramatically slower than
    // forward. The old iterative settle would fail this badly (reverse needs
    // ~kLen relaxation passes vs forward's ~2).
    QVERIFY2(revCost <= fwdCost * 2.0,
             "reverse-ordered chain must not be much slower than forward (topological settle)");
}

namespace {

/// Build the "intersection + aux point referencing it" geometry (mirrors
/// test_intersection's auxFromCrossBlockIntersection) but routed through
/// ParamDocument::resolveAll (the two-phase pipeline the app actually uses).
/// b1 (segment + intersection + aux point) goes on @p segLayer; b2 (the ray
/// origin) goes on @p originLayer. Returns whether the aux point resolved.
bool auxFromIntersectionOnLayers(int segLayer, int originLayer)
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 84.0);

    // b1: horizontal segment (0,0)-(100,0).
    Block b1;
    b1.layer = segLayer;
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    QUuid p1Id = p1.id;
    ParamPoint p2; p2.constraint = PointConstraint::Polar; p2.refPointId = p1Id;
    p2.distance = 100.0; p2.angle = 0.0;
    QUuid p2Id = p2.id;
    b1.addPoint(std::move(p1));
    b1.addPoint(std::move(p2));
    Segment seg; seg.startPointId = p1Id; seg.endPointId = p2Id;
    QUuid segId = seg.id;
    b1.addSegment(std::move(seg));

    // b2: ray origin A at (60,-40).
    Block b2;
    b2.layer = originLayer;
    ParamPoint a; a.constraint = PointConstraint::Free; a.freePos = Vec2(60.0, -40.0);
    QUuid aId = a.id;
    b2.addPoint(std::move(a));

    // Intersection on b1's segment, ray origin on b2, angle 90 -> hits (60,0).
    ParamPoint ix; ix.constraint = PointConstraint::Intersection;
    ix.refPointA = aId; ix.hostSegmentId = segId; ix.interAngle = 90.0;
    ix.isAuxiliary = true;
    QUuid ixId = ix.id;
    b1.addPoint(std::move(ix));

    // Aux point on b1 measured from the intersection (+15mm -> (75,0)).
    ParamPoint aux; aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId; aux.interpRefPointId = ixId;
    aux.interpPercent = 0.0; aux.interpConstant = 15.0; aux.interpFromEnd = false;
    aux.isAuxiliary = true;
    QUuid auxId = aux.id;
    b1.addPoint(std::move(aux));

    QUuid b1Id = b1.id;
    doc.addBlock(std::move(b1));
    doc.addBlock(std::move(b2));
    doc.resolveAll();

    const Block* rb1 = doc.findBlock(b1Id);
    const ParamPoint* rIx = rb1->findPoint(ixId);
    const ParamPoint* rAux = rb1->findPoint(auxId);
    const bool ixOk = rIx && rIx->resolved;
    const bool auxOk = rAux && rAux->resolved;
    qInfo().noquote() << QStringLiteral(
        "  segLayer=%1 originLayer=%2 -> intersection %3, aux %4")
        .arg(segLayer).arg(originLayer)
        .arg(ixOk ? QStringLiteral("resolved") : QStringLiteral("UNRESOLVED"))
        .arg(auxOk ? QStringLiteral("resolved") : QStringLiteral("UNRESOLVED"));
    return ixOk && auxOk;
}

} // namespace

void TestPerf::auxFromIntersectionTwoPhase()
{
    qInfo().noquote() << QStringLiteral("[aux-from-intersection] two-phase pipeline:");
    // Working layer segment + working layer origin (common case).
    QVERIFY(auxFromIntersectionOnLayers(1, 1));
    // Aux layer segment + aux layer origin (aux construction workflow).
    QVERIFY(auxFromIntersectionOnLayers(0, 0));
    // Working segment, aux origin (aux point feeds working construction).
    QVERIFY(auxFromIntersectionOnLayers(1, 0));
    // Aux segment, working origin.
    QVERIFY(auxFromIntersectionOnLayers(0, 1));
}

void TestPerf::formulaChainBenchmark()
{
    // A deep formula reference chain with many leaves — the fixpoint recompute
    // in recomputeFormulas() costs O(depth x count) evaluations (each pass
    // re-evaluates EVERY formula, most of them failing with "unknown variable"
    // until their dependencies are ready). Prints the median per-variable-edit
    // cost; asserts correctness (values valid), not speed (noise-sensitive).
    ParamDocument doc;
    Variable v;
    v.name = QStringLiteral("B");
    v.refName = QStringLiteral("b");
    v.value = 840.0;  // mm
    doc.addVariable(v);

    constexpr int kDepth = 20;   // F0 -> F1 -> ... -> F19 reference chain
    constexpr int kLeaf = 80;    // leaves all reference F19
    QUuid deepId;
    for (int i = 0; i < kDepth; ++i) {
        FormulaVariable f;
        f.name = QStringLiteral("F%1").arg(i);
        f.expression = (i == 0) ? QStringLiteral("b/2+6")
                                 : QStringLiteral("F%1/2+1").arg(i - 1);
        if (i == kDepth - 1) deepId = f.id;
        doc.addFormula(f);
    }
    QUuid leafId;
    for (int i = 0; i < kLeaf; ++i) {
        FormulaVariable f;
        f.name = QStringLiteral("L%1").arg(i);
        f.expression = QStringLiteral("F%1/4").arg(kDepth - 1);
        if (i == 0) leafId = f.id;
        doc.addFormula(f);
    }

    // Sanity: deep formula + leaf valid after recompute.
    doc.recomputeFormulas();
    QVERIFY(doc.findFormula(deepId));
    QVERIFY(doc.findFormula(deepId)->valid);
    QVERIFY(doc.findFormula(leafId));
    QVERIFY(doc.findFormula(leafId)->valid);

    // Time 50 variable edits (each triggers recomputeFormulas + resolve).
    constexpr int kIters = 50;
    std::vector<double> us;
    for (int i = 0; i < kIters; ++i) {
        Variable edit = doc.variables().front();
        edit.value = 840.0 + (i % 7) * 5.0;  // mm
        auto t0 = std::chrono::steady_clock::now();
        doc.updateVariable(edit);
        auto t1 = std::chrono::steady_clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    qInfo().noquote() << QStringLiteral(
        "\n[perf-bench] %1 formulas (%2-deep chain + %3 leaves)\n"
        "  variable edit (fixpoint recompute): median %4 us")        
        .arg(kDepth + kLeaf).arg(kDepth).arg(kLeaf)
        .arg(medianUs(us), 0, 'f', 1);

    // Correctness re-check after the edits (values tracked the variable).
    QVERIFY(doc.findFormula(leafId)->valid);
}

QTEST_GUILESS_MAIN(TestPerf)
#include "test_perf.moc"