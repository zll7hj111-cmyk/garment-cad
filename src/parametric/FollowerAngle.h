#pragma once

#include <QHash>
#include <QList>

#include "geometry/Angle.h"
#include "parametric/Attachment.h"
#include "parametric/Condition.h"
#include "parametric/ConditionEngine.h"

namespace cad::param {

/// Back-solve the follower angle (followerAngle, degrees) that preserves the
/// follower's current world direction when attaching to a leader.
///
/// The Resolver drives (closed base, 闭合基准 2026-08 定稿):
///     rotation = refWorld + π − angle·π/180 − localDir
/// so:           angle = (refWorld + π − rotation − localDir)·180/π
///
/// @param followerRotRad  Follower block's transform.rotation (radians).
/// @param localDirRad     Follower's exit direction at the attach point (radians,
///                        from Block::directionAtPoint).
/// @param refWorldRad     Leader's world reference direction (radians), i.e.
///                        leader.transform.rotation + leader.exitDirectionAtPoint(...).
/// @return Follower angle in degrees, normalized to (-180, 180].
inline double backSolveFollowerAngle(double followerRotRad,
                                         double localDirRad,
                                         double refWorldRad)
{
    return cad::geo::normalizeDeg180(cad::geo::radToDeg(
        refWorldRad + cad::geo::kPi - followerRotRad - localDirRad));
}

/// Shared 角度↔弧长 double-mode switch write-back (2026-08-28 收口 A3).
/// Both mode-toggle entries (SegmentAngleCard::onModeToggle / 
/// ConnectGesture::onAngleModeChanged) previously inlined this conversion.
///
/// @param att        The attachment being switched.
/// @param radiusMm   Follower's segment length at its connection point (mm).
/// @param targetMode The mode being switched INTO.
/// @param params     Formula evaluation base values (cm domain).
/// @param condByName Formula conditions.
/// @return The followerAngle/arcLength pair to write back — exactly one is
///         meaningful per @p targetMode (the other keeps its old default).
inline std::pair<double, double> followerModeSwitchValues(
    const Attachment& att, double radiusMm, RotationMode targetMode,
    const QHash<QString, double>& params,
    const QHash<QString, QList<Condition>>& condByName)
{
    // Effective angle (degrees) of the CURRENT mode, preserving geometry.
    double curDeg = att.followerAngle;
    if (att.rotationMode == RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        // 求值失败保持 baseline 的 arcLength (out 参数语义), 用兜底值继续。
        (void)ConditionEngine::evaluateLengthMm(att.arcLengthFormula,
                                                params, condByName, arcMm);
        curDeg = cad::geo::arcMmToDeg(arcMm, radiusMm);
        curDeg = cad::geo::normalizeDeg360(curDeg);
    } else if (!att.followerAngleFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(att.followerAngleFormula,
                                           params, condByName);
        if (r.ok) curDeg = r.value;
    }

    // Write the TARGET mode's storage field.
    // NOTE: the arc write-back uses std::fmod (NOT normalizeDeg360) to match the
    // historical mode-toggle exactly — the effective angle may come from a raw
    // formula value outside [0, 360°), and fmod keeps the signed remainder
    // (multi-turn/negative folds) that normalize would collapse.
    if (targetMode == RotationMode::ArcLength)
        return {0.0, cad::geo::degToArcMm(std::fmod(curDeg, 360.0), radiusMm)};
    return {curDeg, 0.0};
}

} // namespace cad::param
