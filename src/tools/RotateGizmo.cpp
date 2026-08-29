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

namespace {

/// D15 确认态两套视觉 (TOOL_SYSTEM_AUDIT H2): 选中未确认 = 虚线弧 + 空心
/// 锚环 + 减淡配色 ("还差右键/回车一步"); 已确认 = 实线弧 + 实心锚环 +
/// 全强度配色。幂等 —— 同态重复调用无害。
void applyConfirmationStyle(QGraphicsEllipseItem* ring,
                            QGraphicsPathItem* arc, bool confirmed)
{
    if (!ring || !arc) return;
    constexpr QColor kTeal(38, 166, 154);
    constexpr QColor kAmber(251, 140, 0);
    if (confirmed) {
        QPen ringPen(kTeal);
        ringPen.setWidthF(2.0);
        ringPen.setCosmetic(true);
        ring->setPen(ringPen);
        ring->setBrush(QColor(38, 166, 154, 40));
        QPen arcPen(kAmber);
        arcPen.setWidthF(2.0);
        arcPen.setCosmetic(true);
        arc->setPen(arcPen);
    } else {
        QPen ringPen(kTeal);
        ringPen.setWidthF(1.5);
        ringPen.setCosmetic(true);
        ringPen.setStyle(Qt::DashLine);
        ring->setPen(ringPen);
        ring->setBrush(Qt::NoBrush);   // 空心: 一眼区分"未确认"
        QPen arcPen(QColor(251, 140, 0, 140));  // 减淡琥珀
        arcPen.setWidthF(2.0);
        arcPen.setCosmetic(true);
        arcPen.setStyle(Qt::DashLine);
        arc->setPen(arcPen);
    }
}

} // namespace

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
    m_managed.own(m_pivotRing, &m_pivotRing);
    m_scene->addItem(m_refLine);
    m_managed.own(m_refLine, &m_refLine);
    m_scene->addItem(m_arc);
    m_managed.own(m_arc, &m_arc);

    applyConfirmationStyle(m_pivotRing, m_arc, m_confirmed);
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

    // 弧重建后按确认态重上样式 (update 曾无条件写实线琥珀, 会覆盖未确认态).
    applyConfirmationStyle(m_pivotRing, m_arc, m_confirmed);
}

void RotateGizmo::setConfirmed(bool confirmed)
{
    if (m_confirmed == confirmed) return;
    m_confirmed = confirmed;
    applyConfirmationStyle(m_pivotRing, m_arc, m_confirmed);
}

void RotateGizmo::remove()
{
    // 统一释放 + 影子置空 (P1/L1); QGraphicsItem 析构自行脱离 scene,
    // 无 scene 时同样安全 —— 幂等。
    m_managed.clear();
}

} // namespace cad::tools
