#include "geometry/RayCast.h"
#include <cmath>

namespace cad::geo {

std::optional<RayHit> raySegmentIntersect(
    const Vec2& origin, const Vec2& dir,
    const Vec2& w1, const Vec2& segDir,
    bool bidirectional, double eps)
{
    const double denom = dir.cross(segDir);
    if (std::abs(denom) < 1e-9) return std::nullopt;  // Parallel

    const Vec2 w = w1 - origin;
    const double s = w.cross(segDir) / denom;
    const double t = w.cross(dir) / denom;

    const bool validT = (t >= -eps && t <= 1.0 + eps);
    const bool validS = bidirectional ? true : (s >= -eps);
    if (!validT || !validS) return std::nullopt;

    return RayHit{origin + dir * s, t};
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

} // namespace cad::geo
