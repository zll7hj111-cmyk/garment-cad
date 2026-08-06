#pragma once

#include <QUuid>
#include <QString>
#include <QColor>
#include <vector>

namespace cad::param {

/// Type of geometry a segment represents.
enum class SegmentType { Line, Arc, Bezier };

/// Semantic role of a segment in garment pattern context.
enum class SegmentRole {
    Outline,    ///< 轮廓线 (cutting line / pattern outline).
    Internal,   ///< 内部线 (dart, pleat, pocket, etc.).
    Auxiliary,  ///< 辅助线 (construction / reference line, not printed).
};

/// Visual line style.
enum class LineStyle { Solid, Dashed, Dotted };

/// A segment connecting two parametric points within a Block.
struct Segment {
    QUuid id = QUuid::createUuid();
    QString serial;  ///< Human-readable ID, e.g. "k9x2bL1" (assigned by ParamDocument).
    QString name;  ///< User-defined segment name (e.g. "肩线").

    SegmentType type = SegmentType::Line;
    SegmentRole role = SegmentRole::Outline;  ///< Semantic role (轮廓/内部/辅助).

    QUuid startPointId;  ///< References a ParamPoint in the same Block.
    QUuid endPointId;    ///< References a ParamPoint in the same Block.

    // --- Curve extensions (for Bezier) ---
    /// Ordered pass-point (anchor) IDs between start and end. Empty = straight
    /// line; non-empty + type==Bezier = interpolating curve through these points.
    /// Each referenced ParamPoint carries tangent handle data (tangentIn/Out).
    std::vector<QUuid> passPointIds;

    /// Global curve tension (0 = standard Catmull-Rom; >0 tighter; <0 looser).
    /// Only meaningful when passPointIds is non-empty.
    double tension = 0.0;

    // Legacy fields (kept for serialization compat; unused)
    QUuid ctrlPointId;   ///< Deprecated: was single control point.
    QUuid ctrlPoint2Id;  ///< Deprecated: was second control point.

    /// True when this segment represents a curve (has pass points).
    [[nodiscard]] bool isCurve() const {
        return type == SegmentType::Bezier && !passPointIds.empty();
    }

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

    // --- Auxiliary (interpolation) points owned by this segment ---
    std::vector<QUuid> auxPointIds;  ///< ParamPoint IDs (isAuxiliary=true, constraint=Interpolated).
};

} // namespace cad::param
