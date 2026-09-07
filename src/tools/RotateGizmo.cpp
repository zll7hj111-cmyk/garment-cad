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

void RotateGizmo::build(const cad::geo::Vec2& pivotWorld, double refWorldRad, double zoom)
{
    (void)zoom;
    m_pivotWorld = pivotWorld;
    m_refWorldRad = refWorldRad;
    m_arcStartRad = refWorldRad;
    m_arcEndRad = refWorldRad;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_refWorldRad,
                                            m_arcStartRad, m_arcEndRad, m_confirmed);
    }
}

void RotateGizmo::update(double zoom, double dashRad, double arcStartRad, double arcEndRad)
{
    (void)zoom;
    m_refWorldRad = dashRad;
    m_arcStartRad = arcStartRad;
    m_arcEndRad = arcEndRad;
    m_visible = true;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_refWorldRad,
                                            m_arcStartRad, m_arcEndRad, m_confirmed);
    }
}

void RotateGizmo::setConfirmed(bool confirmed)
{
    if (m_confirmed == confirmed) return;
    m_confirmed = confirmed;
    if (m_visible && m_scene && m_scene->overlay()) {
        m_scene->overlay()->showRotateGizmo(m_pivotWorld, m_refWorldRad,
                                            m_arcStartRad, m_arcEndRad, m_confirmed);
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
    return std::abs(m_arcEndRad - m_arcStartRad) <= 1e-6;
}

} // namespace cad::tools
