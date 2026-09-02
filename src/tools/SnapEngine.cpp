#include "SnapEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/PerfProbe.h"
#include "geometry/CurveMath.h"

namespace cad::tools {

std::optional<SnapResult> SnapEngine::findSnap(
    const cad::geo::Vec2& worldPos,
    const cad::param::ParamDocument* paramDoc,
    double zoom,
    double radiusPx,
    const QUuid& excludePointId,
    const QUuid* excludeBlockId,
    bool ignoreLayerFilter) const
{
    GCAD_PERF_SCOPE("snap.point");
    if (!paramDoc) return std::nullopt;

    // Convert screen-space radius to world-space
    const double radius = (radiusPx > 0.0) ? radiusPx : snapRadius;
    double worldRadius = (zoom > 1e-9) ? (radius / zoom) : radius;
    double bestDistSq = worldRadius * worldRadius;
    std::optional<SnapResult> best;

    // Active-layer preference at exact coincidence: when points of DIFFERENT
    // layers stack on the same spot (typical: an aux-layer follower point
    // sitting exactly on a working-layer point), the active layer's point
    // wins — the user is working in that layer and sees its point, and the
    // HUD/ring must agree with the eventual attachment. Everything else
    // stays pure nearest-distance (a meaningfully closer point of another
    // layer still wins). kTieDistSq is (1e-6 mm)² — float noise at
    // SCENE_BOUND scale, far below any visually meaningful difference.
    const QUuid activeLayer = paramDoc->activeLayer();
    constexpr double kTieDistSq = 1e-12;
    bool bestIsActive = false;

    pruneSnapCaches(paramDoc);
    for (const auto& block : paramDoc->blocks()) {
        if (block.isShadow) continue;  // 影子不可捕捉/不可作为连接目标 (R4, 拆开影子基准)
        // Snap-target filter (layerSnappable): manually hidden layers are
        // never snappable; the auxiliary layer only while ACTIVE (its grayed
        // non-active draft is not a snap target — cross-group attachments
        // would be rejected anyway); grayed working layers stay snappable so
        // new geometry can connect.
        //
        // ignoreLayerFilter (no-attachment tools, e.g. the intersection tool)
        // lifts the aux-active restriction: such tools only REFERENCE points
        // and never create attachments, so grayed aux-layer geometry is a
        // legitimate pick target. Hidden layers stay excluded either way.
        if (!paramDoc->layersView().layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layersView().layerSnappable(block.layer)) continue;
        if (excludeBlockId && block.id == *excludeBlockId) continue;

        // World positions come from the per-block cache (validated by epoch +
        // transform + point count; unchanged blocks cost a few compares).
        const SnapBlockEntry* e = cachedSnapBlock(block);
        if (!e) continue;

        for (const auto& pe : e->points) {
            // Skip non-selectable points (e.g. anchors). Hidden (隐藏) points
            // stay snappable — hiding is visual-only, they are often kept
            // precisely as positioning references.
            if (!pe.selectable || !pe.resolved) continue;
            // Skip the excluded point (e.g. the one being dragged).
            if (!excludePointId.isNull() && pe.pointId == excludePointId) continue;
            // A bridge's pinned endpoints are pure downstream leaves — never
            // snap targets. But an AUXILIARY point on a bridge is a legitimate
            // anchor for new geometry (see Block::isBridge).
            if (pe.bridgeNonAux) continue;

            const double dx = worldPos.x - pe.world.x;
            const double dy = worldPos.y - pe.world.y;
            const double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = pe.world,
                    .blockId   = block.id,
                    .pointId   = pe.pointId,
                    .pointName = pe.name
                };
                bestIsActive = (block.layer == activeLayer);
            } else if (distSq - bestDistSq <= kTieDistSq
                       && !bestIsActive
                       && block.layer == activeLayer) {
                // Exact tie (within float noise): prefer the active layer's
                // point over the traversal-order first-comer.
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = pe.world,
                    .blockId   = block.id,
                    .pointId   = pe.pointId,
                    .pointName = pe.name
                };
                bestIsActive = true;
            }
        }
    }

    return best;
}

