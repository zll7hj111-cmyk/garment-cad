#pragma once

#include "geometry/Vec2.h"
#include "canvas/ManagedItems.h"

class QGraphicsEllipseItem;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// Tracks hover snapping to endpoints and press-pending drag state for ToolRotate.
class RotateInputTracker
{
public:
    RotateInputTracker() = default;
    ~RotateInputTracker() = default;

    void updateHoverSnap(CanvasScene* scene,
                         cad::param::ParamDocument* doc,
                         const cad::geo::Vec2& worldPos);
    void hideHoverSnap();
    void teardown();

    [[nodiscard]] bool hoverSnapped() const { return m_hoverSnapped; }
    [[nodiscard]] cad::geo::Vec2 hoverSnapPoint() const { return m_hoverSnapPoint; }

    [[nodiscard]] bool pivotPicked() const { return m_pivotPicked; }
    void setPivotPicked(bool val) { m_pivotPicked = val; }

    [[nodiscard]] bool pressPending() const { return m_pressPending; }
    void setPressPending(bool val) { m_pressPending = val; }

    [[nodiscard]] cad::geo::Vec2 pressPos() const { return m_pressPos; }
    void setPressPos(const cad::geo::Vec2& pos) { m_pressPos = pos; }

    [[nodiscard]] cad::geo::Vec2 pendingPivot() const { return m_pendingPivot; }
    void setPendingPivot(const cad::geo::Vec2& pivot) { m_pendingPivot = pivot; }

    [[nodiscard]] bool pendingHoverSnapped() const { return m_pendingHoverSnapped; }
    void setPendingHoverSnapped(bool val) { m_pendingHoverSnapped = val; }

    void resetPress() {
        m_pressPending = false;
        m_pendingHoverSnapped = false;
        m_pivotPicked = false;
    }

private:
    void ensureHoverSnapRing(CanvasScene* scene);

    bool m_hoverSnapped = false;
    bool m_pivotPicked = false;
    bool m_pressPending = false;
    bool m_pendingHoverSnapped = false;

    cad::geo::Vec2 m_hoverSnapPoint;
    cad::geo::Vec2 m_pressPos;
    cad::geo::Vec2 m_pendingPivot;

    QGraphicsEllipseItem* m_hoverSnapRing = nullptr;
    ManagedItems m_managed;
};

} // namespace cad::tools
