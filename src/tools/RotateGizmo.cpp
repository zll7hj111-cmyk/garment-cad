#include "RotateGizmo.h"

#include <cmath>

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
#include <QPointF>

#include "canvas/CanvasScene.h"
#include "geometry/Units.h"

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
    if (!m_scene) return;
    remove();

    m_refWorldRad = refWorldRad;
    m_pivotScene = cad::geo::Coord::toScene(pivotWorld.x, pivotWorld.y);
    const QPointF& c = m_pivotScene;

    // Pivot ring (teal).
    m_pivotRing = new QGraphicsEllipseItem();
    QPen ringPen(QColor(38, 166, 154));
    ringPen.setWidthF(2.0);
    ringPen.setCosmetic(true);
    m_pivotRing->setPen(ringPen);
    m_pivotRing->setBrush(QColor(38, 166, 154, 40));
    m_pivotRing->setZValue(9998);
    const double r = 6.0 / zoom;
    m_pivotRing->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);

    // Reference dashed line along the reference direction (60 px).
    m_refLine = new QGraphicsPathItem();
    QPen refPen(QColor(120, 144, 156));
    refPen.setWidthF(1.0);
    refPen.setCosmetic(true);
    refPen.setStyle(Qt::DashLine);
    m_refLine->setPen(refPen);
    m_refLine->setZValue(9997);
    const double refLen = 60.0 / zoom;
    QPainterPath refPath;
    refPath.moveTo(c);
    refPath.lineTo(c.x() + refLen * std::cos(m_refWorldRad),
                   c.y() - refLen * std::sin(m_refWorldRad));  // scene is y-down
    m_refLine->setPath(refPath);

    // Angle arc (amber) — geometry refreshed by update().
    m_arc = new QGraphicsPathItem();
    QPen arcPen(QColor(251, 140, 0));
    arcPen.setWidthF(2.0);
    arcPen.setCosmetic(true);
    m_arc->setPen(arcPen);
    m_arc->setZValue(9998);

    m_scene->addItem(m_pivotRing);
    m_scene->addItem(m_refLine);
    m_scene->addItem(m_arc);
}

void RotateGizmo::update(double zoom, double dashRad, double arcStartRad, double arcEndRad)
{
    if (!m_scene || !m_arc) return;
    const QPointF& c = m_pivotScene;

    // The reference dash re-bases on demand (rotate-copy points it along the
    // ORIGINAL line so the amber arc shows the true fold between the two).
    if (std::abs(dashRad - m_refWorldRad) > 1e-9) {
        m_refWorldRad = dashRad;
        const double refLen = 60.0 / zoom;
        QPainterPath refPath;
        refPath.moveTo(c);
        refPath.lineTo(c.x() + refLen * std::cos(m_refWorldRad),
                       c.y() - refLen * std::sin(m_refWorldRad));  // scene is y-down
        m_refLine->setPath(refPath);
    }

    // Arc from the start to the end direction (the tool normalizes the sweep
    // to the inner arc, so the linear interpolation never wraps around).
    const double arcR = 40.0 / zoom;
    QPainterPath arcPath;
    constexpr int kSamples = 40;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = arcStartRad + (arcEndRad - arcStartRad) * i / kSamples;
        const QPointF p(c.x() + arcR * std::cos(t),
                        c.y() - arcR * std::sin(t));           // scene is y-down
        if (i == 0) arcPath.moveTo(p); else arcPath.lineTo(p);
    }
    m_arc->setPath(arcPath);

    QPen arcPen(QColor(251, 140, 0));
    arcPen.setWidthF(2.0);
    arcPen.setCosmetic(true);
    m_arc->setPen(arcPen);
}

void RotateGizmo::remove()
{
    if (!m_scene) return;
    if (m_pivotRing) { m_scene->removeItem(m_pivotRing); delete m_pivotRing; m_pivotRing = nullptr; }
    if (m_refLine)   { m_scene->removeItem(m_refLine);   delete m_refLine;   m_refLine = nullptr; }
    if (m_arc)       { m_scene->removeItem(m_arc);       delete m_arc;       m_arc = nullptr; }
}

} // namespace cad::tools