std::vector<SnapResult> SnapEngine::findSnapCandidates(
    const cad::geo::Vec2& worldPos,
    const cad::param::ParamDocument* paramDoc,
    double zoom,
    double radiusPx,
    const QUuid& excludePointId,
    const QUuid* excludeBlockId,
    bool ignoreLayerFilter) const
{
    GCAD_PERF_SCOPE("snap.pointAll");
    std::vector<SnapResult> out;
    if (!paramDoc) return out;

    const double radius = (radiusPx > 0.0) ? radiusPx : snapRadius;
    const double worldRadius = (zoom > 1e-9) ? (radius / zoom) : radius;
    const double radiusSq = worldRadius * worldRadius;

    pruneSnapCaches(paramDoc);
    for (const auto& block : paramDoc->blocks()) {
        if (block.isShadow) continue;  // 影子不可捕捉/不可作为连接目标 (R4, 拆开影子基准)
        // Same target policy as findSnap (see its doc comment).
        if (!paramDoc->layersView().layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layersView().layerSnappable(block.layer)) continue;
        if (excludeBlockId && block.id == *excludeBlockId) continue;

        const SnapBlockEntry* e = cachedSnapBlock(block);
        if (!e) continue;

        for (const auto& pe : e->points) {
            if (!pe.selectable || !pe.resolved) continue;
            if (!excludePointId.isNull() && pe.pointId == excludePointId) continue;
            if (pe.bridgeNonAux) continue;

            const double dx = worldPos.x - pe.world.x;
            const double dy = worldPos.y - pe.world.y;
            if (dx * dx + dy * dy > radiusSq) continue;

            out.push_back(SnapResult{
                .worldPos  = pe.world,
                .blockId   = block.id,
                .pointId   = pe.pointId,
                .pointName = pe.name
            });
        }
    }

    // Nearest first.
    std::sort(out.begin(), out.end(),
        [&worldPos](const SnapResult& a, const SnapResult& b) {
            return a.worldPos.distanceSquaredTo(worldPos)
                 < b.worldPos.distanceSquaredTo(worldPos);
        });
    return out;
}

std::optional<SnapResult> SnapEngine::findCurvePointSnap(
    const cad::geo::Vec2& worldPos,
    const cad::param::ParamDocument* paramDoc,
    double zoom,
    double radiusPx) const
{
    GCAD_PERF_SCOPE("snap.curvePoint");
    if (!paramDoc) return std::nullopt;

    const double radius = (radiusPx > 0.0) ? radiusPx : snapRadius;
    const double worldRadius = (zoom > 1e-9) ? (radius / zoom) : radius;
    double bestDistSq = worldRadius * worldRadius;
    std::optional<SnapResult> best;

    pruneSnapCaches(paramDoc);
    for (const auto& block : paramDoc->blocks()) {
        if (block.isShadow) continue;  // 影子不可捕捉/不可作为连接目标 (R4, 拆开影子基准)
        if (!paramDoc->layersView().layerSnappable(block.layer)) continue;

        // World positions + curve-relevant flags come from the per-block cache
        // (validated by epoch + transform + point count); the old path rebuilt
        // a QSet<QUuid> of curve endpoints and recomputed cos/sin + per-point
        // world transforms on EVERY mouse move.
        const SnapBlockEntry* e = cachedSnapBlock(block);
        if (!e || !e->hasCurve) continue;
        if (e->curveEndFlags.size() != e->points.size()) continue;  // parity guard

        for (size_t i = 0; i < e->points.size(); ++i) {
            const SnapPointEntry& pe = e->points[i];
            if (!pe.selectable || !pe.resolved) continue;
            // Curve-relevant = a CurveAnchor pass-point, or a curve endpoint.
            if (!pe.isCurveAnchor && e->curveEndFlags[i] == 0) continue;

            const double dx = worldPos.x - pe.world.x;
            const double dy = worldPos.y - pe.world.y;
            const double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = pe.world,
                    .blockId   = block.id,
                    .pointId   = pe.pointId,
                    .pointName = pe.name
                };
            }
        }
    }

    return best;
}

