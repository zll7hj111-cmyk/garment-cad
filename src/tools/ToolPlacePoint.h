#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "canvas/ManagedItems.h"
#include "geometry/Vec2.h"

#include <QUuid>

class QGraphicsPathItem;
class QGraphicsPolygonItem;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// ToolPlacePoint — creates a parametric placed point relative to a base
/// segment and reference point with polar deflection angle and offset distance.
class ToolPlacePoint : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;

    void placePointDistInput(double distCm, bool locked) override;
    void placePointAngleInput(double angleDeg, bool locked) override;
    void placePointCommitted() override;

    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"放置点"); }

private:
    enum class State {
        PickRef,    ///< Hover/click to choose reference point & host segment
        SetOffset,  ///< Move mouse to specify offset angle & distance, click to commit
    };

    void handlePickRefPress(const cad::geo::Vec2& clickPos, double zoom);
    void handleSetOffsetPress(const cad::geo::Vec2& clickPos, bool shiftPressed);
    void updatePreview(const cad::geo::Vec2& cursorWorld, bool shiftPressed);
    void commitPlacedPoint();
    void clearPreview();
    void resetState();

    State m_state = State::PickRef;
    SnapEngine m_snap;

    QUuid m_blockId;
    QUuid m_hostSegmentId;
    QUuid m_refPointId;
    cad::geo::Vec2 m_refPointPos;
    cad::geo::Vec2 m_baseDir;   // unit vector of base segment direction in world coords
    double m_alongDistance = 0.0;
    double m_tRatio = 0.0;

    // Interactive graphics
    ManagedItems m_managed;
    QGraphicsPathItem* m_previewPath = nullptr;
    QGraphicsPolygonItem* m_previewDiamond = nullptr;

    cad::geo::Vec2 m_lastRawCursor;
    double m_currentAngle = 0.0;
    double m_currentDist = 0.0;

    bool m_distLocked = false;
    double m_lockedDist = 0.0;
    bool m_angleLocked = false;
    double m_lockedAngle = 0.0;
};

} // namespace cad::tools
