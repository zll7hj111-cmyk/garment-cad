#pragma once

#include "geometry/Vec2.h"

class CanvasScene;

namespace cad::tools {

/// Protractor gizmo of the rotate tool: pivot ring + reference dash + angle arc.
/// 现已接入统一瞬态视觉层 TransientOverlay (Session tier)。
/// 不再在工具层管理 QGraphicsItem 裸指针，底层几何计算与图元复用全托管。
class RotateGizmo
{
public:
    explicit RotateGizmo(CanvasScene* scene);
    ~RotateGizmo();

    /// (Re)build all items around @p pivotWorld (world mm) with the reference dash along @p refWorldRad.
    void build(const cad::geo::Vec2& pivotWorld, double refWorldRad, double zoom = 1.0);

    /// Refresh the arc from @p arcStartRad to @p arcEndRad (world radians) and reference dash.
    void update(double zoom, double dashRad, double arcStartRad, double arcEndRad);

    /// D15 确认门可视区分: 未确认 = 虚线弧 + 空心环; 确认 = 实线弧 + 实心环。
    void setConfirmed(bool confirmed);

    [[nodiscard]] bool confirmed() const { return m_confirmed; }

    /// Detach and hide all items (idempotent).
    void remove();

    [[nodiscard]] bool visible() const { return m_visible; }
    [[nodiscard]] double refWorldRad() const { return m_refWorldRad; }
    [[nodiscard]] bool isArcEmpty() const;

private:
    CanvasScene* m_scene = nullptr;

    cad::geo::Vec2 m_pivotWorld;
    double m_refWorldRad = 0.0;
    double m_arcStartRad = 0.0;
    double m_arcEndRad = 0.0;
    bool   m_confirmed = false;
    bool   m_visible = false;
};

} // namespace cad::tools
