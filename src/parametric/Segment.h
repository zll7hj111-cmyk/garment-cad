#pragma once

#include <QUuid>
#include <QString>
#include <QColor>

namespace cad::param {

/// Type of geometry a segment represents.
enum class SegmentType { Line, Arc, Bezier };

/// Visual line style.
enum class LineStyle { Solid, Dashed, Dotted };

/// A segment connecting two parametric points within a Block.
struct Segment {
    QUuid id = QUuid::createUuid();
    QString serial;  ///< Human-readable ID, e.g. "k9x2bL1" (assigned by ParamDocument).
    QString name;  ///< User-defined segment name (e.g. "肩线").

    SegmentType type = SegmentType::Line;

    QUuid startPointId;  ///< References a ParamPoint in the same Block.
    QUuid endPointId;    ///< References a ParamPoint in the same Block.

    // --- Curve extensions (for Arc / Bezier) ---
    QUuid ctrlPointId;   ///< Control point (Arc: single; Bezier: first).
    QUuid ctrlPoint2Id;  ///< Second control point (Bezier only).

    // --- Construction parameters (for chain-based resolve) ---
    double constructAngle = 0.0;  ///< Deflection angle relative to previous segment (degrees).
    bool   isDriven = false;      ///< true = length/angle back-calculated from closure constraint.

    // --- Formula-driven length ---
    QString lengthFormula;  ///< e.g. "hip/4+2" → evaluates to length in mm.
                            ///< When non-empty, overrides the geometric distance.

    // --- Visual properties ---
    LineStyle lineStyle = LineStyle::Solid;
    QColor    color     = QColor(30, 30, 30);
    double    weight    = 1.2;  ///< Line thickness in pixels (cosmetic).

    bool visible   = true;
    bool showName  = false;  ///< Whether to display the segment name on canvas.
    bool showLength = false; ///< Whether to display the segment length on canvas.
};

} // namespace cad::param
