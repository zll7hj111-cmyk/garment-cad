/// @file test_aux_layer.cpp
/// Verifies auxiliary-layer rendering: with the auxiliary layer ACTIVE, an
/// auxiliary point (green disc) on an aux-layer segment must be rendered;
/// with a working layer active the aux layer renders GRAYED (visible draft,
/// but never a snap target).

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QPainter>
#include <QPixmap>
#include <QImage>

#include <cmath>

#include <QUndoStack>

#include "canvas/BlockItem.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "parametric/ParamDocument.h"
#include "tools/SnapEngine.h"
#include "tools/ToolManager.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::tools::SnapEngine;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Block: Free start + Polar end (horizontal, 100 mm) + one Interpolated
/// auxiliary point at 50% on the segment. Added to @p doc on @p layerIndex.
QUuid makeAuxLineBlock(ParamDocument& doc, int layerIndex)
{
    Block block;
    block.layer = layerIdAt(doc, layerIndex);
    block.transform.origin = Vec2::zero();
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 100.0;
    p2.angle = 0.0;
    const QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    const QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.isAuxiliary = true;
    aux.visible = true;
    aux.interpPercent = 0.5;
    const QUuid auxId = aux.id;
    block.addPoint(std::move(aux));
    block.segments.back().auxPointIds.push_back(auxId);

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return blockId;
}

/// Render the scene into a pixmap and count pixels exactly matching the
/// auxiliary-point color (read from the scene's CanvasStyle tokens so the
/// test survives palette changes).
int countAuxGreenPixels(CanvasScene& scene)
{
    const QColor aux = scene.style()->pointColor(EntityState::Normal, true);

    QGraphicsView view(&scene);
    view.resize(600, 400);
    scene.setSceneRect(-50, -100, 300, 300);

    QPixmap pm(view.viewport()->size());
    QPainter p(&pm);
    view.render(&p);
    p.end();

    const QImage img = pm.toImage();
    int green = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb rgb = img.pixel(x, y);
            if (qRed(rgb) == aux.red() && qGreen(rgb) == aux.green() && qBlue(rgb) == aux.blue())
                ++green;
        }
    }
    return green;
}

/// Count pixels that differ from the view background (token-driven) — any
/// non-background pixel proves SOMETHING was painted (e.g. the grayed
/// auxiliary draft), regardless of its exact color.
int countNonBackgroundPixels(CanvasScene& scene)
{
    const QColor bg = scene.style()->canvasBackground;

    QGraphicsView view(&scene);
    view.resize(600, 400);
    scene.setSceneRect(-50, -100, 300, 300);

    QPixmap pm(view.viewport()->size());
    QPainter p(&pm);
    view.render(&p);
    p.end();

    const QImage img = pm.toImage();
    int nonBg = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb rgb = img.pixel(x, y);
            if (qRed(rgb) != bg.red() || qGreen(rgb) != bg.green() || qBlue(rgb) != bg.blue())
                ++nonBg;
        }
    }
    return nonBg;
}

/// Handle set for a plain two-point line block (Free start + Polar end).
struct LineRef { QUuid blockId; QUuid startId; QUuid endId; };

/// Handle set for a bridge block (two endpoints + one aux point at 50%).
struct BridgeRef { QUuid blockId; QUuid startId; QUuid endId; QUuid auxId; };

/// Bridge block (Block::isBridge): Free start + Polar end + Interpolated
/// auxiliary point on the segment — the aux point may anchor followers.
BridgeRef makeBridgeBlock(ParamDocument& doc, int layerIndex, Vec2 origin)
{
    Block block;
    block.isBridge = true;
    block.layer = layerIdAt(doc, layerIndex);
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 100.0;
    p2.angle = 0.0;
    const QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    const QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.isAuxiliary = true;
    aux.visible = true;
    aux.interpPercent = 0.5;
    const QUuid auxId = aux.id;
    block.addPoint(std::move(aux));
    block.segments.back().auxPointIds.push_back(auxId);

    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, auxId};
}

/// Block: Free start at local origin + Polar end (length @p lengthMm,
/// direction @p angleDeg), one segment. Added to @p doc on @p layerIndex.
LineRef makeLineBlock(ParamDocument& doc, int layerIndex, Vec2 origin,
                      double lengthMm, double angleDeg = 0.0)
{
    Block block;
    block.layer = layerIdAt(doc, layerIndex);
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lengthMm;
    p2.angle = angleDeg;
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

} // namespace

