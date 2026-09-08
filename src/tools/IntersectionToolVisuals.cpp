#include "tools/IntersectionToolVisuals.h"

#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QPainterPath>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/HudItem.h"
#include "geometry/Units.h"

namespace cad::tools {

IntersectionToolVisuals::IntersectionToolVisuals(CanvasScene* scene)
    : m_scene(scene)
{
}

void IntersectionToolVisuals::setScene(CanvasScene* scene)
{
    m_scene = scene;
}

void IntersectionToolVisuals::showSegHighlight(const cad::geo::Vec2& w1,
                                               const cad::geo::Vec2& w2,
                                               bool hover)
{
    if (!m_scene) return;
    if (m_segHighlight) {
        QPen pen(m_scene->style()->previewLineColor, hover ? 2.0 : 2.5);
        pen.setCosmetic(true);
        m_segHighlight->setPen(pen);
    } else {
        m_segHighlight = new QGraphicsLineItem();
        QPen pen(m_scene->style()->previewLineColor, hover ? 2.0 : 2.5);
        pen.setCosmetic(true);
        m_segHighlight->setPen(pen);
        m_segHighlight->setZValue(100.0);
        m_scene->addItem(m_segHighlight);
        m_managed.own(m_segHighlight, &m_segHighlight);
    }
    m_segHighlight->setLine(QLineF(cad::geo::Coord::toScene(w1),
                                   cad::geo::Coord::toScene(w2)));
    m_segHighlight->setVisible(true);
}

void IntersectionToolVisuals::hideSegHighlight()
{
    if (m_segHighlight)
        m_segHighlight->setVisible(false);
}

void IntersectionToolVisuals::showOriginMarker(const cad::geo::Vec2& originPos)
{
    if (!m_scene) return;
    if (!m_originMarker) {
        constexpr double r = 5.0;
        m_originMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen pen(m_scene->style()->snapPointColor, 2.0);
        pen.setCosmetic(true);
        m_originMarker->setPen(pen);
        m_originMarker->setBrush(Qt::NoBrush);
        m_originMarker->setZValue(102.0);
        m_scene->addItem(m_originMarker);
        m_managed.own(m_originMarker, &m_originMarker);
    }
    m_originMarker->setPos(cad::geo::Coord::toScene(originPos));
    m_originMarker->setVisible(true);
}

void IntersectionToolVisuals::hideOriginMarker()
{
    if (m_originMarker)
        m_originMarker->setVisible(false);
}

void IntersectionToolVisuals::showAimMarker(const cad::geo::Vec2& aimPos)
{
    if (!m_scene) return;
    if (!m_aimMarker) {
        constexpr double r = 5.0;
        m_aimMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen pen(m_scene->style()->snapPointColor, 1.6);
        pen.setCosmetic(true);
        m_aimMarker->setPen(pen);
        m_aimMarker->setBrush(Qt::NoBrush);
        m_aimMarker->setZValue(102.0);
        m_scene->addItem(m_aimMarker);
        m_managed.own(m_aimMarker, &m_aimMarker);
    }
    m_aimMarker->setPos(cad::geo::Coord::toScene(aimPos));
    m_aimMarker->setVisible(true);
}

void IntersectionToolVisuals::hideAimMarker()
{
    if (m_aimMarker)
        m_aimMarker->setVisible(false);
}

void IntersectionToolVisuals::showRayAndHit(const cad::geo::Vec2& originPos,
                                            const std::optional<cad::geo::Vec2>& hit,
                                            double rayThetaRad)
{
    if (!m_scene) return;

    if (!m_previewRay) {
        m_previewRay = new QGraphicsLineItem();
        QPen pen(m_scene->style()->previewLineColor, 1.2);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_previewRay->setPen(pen);
        m_previewRay->setZValue(101.0);
        m_scene->addItem(m_previewRay);
        m_managed.own(m_previewRay, &m_previewRay);
    }

    if (hit) {
        m_previewRay->setLine(QLineF(cad::geo::Coord::toScene(originPos),
                                     cad::geo::Coord::toScene(*hit)));
        m_previewRay->setVisible(true);

        if (!m_intersectDot) {
            constexpr double r = 4.0;
            m_intersectDot = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            m_intersectDot->setPen(Qt::NoPen);
            m_intersectDot->setBrush(m_scene->style()->snapIndicatorColor);
            m_intersectDot->setZValue(103.0);
            m_scene->addItem(m_intersectDot);
            m_managed.own(m_intersectDot, &m_intersectDot);
        }
        m_intersectDot->setPos(cad::geo::Coord::toScene(*hit));
        m_intersectDot->setVisible(true);
        if (m_noHitMarker) m_noHitMarker->setVisible(false);
    } else {
        cad::geo::Vec2 dir{std::cos(rayThetaRad), std::sin(rayThetaRad)};
        cad::geo::Vec2 rayEnd = originPos + dir * 200.0;
        m_previewRay->setLine(QLineF(cad::geo::Coord::toScene(originPos),
                                     cad::geo::Coord::toScene(rayEnd)));
        m_previewRay->setVisible(true);
        if (m_intersectDot) m_intersectDot->setVisible(false);

        if (!m_noHitMarker) {
            constexpr double s = 5.0;
            QPainterPath cross;
            cross.moveTo(-s, -s); cross.lineTo(s, s);
            cross.moveTo(-s, s);  cross.lineTo(s, -s);
            m_noHitMarker = new QGraphicsPathItem(cross);
            QPen pen(m_scene->style()->snapIndicatorColor, 1.8);
            pen.setCosmetic(true);
            m_noHitMarker->setPen(pen);
            m_noHitMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            m_noHitMarker->setZValue(103.0);
            m_scene->addItem(m_noHitMarker);
            m_managed.own(m_noHitMarker, &m_noHitMarker);
        }
        m_noHitMarker->setPos(cad::geo::Coord::toScene(rayEnd));
        m_noHitMarker->setVisible(true);
    }
}

void IntersectionToolVisuals::clearPreview()
{
    if (m_previewRay)   m_previewRay->setVisible(false);
    if (m_intersectDot) m_intersectDot->setVisible(false);
    if (m_noHitMarker)  m_noHitMarker->setVisible(false);
    if (m_originMarker) m_originMarker->setVisible(false);
    if (m_aimMarker)    m_aimMarker->setVisible(false);
}

void IntersectionToolVisuals::clearAll()
{
    clearPreview();
    if (m_segHighlight) m_segHighlight->setVisible(false);
    if (m_scene) {
        m_managed.clear();
    }
    m_previewRay   = nullptr;
    m_intersectDot = nullptr;
    m_noHitMarker  = nullptr;
    m_originMarker = nullptr;
    m_aimMarker    = nullptr;
    m_segHighlight = nullptr;
}

void IntersectionToolVisuals::updateStepHud(HudItem* hud,
                                            const cad::geo::Vec2& cursorPos,
                                            const QString& text)
{
    if (!m_scene || !hud) return;
    hud->setText(text);
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    hud->moveToPoint(cursorPos, view, HudItem::kCursorOffset);
    hud->setVisible(true);
}

void IntersectionToolVisuals::updateAimHud(HudItem* hud,
                                           const cad::geo::Vec2& cursorPos,
                                           bool hasAimPos,
                                           bool isBorrowAim,
                                           const QString& aimLabel,
                                           double displayDeg,
                                           bool worldAngleMode,
                                           bool hasHit,
                                           double t)
{
    if (!m_scene || !hud) return;
    QString text;
    if (hasAimPos) {
        text = QString::fromUtf8("指向点 %1 = %2°")
            .arg(aimLabel.isEmpty() ? QString::fromUtf8("(点)") : aimLabel,
                 cad::geo::Units::formatDegValue(displayDeg));
        if (isBorrowAim)
            text += QString::fromUtf8(" | ⚠ 无交点，射线未穿过线段");
        else
            text += QString::fromUtf8(" | 点击创建");
    } else {
        const QString modeLabel = worldAngleMode
            ? QString::fromUtf8("绝对角度")
            : QString::fromUtf8("跟随角度");
        text = QString::fromUtf8("%1 = %2°")
            .arg(modeLabel, cad::geo::Units::formatDegValue(displayDeg));
    }
    if (hasHit) {
        text += QString::fromUtf8(" | t = %1").arg(cad::geo::Units::formatNumberTrimmed(t, 3));
    } else {
        text += QString::fromUtf8(" | ⚠ 无交点（点击无效）");
    }
    hud->setText(text);
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    hud->moveToPoint(cursorPos, view, HudItem::kCursorOffset);
    hud->setVisible(true);
}

void IntersectionToolVisuals::flashSuccessHud(HudItem* hud,
                                              const cad::geo::Vec2& cursorPos,
                                              const QString& text)
{
    if (!m_scene || !hud) return;
    hud->setText(text);
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    hud->moveToPoint(cursorPos, view, HudItem::kCursorOffset);
    hud->setVisible(true);
}

} // namespace cad::tools
