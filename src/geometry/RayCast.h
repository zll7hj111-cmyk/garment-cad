#pragma once

#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include <optional>
#include <vector>
#include "geometry/Epsilon.h"

namespace cad::geo {

/// Intersection hit result of a ray against a segment or curve.
struct RayHit {
    Vec2 point;
    double t = 0.0;     ///< Parameter along segment [0, 1].
};

/// Result of intersecting a ray with a host segment — straight line OR curve.
/// (2026-12 审计 PAR-P0-3 / U6: the same-block and cross-block resolve paths
/// each carried their own copy of this math, and the cross-block copy had no
/// curve branch, so a cross-block intersection with a curve host landed on the
/// CHORD. One entry point now owns both branches.)
struct RaySegmentHit {
    bool hit = false;  ///< True when a valid intersection exists.
    Vec2 point;        ///< Intersection position (in the caller's space).
    double s = 0.0;    ///< Ray parameter: point == origin + s*dir.
    double t = 0.0;    ///< Segment parameter [0,1]; curves: global span T.
};

/// Intersect a ray with a host segment — THE single entry point for both the
/// straight-line and the curve branch (2026-12 审计 PAR-P0-3 / U6).
/// @param origin        Ray origin (caller's space).
/// @param dir           Ray direction, need not be normalized (caller's space).
/// @param segStart      Host segment start, already at its EFFECTIVE position
///                      (endpoint extension tail) or bootstrap seed.
/// @param segEnd        Host segment end, same space and conventions.
/// @param spans         Curve spans in the SAME space; empty = straight-line
///                      branch. Callers pass spansForSegment() output, so the
///                      curve branch and every other consumer share one Hobby
///                      solve.
/// @param bidirectional false = ray (s >= 0 only); true = full line.
[[nodiscard]] RaySegmentHit raySegmentOrCurveIntersect(
    const Vec2& origin, const Vec2& dir,
    const Vec2& segStart, const Vec2& segEnd,
    const std::vector<BezierSpan>& spans,
    bool bidirectional = false);

/// Intersect a ray (origin + dir * s) with a segment (w1 + segDir * t, t in [0, 1]).
/// If bidirectional is true, s can be negative (line-segment intersection).
/// If bidirectional is false, s >= -kGeomEpsLoose (ray only).
/// U5: thin wrapper over raySegmentOrCurveIntersect (single source of the math).
[[nodiscard]] std::optional<RayHit> raySegmentIntersect(
    const Vec2& origin, const Vec2& dir,
    const Vec2& w1, const Vec2& segDir,
    bool bidirectional = false);

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