class TestAuxLayer : public QObject
{
    Q_OBJECT

private slots:
    void auxPointResolved();
    void auxPointRenderedWhenAuxLayerActive();
    void auxLayerGrayedWhenWorkingLayerActive();
    void auxLayerNotSnappableWhenWorkingLayerActive();
    void auxActiveSnapsWorkingPoints();
    void snapTieBreaksToActiveLayer();   // 同点堆叠时活动层优先
    void auxPointAddedAfterBlockRenders();   // AddAuxPointCommand flow
    void auxPointSurvivesLayerSwitch();
    // ── One-way cross-layer attachment (aux follower → working leader) ──
    void auxFollowerTracksWorkingLeader();
    void workingFollowerAuxLeaderRejected();
    void phase3MatchesFullScopeAll();
    void valueCycleRejectedAtAddAttachment();
    void valueCycleThroughBridgeLeaderRejected();
    void noCrossLayerDocumentUnchanged();
    void snapCandidatesOverlapDetected();
    void snapCoordCacheInvalidatesOnChange();   // SnapEngine 坐标缓存 (2026-09)
    // ── 锁定连接 (锁定 = 焊接) ──
    void auxConnectionDefaultWelded();
    void workingConnectionDefaultsWelded();
    void lockedClosureWeldsChain();
    void lockedDragMovesWholePair();
    void dragLeaderKeepsFollower();
};

void TestAuxLayer::auxPointAddedAfterBlockRenders()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);

    // Step 1: block WITHOUT the aux point (normal draw flow).
    const QUuid blockId = makeAuxLineBlock(doc, 0);
    const Block* b = doc.findBlock(blockId);
    QVERIFY(b);
    // Remove the aux point so the block starts as a plain line.
    const QUuid segId = b->segments.front().id;
    const QUuid auxId = b->points.back().id;
    {
        auto& pts = doc.findBlock(blockId)->points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [&](const ParamPoint& p) { return p.id == auxId; }), pts.end());
        auto& ids = doc.findBlock(blockId)->segments.front().auxPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), auxId), ids.end());
        doc.findBlock(blockId)->rebuildPointIndex();
    }
    doc.resolveAll();
    QCOMPARE(countAuxGreenPixels(scene), 0);  // plain line, no aux point yet

    // Step 2: AddAuxPointCommand flow — add the Interpolated aux point later.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.isAuxiliary = true;
    aux.visible = true;
    aux.interpPercent = 0.5;
    {
        auto* blk = doc.findBlock(blockId);
        blk->addPoint(aux);
        blk->segments.front().auxPointIds.push_back(aux.id);
    }
    doc.resolveAll();  // emits resolved -> CanvasScene::syncBlockPositions

    const int green = countAuxGreenPixels(scene);
    QVERIFY2(green > 0,
             "aux point added AFTER block creation is not rendered (BUG)");
}

void TestAuxLayer::auxPointSurvivesLayerSwitch()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeAuxLineBlock(doc, 0);
    doc.resolveAll();

    // Toggle active layer 0 -> 1 -> 0; the aux point must reappear each time.
    for (int i = 0; i < 3; ++i) {
        doc.setActiveLayer(layerIdAt(doc, 1));
        doc.setActiveLayer(layerIdAt(doc, 0));
        QVERIFY2(countAuxGreenPixels(scene) > 0,
                 "aux point missing after layer switch (BUG)");
    }
}

void TestAuxLayer::auxPointResolved()
{
    ParamDocument doc;
    const QUuid blockId = makeAuxLineBlock(doc, 0);  // aux layer = index 0
    doc.setActiveLayer(layerIdAt(doc, 0));
    doc.resolveAll();

    const Block* b = doc.findBlock(blockId);
    QVERIFY(b);
    int auxCount = 0, resolvedCount = 0;
    for (const auto& pt : b->points) {
        if (pt.isAuxiliary) {
            ++auxCount;
            if (pt.resolved) ++resolvedCount;
        }
    }
    QCOMPARE(auxCount, 1);
    QCOMPARE(resolvedCount, 1);
}

void TestAuxLayer::auxPointRenderedWhenAuxLayerActive()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);  // must exist BEFORE addBlock (blockAdded signal)
    const QUuid blockId = makeAuxLineBlock(doc, 0);
    doc.resolveAll();

    QVERIFY(scene.findBlockItem(blockId) != nullptr);

    const int green = countAuxGreenPixels(scene);
    QVERIFY2(green > 0,
             "aux layer active: no green aux-point pixels rendered (BUG)");
}

void TestAuxLayer::auxLayerGrayedWhenWorkingLayerActive()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    CanvasScene scene(&doc);
    makeAuxLineBlock(doc, 0);
    doc.setActiveLayer(layerIdAt(doc, 1));  // working layer
    doc.resolveAll();

    // The aux layer is GRAYED, not hidden: no green (the draft's identity
    // color is suppressed), but the draft geometry is still painted.
    QCOMPARE(countAuxGreenPixels(scene), 0);        // not full color
    QVERIFY(countNonBackgroundPixels(scene) > 0);   // visible grayed draft
}

void TestAuxLayer::auxLayerNotSnappableWhenWorkingLayerActive()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    makeAuxLineBlock(doc, 0);
    doc.resolveAll();

    // The aux point sits at world (50, 0) (block origin (0,0), 50% of a
    // horizontal 100 mm segment).
    const Vec2 auxWorld(50.0, 0.0);

    // Aux layer ACTIVE → the point is a snap target.
    cad::tools::SnapEngine snap;
    QVERIFY(snap.findSnap(auxWorld, &doc, 1.0).has_value());

    // Working layer ACTIVE → the grayed draft must NOT be snappable
    // (cross-group attachments are rejected by addAttachment; a snap target
    // that cannot connect would be an interaction trap).
    doc.setActiveLayer(layerIdAt(doc, 1));
    doc.resolveAll();
    QVERIFY(!snap.findSnap(auxWorld, &doc, 1.0).has_value());
}

