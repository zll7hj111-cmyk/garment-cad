#pragma once

#include <cmath>

#include <QHash>
#include <QList>

#include "geometry/Angle.h"
#include "geometry/Units.h"
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
/// @return Follower angle in degrees, **storage domain [0, 360)**
///         (2026-12 审计 P0-2: 全库存储域收口 —— 此前返回折角 (−180,180],
///         同一物理角经「反算」写入存储为负、经「数值输入」写入为正)。
///         唯一例外仍是旋转手势的多圈累积路径, 不经本函数。
inline double backSolveFollowerAngle(double followerRotRad,
                                         double localDirRad,
                                         double refWorldRad)
{
    return cad::geo::normalizeDeg360(cad::geo::radToDeg(
        refWorldRad + cad::geo::kPi - followerRotRad - localDirRad));
}

// ─── 角度域契约 (2026-12 审计 P0-2 收口) ────────────────────────────────────
//
// followerAngle 有两个域，**全库唯一入口 = 下面两个函数**，禁止各处自行调用
// normalizeDeg180 / normalizeDeg360（审计发现同一物理角 270° 在组件页签显示
// 270.0、在角度卡显示 −90.0，用户报告「同一个物理角度显示出两个数字」）：
//
//   存储域 [0,360)   —— Resolver / 序列化 / 几何反算消费 (CONVENTIONS.md 角度收口)
//   显示域 (−180,180] —— 带符号折角: 0°=折叠 / ±90°=垂直 / ±180°=开平
//
// 例外：旋转手势的多圈累积值是合法存储值（tests/test_rotate_strip.cpp:159 锁定
// 1260°），不得在此归一化；显示时按折角取模（fmod 后落在同一物理角上）。
inline double followerAngleToStorage(double deg)
{
    return cad::geo::normalizeDeg360(deg);
}

inline double followerAngleToDisplay(double storedDeg)
{
    return cad::geo::normalizeDeg180(storedDeg);
}

/// 附件当前模式的**数值**显示文本（无公式时）：角度走折角域，弧长/开度按存储值
/// 原样显示（cm）。**禁止 arc→deg→fold→arc 往返**——弧长 > πr（等效角 >180°）
/// 时该往返会把输入框显示成负值，与用户输入/存储值不同数（审计 P0-2）。
inline QString attachmentValueDisplayText(const Attachment& att)
{
    switch (att.rotationMode) {
    case RotationMode::ArcLength:
        return cad::geo::Units::formatCm(att.arcLength);
    case RotationMode::ChordLength:
        return cad::geo::Units::formatCm(att.chordLength);
    case RotationMode::Angle:
    default:
        return cad::geo::Units::formatDegValue(followerAngleToDisplay(att.followerAngle));
    }
}

/// 旋转手势「原始角 → 连接存储字段」的**唯一写入口** (2026-12 审计 P0-4 收口)。
///
/// 同一手势角 rawDeg 必须同时派生三个域, 此前 RotateSession::applyAngleDeg
/// 三分支各自 normalize, 与角度卡/徽标/状态提示的读数域漂移 (审计 P0-4)。
/// 域契约 (与 followerAngleToStorage/ToDisplay、followerModeSwitchValues 一致):
///   · Angle       → followerAngle = normalizeDeg360(rawDeg)   存储域 [0,360)
///   · ArcLength   → arcLength     = degToArcMm(存储域, r)      弧长带符号, 可区分 θ 与 360−θ
///   · ChordLength → chordLength   = degToChordMm(折角域, r)    弦长 2r·sin(θ/2) 无法区分
///                                 θ 与 360−θ, 故必须用 (−180,180] 折角带符号表示
/// 写入同时清空该模式的公式 (手势覆盖公式驱动值) 并设置 rotationMode。
///
/// @param att       目标连接 (调用方保证可写)。
/// @param mode      本次写入的模式 (通常取自拖拽起始快照 base.rotationMode)。
/// @param rawDeg    手势原始角 (度, 可多圈/带符号)。
/// @param radiusMm  Follower 在连接点的线段长度 (mm)。
/// @return 写入 followerAngle 的存储域角度 (度), 供调用方复用避免重复换算。
inline double writeFollowerAngleForMode(Attachment& att, RotationMode mode,
                                        double rawDeg, double radiusMm)
{
    const double storageDeg = followerAngleToStorage(rawDeg);
    att.rotationMode = mode;
    switch (mode) {
    case RotationMode::ArcLength:
        att.arcLength = cad::geo::degToArcMm(storageDeg, radiusMm);
        att.arcLengthFormula.clear();
        break;
    case RotationMode::ChordLength:
        att.chordLength = cad::geo::degToChordMm(followerAngleToDisplay(rawDeg), radiusMm);
        att.chordLengthFormula.clear();
        break;
    case RotationMode::Angle:
    default:
        att.followerAngle = storageDeg;
        att.followerAngleFormula.clear();
        break;
    }
    return storageDeg;
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
    double angle = 0.0;      ///< followerAngle write-back (target Angle).
    double arcMm = 0.0;      ///< arcLength write-back (target ArcLength, mm).
    double chordMm = 0.0;    ///< chordLength write-back (target ChordLength, mm).
    QString angleFormula;    ///< target Angle: 源公式原样搬移 (非空 = 公式驱动).
    QString arcFormula;      ///< target ArcLength: 源公式原样搬移.
    QString chordFormula;    ///< target ChordLength: 源公式原样搬移.
};

inline FollowerModeSwitchResult followerModeSwitchValues(
    const Attachment& att, double radiusMm, RotationMode targetMode,
    const QHash<QString, double>& params,
    const QHash<QString, QList<Condition>>& condByName)
{
    FollowerModeSwitchResult out;

    // Effective angle (degrees) of the CURRENT mode, preserving geometry.
    double curDeg = att.followerAngle;
    QString curFormula;
    if (att.rotationMode == RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        // 求值失败保持 baseline 的 arcLength (out 参数语义), 用兜底值继续。
        (void)ConditionEngine::evaluateLengthMm(att.arcLengthFormula,
                                                params, condByName, arcMm);
        curDeg = cad::geo::arcMmToDeg(arcMm, radiusMm);
        curDeg = cad::geo::normalizeDeg360(curDeg);
        curFormula = att.arcLengthFormula;
    } else if (att.rotationMode == RotationMode::ChordLength) {
        double chordMm = att.chordLength;
        (void)ConditionEngine::evaluateLengthMm(att.chordLengthFormula,
                                                params, condByName, chordMm);
        curDeg = cad::geo::chordMmToDeg(chordMm, radiusMm);
        curDeg = cad::geo::normalizeDeg360(curDeg);
        curFormula = att.chordLengthFormula;
    } else {
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(att.followerAngleFormula,
                                               params, condByName);
            if (r.ok) curDeg = r.value;
        }
        curFormula = att.followerAngleFormula;
    }

    // Write the TARGET mode's storage field.
    if (targetMode == RotationMode::ArcLength) {
        out.arcMm = cad::geo::degToArcMm(std::fmod(curDeg, 360.0), radiusMm);
        out.arcFormula = curFormula;
    } else if (targetMode == RotationMode::ChordLength) {
        out.chordMm = cad::geo::degToChordMm(cad::geo::normalizeDeg180(curDeg), radiusMm);
        out.chordFormula = curFormula;
    } else {
        out.angle = curDeg;
        out.angleFormula = curFormula;
    }
    return out;
}

} // namespace cad::param
