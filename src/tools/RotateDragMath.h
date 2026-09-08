#pragma once

#include <QString>
#include <QSet>
#include <QUuid>
#include "geometry/Vec2.h"

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// 角度约束模式（Shift 键循环切换）
enum class RotateConstraintMode {
    None,       ///< 无约束
    Baseline,   ///< 母线基准约束 (15° 吸附相对于母线)
    World       ///< 世界约束 (15° 吸附相对于世界网格)
};

/// 拖拽步进计算结果 (光标角度与步进角度增量)
struct DragStepResult {
    double newCursorAngle = 0.0;
    double stepDeg = 0.0;
};

/// 计算单步拖拽光标旋转量
DragStepResult computeDragStep(const cad::geo::Vec2& cursorWorld,
                               const cad::geo::Vec2& pivotWorld,
                               double prevCursorAngle);

/// 单线段拖拽目标角度参数
struct DragSample {
    bool isCopyGestureActive = false;
    bool isConnected = false;
    RotateConstraintMode constraintMode = RotateConstraintMode::None;
    double dragAngle0 = 0.0;
    double accumulatedAngleDeg = 0.0;
    double refWorldRad = 0.0;
    double localDir = 0.0;
};

/// 计算单线段拖拽目标角度
double computeDragTargetDeg(const DragSample& sample);

/// 计算多选线段拖拽角度增量
double computeMultiDeltaDeg(double accumulatedAngleDeg, RotateConstraintMode mode);

/// Gizmo 位姿计算输入
struct GizmoPoseInput {
    bool isMultiOrMarquee = false;
    bool isCopyGestureActive = false;
    bool isConnected = false;
    bool isRotating = false;
    double originalWorldRotRad = 0.0;
    bool isAnchorEnd = false;
    double refWorldRad = 0.0;
    double localDir = 0.0;
    double baseAngleDeg = 0.0;
    double currentAngleDeg = 0.0;
    double dragAngle0 = 0.0;
    double dragCursorAngle0 = 0.0;
    double accumulatedAngleDeg = 0.0;
    double copyRelativeAngle = 0.0;
};

/// Gizmo 位姿计算结果
struct GizmoPose {
    double refBaseRad = 0.0;
    double currentPoseRad = 0.0;
    double deltaDeg = 0.0;
    QString badgeText;
};

/// 旋转 HUD 徽标显示的物理量 (2026-12 审计 P0-4 / UI-P0-1 收口)。
///
/// 旋转手势里的「角度」是**三个不同的物理量**, 每个量只有一个显示域; 此前
/// computeGizmoPose 按连接状态各自选域, 同一姿态连接段显示 −90.0°、自由段
/// 显示 270.0° (审计 P0-4/UI-P0-1)。域由物理量决定, 且与该段的卡片读数同数:
///   · Delta —— 旋转量 (多选/框选累积角, 或旋转复制的相对角): 不是姿态角,
///              不折叠, 带符号以区分顺/逆时针
///   · Fold  —— 连接段姿态 = 跟随折角: (−180,180], 与角度卡「跟随角」
///              / FollowerAngle.h 契约一致 (0 = 折叠、±180 = 直行)
///   · World —— 自由段姿态 = 世界方向: 0..360 (normalizeDeg360, 行业默认),
///              与角度卡「= 世界角度 N°」同数
enum class RotateBadgeQuantity {
    Delta,
    Fold,
    World
};

/// 旋转 HUD 徽标文本的唯一入口 (返回空串 = 不显示)。
[[nodiscard]] QString formatRotationBadge(double deg, RotateBadgeQuantity quantity);

/// 计算 Gizmo 角度与位姿
GizmoPose computeGizmoPose(const GizmoPoseInput& in);

/// 寻找多选线段中最接近点击位置的已解析端点作为引导点
cad::geo::Vec2 findClosestGuidePoint(const cad::param::ParamDocument* doc,
                                     const QSet<QUuid>& selection,
                                     const cad::geo::Vec2& clickPos);

} // namespace cad::tools