void TestAuxLayer::auxActiveSnapsWorkingPoints()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));  // aux layer ACTIVE (the H-toggle target state)
    const LineRef w = makeLineBlock(doc, 1, Vec2(100.0, 50.0), 80.0);  // working leader
    doc.resolveAll();

    cad::tools::SnapEngine snap;
    // Point snap on the grayed working block — both endpoints must be
    // targets while the aux layer is active (the legal connection direction).
    const auto s1 = snap.findSnap(Vec2(100.0, 50.0), &doc, 1.0);
    QVERIFY2(s1.has_value(),
             "aux active: working-layer point must be a snap target");
    QCOMPARE(s1->blockId, w.blockId);
    const auto s2 = snap.findSnap(Vec2(180.0, 50.0), &doc, 1.0);
    QVERIFY(s2.has_value());
    QCOMPARE(s2->pointId, w.endId);

    // Segment-body snap on the working block.
    const auto seg = snap.findSegmentSnap(Vec2(140.0, 50.0), &doc, 1.0);
    QVERIFY2(seg.has_value(),
             "aux active: working-layer segment must be a snap target");
    QCOMPARE(seg->blockId, w.blockId);

    // Full smart-pen flow: an aux follower attached to the working leader
    // (one-way rule) — it must TRACK the leader's movement afterwards.
    const LineRef f = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 60.0);  // aux follower
    Attachment att;
    att.fromBlockId = f.blockId;
    att.fromPointId = f.startId;
    att.toBlockId = w.blockId;
    att.toPointId = w.endId;
    QVERIFY2(doc.addAttachment(att),
             "aux follower → working leader must be accepted");
    QVERIFY(doc.hasCrossLayerAttachments());
    doc.resolveAll();
    const Vec2 leaderEnd = doc.findBlock(w.blockId)->worldPos(w.endId);
    QVERIFY(doc.findBlock(f.blockId)->worldPos(f.startId)
                .distanceTo(leaderEnd) < 1e-6);
}

// ═════════════════════════════════════════════════════════════════════
// Coincident-point tie-break: at an EXACT stack the ACTIVE layer wins
// ═════════════════════════════════════════════════════════════════════

void TestAuxLayer::snapTieBreaksToActiveLayer()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));  // aux layer ACTIVE
    // Working leader FIRST, then an aux line STARTING on the working start:
    // an exact coincidence — pure nearest-distance would pick the
    // traversal-order first-comer (the working block, created first).
    const LineRef w = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 100.0);  // (0,0)→(100,0)
    const LineRef a = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 60.0, 90.0);  // (0,0)→(0,60)
    doc.resolveAll();

    cad::tools::SnapEngine snap;
    const auto s = snap.findSnap(Vec2(0.0, 0.0), &doc, 1.0);
    QVERIFY(s.has_value());
    QCOMPARE(s->blockId, a.blockId);   // exact stack → ACTIVE (aux) layer wins
    QCOMPARE(s->pointId, a.startId);

    // The candidates API still reports BOTH stacked points — the smart pen's
    // confirm/switch flow needs the full pool.
    const auto cands = snap.findSnapCandidates(Vec2(0.0, 0.0), &doc, 1.0);
    QCOMPARE(cands.size(), static_cast<size_t>(2));

    // Working layer ACTIVE → the grayed aux point is not snappable; the
    // working point is the single candidate (one-way rule unchanged).
    doc.setActiveLayer(layerIdAt(doc, 1));
    doc.resolveAll();
    const auto s2 = snap.findSnap(Vec2(0.0, 0.0), &doc, 1.0);
    QVERIFY(s2.has_value());
    QCOMPARE(s2->blockId, w.blockId);
    QCOMPARE(s2->pointId, w.startId);
    const auto cands2 = snap.findSnapCandidates(Vec2(0.0, 0.0), &doc, 1.0);
    QCOMPARE(cands2.size(), static_cast<size_t>(1));
}

// ═════════════════════════════════════════════════════════════════════
// One-way cross-layer attachment (aux follower → working leader)
// ═════════════════════════════════════════════════════════════════════

