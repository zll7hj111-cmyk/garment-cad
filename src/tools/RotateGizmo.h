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

    /// (Re)build all items around @p pivotWorld with dual reference dashes.
    void build(const cad::geo::Vec2& pivotWorld, double refBaseRad, double prevPoseRad, double zoom = 1.0);

    /// Refresh dual dashes and sweep wedge for deltaDeg.
    void update(double zoom, double refBaseRad, double prevPoseRad, double deltaDeg);

    /// 兼容旧版：单基准弧线构建
    void build(const cad::geo::Vec2& pivotWorld, double refWorldRad, double zoom = 1.0);

    /// D15 确认门兼容（已无实际视觉差别）
    void setConfirmed(bool confirmed);

    [[nodiscard]] bool confirmed() const { return m_confirmed; }

    /// Detach and hide all items (idempotent).
    void remove();

    [[nodiscard]] bool visible() const { return m_visible; }
    [[nodiscard]] double refBaseRad() const { return m_refBaseRad; }
    [[nodiscard]] double prevPoseRad() const { return m_prevPoseRad; }
    [[nodiscard]] double deltaDeg() const { return m_deltaDeg; }
    [[nodiscard]] double refWorldRad() const { return m_refBaseRad; }
    [[nodiscard]] bool isArcEmpty() const;

private:
    CanvasScene* m_scene = nullptr;

    cad::geo::Vec2 m_pivotWorld;
    double m_refBaseRad = 0.0;
    double m_prevPoseRad = 0.0;
    double m_deltaDeg = 0.0;
    bool   m_confirmed = false;
    bool   m_visible = false;
};

} // namespace cad::tools
