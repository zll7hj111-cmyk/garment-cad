#include "RotateGizmo.h"

#include <cmath>
#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"

namespace cad::tools {

RotateGizmo::RotateGizmo(CanvasScene* scene)
    : m_scene(scene)
{
}

RotateGizmo::~RotateGizmo()
{
    remove();
}

void RotateGizmo::build(const cad::geo::Vec2& pivotWorld, double refBaseRad, double prevPoseRad, double zoom)
{
    (void)zoom;
    m_pivotWorld = pivotWorld;
    m_refBaseRad = refBaseRad;
    m_prevPoseRad = prevPoseRad;
    m_deltaDeg = 0.0;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_refBaseRad, m_prevPoseRad, m_deltaDeg);
    }
}

void RotateGizmo::update(double zoom, double refBaseRad, double prevPoseRad, double deltaDeg)
{
    (void)zoom;
    m_refBaseRad = refBaseRad;
    m_prevPoseRad = prevPoseRad;
    m_deltaDeg = deltaDeg;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_refBaseRad, m_prevPoseRad, m_deltaDeg);
    }
}

void RotateGizmo::build(const cad::geo::Vec2& pivotWorld, double refWorldRad, double zoom)
{
    build(pivotWorld, refWorldRad, refWorldRad, zoom);
}

void RotateGizmo::setConfirmed(bool confirmed)
{
    m_confirmed = confirmed;
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
    return std::abs(m_deltaDeg) <= 1e-4;
}

} // namespace cad::tools