void TestAuxLayer::auxFollowerTracksWorkingLeader()
{
    ParamDocument doc;
    const LineRef w = makeLineBlock(doc, 1, Vec2(100.0, 50.0), 80.0);  // working leader
    const LineRef f = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 60.0);     // aux follower

    Attachment att;
    att.fromBlockId = f.blockId;
    att.fromPointId = f.startId;
    att.toBlockId = w.blockId;
    att.toPointId = w.endId;
    att.followerAngle = 0.0;
    QVERIFY2(doc.addAttachment(att), "aux follower → working leader must be accepted");
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(1));
    QVERIFY(doc.hasCrossLayerAttachments());

    // addAttachment() resolves internally — the aux follower's snapped point
    // must already sit on the working leader's endpoint (Phase 3 settle).
    const Vec2 leaderEnd = doc.findBlock(w.blockId)->worldPos(w.endId);
    const Vec2 followerStart = doc.findBlock(f.blockId)->worldPos(f.startId);
    QVERIFY(followerStart.distanceTo(leaderEnd) < 1e-6);

    // Move the working leader → full resolve: the aux follower must follow.
    doc.findBlock(w.blockId)->transform.origin = Vec2(150.0, 90.0);
    doc.invalidateAllLayers();
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());
    QVERIFY(doc.findBlock(f.blockId)->worldPos(f.startId)
                .distanceTo(doc.findBlock(w.blockId)->worldPos(w.endId)) < 1e-6);

    // Move again through the DRAG path with a narrow working-layer seed:
    // resolveForDrag must settle the aux follower within the SAME frame.
    doc.findBlock(w.blockId)->transform.origin = Vec2(10.0, -40.0);
    doc.invalidateLayer(layerIdAt(doc, 1));  // tool annotates only the working layer
    doc.resolveForDrag({w.blockId});
    QVERIFY(doc.findBlock(f.blockId)->worldPos(f.startId)
                .distanceTo(doc.findBlock(w.blockId)->worldPos(w.endId)) < 1e-6);
}

void TestAuxLayer::workingFollowerAuxLeaderRejected()
{
    ParamDocument doc;
    const LineRef a = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 100.0);   // aux leader
    const LineRef w = makeLineBlock(doc, 1, Vec2(200.0, 0.0), 60.0);  // working follower

    Attachment att;
    att.fromBlockId = w.blockId;  // working follower
    att.fromPointId = w.startId;
    att.toBlockId = a.blockId;    // aux leader — forbidden direction
    att.toPointId = a.endId;
    QVERIFY2(!doc.addAttachment(att), "working follower → aux leader must be rejected");
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(0));
    QVERIFY(!doc.hasCrossLayerAttachments());
}

void TestAuxLayer::phase3MatchesFullScopeAll()
{
    ParamDocument doc;
    const LineRef w = makeLineBlock(doc, 1, Vec2(40.0, 30.0), 80.0);
    const LineRef f = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 60.0);

    Attachment att;
    att.fromBlockId = f.blockId;
    att.fromPointId = f.startId;
    att.toBlockId = w.blockId;
    att.toPointId = w.endId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));

    // Move the leader, let the phased pipeline (Phase 1/2/3) settle.
    doc.findBlock(w.blockId)->transform.origin = Vec2(120.0, -25.0);
    doc.invalidateAllLayers();
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // The phased result must be a fixpoint: one conservative Scope::All pass
    // (legacy monolithic resolve) must not move ANY block or point. The pass
    // runs on a COPY — the facade's block container is read-only.
    const std::vector<Block> snapshot = doc.blocks();
    std::vector<Block> candidate = doc.blocks();
    std::vector<ResolveDiagnostic> diag;
    const QHash<QString, QList<Condition>> noCond;
    Resolver::resolveAll(candidate, doc.attachments(), doc.parameters(),
                         noCond, &diag, Resolver::Scope::All, QUuid(), nullptr);
    QVERIFY(diag.empty());
    QCOMPARE(candidate.size(), snapshot.size());
    for (const Block& before : snapshot) {
        const Block* after = nullptr;
        for (const Block& cb : candidate)
            if (cb.id == before.id) { after = &cb; break; }
        QVERIFY(after != nullptr);
        QVERIFY(std::abs(after->transform.origin.x - before.transform.origin.x) < 1e-6);
        QVERIFY(std::abs(after->transform.origin.y - before.transform.origin.y) < 1e-6);
        QVERIFY(std::abs(after->transform.rotation - before.transform.rotation) < 1e-9);
        QCOMPARE(after->points.size(), before.points.size());
        for (size_t i = 0; i < before.points.size(); ++i) {
            QVERIFY(after->points[i].resolvedPos
                        .distanceTo(before.points[i].resolvedPos) < 1e-6);
        }
    }
}

void TestAuxLayer::valueCycleRejectedAtAddAttachment()
{
    ParamDocument doc;
    const LineRef s = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 100.0);    // aux measurement source
    const LineRef w = makeLineBlock(doc, 1, Vec2(200.0, 0.0), 50.0);   // working consumer
    doc.findBlock(w.blockId)->segments.front().lengthFormula = QStringLiteral("M_cyc");

    MeasureVariable mv;
    mv.refName = QStringLiteral("M_cyc");
    mv.blockA = s.blockId; mv.pointA = s.startId;
    mv.blockB = s.blockId; mv.pointB = s.endId;
    doc.addMeasure(mv);

    // Hanging the measurement SOURCE beneath its CONSUMER closes a value
    // cycle (consumer pose → M_cyc → consumer pose) → must be rejected.
    Attachment att;
    att.fromBlockId = s.blockId;
    att.fromPointId = s.startId;
    att.toBlockId = w.blockId;
    att.toPointId = w.endId;
    QVERIFY2(!doc.addAttachment(att), "value cycle through M_cyc must be rejected");
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(0));
    QVERIFY(!doc.hasCrossLayerAttachments());

    // An unrelated aux block (not a measurement source) still attaches fine.
    const LineRef t = makeLineBlock(doc, 0, Vec2(0.0, 120.0), 40.0);
    Attachment attT;
    attT.fromBlockId = t.blockId;
    attT.fromPointId = t.startId;
    attT.toBlockId = w.blockId;
    attT.toPointId = w.endId;
    QVERIFY(doc.addAttachment(attT));

    // Removing the measurement dissolves the cycle → the edge becomes legal.
    doc.removeMeasure(mv.id);
    QVERIFY(doc.addAttachment(att));
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(2));
}