std::optional<SegmentSnapResult> SnapEngine::findSegmentSnap(
    const cad::geo::Vec2& worldPos,
    const cad::param::ParamDocument* paramDoc,
    double zoom,
    double radiusPx,
    const QSet<QUuid>* excludeSegments,
    bool ignoreLayerFilter) const
{
    GCAD_PERF_SCOPE("snap.segment");
    if (!paramDoc) return std::nullopt;

    // Convert screen-space radius to world-space
    const double radius = (radiusPx > 0.0) ? radiusPx : snapRadius;
    const double worldRadius = (zoom > 1e-9) ? (radius / zoom) : radius;
    double bestDist = worldRadius;
    std::optional<SegmentSnapResult> best;

    pruneSnapCaches(paramDoc);
    for (const auto& block : paramDoc->blocks()) {
        if (block.isShadow) continue;  // 影子不可捕捉/不可作为连接目标 (R4, 拆开影子基准)
        // Same snap-target policy as findSnap (see layerSnappable / ignoreLayerFilter).
        if (!paramDoc->layersView().layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layersView().layerSnappable(block.layer)) continue;
        if (block.segments.empty()) continue;

        // Endpoint world positions come from the per-block segment cache
        // (validated by epoch + transform + segment count; straight branches
        // skip findPoint + trig entirely).
        const SnapSegBlockEntry* se = cachedSnapSegBlock(block);
        if (!se) continue;

        for (const auto& seg : se->segs) {
            // Hidden (隐藏) segments remain snappable: hiding only suppresses
            // rendering, the geometry is still a positioning reference.
            if (excludeSegments && excludeSegments->contains(seg.segId)) continue;
            if (!seg.valid) continue;

            // --- Curve segment: project onto Bézier path ---
            if (seg.isCurve) {
                const auto* liveSeg = block.findSegment(seg.segId);
                if (!liveSeg) continue;
                // Frame-level cache: spans + control-polygon bbox are built
                // once per resolve pass (Block::rebuildCurveCache) — no C2
                // re-solve per snap query.
                const cad::param::CurveSpanEntry* entry = block.curveSpanEntry(seg.segId);
                if (!entry || entry->spans.empty()) continue;
                const auto& spans = entry->spans;

                // Transform cursor to local coords for projection.
                const cad::geo::Vec2 localCursor = block.transform.toLocal(worldPos);

                // Coarse bbox cull: the curve lies inside the control-polygon
                // box (convex hull property), so when the cursor is farther
                // than the current best distance from the box the projection
                // cannot win — skip the expensive point-on-curve solve. This
                // keeps idle mouse-move cheap on documents with many curves.
                const double cull = bestDist;
                if (localCursor.x < entry->bboxMin.x - cull ||
                    localCursor.x > entry->bboxMax.x + cull ||
                    localCursor.y < entry->bboxMin.y - cull ||
                    localCursor.y > entry->bboxMax.y + cull)
                    continue;

                auto proj = cad::geo::projectPointOnCurve(localCursor, spans);
                if (!proj.valid) continue;

                if (proj.distance < bestDist) {
                    bestDist = proj.distance;
                    // Transform projection result back to world
                    cad::geo::Vec2 worldProj = block.transform.toWorld(proj.point);
                    // Tangent in world: rotate local tangent by block rotation
                    cad::geo::Vec2 worldTan = proj.tangent.rotated(block.transform.rotation);
                    cad::geo::Vec2 worldNorm = proj.normal.rotated(block.transform.rotation);
                    // Exact arc length was integrated ONCE at cache build
                    // (entry->arcLengthMm) — re-summing spans per acceptance
                    // is pure waste (same value, same function).
                    const double totalArc = entry->arcLengthMm;
                    double arcRatio = (totalArc > 1e-9) ? proj.s / totalArc : 0.0;

                    best = SegmentSnapResult{
                        .worldPos  = worldProj,
                        .blockId   = block.id,
                        .segmentId = seg.segId,
                        .t         = arcRatio,
                        .tangent   = worldTan,
                        .normal    = worldNorm
                    };
                }
                continue;
            }

            // --- Straight-line segment (cached world endpoints) ---
            const double ax = seg.a.x, ay = seg.a.y;
            const double bx = seg.b.x, by = seg.b.y;

            // Perpendicular projection of the cursor onto [a, b].
            const double abx = bx - ax, aby = by - ay;
            const double lenSq = abx * abx + aby * aby;
            if (lenSq < 1e-12) continue;  // degenerate segment
            double t = ((worldPos.x - ax) * abx + (worldPos.y - ay) * aby) / lenSq;
            t = std::clamp(t, 0.0, 1.0);
            const double projX = ax + abx * t;
            const double projY = ay + aby * t;

            const double dx = worldPos.x - projX;
            const double dy = worldPos.y - projY;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                best = SegmentSnapResult{
                    .worldPos  = {projX, projY},
                    .blockId   = block.id,
                    .segmentId = seg.segId,
                    .t         = t
                };
            }
        }
    }

    return best;
}

// ── Per-block snap cache helpers (2026-09 性能专项) ─────────────────────────
// Cache key = (geometryEpoch, origin, rotation, count). geometryEpoch captures
// point moves; count captures add/remove (which may NOT bump the epoch — e.g.
// removing a pass point leaves the survivors untouched); transform captures
// drags. No layer state is cached — visibility/snappability are checked live
// per call.

