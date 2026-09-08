#pragma once

#include "canvas/ManagedItems.h"
#include "geometry/Vec2.h"
#include <optional>
#include <QString>

class CanvasScene;
class HudItem;
class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsPathItem;

namespace cad::tools {

class IntersectionToolVisuals
{
public:
    explicit IntersectionToolVisuals(CanvasScene* scene = nullptr);

    void setScene(CanvasScene* scene);

    void showSegHighlight(const cad::geo::Vec2& w1, const cad::geo::Vec2& w2, bool hover);
    void hideSegHighlight();

    void showOriginMarker(const cad::geo::Vec2& originPos);
    void hideOriginMarker();

    void showAimMarker(const cad::geo::Vec2& aimPos);
    void hideAimMarker();

    void showRayAndHit(const cad::geo::Vec2& originPos,
                       const std::optional<cad::geo::Vec2>& hit,
                       double rayThetaRad);

    void clearPreview();
    void clearAll();

    void updateStepHud(HudItem* hud, const cad::geo::Vec2& cursorPos, const QString& text);
    void updateAimHud(HudItem* hud, const cad::geo::Vec2& cursorPos,
                      bool hasAimPos, bool isBorrowAim, const QString& aimLabel,
                      double displayDeg, bool worldAngleMode,
                      bool hasHit, double t);
    void flashSuccessHud(HudItem* hud, const cad::geo::Vec2& cursorPos, const QString& text);

private:
    CanvasScene* m_scene = nullptr;
    QGraphicsLineItem*    m_previewRay   = nullptr;
    QGraphicsEllipseItem* m_intersectDot = nullptr;
    QGraphicsPathItem*    m_noHitMarker  = nullptr;
    QGraphicsEllipseItem* m_originMarker = nullptr;
    QGraphicsEllipseItem* m_aimMarker    = nullptr;
    QGraphicsLineItem*    m_segHighlight = nullptr;
    ManagedItems          m_managed;
};

} // namespace cad::tools