void TestAuxLayer::valueCycleThroughBridgeLeaderRejected()
{
    ParamDocument doc;
    const LineRef h1 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 80.0);    // pin host + consumer
    const LineRef h2 = makeLineBlock(doc, 1, Vec2(0.0, 150.0), 80.0);  // pin host
    doc.findBlock(h1.blockId)->segments.front().lengthFormula = QStringLiteral("M_br");

    // Bridge pinned to both hosts — its pose is driven purely by PIN edges.
    const BridgeRef br = makeBridgeBlock(doc, 1, Vec2(10.0, 60.0));
    Attachment pinA;
    pinA.isPin = true;
    pinA.fromBlockId = br.blockId; pinA.fromPointId = br.startId;
    pinA.toBlockId = h1.blockId;  pinA.toPointId = h1.startId;
    QVERIFY(doc.addAttachment(pinA));
    Attachment pinB;
    pinB.isPin = true;
    pinB.fromBlockId = br.blockId; pinB.fromPointId = br.endId;
    pinB.toBlockId = h2.blockId;  pinB.toPointId = h2.startId;
    QVERIFY(doc.addAttachment(pinB));

    // Aux measurement source.
    const LineRef s = makeLineBlock(doc, 0, Vec2(0.0, -100.0), 60.0);
    MeasureVariable mv;
    mv.refName = QStringLiteral("M_br");
    mv.blockA = s.blockId; mv.pointA = s.startId;
    mv.blockB = s.blockId; mv.pointB = s.endId;
    doc.addMeasure(mv);

    // Candidate: the aux source follows an AUX point of the bridge. The
    // bridge's pose rides on h1/h2 through PIN edges — h1 consumes M_br
    // while its source S would hang beneath h1 via the bridge → value
    // cycle. The guard must walk pin edges when collecting ancestors.
    Attachment att;
    att.fromBlockId = s.blockId;
    att.fromPointId = s.startId;
    att.toBlockId = br.blockId;
    att.toPointId = br.auxId;
    QVERIFY2(!doc.addAttachment(att),
             "value cycle through a bridge leader (pin edges) must be rejected");
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(2));  // only the two pins

    // Without the measurement there is no cycle → the same edge is legal.
    doc.removeMeasure(mv.id);
    QVERIFY(doc.addAttachment(att));
    QCOMPARE(doc.attachments().size(), static_cast<size_t>(3));
}

void TestAuxLayer::noCrossLayerDocumentUnchanged()
{
    ParamDocument doc;
    // Same-layer working chain + an unrelated aux block: the document never
    // gains a cross-layer edge, so behaviour must match the pre-change model
    // (narrowed drag, frozen aux layer, zero diagnostics).
    const LineRef w1 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 100.0);
    const LineRef w2 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 60.0);
    Attachment att;
    att.fromBlockId = w2.blockId;
    att.fromPointId = w2.startId;
    att.toBlockId = w1.blockId;
    att.toPointId = w1.endId;
    QVERIFY(doc.addAttachment(att));
    QVERIFY(!doc.hasCrossLayerAttachments());

    const LineRef auxB = makeLineBlock(doc, 0, Vec2(500.0, 500.0), 30.0);

    // Narrowed drag of the chain root: the follower tracks, everything else
    // — especially the untouched aux block — stays frozen.
    doc.findBlock(w1.blockId)->transform.origin = Vec2(40.0, 20.0);
    doc.invalidateLayer(layerIdAt(doc, 1));
    doc.resolveForDrag({w1.blockId});
    QVERIFY(doc.diagnostics().empty());

    QVERIFY(doc.findBlock(w2.blockId)->worldPos(w2.startId)
                .distanceTo(doc.findBlock(w1.blockId)->worldPos(w1.endId)) < 1e-6);
    const Block* frozenAux = doc.findBlock(auxB.blockId);
    QCOMPARE(frozenAux->transform.origin.x, 500.0);
    QCOMPARE(frozenAux->transform.origin.y, 500.0);
}

