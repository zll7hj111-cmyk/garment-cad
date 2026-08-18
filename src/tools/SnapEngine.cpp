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

    for (const auto& block : paramDoc->blocks()) {
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
        if (!paramDoc->layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layerSnappable(block.layer)) continue;
        if (excludeBlockId && block.id == *excludeBlockId) continue;

        // Hoist the rotation trig: Transform2D::toWorld() recomputes cos/sin
        // per call — at mouse-move frequency over hundreds of points this
        // dominates the frame budget. Inline the rigid transform instead.
        const double c = std::cos(block.transform.rotation);
        const double s = std::sin(block.transform.rotation);
        const double ox = block.transform.origin.x;
        const double oy = block.transform.origin.y;

        for (const auto& pt : block.points) {
            // Skip non-selectable points (e.g. anchors). Hidden (隐藏) points
            // stay snappable — hiding is visual-only, they are often kept
            // precisely as positioning references.
            if (!pt.selectable || !pt.resolved) continue;
            // Skip the excluded point (e.g. the one being dragged).
            if (!excludePointId.isNull() && pt.id == excludePointId) continue;
            // A bridge's pinned endpoints are pure downstream leaves — never
            // snap targets. But an AUXILIARY point on a bridge is a legitimate
            // anchor for new geometry (see Block::isBridge).
            if (block.isBridge && !pt.isAuxiliary) continue;

            // Inline toWorld: rotate then translate.
            const double wx = ox + pt.resolvedPos.x * c - pt.resolvedPos.y * s;
            const double wy = oy + pt.resolvedPos.x * s + pt.resolvedPos.y * c;
            const double dx = worldPos.x - wx;
            const double dy = worldPos.y - wy;
            const double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = {wx, wy},
                    .blockId   = block.id,
                    .pointId   = pt.id,
                    .pointName = pt.name
                };
                bestIsActive = (block.layer == activeLayer);
            } else if (distSq - bestDistSq <= kTieDistSq
                       && !bestIsActive
                       && block.layer == activeLayer) {
                // Exact tie (within float noise): prefer the active layer's
                // point over the traversal-order first-comer.
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = {wx, wy},
                    .blockId   = block.id,
                    .pointId   = pt.id,
                    .pointName = pt.name
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

    for (const auto& block : paramDoc->blocks()) {
        // Same target policy as findSnap (see its doc comment).
        if (!paramDoc->layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layerSnappable(block.layer)) continue;
        if (excludeBlockId && block.id == *excludeBlockId) continue;

        const double c = std::cos(block.transform.rotation);
        const double s = std::sin(block.transform.rotation);
        const double ox = block.transform.origin.x;
        const double oy = block.transform.origin.y;

        for (const auto& pt : block.points) {
            if (!pt.selectable || !pt.resolved) continue;
            if (!excludePointId.isNull() && pt.id == excludePointId) continue;
            if (block.isBridge && !pt.isAuxiliary) continue;

            const double wx = ox + pt.resolvedPos.x * c - pt.resolvedPos.y * s;
            const double wy = oy + pt.resolvedPos.x * s + pt.resolvedPos.y * c;
            const double dx = worldPos.x - wx;
            const double dy = worldPos.y - wy;
            if (dx * dx + dy * dy > radiusSq) continue;

            out.push_back(SnapResult{
                .worldPos  = {wx, wy},
                .blockId   = block.id,
                .pointId   = pt.id,
                .pointName = pt.name
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

    for (const auto& block : paramDoc->blocks()) {
        if (!paramDoc->layerSnappable(block.layer)) continue;

        // Collect the endpoint ids of this block's CURVE segments — those are
        // the curve endpoints the tool must be able to grab.
        QSet<QUuid> curveEndIds;
        bool hasCurve = false;
        for (const auto& seg : block.segments) {
            if (!seg.isCurve()) continue;
            hasCurve = true;
            curveEndIds.insert(seg.startPointId);
            curveEndIds.insert(seg.endPointId);
        }
        if (!hasCurve) {
            // A block with no curve can still host CurveAnchor pass-points? No —
            // pass-points belong to a curve segment, so skip curve-less blocks.
            continue;
        }

        const double c = std::cos(block.transform.rotation);
        const double s = std::sin(block.transform.rotation);
        const double ox = block.transform.origin.x;
        const double oy = block.transform.origin.y;

        for (const auto& pt : block.points) {
            if (!pt.selectable || !pt.resolved) continue;
            // Curve-relevant = a CurveAnchor pass-point, or a curve endpoint.
            const bool isCurveAnchor =
                pt.constraint == cad::param::PointConstraint::CurveAnchor;
            const bool isCurveEnd = curveEndIds.contains(pt.id);
            if (!isCurveAnchor && !isCurveEnd) continue;

            const double wx = ox + pt.resolvedPos.x * c - pt.resolvedPos.y * s;
            const double wy = oy + pt.resolvedPos.x * s + pt.resolvedPos.y * c;
            const double dx = worldPos.x - wx;
            const double dy = worldPos.y - wy;
            const double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = {wx, wy},
                    .blockId   = block.id,
                    .pointId   = pt.id,
                    .pointName = pt.name
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

    for (const auto& block : paramDoc->blocks()) {
        // Same snap-target policy as findSnap (see layerSnappable / ignoreLayerFilter).
        if (!paramDoc->layerVisible(block.layer)) continue;
        if (!ignoreLayerFilter && !paramDoc->layerSnappable(block.layer)) continue;
        if (block.segments.empty()) continue;

        // Hoist rotation trig once per block (see findSnap rationale).
        const double c = std::cos(block.transform.rotation);
        const double s = std::sin(block.transform.rotation);
        const double ox = block.transform.origin.x;
        const double oy = block.transform.origin.y;

        for (const auto& seg : block.segments) {
            // Hidden (隐藏) segments remain snappable: hiding only suppresses
            // rendering, the geometry is still a positioning reference.
            if (excludeSegments && excludeSegments->contains(seg.id)) continue;

            const auto* a = block.findPoint(seg.startPointId);
            const auto* b = block.findPoint(seg.endPointId);
            if (!a || !b || !a->resolved || !b->resolved) continue;

            // --- Curve segment: project onto Bézier path ---
            if (seg.isCurve()) {
                // Frame-level cache: spans + control-polygon bbox are built
                // once per resolve pass (Block::rebuildCurveCache) — no C2
                // re-solve per snap query.
                const cad::param::CurveSpanEntry* entry = block.curveSpanEntry(seg.id);
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
                    double totalArc = cad::geo::totalArcLength(spans);
                    double arcRatio = (totalArc > 1e-9) ? proj.s / totalArc : 0.0;

                    best = SegmentSnapResult{
                        .worldPos  = worldProj,
                        .blockId   = block.id,
                        .segmentId = seg.id,
                        .t         = arcRatio,
                        .tangent   = worldTan,
                        .normal    = worldNorm
                    };
                }
                continue;
            }

            // --- Straight-line segment (existing logic) ---

            // Inline toWorld for both endpoints.
            const double ax = ox + a->resolvedPos.x * c - a->resolvedPos.y * s;
            const double ay = oy + a->resolvedPos.x * s + a->resolvedPos.y * c;
            const double bx = ox + b->resolvedPos.x * c - b->resolvedPos.y * s;
            const double by = oy + b->resolvedPos.x * s + b->resolvedPos.y * c;

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
                    .segmentId = seg.id,
                    .t         = t
                };
            }
        }
    }

    return best;
}

} // namespace cad::tools
