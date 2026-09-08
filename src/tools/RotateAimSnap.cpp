#include "tools/RotateAimSnap.h"

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "tools/RotateCopyGesture.h"
#include "tools/InteractionTolerances.h"  // kAimSearchRadiusPx / kAimAlignTolDeg
#include "geometry/Epsilon.h"

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
        worldDirRad = refWorldRad + cad::geo::kPi
                      - cad::geo::degToRad(cad::geo::normalizeDeg360(angleDeg));
    } else {
        worldDirRad = cad::geo::degToRad(angleDeg);
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
    const double searchRadius = kAimSearchRadiusPx / cad::canvas::safeZoomOr(zoom);
    const double alignTolRad = cad::geo::degToRad(kAimAlignTolDeg);

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
            if (aim.lengthSquared() < cad::geo::kGeomEpsTight) continue;
            const double dirToP = std::atan2(aim.y, aim.x);

            double worldDirRad;
            if (copyGesture && copyGesture->active()) {
                worldDirRad = copyGesture->relToWorldRad(inOutAngleDeg);
            } else if (isConnected) {
                worldDirRad = refWorldRad + cad::geo::kPi
                              - cad::geo::degToRad(cad::geo::normalizeDeg360(inOutAngleDeg));
            } else {
                worldDirRad = cad::geo::degToRad(inOutAngleDeg);
            }

            double diff = dirToP - worldDirRad;
            while (diff >  cad::geo::kPi) diff -= 2.0 * cad::geo::kPi;
            while (diff <= -cad::geo::kPi) diff += 2.0 * cad::geo::kPi;

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
            cad::geo::radToDeg(refWorldRad + cad::geo::kPi - dirToP));
    } else {
        inOutAngleDeg = cad::geo::normalizeDeg360(cad::geo::radToDeg(dirToP));
    }

    m_aimBlockId = bestBlockId;
    m_aimPointId = bestPointId;
    m_scene = scene;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showSnapAim(bestPos);
    }
}

void RotateAimSnap::checkGuideSnap(cad::param::ParamDocument* doc,
                                   CanvasScene* scene,
                                   const QSet<QUuid>& rotatingBlockIds,
                                   const cad::geo::Vec2& pivot,
                                   const cad::geo::Vec2& guidePointInitialWorld,
                                   double zoom,
                                   double& inOutDeltaDeg)
{
    if (!doc || !scene) return;

    const cad::geo::Vec2 initialVec = guidePointInitialWorld - pivot;
    const double r0 = initialVec.length();
    if (r0 < 1e-4) {
        clear();
        return;
    }
    const double initialAngleRad = std::atan2(initialVec.y, initialVec.x);
    const double currentAngleRad = initialAngleRad + cad::geo::degToRad(inOutDeltaDeg);
    const cad::geo::Vec2 currentPos = pivot + cad::geo::Vec2{std::cos(currentAngleRad), std::sin(currentAngleRad)} * r0;

    const double searchRadius = kAimSearchRadiusPx / cad::canvas::safeZoomOr(zoom);

    QUuid bestBlockId, bestPointId;
    cad::geo::Vec2 bestPos;
    double bestDist = searchRadius;

    for (const auto& blk : doc->blocks()) {
        if (rotatingBlockIds.contains(blk.id)) continue;
        for (const auto& pt : blk.points) {
            if (!pt.resolved || !pt.selectable) continue;
            const cad::geo::Vec2 wpos = blk.transform.toWorld(pt.resolvedPos);
            const double d = wpos.distanceTo(currentPos);
            if (d > searchRadius) continue;

            const double targetR = (wpos - pivot).length();
            if (std::abs(targetR - r0) > searchRadius) continue;

            if (d < bestDist) {
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

    const cad::geo::Vec2 targetVec = bestPos - pivot;
    const double targetAngleRad = std::atan2(targetVec.y, targetVec.x);
    double diffRad = targetAngleRad - initialAngleRad;
    while (diffRad > cad::geo::kPi) diffRad -= 2.0 * cad::geo::kPi;
    while (diffRad <= -cad::geo::kPi) diffRad += 2.0 * cad::geo::kPi;

    inOutDeltaDeg = cad::geo::radToDeg(diffRad);
    m_aimBlockId = bestBlockId;
    m_aimPointId = bestPointId;
    m_scene = scene;

    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->showSnapAim(bestPos);
    }
}

void RotateAimSnap::clear()
{
    m_aimBlockId = QUuid();
    m_aimPointId = QUuid();
    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->clear(cad::canvas::OverlayTier::Hover);
    }
}

void RotateAimSnap::teardown()
{
    clear();
    m_scene = nullptr;
}

} // namespace cad::tools
