#include "tools/RotateDragMath.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::tools {

DragStepResult computeDragStep(const cad::geo::Vec2& cursorWorld,
                               const cad::geo::Vec2& pivotWorld,
                               double prevCursorAngle)
{
    const cad::geo::Vec2 d = cursorWorld - pivotWorld;
    const double theta = std::atan2(d.y, d.x);
    const double stepRad = cad::geo::normalizeRad(theta - prevCursorAngle);
    return { theta, cad::geo::radToDeg(stepRad) };
}

double computeMultiDeltaDeg(double accumulatedAngleDeg, RotateConstraintMode mode)
{
    if (mode != RotateConstraintMode::None) {
        return std::round(accumulatedAngleDeg / 15.0) * 15.0;
    }
    return accumulatedAngleDeg;
}

double computeDragTargetDeg(const DragSample& sample)
{
    if (sample.isCopyGestureActive) {
        double target = sample.dragAngle0 + sample.accumulatedAngleDeg;
        if (sample.constraintMode != RotateConstraintMode::None)
            target = std::round(target / 15.0) * 15.0;
        return target;
    }

    if (sample.isConnected) {
        const double alpha0 = cad::geo::normalizeDeg360(sample.dragAngle0);
        double smoothAlpha = cad::geo::normalizeDeg360(alpha0 - sample.accumulatedAngleDeg);
        if (sample.constraintMode == RotateConstraintMode::World) {
            double worldRot = sample.refWorldRad + cad::geo::kPi - cad::geo::degToRad(smoothAlpha) - sample.localDir;
            double snappedWorldRad = cad::geo::degToRad(std::round(cad::geo::radToDeg(worldRot) / 15.0) * 15.0);
            return cad::param::backSolveFollowerAngle(snappedWorldRad, sample.localDir, sample.refWorldRad);
        }
        if (sample.constraintMode == RotateConstraintMode::Baseline) {
            return cad::geo::normalizeDeg180(std::round(smoothAlpha / 15.0) * 15.0);
        }
        return cad::geo::normalizeDeg180(smoothAlpha);
    }

    double target = sample.dragAngle0 + sample.accumulatedAngleDeg;
    if (sample.constraintMode == RotateConstraintMode::World) {
        target = std::round(target / 15.0) * 15.0;
    }
    return target;
}

QString formatRotationBadge(double deg, RotateBadgeQuantity quantity)
{
    switch (quantity) {
    case RotateBadgeQuantity::Delta:
        if (std::abs(deg) <= 0.01) return QString();
        return cad::geo::Units::formatDegTrimmed(deg);
    case RotateBadgeQuantity::Fold:
        return cad::geo::Units::formatDegTrimmed(cad::geo::normalizeDeg180(deg));
    case RotateBadgeQuantity::World:
    default:
        // 2026-12 审计 UI-P0-1: 自由段姿态 = 世界方向, 0..360, 与角度卡
        // 「= 世界角度 N°」同数。
        return cad::geo::Units::formatDegTrimmed(cad::geo::normalizeDeg360(deg));
    }
}

GizmoPose computeGizmoPose(const GizmoPoseInput& in)
{
    GizmoPose out;
    if (in.isMultiOrMarquee) {
        out.refBaseRad = in.isRotating ? in.dragCursorAngle0 : 0.0;
        out.currentPoseRad = in.isRotating ? (in.dragCursorAngle0 + cad::geo::degToRad(in.accumulatedAngleDeg)) : 0.0;
        out.deltaDeg = in.isRotating ? in.accumulatedAngleDeg : 0.0;
    } else if (in.isCopyGestureActive) {
        double origRad = in.originalWorldRotRad;
        if (in.isAnchorEnd) origRad += cad::geo::kPi;
        out.refBaseRad = in.refWorldRad;
        out.currentPoseRad = cad::geo::normalizeRad(origRad) + cad::geo::degToRad(in.copyRelativeAngle);
        out.deltaDeg = in.copyRelativeAngle;
    } else if (in.isConnected) {
        out.refBaseRad = in.refWorldRad;
        if (in.isRotating) {
            out.currentPoseRad = in.refWorldRad + cad::geo::kPi - cad::geo::degToRad(in.currentAngleDeg) - in.localDir;
            out.deltaDeg = in.dragAngle0 - in.currentAngleDeg;
        } else {
            out.currentPoseRad = in.refWorldRad + cad::geo::kPi - cad::geo::degToRad(in.baseAngleDeg) - in.localDir;
            out.deltaDeg = 0.0;
        }
    } else {
        double origRad = in.originalWorldRotRad;
        if (in.isAnchorEnd) origRad += cad::geo::kPi;
        out.refBaseRad = cad::geo::normalizeRad(origRad);
        out.deltaDeg = in.isRotating ? (in.currentAngleDeg - in.dragAngle0) : 0.0;
        out.currentPoseRad = out.refBaseRad + cad::geo::degToRad(out.deltaDeg);
    }

    if (in.isRotating) {
        // 2026-12 审计 P0-4 / UI-P0-1: 唯一格式化入口, 物理量决定显示域。
        // RotateSession::currentAngleDeg 的返回值随分支换物理量: 连接段 = 折角,
        // 自由段 = 世界方向, 复制手势 = 相对旋转量 —— 此处必须按物理量选域。
        if (in.isMultiOrMarquee)
            out.badgeText = formatRotationBadge(out.deltaDeg, RotateBadgeQuantity::Delta);
        else if (in.isCopyGestureActive)
            out.badgeText = formatRotationBadge(in.currentAngleDeg, RotateBadgeQuantity::Delta);
        else if (in.isConnected)
            out.badgeText = formatRotationBadge(in.currentAngleDeg, RotateBadgeQuantity::Fold);
        else
            out.badgeText = formatRotationBadge(in.currentAngleDeg, RotateBadgeQuantity::World);
    }
    return out;
}

cad::geo::Vec2 findClosestGuidePoint(const cad::param::ParamDocument* doc,
                                     const QSet<QUuid>& selection,
                                     const cad::geo::Vec2& clickPos)
{
    cad::geo::Vec2 guide = clickPos;
    if (!doc) return guide;

    double bestDist = std::numeric_limits<double>::max();
    for (const auto& blkId : selection) {
        if (const auto* b = doc->findBlock(blkId)) {
            for (const auto& pt : b->points) {
                if (pt.resolved) {
                    const cad::geo::Vec2 wpt = b->worldPos(pt.id);
                    const double dist = wpt.distanceTo(clickPos);
                    if (dist < bestDist) {
                        bestDist = dist;
                        guide = wpt;
                    }
                }
            }
        }
    }
    return guide;
}

} // namespace cad::tools
