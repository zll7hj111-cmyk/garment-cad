#include "geometry/RayCast.h"
#include <cmath>
#include "geometry/Epsilon.h"

namespace cad::geo {

std::optional<RayHit> raySegmentIntersect(
    const Vec2& origin, const Vec2& dir,
    const Vec2& w1, const Vec2& segDir,
    bool bidirectional)
{
    const RaySegmentHit hit = raySegmentOrCurveIntersect(
        origin, dir, w1, w1 + segDir, {}, bidirectional);
    if (!hit.hit) return std::nullopt;
    return RayHit{hit.point, hit.t};
}

std::optional<RayHit> rayCurveSpansLocal(
    const Vec2& localOrigin, const Vec2& localDir,
    const std::vector<BezierSpan>& spans,
    bool bidirectional)
{
    auto hits = rayCurveIntersect(localOrigin, localDir, spans, bidirectional);
    if (hits.empty()) return std::nullopt;

    const int spanCount = static_cast<int>(spans.size());
    const double t = (spanCount > 0) ? hits[0].t / spanCount : 0.0;
    return RayHit{hits[0].point, t};
}

std::optional<RayHit> rayVsCurveSpans(
    const Vec2& worldOrigin, const Vec2& worldDir,
    const std::vector<BezierSpan>& spans,
    const Vec2& blockOrigin, double blockRotation,
    bool bidirectional)
{
    const Vec2 localOrigin = (worldOrigin - blockOrigin).rotated(-blockRotation);
    const Vec2 localDir = worldDir.rotated(-blockRotation);

    auto hit = rayCurveSpansLocal(localOrigin, localDir, spans, bidirectional);
    if (!hit) return std::nullopt;

    return RayHit{blockOrigin + hit->point.rotated(blockRotation), hit->t};
}

RaySegmentHit raySegmentOrCurveIntersect(const Vec2& origin, const Vec2& dir,
                                         const Vec2& segStart, const Vec2& segEnd,
                                         const std::vector<BezierSpan>& spans,
                                         bool bidirectional)
{
    RaySegmentHit out;
    if (!spans.empty()) {
        const auto hits = rayCurveIntersect(origin, dir, spans, bidirectional);
        if (hits.empty()) return out;
        out.hit = true;
        out.point = hits[0].point;
        out.t = hits[0].t;
        out.s = (out.point - origin).dot(dir.normalized());
        return out;
    }

    // Ray: R(s) = origin + s*dir, s >= 0 (any s when bidirectional).
    // Segment: L(t) = segStart + t*segDir, t in [0,1].
    const Vec2 segDir = segEnd - segStart;
    const double denom = dir.cross(segDir);
    if (std::abs(denom) < kGeomEps) return out;  // Parallel.

    const Vec2 w = segStart - origin;
    const double s = w.cross(segDir) / denom;  // Ray parameter.
    const double t = w.cross(dir) / denom;     // Segment parameter.

    const bool validT = (t >= -kGeomEpsLoose && t <= 1.0 + kGeomEpsLoose);
    const bool validS = bidirectional || (s >= -kGeomEpsLoose);
    if (!validT || !validS) return out;

    out.hit = true;
    out.s = s;
    out.t = t;
    out.point = origin + dir * s;
    return out;
}

} // namespace cad::geo