// ---------------------------------------------------------------------------
// findSnapCandidates: when several blocks' endpoints stack at ONE spot, the
// connect gesture must see ALL of them (overlapping candidates) instead of
// silently picking the traversal order (the basis of the ConfirmTarget
// leader-selection flow).
// ---------------------------------------------------------------------------
void TestAuxLayer::snapCandidatesOverlapDetected()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));

    // Block A: start at world (50,0), extends +X.
    Block a;
    a.layer = layerIdAt(doc, 0);
    a.transform.origin = Vec2(50.0, 0.0);
    ParamPoint a1;
    a1.constraint = PointConstraint::Free;
    a1.freePos = Vec2::zero();
    const QUuid aStart = a1.id;
    ParamPoint a2;
    a2.constraint = PointConstraint::Polar;
    a2.refPointId = aStart;
    a2.distance = 80.0;
    a2.angle = 0.0;
    const QUuid aEnd = a2.id;
    a.addPoint(std::move(a1));
    a.addPoint(std::move(a2));
    Segment as;
    as.startPointId = aStart;
    as.endPointId = aEnd;
    a.addSegment(std::move(as));
    const QUuid aId = a.id;
    doc.addBlock(std::move(a));

    // Block B: start at the SAME world spot, extends +Y.
    Block b;
    b.layer = layerIdAt(doc, 0);
    b.transform.origin = Vec2(50.0, 0.0);
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    const QUuid bStart = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = bStart;
    b2.distance = 60.0;
    b2.angle = 90.0;
    const QUuid bEnd = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = bStart;
    bs.endPointId = bEnd;
    b.addSegment(std::move(bs));
    const QUuid bId = b.id;
    doc.addBlock(std::move(b));
    doc.resolveAll();

    SnapEngine eng;
    const Vec2 cursor(50.0, 0.0);

    // Nearest-only snap still returns exactly ONE point.
    const auto single = eng.findSnap(cursor, &doc, 1.0, 12.0);
    QVERIFY(single.has_value());

    // The candidate set exposes BOTH stacked endpoints.
    const auto cands = eng.findSnapCandidates(cursor, &doc, 1.0, 12.0);
    QVERIFY2(cands.size() >= 2,
             qPrintable(QStringLiteral("expected >=2 candidates, got %1")
                        .arg(cands.size())));
    bool hasA = false, hasB = false;
    for (const auto& c : cands) {
        QVERIFY2(c.worldPos.distanceTo(cursor) < 0.5,
                 qPrintable(QStringLiteral("candidate off-spot: %1")
                            .arg(c.worldPos.x)));
        if (c.blockId == aId && c.pointId == aStart) hasA = true;
        if (c.blockId == bId && c.pointId == bStart) hasB = true;
    }
    QVERIFY(hasA && hasB);
}

// SnapEngine 每块坐标缓存 (2026-09 性能专项): 缓存键 = (geometryEpoch,
// transform, count)。刚体平移 (transform 键) 与点位变化 (epoch 键) 后必须
// 立即失效, 缓存对结果完全透明 (多次查询 / 变化前后一致)。
void TestAuxLayer::snapCoordCacheInvalidatesOnChange()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const QUuid a = makeAuxLineBlock(doc, 1);   // (0,0)-(100,0) + 50% 辅助点
    doc.resolveAll();

    Block* b = doc.findBlock(a);
    QVERIFY(b);
    const ParamPoint* aux = nullptr;
    for (const auto& pt : b->points)
        if (pt.constraint == PointConstraint::Interpolated) aux = &pt;
    QVERIFY(aux);
    const QUuid auxId = aux->id;

    SnapEngine eng;

    // 0) 基线: 光标正好在 50% 辅助点上.
    const auto hit0 = eng.findSnap({50.0, 0.0}, &doc, 1.0, 12.0);
    QVERIFY(hit0.has_value() && hit0->pointId == auxId);
    QVERIFY(hit0->worldPos.distanceTo(Vec2(50.0, 0.0)) < 1e-6);

    // 1) 纯刚体平移 (原点不 bump epoch — 缓存必须按 transform 键失效).
    b->transform.origin = {10.0, 0.0};
    doc.resolveAll();
    const auto hit1 = eng.findSnap({60.0, 0.0}, &doc, 1.0, 12.0);
    QVERIFY(hit1.has_value() && hit1->pointId == auxId);
    QVERIFY(hit1->worldPos.distanceTo(Vec2(60.0, 0.0)) < 1e-6);

    // 2) 点位变化 (aux 50% → 80%, epoch bump) — 旧位置不再捕捉、新位置捕捉.
    //    注意: 块已平移 (原点 10,0), 段 = (10,0)-(110,0) → 80% = x=90.
    if (auto* auxm = b->findPoint(auxId)) auxm->interpPercent = 0.8;
    doc.resolveAll();
    const auto hit80 = eng.findSnap({90.0, 0.0}, &doc, 1.0, 12.0);
    QVERIFY(hit80.has_value() && hit80->pointId == auxId);
    QVERIFY(hit80->worldPos.distanceTo(Vec2(90.0, 0.0)) < 1e-6);
    const auto hit50 = eng.findSnap({50.0, 0.0}, &doc, 1.0, 12.0);
    QVERIFY(!hit50.has_value() || hit50->pointId != auxId);

    // 3) 线段缓存: 平移后线身投影跟随 (世界端点缓存按 transform 键).
    const auto segHit = eng.findSegmentSnap({50.0, 5.0}, &doc, 1.0, 12.0);
    QVERIFY(segHit.has_value());
    QVERIFY(segHit->worldPos.distanceTo(Vec2(50.0, 0.0)) < 1e-6);
}

