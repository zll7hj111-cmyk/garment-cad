#pragma once

#include "geometry/Vec2.h"

#include <vector>
#include <cmath>

class QPainterPath;

namespace cad::geo {

/// A single cubic Bézier span defined by four control points.
struct BezierSpan {
    Vec2 p0;     ///< Start point.
    Vec2 ctrl1;  ///< First control point.
    Vec2 ctrl2;  ///< Second control point.
    Vec2 p3;     ///< End point.
};

/// Result of projecting a query point onto a curve.
struct CurveProjection {
    double t = 0.0;         ///< Global parameter [0, spanCount].
    double s = 0.0;         ///< Arc-length position (mm).
    Vec2 point;             ///< Closest point on curve.
    Vec2 tangent;           ///< Unit tangent at the closest point.
    Vec2 normal;            ///< Unit normal (tangent rotated 90° CCW).
    double distance = 0.0;  ///< Distance from query to curve.
    bool valid = false;     ///< False if curve is degenerate.
};

/// A ray-curve intersection hit.
struct CurveHit {
    double t = 0.0;  ///< Global parameter [0, spanCount].
    Vec2 point;      ///< Intersection position.
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

/// Compute the Catmull-Rom tangent at point[index] given the full point sequence.
/// Uses centripetal parameterization for robustness. Tension 0 = standard
/// Catmull-Rom; positive = tighter; negative = looser.
[[nodiscard]] Vec2 catmullRomTangent(const std::vector<Vec2>& points,
                                     int index, double tension = 0.0);

/// Solve for C2-continuous (curvature-continuous) tangents at the AUTO points
/// of an interpolating cubic spline through `points`, treating MANUAL points
/// (isAuto[i] == false) as fixed constraints whose stored tangents
/// (manualTanIn/manualTanOut) the solver works around. This is the "automatic
/// algorithm" that produces ETCAD-like flowing curves: unlike Catmull-Rom
/// (only C1, tangent-continuous), the global solve makes the curvature vary
/// smoothly with no visible "kink" at each point.
///
/// Boundary condition: natural (zero curvature) at a curve endpoint when that
/// endpoint is auto. Returns one tangent per point; entries for manual points
/// are left as Vec2::zero() (callers use their stored tangents instead).
[[nodiscard]] std::vector<Vec2> solveC2Tangents(
    const std::vector<Vec2>& points,
    const std::vector<bool>& isAuto,
    const std::vector<Vec2>& manualTanIn,
    const std::vector<Vec2>& manualTanOut);

/// Which automatic tangent algorithm buildBezierSpans uses for AUTO points.
///   - C2:     curvature-continuous global solve (natural boundary: zero
///             curvature at curve ends). Mathematically smoothest, but the
///             zero-curvature endpoints flatten the shape and steep
///             configurations can overshoot the anchor convex hull.
///   - Hobby:  Hobby spline (Seamly2D). Tangent DIRECTIONS come from a
///             weighted angle system (C1 aesthetic), handle LENGTHS from the
///             Hobby velocity formula. Endpoints follow the chord direction
///             (no forced zero curvature), overshoot stays small, and the
///             shape reads more "lively" for garment curves.
enum class AutoCurveMode { C2, Hobby };

/// Solve Hobby-style tangents (Seamly2D technique): tangent DIRECTIONS come
/// from a weighted angle system, handle LENGTHS from the Hobby velocity
/// formula. Auto curve endpoints follow their adjacent chord direction (no
/// forced zero curvature). Manual points keep their stored tangents as fixed
/// constraints. Returns {tangentIn, tangentOut} — at an auto point the two
/// share a direction but may differ in length (the natural Hobby asymmetry).
/// Overshoot clamp (P3-2): auto handle lengths are capped at 2.0× their chord
/// (kMaxHandleRatio in CurveMath.cpp) so fold-back / extrapolated anchors
/// (interpPercent outside [0,1]) can no longer emit multi-chord-length handles
/// that loop the curve; normal shapes (ratio 0.3~0.7) are unaffected. Manual
/// tangents are never clamped.
/// @param tension   Hobby tension: >1 shortens both handles (tighter curve),
///                  <1 lengthens (looser). 1.0 = classic Hobby.
[[nodiscard]] std::pair<std::vector<Vec2>, std::vector<Vec2>> solveHobbyTangents(
    const std::vector<Vec2>& points,
    const std::vector<bool>& isAuto,
    const std::vector<Vec2>& manualTanIn,
    const std::vector<Vec2>& manualTanOut,
    double tension = 1.0);

/// Build cubic Bézier spans from an ordered point sequence with per-point
/// tangent information.
///
/// @param points       Ordered points (including endpoints), size >= 2.
/// @param tangentIn    Per-point incoming tangent vector (direction + magnitude).
/// @param tangentOut   Per-point outgoing tangent vector.
/// @param autoTangent  Per-point flag: true = compute the automatic tangent
///                     (mode-dependent, overrides the stored values).
/// @param tension      Global tension (Hobby: >1 tighter; ignored by C2).
/// @param mode         Automatic tangent algorithm (default C2).
/// @return             (points.size() - 1) Bézier spans.
[[nodiscard]] std::vector<BezierSpan> buildBezierSpans(
    const std::vector<Vec2>& points,
    const std::vector<Vec2>& tangentIn,
    const std::vector<Vec2>& tangentOut,
    const std::vector<bool>& autoTangent,
    double tension = 0.0,
    AutoCurveMode mode = AutoCurveMode::C2);

/// Convenience overload: all points use Catmull-Rom auto tangents.
[[nodiscard]] std::vector<BezierSpan> buildCatmullRomSpans(
    const std::vector<Vec2>& points,
    double tension = 0.0);

// ─────────────────────────────────────────────────────────────────────────────
// Evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluate a point on a single Bézier span at parameter t ∈ [0,1].
[[nodiscard]] Vec2 evalBezier(const BezierSpan& span, double t);

/// Evaluate the first derivative (tangent vector, NOT normalized) at t.
[[nodiscard]] Vec2 evalBezierDerivative(const BezierSpan& span, double t);

/// Subdivide a single Bézier span at parameter t using de Casteljau's algorithm.
/// Returns two spans [0,t] and [t,1] that exactly reproduce the original shape.
/// The junction point is evalBezier(span, t); its outgoing derivative on the
/// left span equals the incoming derivative on the right span (C∞ at the join).
[[nodiscard]] std::pair<BezierSpan, BezierSpan> subdivideBezier(
    const BezierSpan& span, double t);

/// Evaluate a point on a multi-span curve at global parameter T ∈ [0, spanCount].
[[nodiscard]] Vec2 evalCurve(const std::vector<BezierSpan>& spans, double T);

/// Evaluate the unit tangent at global parameter T.
[[nodiscard]] Vec2 evalCurveTangent(const std::vector<BezierSpan>& spans, double T);

/// Evaluate the first derivative (non-normalized tangent vector) at global
/// parameter T. The magnitude encodes the parametric speed — useful for
/// extracting Bézier tangent handles (tangent = derivative at the point).
[[nodiscard]] Vec2 evalCurveDerivative(const std::vector<BezierSpan>& spans, double T);

// ─────────────────────────────────────────────────────────────────────────────
// Arc length
// ─────────────────────────────────────────────────────────────────────────────

/// Arc length of a single span (5-point Gauss-Legendre quadrature).
[[nodiscard]] double spanArcLength(const BezierSpan& span);

/// Total arc length of a multi-span curve.
[[nodiscard]] double totalArcLength(const std::vector<BezierSpan>& spans);

/// Convert an arc-length distance to a global parameter T.
/// Safeguarded Newton within the located span (bisection guards the bracket,
/// Newton accelerates with speed = |B'(t)|). Returns T ∈ [0, spanCount].
[[nodiscard]] double arcLengthToParam(const std::vector<BezierSpan>& spans,
                                      double targetS);

// ─────────────────────────────────────────────────────────────────────────────
// Projection & Intersection
// ─────────────────────────────────────────────────────────────────────────────

/// Project a query point onto the curve: find the closest point.
/// Returns the projection with distance, tangent, and normal.
[[nodiscard]] CurveProjection projectPointOnCurve(
    const Vec2& query, const std::vector<BezierSpan>& spans);

/// Find all intersections of a ray with the curve.
/// @param origin         Ray origin.
/// @param dir            Ray direction (need not be normalized).
/// @param spans          The curve.
/// @param bidirectional  false = ray (s >= 0 only); true = full line.
/// @return               All hits sorted by global parameter T.
[[nodiscard]] std::vector<CurveHit> rayCurveIntersect(
    const Vec2& origin, const Vec2& dir,
    const std::vector<BezierSpan>& spans,
    bool bidirectional = false);

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

/// Build a QPainterPath from Bézier spans (scene coordinates: caller must
/// flip Y before calling if needed). The path starts with moveTo(p0 of span 0).
[[nodiscard]] QPainterPath buildPainterPath(const std::vector<BezierSpan>& spans);

/// Flatten Bézier spans into a dense polyline approximation (Seamly2D-style
/// rendering). Rendering a polyline is far cheaper than a QPainterPath full of
/// cubic segments: the rasterizer / GL backend draws line segments directly,
/// whereas cubic segments must be recursively subdivided and triangulated on
/// EVERY repaint. Adaptive subdivision stops when the control points deviate
/// from the chord by less than @p tolerance (mm). The first point of the
/// first span is included; span endpoints are shared (no duplicates).
[[nodiscard]] std::vector<Vec2> flattenBezierSpans(
    const std::vector<BezierSpan>& spans, double tolerance);

} // namespace cad::geo