const SnapEngine::SnapBlockEntry* SnapEngine::cachedSnapBlock(
    const cad::param::Block& block) const
{
    auto it = m_snapBlockCache.constFind(block.id);
    if (it != m_snapBlockCache.constEnd()
        && it->epoch == block.geometryEpoch()
        && it->ox == block.transform.origin.x
        && it->oy == block.transform.origin.y
        && it->rot == block.transform.rotation
        && it->pointCount == static_cast<int>(block.points.size())) {
        return &it.value();
    }
    SnapBlockEntry e;
    e.epoch = block.geometryEpoch();
    e.ox = block.transform.origin.x;
    e.oy = block.transform.origin.y;
    e.rot = block.transform.rotation;
    e.pointCount = static_cast<int>(block.points.size());
    e.points.reserve(block.points.size());

    // Curve-endpoint set (for findCurvePointSnap): which point ids are the
    // START/END of a curve segment. The QSet lives only for the duration of
    // this cache rebuild — the per-frame path reads the aligned flags.
    QSet<QUuid> curveEnds;
    for (const auto& seg : block.segments) {
        if (!seg.isCurve()) continue;
        e.hasCurve = true;
        curveEnds.insert(seg.startPointId);
        curveEnds.insert(seg.endPointId);
    }
    e.curveEndFlags.clear();
    e.curveEndFlags.reserve(block.points.size());

    const double c = std::cos(block.transform.rotation);
    const double s = std::sin(block.transform.rotation);
    const double ox = block.transform.origin.x;
    const double oy = block.transform.origin.y;
    for (const auto& pt : block.points) {
        SnapPointEntry pe;
        pe.pointId = pt.id;
        pe.name = pt.name;
        pe.selectable = pt.selectable;
        pe.resolved = pt.resolved;
        pe.bridgeNonAux = block.isBridge && !pt.isAuxiliary;
        pe.isCurveAnchor = (pt.constraint == cad::param::PointConstraint::CurveAnchor);
        // 端点延长线：点的实际位置 = 有效位置（含尾巴），缩放/旋转后入缓存
        // （EXTEND_LINE_DESIGN.md：捕捉/命中按实际位置）。
        const geo::Vec2 eff = block.effectiveLocalPos(pt.id);
        pe.world = { ox + eff.x * c - eff.y * s,
                     oy + eff.x * s + eff.y * c };
        e.curveEndFlags.push_back(curveEnds.contains(pt.id) ? 1u : 0u);
        e.points.push_back(std::move(pe));
    }
    m_snapBlockCache.insert(block.id, std::move(e));
    return &m_snapBlockCache[block.id];
}

const SnapEngine::SnapSegBlockEntry* SnapEngine::cachedSnapSegBlock(
    const cad::param::Block& block) const
{
    auto it = m_snapSegCache.constFind(block.id);
    if (it != m_snapSegCache.constEnd()
        && it->epoch == block.geometryEpoch()
        && it->ox == block.transform.origin.x
        && it->oy == block.transform.origin.y
        && it->rot == block.transform.rotation
        && it->segCount == static_cast<int>(block.segments.size())) {
        return &it.value();
    }
    SnapSegBlockEntry e;
    e.epoch = block.geometryEpoch();
    e.ox = block.transform.origin.x;
    e.oy = block.transform.origin.y;
    e.rot = block.transform.rotation;
    e.segCount = static_cast<int>(block.segments.size());
    e.segs.reserve(block.segments.size());
    const double c = std::cos(block.transform.rotation);
    const double s = std::sin(block.transform.rotation);
    const double ox = block.transform.origin.x;
    const double oy = block.transform.origin.y;
    for (const auto& seg : block.segments) {
        const auto* a = block.findPoint(seg.startPointId);
        const auto* b = block.findPoint(seg.endPointId);
        if (!a || !b) continue;
        SnapSegmentEntry e2;
        e2.segId = seg.id;
        e2.isCurve = seg.isCurve();
        e2.valid = a->resolved && b->resolved;
        // 端点延长线：段身按有效位置（实际画出的几何）捕捉（EXTEND_LINE_DESIGN.md）。
        const geo::Vec2 effA = block.effectiveLocalPos(seg.startPointId);
        const geo::Vec2 effB = block.effectiveLocalPos(seg.endPointId);
        e2.a = { ox + effA.x * c - effA.y * s,
                 oy + effA.x * s + effA.y * c };
        e2.b = { ox + effB.x * c - effB.y * s,
                 oy + effB.x * s + effB.y * c };
        e.segs.push_back(std::move(e2));
    }
    m_snapSegCache.insert(block.id, std::move(e));
    return &m_snapSegCache[block.id];
}

void SnapEngine::pruneSnapCaches(const cad::param::ParamDocument* doc) const
{
    // Removed blocks leave dead entries (UUID-keyed, never re-read) — cap the
    // cache at 2× the live block count to keep long sessions bounded.
    const int n = doc ? static_cast<int>(doc->blocks().size()) : 0;
    if (m_snapBlockCache.size() > n * 2 + 8)
        m_snapBlockCache.clear();
    if (m_snapSegCache.size() > n * 2 + 8)
        m_snapSegCache.clear();
}

} // namespace cad::tools
