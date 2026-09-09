#pragma once

#include <QUuid>

#include "geometry/Vec2.h"

class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// CircleFactory — the M1 creation path for the parametric circle
/// (docs/design/CIRCLE_TOOL_DESIGN.md D1/D3/D11/D12/D21).
///
/// One circle = one Block holding a single Bezier segment whose
/// `fitKind == FitKind::Circle`:
///   · a Free center point at the block origin;
///   · two Polar endpoints at (r, a0) and (r, a0 + 360°). They coincide in
///     position but keep distinct ids, so the whole code base still sees a
///     start and an end point (D12). The end point is hidden and unselectable
///     so the canvas does not stack two handles on the same spot;
///   · three CurveAnchor points at 25% / 50% / 75% of the sweep (D11 places
///     them analytically on the circle; without that branch a full circle's
///     zero-length chord would collapse all three onto the center);
///   · `Block::applyCircleFitTangents()` refits the four 90° cubic spans to a
///     true circle on every resolve, so editing the radius keeps it a circle.
///
/// Creation goes through the undo stack as a single command (reusing
/// DrawLineCommand — the model shape is identical; only the undo text differs).
class CircleFactory
{
public:
    using Vec2 = cad::geo::Vec2;

    CircleFactory(cad::param::ParamDocument* doc, QUndoStack* undoStack);

    /// Create a full circle centered at @p center with radius @p radiusMm.
    /// @p startAngleDeg is the base angle a0 of the start point (D18): the seam
    /// and the quadrant anchors rotate with it, the center does not move.
    /// @return the new block id, or a null QUuid when nothing was created.
    QUuid createCircle(const Vec2& center, double radiusMm, double startAngleDeg = 0.0);

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
};

} // namespace cad::tools
