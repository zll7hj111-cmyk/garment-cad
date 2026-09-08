#include "TransientOverlay.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QFont>
#include <QPen>
#include <QBrush>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasFonts.h"
#include "canvas/CanvasStyle.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"

namespace cad::canvas {

class TransientOverlay::Impl {
public:
    explicit Impl(CanvasScene* scene) : scene(scene) {}

    ~Impl() {
        // QGraphicsItem 会随 CanvasScene 销毁或在此安全析构（析构时自动脱离 Scene）
    }

    CanvasScene* scene = nullptr;

    /// 画布调色板：无场景时回落到共享默认表 (审计 P0-1)
    const CanvasStyle& style() const {
        return scene ? *scene->style() : CanvasStyle::fallback();
    }

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
    QGraphicsEllipseItem*    gizmoPivotRing = nullptr;
    QGraphicsEllipseItem*    gizmoPivotDot = nullptr;
    QGraphicsPathItem*       gizmoPivotCross = nullptr;
    QGraphicsPathItem*       gizmoRefLine = nullptr;
    QGraphicsPathItem*       gizmoPrevPoseLine = nullptr;
    QGraphicsPathItem*       gizmoArc = nullptr;
    QGraphicsPathItem*       gizmoBadgeBg = nullptr;
    QGraphicsSimpleTextItem* gizmoBadgeText = nullptr;

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

    QPen pen(m_impl->style().endpointHoverColor, 2.0);
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