// ---------------------------------------------------------------------------
// 锁定连接 (锁定 = 焊接): aux-layer connections lock automatically; the
// locked closure welds both sides into one drag set; dragging either side
// moves the whole pair and NEVER tears the connection; dragging a plain
// LEADER keeps its follower attached (方向感知拆除).
// ---------------------------------------------------------------------------

void TestAuxLayer::auxConnectionDefaultWelded()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));
    const LineRef aux = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 80.0);
    const LineRef w   = makeLineBlock(doc, 1, Vec2(200.0, 0.0), 50.0);

    // Cross-layer (aux follower → working leader): 新建连接默认勾选「拖动保护」
    // (用户拍板 2026-08 复旧; 2026-10 曾改为可选后按用户要求回滚).
    Attachment cross;
    cross.fromBlockId = aux.blockId;
    cross.fromPointId = aux.startId;
    cross.toBlockId   = w.blockId;
    cross.toPointId   = w.startId;
    QVERIFY(doc.addAttachment(cross));
    const auto& atts = doc.attachments();
    QVERIFY(atts.size() == 1);
    QVERIFY2(atts[0].isLocked, "aux-layer connection must default welded");
    // 面板取消拖动保护 = 解焊 (连接保持完整).
    doc.setAttachmentLocked(atts[0].id, false);
    QVERIFY(!doc.attachments()[0].isLocked);

    // Same-layer aux-internal connection: also welded by default.
    const LineRef aux2 = makeLineBlock(doc, 0, Vec2(0.0, 150.0), 40.0);
    Attachment inner;
    inner.fromBlockId = aux2.blockId;
    inner.fromPointId = aux2.startId;
    inner.toBlockId   = aux.blockId;
    inner.toPointId   = aux.endId;
    QVERIFY(doc.addAttachment(inner));
    QVERIFY(doc.attachments().size() == 2);
    QVERIFY2(doc.attachments()[1].isLocked, "aux-internal connection must default welded");
    doc.setAttachmentLocked(doc.attachments()[1].id, false);
    QVERIFY(!doc.attachments()[1].isLocked);  // 解焊仍完整连接
}

void TestAuxLayer::workingConnectionDefaultsWelded()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineRef w1 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 100.0);
    const LineRef w2 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 60.0);

    // Working-layer connection: 新建连接默认勾选「拖动保护」(焊接).
    Attachment att;
    att.fromBlockId = w2.blockId;
    att.fromPointId = w2.startId;
    att.toBlockId   = w1.blockId;
    att.toPointId   = w1.endId;
    QVERIFY(doc.addAttachment(att));
    QVERIFY(doc.attachments().size() == 1);
    QVERIFY2(doc.attachments()[0].isLocked, "working connection must default welded");

    // Manual unlock/lock round-trip (属性面板拖动保护开关): 取消 = 解焊仍完整连接.
    const QUuid attId = doc.attachments()[0].id;
    doc.setAttachmentLocked(attId, false);
    QVERIFY(!doc.attachments()[0].isLocked);
    doc.setAttachmentLocked(attId, true);
    QVERIFY(doc.attachments()[0].isLocked);
    QCOMPARE(doc.attachments().size(), size_t(1));  // toggle never drops the connection
}

void TestAuxLayer::lockedClosureWeldsChain()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // A(leader) ← B(follower) ← C(follower), all locked: dragging A must
    // weld B AND C into the drag set (递归焊接闭包).
    const LineRef a = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 100.0);
    const LineRef b = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 60.0);
    const LineRef c = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 40.0);

    Attachment ab;
    ab.fromBlockId = b.blockId; ab.fromPointId = b.startId;
    ab.toBlockId   = a.blockId; ab.toPointId   = a.startId;
    // 新建连接已默认焊接 (isLocked=true, addAttachment 置位); 显式再置位
    // 保持幂等 — 用例要点是焊接闭包.
    ab.isLocked = true;
    QVERIFY(doc.addAttachment(ab));
    Attachment bc;
    bc.fromBlockId = c.blockId; bc.fromPointId = c.startId;
    bc.toBlockId   = b.blockId; bc.toPointId   = b.endId;
    bc.isLocked = true;
    QVERIFY(doc.addAttachment(bc));

    const QSet<QUuid> closure = doc.lockedClosure({a.blockId});
    QCOMPARE(closure.size(), 3);
    QVERIFY(closure.contains(a.blockId));
    QVERIFY(closure.contains(b.blockId));
    QVERIFY(closure.contains(c.blockId));

    // An explicitly-UNLOCKED sibling does NOT get welded in (取消拖动保护 =
    // 解焊仍完整连接; 面板开关是每连接可选的).
    const LineRef d = makeLineBlock(doc, 1, Vec2(0.0, 200.0), 30.0);
    Attachment bd;
    bd.fromBlockId = d.blockId; bd.fromPointId = d.startId;
    bd.toBlockId   = b.blockId; bd.toPointId   = b.startId;
    QVERIFY(doc.addAttachment(bd));
    doc.setAttachmentLocked(bd.id, false);
    QCOMPARE(doc.lockedClosure({a.blockId}).size(), 3);
}

