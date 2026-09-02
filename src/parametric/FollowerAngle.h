#pragma once

#include <cmath>

#include <QHash>
#include <QList>

#include "geometry/Angle.h"
#include "parametric/Attachment.h"
#include "parametric/Condition.h"
#include "parametric/ConditionEngine.h"

namespace cad::param {

class ParamDocument;

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

/// 有效角度基准方向 (radians) —— 与 Resolver::applyAttachment 的 refWorld
/// 计算逐位同构 (Resolver.cpp), 供读数/反算/可视化等消费方复用, 避免各路径
/// 各自实现导致基准语义漂移 (2026-09 审核 F0):
///   ① 自定义角度基准 (angleRefBlockId 非空): 点1→点2 世界连线方向优先,
///      其次点1 出口方向, 再次基准线段 start→end 方向;
///   ② 否则 = 位置宿主 (toBlockId) 在吸附点的出口方向。
/// 滑轨轨道方向 (leaderRefWorld) 刻意不在此列 —— 轨道属于位置宿主 (Resolver
/// 同注释)。
///
/// @param doc    ParamDocument (块查找).
/// @param att    目标连接.
/// @return 有效基准方向 (radians); 宿主/基准块缺失时回退 0 (调用方自行兜底)。
/// 实现见 ParamDocumentAttachments.cpp (需 ParamDocument 完整类型)。
double effectiveAngleRefWorld(const ParamDocument* doc, const Attachment& att);

/// 重连时保持角度基准 (用户拍板 2026-09): 自动态 (angleRefBlockId 为空) 下
/// 重连 = 把旧所连线段固化为两点基准 (点1 = 旧目标点, 点2 = 旧线段另一端),
/// 方向基准不随新宿主漂移 —— 此前各重连路径只固化点1, 点2 留空, 两点连线
/// 方向退化为单点出口方向, 且面板重定向后基准跟随新宿主 (用户报告「重连
/// 覆盖方向基准, 只覆盖点1, 点2 没有内容」)。已自定义的基准原样保留;
/// 独立角 (angleIndependent) 时基准字段是还原缓存, 不动。
/// **调用方必须在改写 toBlockId/toPointId 之前调用** (旧宿主信息仍在 att 上)。
/// @return 是否发生了固化 (自动态 → 两点基准)。
/// 实现见 ParamDocumentAttachments.cpp (需 ParamDocument 完整类型)。
bool preserveAngleRefOnReattach(ParamDocument* doc, Attachment& att);

/// Shared 角度↔弧长 double-mode switch write-back (2026-08-28 收口 A3).
/// Both mode-toggle entries (SegmentAngleCard::onModeToggle / 
/// ConnectGesture::onAngleModeChanged) previously inlined this conversion.
///
/// @param att        The attachment being switched.
/// @param radiusMm   Follower's segment length at its connection point (mm).
/// @param targetMode The mode being switched INTO.
/// @param params     Formula evaluation base values (cm domain).
/// @param condByName Formula conditions.
/// @return The write-back pair for the TARGET mode — exactly one of
///         angle/arcMm is meaningful per @p targetMode, and the matching
///         formula field is non-empty when the CURRENT value is formula-driven.
///         2026-12 用户拍板: 公式在切换时**原样搬移、绝不改写/乘系数**——
///         "一个表达式只会在一种模式下表达, 用户选择哪个模式, 公式就按
///         哪个模式求值"。数值字段仍做几何保持换算 (无公式时 45°↔4.71cm
///         一致); 公式存在时 Resolver 按公式求值, 公式语义跟随当前模式。
struct FollowerModeSwitchResult
{
    double angle = 0.0;    ///< followerAngle write-back (target Angle).
    double arcMm = 0.0;    ///< arcLength write-back (target ArcLength, mm).
    QString angleFormula;  ///< target Angle: 源公式原样搬移 (非空 = 公式驱动).
    QString arcFormula;    ///< target ArcLength: 源公式原样搬移.
};

inline FollowerModeSwitchResult followerModeSwitchValues(
    const Attachment& att, double radiusMm, RotationMode targetMode,
    const QHash<QString, double>& params,
    const QHash<QString, QList<Condition>>& condByName)
{
    FollowerModeSwitchResult out;

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
    if (targetMode == RotationMode::ArcLength) {
        out.arcMm = cad::geo::degToArcMm(std::fmod(curDeg, 360.0), radiusMm);
        // 公式原样搬移 (用户拍板 2026-12: 不乘换算系数, 语义跟随当前模式)。
        out.arcFormula = att.followerAngleFormula;
    } else {
        out.angle = curDeg;
        out.angleFormula = att.arcLengthFormula;
    }
    return out;
}

} // namespace cad::param
