#include "RotateGizmo.h"

#include <cmath>
#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "geometry/Angle.h"

namespace cad::tools {

RotateGizmo::RotateGizmo(CanvasScene* scene)
    : m_scene(scene)
{
}

RotateGizmo::~RotateGizmo()
{
    remove();
}

void RotateGizmo::build(const cad::geo::Vec2& pivotWorld, double startPoseRad, double currentPoseRad, double zoom)
{
    (void)zoom;
    m_pivotWorld = pivotWorld;
    m_startPoseRad = startPoseRad;
    m_currentPoseRad = currentPoseRad;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        if (m_confirmed) {
            m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_startPoseRad, m_currentPoseRad);
        } else {
            m_scene->overlay()->hideRotateGizmo();
        }
    }
}

void RotateGizmo::update(double zoom, double startPoseRad, double currentPoseRad, const QString& badgeText)
{
    (void)zoom;
    m_startPoseRad = startPoseRad;
    m_currentPoseRad = currentPoseRad;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        if (m_confirmed) {
            m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_startPoseRad, m_currentPoseRad, badgeText);
        } else {
            m_scene->overlay()->hideRotateGizmo();
        }
    }
}

void RotateGizmo::setConfirmed(bool confirmed)
{
    if (m_confirmed == confirmed) return;
    m_confirmed = confirmed;
    if (m_scene && m_scene->overlay() && m_visible) {
        if (m_confirmed) {
            m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_startPoseRad, m_currentPoseRad);
        } else {
            m_scene->overlay()->hideRotateGizmo();
        }
    }
}

void RotateGizmo::remove()
{
    m_visible = false;
    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->hideRotateGizmo();
    }
}

bool RotateGizmo::isArcEmpty() const
{
    if (!m_visible) return true;
    // M2：跨度 = normalizeRad(currentPoseRad − startPoseRad)，姿态角是唯一真相。
    return std::abs(cad::geo::normalizeRad(m_currentPoseRad - m_startPoseRad)) <= 1e-4;
}

} // namespace cad::tools
