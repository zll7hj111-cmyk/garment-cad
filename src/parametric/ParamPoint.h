#pragma once

#include <QUuid>
#include <QString>

#include "geometry/Vec2.h"

namespace cad::param {

/// How a point's position is determined.
enum class PointConstraint {
    Free,         ///< Position given directly in local coordinates.
    Polar,        ///< refPoint + distance + angle.
    Midpoint,     ///< Midpoint of refPointA and refPointB.
    OnSegment,    ///< refPointA + ratio * (refPointB - refPointA).
    Intersection, ///< Intersection of two lines (reserved).
};

/// A parametric point entity. Position is computed from constraints.
struct ParamPoint {
    QUuid   id   = QUuid::createUuid();
    QString serial;  ///< Human-readable ID, e.g. "a5sdfP1" (assigned by ParamDocument).
    QString name;  ///< Display label (e.g. "A", "B").
    QString annotation;  ///< User annotation / comment.

    PointConstraint constraint = PointConstraint::Free;

    // --- Free mode ---
    geo::Vec2 freePos;  ///< Direct local-coordinate position.

    // --- Polar mode ---
    QUuid  refPointId;    ///< Reference point this point is relative to.
    double distance = 0.0;  ///< Distance from reference (mm).
    double angle    = 0.0;  ///< Angle in degrees (relative to reference direction).
    QUuid  refSegmentId;    ///< Optional: segment whose direction is the angle baseline.

    // --- Formula support (overrides numeric value when non-empty) ---
    /// Formulas operate in the formula domain (cm): variables resolve to cm
    /// values and the result is interpreted as cm, then converted to mm by
    /// Block::resolve(). Example: "hip/4+2" with hip=92cm → 25cm → 250mm.
    QString distanceFormula;  ///< e.g. "hip/4+2" → evaluates to distance (cm, auto-converted to mm).
    QString angleFormula;     ///< e.g. "45" or expression → evaluates to angle (deg).

    // --- Midpoint / OnSegment mode ---
    QUuid  refPointA;
    QUuid  refPointB;
    double ratio = 0.5;  ///< 0.0~1.0 for OnSegment; ignored for Midpoint.

    // --- Resolved result (filled by Resolver) ---
    geo::Vec2 resolvedPos;  ///< Final position in local coordinates.
    bool      resolved = false;

    // --- Metadata ---
    bool isAuxiliary = false;  ///< Auxiliary point (positioning only, no line drawn).
    bool visible     = true;
    bool selectable  = true;   ///< false for anchor points (invisible pivot).
    bool showName    = false;  ///< Whether to display the point name on canvas.
};

} // namespace cad::param
