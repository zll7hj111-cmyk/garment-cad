#include "TransientOverlay.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QPen>
#include <QBrush>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "geometry/Units.h"

namespace cad::canvas {

class TransientOverlay::Impl {
public:
    explicit Impl(CanvasScene* scene) : scene(scene) {}

    ~Impl() {
        // QGraphicsItem 会随 CanvasScene 销毁或在此安全析构（析构时自动脱离 Scene）
    }

    CanvasScene* scene = nullptr;

    // ── Tier 1: Hover Slots ──
    QGraphicsEllipseItem* endpointRing = nullptr;
    QGraphicsEllipseItem* snapRing = nullptr;
    QGraphicsEllipseItem* markerRing = nullptr;
    QGraphicsEllipseItem* markerPoint = nullptr;
    QGraphicsPathItem*    markerCross = nullptr;
    QGraphicsPathItem*    hoverGuideLine = nullptr;

    // ── Tier 2: Gesture Slots ──
    QGraphicsRectItem*    marqueeBox = nullptr;
    QGraphicsPathItem*    gestureGuideLine = nullptr;

    // ── Tier 3: Session Slots (RotateGizmo) ──
    QGraphicsEllipseItem* gizmoPivotRing = nullptr;
    QGraphicsPathItem*    gizmoRefLine = nullptr;
    QGraphicsPathItem*    gizmoPrevPoseLine = nullptr;
    QGraphicsPathItem*    gizmoArc = nullptr;

