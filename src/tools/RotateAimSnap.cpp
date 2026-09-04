#include "tools/RotateAimSnap.h"

#include <cmath>
#include <QGraphicsEllipseItem>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "tools/RotateCopyGesture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace cad::tools {

cad::geo::Vec2 RotateAimSnap::endpointAtAngle(
    cad::param::ParamDocument* doc,
    const QUuid& blockId,
    const cad::geo::Vec2& pivot,
    double refWorldRad,
    bool isConnected,
    RotateCopyGesture* copyGesture,
    double angleDeg)
{
    double worldDirRad;
    if (copyGesture && copyGesture->active()) {
        worldDirRad = copyGesture->relToWorldRad(angleDeg);
    } else if (isConnected) {
        worldDirRad = refWorldRad + M_PI
                      - cad::geo::normalizeDeg360(angleDeg) * M_PI / 180.0;
    } else {
        worldDirRad = angleDeg * M_PI / 180.0;
    }

    double segLen = 0.0;
    if (doc) {
        if (const auto* blk = doc->findBlock(blockId)) {
            for (const auto& seg : blk->segments) {
                const auto* sp = blk->findPoint(seg.startPointId);
                const auto* ep = blk->findPoint(seg.endPointId);
                if (sp && ep && sp->resolved && ep->resolved) {
                    segLen = std::max(segLen, sp->resolvedPos.distanceTo(ep->resolvedPos));
                }
            }
        }
    }
    return pivot + cad::geo::Vec2{std::cos(worldDirRad), std::sin(worldDirRad)} * segLen;
}

void RotateAimSnap::checkSnap(cad::param::ParamDocument* doc,
                              CanvasScene* scene,
                              const QUuid& currentBlockId,
                              const cad::geo::Vec2& pivot,
                              double refWorldRad,
                              bool isConnected,
                              double zoom,
                              RotateCopyGesture* copyGesture,
                              double& inOutAngleDeg)
{
    if (!doc || !scene) return;

    const cad::geo::Vec2 endPos = endpointAtAngle(doc, currentBlockId, pivot,
                                                  refWorldRad, isConnected,
                                                  copyGesture, inOutAngleDeg);
    constexpr double searchRadiusPx = 15.0;
    const double searchRadius = searchRadiusPx / (zoom > 1e-9 ? zoom : 1.0);
    constexpr double alignTolRad = 2.0 * M_PI / 180.0;

    QUuid bestBlockId, bestPointId;
    cad::geo::Vec2 bestPos;
    double bestDist = searchRadius;

    for (const auto& blk : doc->blocks()) {
        if (blk.id == currentBlockId) continue;
        for (const auto& pt : blk.points) {
            if (!pt.resolved || !pt.selectable) continue;
            const cad::geo::Vec2 wpos = blk.transform.toWorld(pt.resolvedPos);

            const double d = wpos.distanceTo(endPos);
            if (d > searchRadius) continue;

            const cad::geo::Vec2 aim = wpos - pivot;
            if (aim.lengthSquared() < 1e-12) continue;
            const double dirToP = std::atan2(aim.y, aim.x);

            double worldDirRad;
            if (copyGesture && copyGesture->active()) {
                worldDirRad = copyGesture->relToWorldRad(inOutAngleDeg);
            } else if (isConnected) {
                worldDirRad = refWorldRad + M_PI
                              - cad::geo::normalizeDeg360(inOutAngleDeg) * M_PI / 180.0;
            } else {
                worldDirRad = inOutAngleDeg * M_PI / 180.0;
            }

            double diff = dirToP - worldDirRad;
            while (diff >  M_PI) diff -= 2.0 * M_PI;
            while (diff <= -M_PI) diff += 2.0 * M_PI;

            if (std::abs(diff) < alignTolRad && d < bestDist) {
                bestDist = d;
                bestBlockId = blk.id;
                bestPointId = pt.id;
                bestPos = wpos;
            }
        }
    }

    if (bestPointId.isNull()) {
        clear();
        return;
    }

    const cad::geo::Vec2 aim = bestPos - pivot;
    const double dirToP = std::atan2(aim.y, aim.x);
    if (copyGesture && copyGesture->active()) {
        inOutAngleDeg = cad::geo::normalizeDeg360(copyGesture->worldRadToRel(dirToP));
    } else if (isConnected) {
        inOutAngleDeg = cad::geo::normalizeDeg180(
            (refWorldRad + M_PI - dirToP) * 180.0 / M_PI);
    } else {
        inOutAngleDeg = cad::geo::normalizeDeg360(dirToP * 180.0 / M_PI);
    }

    m_aimBlockId = bestBlockId;
    m_aimPointId = bestPointId;

    if (!m_aimRing) {
        constexpr double r = 8.0;
        m_aimRing = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen pen(QColor(255, 152, 0));  // amber
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_aimRing->setPen(pen);
        m_aimRing->setBrush(Qt::NoBrush);
        m_aimRing->setZValue(105.0);
        scene->addItem(m_aimRing);
        m_managed.own(m_aimRing, &m_aimRing);
    }
    m_aimRing->setPos(cad::geo::Coord::toScene(bestPos));
    m_aimRing->setVisible(true);
}

void RotateAimSnap::clear()
{
    m_aimBlockId = QUuid();
    m_aimPointId = QUuid();
    if (m_aimRing) {
        m_aimRing->setVisible(false);
    }
}

void RotateAimSnap::teardown()
{
    clear();
    m_managed.clear();
    m_aimRing = nullptr;
}

} // namespace cad::tools
