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

/// 计算 Gizmo 角度与位姿
GizmoPose computeGizmoPose(const GizmoPoseInput& in);

/// 寻找多选线段中最接近点击位置的已解析端点作为引导点
cad::geo::Vec2 findClosestGuidePoint(const cad::param::ParamDocument* doc,
                                     const QSet<QUuid>& selection,
                                     const cad::geo::Vec2& clickPos);

} // namespace cad::tools