    // 辅助获取或创建 Item（保持 Item 始终挂载在 Scene）
    template <typename T>
    T* ensureItem(T*& slot) {
        if (!slot) {
            slot = new T();
            slot->setVisible(false);
            if (scene) {
                scene->addItem(slot);
            }
        }
        return slot;
    }
};

TransientOverlay::TransientOverlay(CanvasScene* scene)
    : m_impl(std::make_unique<Impl>(scene))
{
}

TransientOverlay::~TransientOverlay() = default;

void TransientOverlay::clear(OverlayTier tier)
{
    if (tier == OverlayTier::Hover || tier == OverlayTier::All) {
        if (m_impl->endpointRing) m_impl->endpointRing->setVisible(false);
        if (m_impl->snapRing) m_impl->snapRing->setVisible(false);
        if (m_impl->markerRing) m_impl->markerRing->setVisible(false);
        if (m_impl->markerPoint) m_impl->markerPoint->setVisible(false);
        if (m_impl->markerCross) m_impl->markerCross->setVisible(false);
        if (m_impl->hoverGuideLine) m_impl->hoverGuideLine->setVisible(false);
    }

    if (tier == OverlayTier::Gesture || tier == OverlayTier::All) {
        if (m_impl->marqueeBox) m_impl->marqueeBox->setVisible(false);
        if (m_impl->gestureGuideLine) m_impl->gestureGuideLine->setVisible(false);
    }

    if (tier == OverlayTier::Session || tier == OverlayTier::All) {
        hideRotateGizmo();
    }
}

void TransientOverlay::showEndpointHover(const cad::geo::Vec2& worldPos, ScreenPx radius)
{
    auto* item = m_impl->ensureItem(m_impl->endpointRing);
    const double r = radius.value;
    item->setRect(-r, -r, 2.0 * r, 2.0 * r);
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    QPen pen(QColor(0, 172, 193), 2.0);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setBrush(Qt::NoBrush);
    item->setZValue(10000.0);

    item->setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
    item->setVisible(true);
}

void TransientOverlay::showSnapAim(const cad::geo::Vec2& worldPos, ScreenPx radius)
{
    auto* item = m_impl->ensureItem(m_impl->snapRing);
    const double r = radius.value;
    item->setRect(-r, -r, 2.0 * r, 2.0 * r);
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    QPen pen(QColor(140, 100, 0), 1.0);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setBrush(QColor(255, 193, 7)); // 小实心黄点
    item->setZValue(10000.0);

    item->setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
    item->setVisible(true);
}

void TransientOverlay::showMarkerRing(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx radius, double penWidth)
{
    auto* item = m_impl->ensureItem(m_impl->markerRing);
    const double r = radius.value;
    item->setRect(-r, -r, 2.0 * r, 2.0 * r);
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    QPen pen(color, penWidth);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setBrush(Qt::NoBrush);
    item->setZValue(9999.0);

    item->setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
    item->setVisible(true);
}

void TransientOverlay::showMarkerPoint(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx radius)
{
    auto* item = m_impl->ensureItem(m_impl->markerPoint);
    const double r = radius.value;
    item->setRect(-r, -r, 2.0 * r, 2.0 * r);
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    item->setPen(Qt::NoPen);
    item->setBrush(color);
    item->setZValue(9999.0);

    item->setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
    item->setVisible(true);
}

void TransientOverlay::showMarkerCross(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx halfSize)
{
    auto* item = m_impl->ensureItem(m_impl->markerCross);
    const double s = halfSize.value;

    QPainterPath path;
    path.moveTo(-s, -s); path.lineTo(s, s);
    path.moveTo(-s, s);  path.lineTo(s, -s);
    item->setPath(path);
    item->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    QPen pen(color, 1.8);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setBrush(Qt::NoBrush);
    item->setZValue(9999.0);

    item->setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
    item->setVisible(true);
}

void TransientOverlay::showGuideLine(const cad::geo::Vec2& p1World, const cad::geo::Vec2& p2World,
                                     const QColor& color, bool dashed, OverlayTier tier)
{
    auto*& targetSlot = (tier == OverlayTier::Gesture) ? m_impl->gestureGuideLine : m_impl->hoverGuideLine;
    auto* item = m_impl->ensureItem(targetSlot);

    QPointF p1 = cad::geo::Coord::toScene(p1World.x, p1World.y);
    QPointF p2 = cad::geo::Coord::toScene(p2World.x, p2World.y);

    QPainterPath path;
    path.moveTo(p1);
    path.lineTo(p2);
    item->setPath(path);

    QPen pen(color, 1.0);
    pen.setCosmetic(true);
    if (dashed) {
        pen.setStyle(Qt::DashLine);
    }
    item->setPen(pen);
    item->setBrush(Qt::NoBrush);
    item->setZValue(9996.0);
    item->setPos(0, 0);
    item->setVisible(true);
}

void TransientOverlay::showMarqueeBox(const cad::geo::Vec2& p1World, const cad::geo::Vec2& p2World)
{
    auto* item = m_impl->ensureItem(m_impl->marqueeBox);

    QPointF s1 = cad::geo::Coord::toScene(p1World.x, p1World.y);
    QPointF s2 = cad::geo::Coord::toScene(p2World.x, p2World.y);

    QRectF rect(std::min(s1.x(), s2.x()),
               std::min(s1.y(), s2.y()),
               std::abs(s1.x() - s2.x()),
               std::abs(s1.y() - s2.y()));

    item->setRect(rect);
    item->setPen(QPen(QColor(33, 150, 243), 1.0, Qt::DashLine));
    item->setBrush(QColor(33, 150, 243, 30));
    item->setZValue(9995.0);
    item->setVisible(true);
}

void TransientOverlay::showRotateGizmo(const cad::geo::Vec2& pivotWorld,
                                      double refBaseWorldRad,
                                      double prevPoseWorldRad,
                                      double deltaDeg)
{
    const QPointF c = cad::geo::Coord::toScene(pivotWorld.x, pivotWorld.y);

    // 1. Pivot Ring
    auto* ring = m_impl->ensureItem(m_impl->gizmoPivotRing);
    constexpr double ringR = 5.0;
    ring->setRect(-ringR, -ringR, ringR * 2.0, ringR * 2.0);
    ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    ring->setPos(c);

    QPen ringPen(QColor(38, 166, 154), 1.5);
    ringPen.setCosmetic(true);
    ring->setPen(ringPen);
    ring->setBrush(QColor(38, 166, 154, 80));
    ring->setZValue(9998.0);
    ring->setVisible(true);

    // 2. Dash 1: Reference Base Dash along refBaseWorldRad (55 px)
    auto* refLine = m_impl->ensureItem(m_impl->gizmoRefLine);
    constexpr double refLen = 55.0;
    QPainterPath refPath;
    refPath.moveTo(0, 0);
    refPath.lineTo(refLen * std::cos(refBaseWorldRad), -refLen * std::sin(refBaseWorldRad));
    refLine->setPath(refPath);
    refLine->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    refLine->setPos(c);

    QPen refPen(QColor(120, 144, 156), 1.2, Qt::DashLine);
    refPen.setCosmetic(true);
    refLine->setPen(refPen);
    refLine->setBrush(Qt::NoBrush);
    refLine->setZValue(9996.0);
    refLine->setVisible(true);

    // 3. Dash 2: Previous Pose Dash along prevPoseWorldRad (65 px)
    auto* prevLine = m_impl->ensureItem(m_impl->gizmoPrevPoseLine);
    constexpr double prevLen = 65.0;
    QPainterPath prevPath;
    prevPath.moveTo(0, 0);
    prevPath.lineTo(prevLen * std::cos(prevPoseWorldRad), -prevLen * std::sin(prevPoseWorldRad));
    prevLine->setPath(prevPath);
    prevLine->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    prevLine->setPos(c);

    QPen prevPen(QColor(245, 124, 0), 1.2, Qt::DashLine);
    prevPen.setCosmetic(true);
    prevLine->setPen(prevPen);
    prevLine->setBrush(Qt::NoBrush);
    prevLine->setZValue(9997.0);
    prevLine->setVisible(true);

    // 4. Angle Wedge (Arc + Sector) from prevPoseWorldRad by deltaDeg (42 px)
    auto* arc = m_impl->ensureItem(m_impl->gizmoArc);
    constexpr double arcR = 42.0;
    QPainterPath arcPath;
    if (std::abs(deltaDeg) > 1e-4) {
        arcPath.moveTo(0, 0);
        // Scene Y is down => screen angle is negated
        const double sceneStartDeg = -prevPoseWorldRad * 180.0 / M_PI;
        const double sceneSweepDeg = -deltaDeg;
        arcPath.arcTo(QRectF(-arcR, -arcR, arcR * 2.0, arcR * 2.0), sceneStartDeg, sceneSweepDeg);
        arcPath.closeSubpath();
    }
    arc->setPath(arcPath);
    arc->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    arc->setPos(c);

    QPen arcPen(QColor(251, 140, 0), 1.8, Qt::SolidLine);
    arcPen.setCosmetic(true);
    arc->setPen(arcPen);
    arc->setBrush(QBrush(QColor(251, 140, 0, 38))); // semi-transparent sector
    arc->setZValue(9998.0);
    arc->setVisible(true);
}

void TransientOverlay::showRotateGizmo(const cad::geo::Vec2& pivotWorld,
                                      double refWorldRad,
                                      double arcStartWorldRad,
                                      double arcEndWorldRad,
                                      bool isConfirmed)
{
    (void)isConfirmed;
    const double deltaDeg = (arcEndWorldRad - arcStartWorldRad) * 180.0 / M_PI;
    showRotateGizmo(pivotWorld, refWorldRad, arcStartWorldRad, deltaDeg);
}

void TransientOverlay::hideRotateGizmo()
{
    if (m_impl->gizmoPivotRing) m_impl->gizmoPivotRing->setVisible(false);
    if (m_impl->gizmoRefLine) m_impl->gizmoRefLine->setVisible(false);
    if (m_impl->gizmoPrevPoseLine) m_impl->gizmoPrevPoseLine->setVisible(false);
    if (m_impl->gizmoArc) m_impl->gizmoArc->setVisible(false);
}

bool TransientOverlay::isEndpointHoverVisible() const
{
    return m_impl->endpointRing && m_impl->endpointRing->isVisible();
}

bool TransientOverlay::isSnapAimVisible() const
{
    return m_impl->snapRing && m_impl->snapRing->isVisible();
}

bool TransientOverlay::isMarkerCrossVisible() const
{
    return m_impl->markerCross && m_impl->markerCross->isVisible();
}

bool TransientOverlay::isRotateGizmoVisible() const
{
    return (m_impl->gizmoPivotRing && m_impl->gizmoPivotRing->isVisible()) ||
           (m_impl->gizmoArc && m_impl->gizmoArc->isVisible());
}

} // namespace cad::canvas
