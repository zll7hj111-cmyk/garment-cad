#pragma once

#include <QString>
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

    /// (Re)build all items around @p pivotWorld with start-pose + current-pose rays.
    void build(const cad::geo::Vec2& pivotWorld, double startPoseRad, double currentPoseRad, double zoom = 1.0);

    /// Refresh dashes and sweep wedge from the two poses, with optional degree badge.
    /// 跨度由姿态差唯一确定（M2），不再另收 deltaDeg —— 避免两个角度真相互相矛盾。
    void update(double zoom, double startPoseRad, double currentPoseRad, const QString& badgeText = QString());

    /// D15 确认门兼容（已无实际视觉差别）
    void setConfirmed(bool confirmed);

    [[nodiscard]] bool confirmed() const { return m_confirmed; }

    /// Detach and hide all items (idempotent).
    void remove();

    [[nodiscard]] bool visible() const { return m_visible; }
    [[nodiscard]] double startPoseRad() const { return m_startPoseRad; }
    [[nodiscard]] double currentPoseRad() const { return m_currentPoseRad; }
    [[nodiscard]] bool isArcEmpty() const;

private:
    CanvasScene* m_scene = nullptr;

    cad::geo::Vec2 m_pivotWorld;
    double m_startPoseRad = 0.0;
    double m_currentPoseRad = 0.0;
    bool   m_confirmed = false;
    bool   m_visible = false;
};

} // namespace cad::tools
