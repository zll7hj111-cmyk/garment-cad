#include "tools/RotateAimSnap.h"

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "tools/RotateCopyGesture.h"
#include "tools/RotateDragMath.h"
#include "tools/InteractionTolerances.h"  // kAimSearchRadiusPx / kAimAlignTolDeg
#include "geometry/Epsilon.h"

namespace cad::tools {

namespace {

/// 自由段「瞄准端」信息（2026-09 统一 S1）：离枢轴最远的端点 + 该端点相对
/// 「块世界方向（start→end）」的角偏移。块绕枢轴刚体旋转时
/// tipDir(α) = α + dirOffsetRad，其中 α = 段 start→end 世界方向角（= 自由段姿态）。
struct FreeAimTip {
    bool valid = false;
    cad::geo::Vec2 pos;
    double curDirRad = 0.0;      ///< 当前 start→end 世界方向角
    double dirOffsetRad = 0.0;   ///< angle(tip − pivot) − curDirRad
};

FreeAimTip freeAimTip(const cad::param::ParamDocument* doc,
                      const QUuid& blockId,
                      const cad::geo::Vec2& pivot)
{
    FreeAimTip tip;
    const cad::param::Block* blk = doc ? doc->findBlock(blockId) : nullptr;
    if (!blk || blk->segments.empty()) return tip;

    const cad::param::Segment& seg0 = blk->segments.front();
    const auto* sp0 = blk->findPoint(seg0.startPointId);
    const auto* ep0 = blk->findPoint(seg0.endPointId);
    if (!sp0 || !ep0 || !sp0->resolved || !ep0->resolved) return tip;

    const cad::param::ParamPoint* farPt = nullptr;
    double farDist = -1.0;
    for (const auto& seg : blk->segments) {
        for (const QUuid& pid : {seg.startPointId, seg.endPointId}) {
            const auto* pt = blk->findPoint(pid);
            if (!pt || !pt->resolved) continue;
            const double d = blk->transform.toWorld(pt->resolvedPos).distanceTo(pivot);
            if (d > farDist) {
                farDist = d;
                farPt = pt;
            }
        }
    }
    if (!farPt) return tip;

    // 姿态方向 = 世界旋转 + 局部姿态方向（圆段 = 圆心→接缝半径方向）。圆沿用
    // 弦向时两端点重合 ⇒ 弦向恒 0 ⇒ 瞄准端增量与姿态增量差一个 a₀。
    tip.curDirRad = blk->transform.rotation + localPoseDirRad(*blk, seg0);
    tip.pos = blk->transform.toWorld(farPt->resolvedPos);
    tip.dirOffsetRad = (tip.pos - pivot).angle() - tip.curDirRad;
    tip.valid = true;
    return tip;
}

} // namespace

cad::geo::Vec2 RotateAimSnap::endpointAtAngle(
    cad::param::ParamDocument* doc,
    const QUuid& blockId,
    const cad::geo::Vec2& pivot,
    double refWorldRad,
    bool isConnected,
    RotateCopyGesture* copyGesture,
    double angleDeg)
{
    const bool isFree = !(copyGesture && copyGesture->active()) && !isConnected;
    if (isFree) {
        // 2026-09 统一 S1（D2/D4）：自由段枢轴可为任意点，块按刚体绕枢轴旋转 ⇒
        // 瞄准端落点 = 当前落点绕枢轴转「目标世界方向 − 当前世界方向」。
        // 旧式 `pivot + dir(angleDeg)·segLen` 隐含 pivot = 锚心端点，任意枢轴会错。
        const FreeAimTip tip = freeAimTip(doc, blockId, pivot);
        if (!tip.valid) return pivot;
        const double deltaRad = cad::geo::degToRad(angleDeg) - tip.curDirRad;
        return pivot + (tip.pos - pivot).rotated(deltaRad);
    }

    double worldDirRad;
    if (copyGesture && copyGesture->active()) {
        worldDirRad = copyGesture->relToWorldRad(angleDeg);
    } else {
        worldDirRad = refWorldRad + cad::geo::kPi
                      - cad::geo::degToRad(cad::geo::normalizeDeg360(angleDeg));
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
                // 圆段两端点同为接缝点 ⇒ 弦长 0 ⇒ 瞄准端塌到枢轴。取「过接缝的
                // 直径另一端」：长度 = 直径，方向 = 圆姿态方向（relToWorldRad）。
                if (seg.fitKind == cad::param::FitKind::Circle) {
                    segLen = std::max(segLen, 2.0 * blk->circleRadiusMm(seg));
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

    // 自由段：瞄准端方向 ≠ 段世界方向（枢轴任意时二者差一个固定偏移），
    // 命中后由 dirToP 反解姿态角 α = dirToP − offset（2026-09 统一 S1）。
    const bool isFree = !(copyGesture && copyGesture->active()) && !isConnected;
    const FreeAimTip freeTip = isFree ? freeAimTip(doc, currentBlockId, pivot) : FreeAimTip{};
    if (isFree && !freeTip.valid) { clear(); return; }   // 几何未解析: 无瞄准端可吸附

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
                worldDirRad = cad::geo::degToRad(cad::geo::normalizeDeg360(inOutAngleDeg))
                              + freeTip.dirOffsetRad;
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
        inOutAngleDeg = cad::geo::normalizeDeg360(cad::geo::radToDeg(dirToP - freeTip.dirOffsetRad));
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