void TestAuxLayer::lockedDragMovesWholePair()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 0));  // aux active so the follower line is clickable
    // NOTE: the scene must exist BEFORE addBlock — CanvasScene only listens
    // to blockAdded; pre-existing blocks would have no item (not clickable).
    CanvasScene scene(&doc);
    const LineRef aux = makeLineBlock(doc, 0, Vec2(0.0, 0.0), 100.0);
    const LineRef w   = makeLineBlock(doc, 1, Vec2(50.0, 0.0), 50.0);

    Attachment att;
    att.fromBlockId = aux.blockId;
    att.fromPointId = aux.startId;
    att.toBlockId   = w.blockId;
    att.toPointId   = w.startId;
    att.followerAngle = 180.0;   // 闭合基准: 180° = 沿 leader 起点出口方向朝左展开
    // 本用例主题 = "焊接拖动整对"; 新建连接默认已焊接 (拖动保护默认勾选).
    att.isLocked = true;
    QVERIFY(doc.addAttachment(att));
    QVERIFY(doc.attachments()[0].isLocked);
    doc.resolveAll();

    // Resolved: aux start welds onto the working start at world (50,0).
    QVERIFY(doc.findBlock(aux.blockId)->worldPos(aux.startId)
                .distanceTo(Vec2(50.0, 0.0)) < 1e-6);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Click the aux follower body → select. (2026-09 取消确认基准: 无右键
    // 确认, 选中即就绪。) The follower's Polar end points along the leader's
    // exit direction: the leader extends +X from (50,0), so the follower lies
    // LEFT of the weld point (follower rotation = leader exit 180°). Click a
    // spot ONLY on the follower's span ((-50,0)→(50,0)); the leader's span
    // (50,0)→(150,0) would be excluded by hitBlock anyway (non-active layer).
    const QPoint hit = vp(0.0, 0.0);   // follower body mid-span
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Drag the locked follower +100 mm right: the LEADER must move with it
    // (焊接整体移动), and the connection must survive.
    const Vec2 w0 = doc.findBlock(w.blockId)->transform.origin;
    const Vec2 a0 = doc.findBlock(aux.blockId)->transform.origin;
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(60.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 0.0), Qt::LeftButton,
              Qt::NoModifier);

    QCOMPARE(doc.attachments().size(), size_t(1));  // welded, never torn
    QVERIFY2(std::abs(doc.findBlock(w.blockId)->transform.origin.x
                      - (w0.x + 100.0)) < 1e-6,
             "leader must be dragged along with the locked follower");
    QVERIFY2(std::abs(doc.findBlock(aux.blockId)->transform.origin.x
                      - (a0.x + 100.0)) < 1e-6,
             "follower moves by the drag delta");
    // Still welded: the follower start stays glued to the leader start.
    QVERIFY(doc.findBlock(aux.blockId)->worldPos(aux.startId)
                .distanceTo(doc.findBlock(w.blockId)->worldPos(w.startId)) < 1e-6);
}

void TestAuxLayer::dragLeaderKeepsFollower()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    // NOTE: scene before addBlock (see lockedDragMovesWholePair).
    CanvasScene scene(&doc);
    const LineRef w1 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 100.0);
    const LineRef w2 = makeLineBlock(doc, 1, Vec2(0.0, 0.0), 60.0);

    Attachment att;
    att.fromBlockId = w2.blockId;
    att.fromPointId = w2.startId;
    att.toBlockId   = w1.blockId;
    att.toPointId   = w1.startId;
    QVERIFY(doc.addAttachment(att));   // working connection (新建默认焊接 — 拖 leader 时 follower 由位置约束自动跟随, 焊接无关紧要)
    doc.resolveAll();
    QVERIFY(doc.findBlock(w2.blockId)->worldPos(w2.startId)
                .distanceTo(doc.findBlock(w1.blockId)->worldPos(w1.startId)) < 1e-6);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select the LEADER (w1), then drag it +120 mm right (2026-09 取消确认
    // 基准: 选中即就绪, 无右键确认). Click a spot on w1 OUTSIDE the
    // follower's span (w2 covers 0→60 only).
    const QPoint hit = vp(75.0, 0.0);   // w1 body (spans 0→100)
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(135.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(195.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(195.0, 0.0), Qt::LeftButton,
              Qt::NoModifier);

    // 方向感知拆除: dragging the LEADER keeps the connection alive and
    // the follower tracks (old behaviour tore it apart → follower dropped).
    // The connection is 拖动保护 locked by default, which also welds —
    // the follower is dragged along and re-settled onto the leader start.
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(doc.findBlock(w2.blockId)->worldPos(w2.startId)
                .distanceTo(doc.findBlock(w1.blockId)->worldPos(w1.startId)) < 1e-6);
    QVERIFY2(std::abs(doc.findBlock(w1.blockId)->transform.origin.x - 120.0) < 1e-6,
             "leader moved by +120");
}

QTEST_MAIN(TestAuxLayer)
#include "test_aux_layer.moc"
