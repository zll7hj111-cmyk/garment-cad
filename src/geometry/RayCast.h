#pragma once

#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include <optional>
#include <vector>

namespace cad::geo {

/// Intersection hit result of a ray against a segment or curve.
struct RayHit {
    Vec2 point;
    double t = 0.0;     ///< Parameter along segment [0, 1].
};

/// Intersect a ray (origin + dir * s) with a segment (w1 + segDir * t, t in [0, 1]).
/// If bidirectional is true, s can be negative (line-segment intersection).
/// If bidirectional is false, s >= -eps (ray only).
[[nodiscard]] std::optional<RayHit> raySegmentIntersect(
    const Vec2& origin, const Vec2& dir,
    const Vec2& w1, const Vec2& segDir,
    bool bidirectional = false, double eps = 1e-6);

/// Intersect a ray with a sequence of Bézier spans in local coordinates.
[[nodiscard]] std::optional<RayHit> rayCurveSpansLocal(
    const Vec2& localOrigin, const Vec2& localDir,
    const std::vector<BezierSpan>& spans,
    bool bidirectional = false);

/// Intersect a world ray with a sequence of Bézier spans belonging to a block with (blockOrigin, blockRotation).
/// Converts ray to block's local coordinates, tests spans, and converts the result back to world coordinates.
[[nodiscard]] std::optional<RayHit> rayVsCurveSpans(
    const Vec2& worldOrigin, const Vec2& worldDir,
    const std::vector<BezierSpan>& spans,
    const Vec2& blockOrigin, double blockRotation,
    bool bidirectional = false);

} // namespace cad::geo
