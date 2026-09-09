#include "geometry/RayCast.h"
#include <cmath>
#include "geometry/Epsilon.h"

namespace cad::geo {

namespace {

/// Pick the crossing the ray meets FIRST, not the first one in curve order.
/// rayCurveIntersect returns span-ordered hits, which only coincide for an
/// open monotone curve: a closed circle is crossed twice (e.g. a ray along +X
/// hits span 2 at (-r,0) and span 0 at (+r,0), and span 0 comes first in the
/// vector). Bidirectional rays want the closest crossing in EITHER direction,
/// so the key is |s| there.
const CurveHit& nearestAlongRay(const std::vector<CurveHit>& hits,
                                const Vec2& origin, const Vec2& dir,
                                bool bidirectional)
{
    const Vec2 d = dir.normalized();
    size_t best = 0;
    double bestKey = bidirectional
        ? std::abs((hits[0].point - origin).dot(d))
        : (hits[0].point - origin).dot(d);
    for (size_t i = 1; i < hits.size(); ++i) {
        const double s = (hits[i].point - origin).dot(d);
        const double key = bidirectional ? std::abs(s) : s;
        if (key < bestKey) {
            bestKey = key;
            best = i;
        }
    }
    return hits[best];
}

} // namespace

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

    const CurveHit& h = nearestAlongRay(hits, localOrigin, localDir, bidirectional);
    const int spanCount = static_cast<int>(spans.size());
    const double t = (spanCount > 0) ? h.t / spanCount : 0.0;
    return RayHit{h.point, t};
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
        const CurveHit& h = nearestAlongRay(hits, origin, dir, bidirectional);
        out.hit = true;
        out.point = h.point;
        out.t = h.t;
        out.s = (h.point - origin).dot(dir.normalized());
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
