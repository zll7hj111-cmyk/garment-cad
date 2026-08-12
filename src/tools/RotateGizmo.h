#pragma once

#include <QPointF>

#include "geometry/Vec2.h"

class QGraphicsEllipseItem;
class QGraphicsPathItem;

class CanvasScene;

namespace cad::tools {

/// Protractor gizmo of the rotate tool: pivot ring + reference dash + angle
/// arc. A PURE RENDERER — the owning tool computes all values (zoom /
/// reference direction / arc start & end) and the gizmo only builds and
/// paints the scene items. Owns its items; remove() / destructor detaches
/// them from the scene.
class RotateGizmo
{
public:
    explicit RotateGizmo(CanvasScene* scene);
    ~RotateGizmo();

    /// (Re)build all items around @p pivotWorld (world mm) with the
    /// reference dash along @p refWorldRad. @p zoom keeps item sizes and
    /// the label at constant screen size.
    void build(const cad::geo::Vec2& pivotWorld, double refWorldRad, double zoom);

    /// Refresh the arc from @p arcStartRad to @p arcEndRad (world radians;
    /// the tool normalizes the sweep to the inner arc ≤ 180°) and re-point
    /// the reference dash along @p dashRad (rad) on the fly, so the tool can
    /// re-base the protractor without rebuilding (rotate-copy re-bases on
    /// the original line's direction).
    void update(double zoom, double dashRad, double arcStartRad, double arcEndRad);

    /// Detach and destroy all items (idempotent).
    void remove();

    [[nodiscard]] bool visible() const { return m_arc != nullptr; }

private:
    CanvasScene* m_scene = nullptr;

    QGraphicsEllipseItem*    m_pivotRing = nullptr;  ///< Pivot ring (teal).
    QGraphicsPathItem*       m_refLine = nullptr;    ///< Reference dash (grey).
    QGraphicsPathItem*       m_arc = nullptr;        ///< Angle arc (amber).

    QPointF m_pivotScene;     ///< Pivot in scene coords (cached at build).
    double  m_refWorldRad = 0.0;  ///< Reference direction (rad), cached at build.
};

} // namespace cad::tools
