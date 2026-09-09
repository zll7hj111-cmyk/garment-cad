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

double localPoseDirRad(const cad::param::Block& blk, const cad::param::Segment& seg)
{
    if (seg.fitKind == cad::param::FitKind::Circle) {
        // 圆：姿态 = 圆心→接缝半径方向（块内 a₀）。整圆 start/end 同为接缝点，
        // 弦向退化为 (0,0)，继续用弦向会让姿态恒 0°（黄虚线压灰虚线、黄弧空）。
        return cad::geo::degToRad(blk.circleStartAngleDeg(seg));
    }
    const auto* sp = blk.findPoint(seg.startPointId);
    const auto* ep = blk.findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    return (ep->resolvedPos - sp->resolvedPos).angle();
}

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
    // 2026-09 统一 M2（用户拍板 D4）：三元素各有固定语义 ——
    //   灰虚线 = 世界 0° 射线（TransientOverlay 内部固定，不经此函数）
    //   黄虚线 = 起手姿态（按下瞬间冻结，不再每帧现读文档）
    //   黄弧   = 起手姿态 → 当前姿态
    // 旧式基准取「当前」姿态（自由线每帧现读文档）⇒ 灰虚线压线身、
    // 黄虚线多转一倍、黄弧固定边跟着线段走（用户点 1 / 点 3）。
    GizmoPose out;
    if (in.isMultiOrMarquee) {
        out.startPoseRad = in.isRotating ? in.dragCursorAngle0 : 0.0;
        out.currentPoseRad = in.isRotating ? (in.dragCursorAngle0 + cad::geo::degToRad(in.accumulatedAngleDeg)) : 0.0;
    } else if (in.isCopyGestureActive) {
        // 复制手势：0° 相对角 = 副本与原线重叠（相对角以原线起手世界向为基准）。
        const double origRad = cad::geo::normalizeRad(in.originalWorldRotRad);
        out.startPoseRad = origRad;
        out.currentPoseRad = origRad + cad::geo::degToRad(in.copyRelativeAngle);
    } else if (in.isConnected) {
        // 连接线（跟随线）世界段向 (start→end) = refWorld + π − α：
        // ResolverAttachment.cpp:129 算出的是块 rotation = refWorld + π − α − localDir，
        // 世界段向 = rotation + localDir，两者相消 —— 故此处**不得再减 localDir**，
        // 减了会把「世界方向」算成「块 rotation」（黄虚线/黄弧整体转偏 localDir）。
        // 锚心在跟随线终点时线体自枢轴反向延伸，需再翻 180°（与自由线分支同规）。
        const double anchorFlip = in.isAnchorEnd ? cad::geo::kPi : 0.0;
        const auto poseRad = [&](double alphaDeg) {
            return in.refWorldRad + cad::geo::kPi - cad::geo::degToRad(alphaDeg) + anchorFlip;
        };
        if (in.isRotating) {
            out.startPoseRad = poseRad(in.dragAngle0);
            out.currentPoseRad = poseRad(in.currentAngleDeg);
        } else {
            out.startPoseRad = poseRad(in.currentAngleDeg);
            out.currentPoseRad = out.startPoseRad;
        }
    } else {
        // 自由线：起手姿态 = 按下瞬间冻结的世界方向（ToolRotate::beginRotation 的
        // m_dragAngle0），当前姿态 = 线段当前世界方向。
        const double curRad = cad::geo::degToRad(in.currentAngleDeg);
        out.startPoseRad = in.isRotating ? cad::geo::degToRad(in.dragAngle0) : curRad;
        out.currentPoseRad = curRad;
    }

    if (in.isRotating) {
        // 2026-12 审计 P0-4 / UI-P0-1: 唯一格式化入口, 物理量决定显示域。
        // RotateSession::currentAngleDeg 的返回值随分支换物理量: 连接段 = 折角,
        // 自由段 = 世界方向, 复制手势 = 相对旋转量 —— 此处必须按物理量选域。
        if (in.isMultiOrMarquee)
            out.badgeText = formatRotationBadge(in.accumulatedAngleDeg, RotateBadgeQuantity::Delta);
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
