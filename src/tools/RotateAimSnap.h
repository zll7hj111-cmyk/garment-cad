#pragma once

#include <QUuid>
#include "geometry/Vec2.h"
#include "canvas/ManagedItems.h"

class QGraphicsEllipseItem;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

class RotateCopyGesture;

/// Endpoint aim snap during rotation: searches for nearby candidate points
/// along the rotation arc and snaps the angle towards them.
class RotateAimSnap
{
public:
    RotateAimSnap() = default;
    ~RotateAimSnap() = default;

    /// Calculate candidate point and snap angleDeg if within threshold.
    void checkSnap(cad::param::ParamDocument* doc,
                   CanvasScene* scene,
                   const QUuid& currentBlockId,
                   const cad::geo::Vec2& pivot,
                   double refWorldRad,
                   bool isConnected,
                   double zoom,
                   RotateCopyGesture* copyGesture,
                   double& inOutAngleDeg);

    /// Clear candidate and hide visual ring.
    void clear();

    /// Clean up scene items on tool deactivation.
    void teardown();

    [[nodiscard]] QUuid aimBlockId() const { return m_aimBlockId; }
    [[nodiscard]] QUuid aimPointId() const { return m_aimPointId; }
    [[nodiscard]] bool hasCandidate() const { return !m_aimPointId.isNull(); }

    /// Calculate world position of rotating segment end at angleDeg.
    [[nodiscard]] static cad::geo::Vec2 endpointAtAngle(
        cad::param::ParamDocument* doc,
        const QUuid& blockId,
        const cad::geo::Vec2& pivot,
        double refWorldRad,
        bool isConnected,
        RotateCopyGesture* copyGesture,
        double angleDeg);

private:
    QUuid m_aimBlockId;
    QUuid m_aimPointId;
    QGraphicsEllipseItem* m_aimRing = nullptr;
    ManagedItems m_managed;
};

} // namespace cad::tools