    const CanvasStyle& st = m_impl->style();
    QPen pen(st.snapAimOutlineColor, 1.0);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setBrush(st.snapAimColor); // 小实心黄点
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
    const QColor marquee = m_impl->style().marqueeColor;
    item->setPen(QPen(marquee, 1.0, Qt::DashLine));
    item->setBrush(QColor(marquee.red(), marquee.green(), marquee.blue(), 30));
    item->setZValue(9995.0);
    item->setVisible(true);
}

void TransientOverlay::showRotateGizmo(const cad::geo::Vec2& pivotWorld,
                                      double refBaseWorldRad,
                                      double prevPoseWorldRad,
                                      double deltaDeg,
                                      const QString& badgeText)
{
    const QPointF c = cad::geo::Coord::toScene(pivotWorld.x, pivotWorld.y);

    // 1. Pivot Anchor (Professional CAD Bullseye: Ring + Center Dot + Crosshair Ticks)
    auto* ring = m_impl->ensureItem(m_impl->gizmoPivotRing);
    constexpr double ringR = 6.0;
    ring->setRect(-ringR, -ringR, ringR * 2.0, ringR * 2.0);
    ring->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    ring->setPos(c);

    const CanvasStyle& st = m_impl->style();
    const QColor teal = st.snapNodeColor;
    QPen ringPen(teal, 1.5);
    ringPen.setCosmetic(true);
    ring->setPen(ringPen);
    ring->setBrush(QColor(teal.red(), teal.green(), teal.blue(), 40));
    ring->setZValue(9998.0);
    ring->setVisible(true);

    auto* dot = m_impl->ensureItem(m_impl->gizmoPivotDot);
    constexpr double dotR = 2.0;
    dot->setRect(-dotR, -dotR, dotR * 2.0, dotR * 2.0);
    dot->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    dot->setPos(c);
    dot->setPen(Qt::NoPen);
    dot->setBrush(teal);
    dot->setZValue(9999.0);
    dot->setVisible(true);

    auto* cross = m_impl->ensureItem(m_impl->gizmoPivotCross);
    QPainterPath crossPath;
    crossPath.moveTo(-9.0, 0.0); crossPath.lineTo(-4.0, 0.0);
    crossPath.moveTo(4.0, 0.0);  crossPath.lineTo(9.0, 0.0);
    crossPath.moveTo(0.0, -9.0); crossPath.lineTo(0.0, -4.0);
    crossPath.moveTo(0.0, 4.0);  crossPath.lineTo(0.0, 9.0);
    cross->setPath(crossPath);
    cross->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    cross->setPos(c);

    QPen crossPen(teal, 1.2);
    crossPen.setCosmetic(true);
    cross->setPen(crossPen);
    cross->setBrush(Qt::NoBrush);
    cross->setZValue(9999.0);
    cross->setVisible(true);

    // 2. Dash 1: Reference Base Dash along refBaseWorldRad (55 px)
    auto* refLine = m_impl->ensureItem(m_impl->gizmoRefLine);
    constexpr double refLen = 55.0;
    QPainterPath refPath;
    refPath.moveTo(0, 0);
    refPath.lineTo(refLen * std::cos(refBaseWorldRad), -refLen * std::sin(refBaseWorldRad));
    refLine->setPath(refPath);
    refLine->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    refLine->setPos(c);

    QPen refPen(st.gizmoBaseColor, 1.2, Qt::DashLine);
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

    QPen prevPen(st.gizmoAccentColor, 1.2, Qt::DashLine);
    prevPen.setCosmetic(true);
    prevLine->setPen(prevPen);
    prevLine->setBrush(Qt::NoBrush);
    prevLine->setZValue(9997.0);
    prevLine->setVisible(true);

    // 4. Angle Wedge (Arc + Sector) from refBaseWorldRad to prevPoseWorldRad (current angle)
    auto* arc = m_impl->ensureItem(m_impl->gizmoArc);
    constexpr double arcR = 42.0;
    QPainterPath arcPath;
    const double sweepDeg = cad::geo::radToDeg(cad::geo::normalizeRad(prevPoseWorldRad - refBaseWorldRad));
    if (std::abs(sweepDeg) > 1e-3) {
        arcPath.moveTo(0, 0);
        const double sceneStartDeg = cad::geo::radToDeg(refBaseWorldRad);
        arcPath.arcTo(QRectF(-arcR, -arcR, arcR * 2.0, arcR * 2.0), sceneStartDeg, sweepDeg);
        arcPath.closeSubpath();
    }
    arc->setPath(arcPath);
    arc->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    arc->setPos(c);

    const QColor accent = st.gizmoAccentColor;
    QPen arcPen(accent, 1.8, Qt::SolidLine);
    arcPen.setCosmetic(true);
    arc->setPen(arcPen);
    arc->setBrush(QBrush(QColor(accent.red(), accent.green(), accent.blue(), 38))); // semi-transparent sector
    arc->setZValue(9998.0);
    arc->setVisible(true);

    // 5. Canvas Floating Degree Text Badge
    if (!badgeText.isEmpty()) {
        auto* bg = m_impl->ensureItem(m_impl->gizmoBadgeBg);
        auto* textItem = m_impl->ensureItem(m_impl->gizmoBadgeText);

        const double curRad = prevPoseWorldRad;
        constexpr double badgeDist = 58.0;
        const double bx = badgeDist * std::cos(curRad);
        const double by = -badgeDist * std::sin(curRad);

        textItem->setFont(canvas_fonts::uiFontPt(9, true));
        textItem->setText(badgeText);
        textItem->setBrush(st.gizmoBadgeFg);
        textItem->setPen(Qt::NoPen);

        QRectF tb = textItem->boundingRect();
        constexpr double padX = 5.0;
        constexpr double padY = 2.0;
        QRectF bgRect(bx - tb.width() / 2.0 - padX, by - tb.height() / 2.0 - padY,
                      tb.width() + 2.0 * padX, tb.height() + 2.0 * padY);
        QPainterPath bgPath;
        bgPath.addRoundedRect(bgRect, 3.0, 3.0);
        bg->setPath(bgPath);
        const QColor badgeFg = st.gizmoBadgeFg;
        bg->setPen(QPen(QColor(badgeFg.red(), badgeFg.green(), badgeFg.blue(), 120), 1.0));
        bg->setBrush(st.gizmoBadgeBg);

        bg->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        bg->setPos(c);
        bg->setZValue(10001.0);
        bg->setVisible(true);

        textItem->setParentItem(bg);
        textItem->setPos(bx - tb.width() / 2.0, by - tb.height() / 2.0);
        textItem->setZValue(10002.0);
        textItem->setVisible(true);
    } else {
        if (m_impl->gizmoBadgeBg) m_impl->gizmoBadgeBg->setVisible(false);
        if (m_impl->gizmoBadgeText) m_impl->gizmoBadgeText->setVisible(false);
    }
}

void TransientOverlay::showRotateGizmo(const cad::geo::Vec2& pivotWorld,
                                      double refWorldRad,
                                      double arcStartWorldRad,
                                      double arcEndWorldRad,
                                      bool isConfirmed)
{
    (void)isConfirmed;
    const double deltaDeg = cad::geo::radToDeg(arcEndWorldRad - arcStartWorldRad);
    showRotateGizmo(pivotWorld, refWorldRad, arcStartWorldRad, deltaDeg, QString());
}

void TransientOverlay::hideRotateGizmo()
{
    if (m_impl->gizmoPivotRing) m_impl->gizmoPivotRing->setVisible(false);
    if (m_impl->gizmoPivotDot) m_impl->gizmoPivotDot->setVisible(false);
    if (m_impl->gizmoPivotCross) m_impl->gizmoPivotCross->setVisible(false);
    if (m_impl->gizmoRefLine) m_impl->gizmoRefLine->setVisible(false);
    if (m_impl->gizmoPrevPoseLine) m_impl->gizmoPrevPoseLine->setVisible(false);
    if (m_impl->gizmoArc) m_impl->gizmoArc->setVisible(false);
    if (m_impl->gizmoBadgeBg) m_impl->gizmoBadgeBg->setVisible(false);
    if (m_impl->gizmoBadgeText) m_impl->gizmoBadgeText->setVisible(false);
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
