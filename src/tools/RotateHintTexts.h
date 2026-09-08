#pragma once

#include <QString>
#include <QPointF>
#include "geometry/Angle.h"
#include "tools/RotateDragMath.h"

class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

enum class RotateState;

/// 状态栏文案生成快照
struct RotateHintSnapshot {
    bool isCopyGestureActive = false;
    double copyRelativeAngle = 0.0;
    RotateState state;
    RotateConstraintMode constraintMode = RotateConstraintMode::None;
    bool selectionConfirmed = false;
    bool hasSingleBlock = false;
    int selectionSize = 0;
    QString anchorTag;
    bool pivotPicked = false;
    bool isAngleLocked = false;
    double baseAngleDeg = 0.0;
    double currentAngleDeg = 0.0;
    /// 角度字段的显示域（2026-09 统一 M3）：由物理量决定，禁止各自选域。
    cad::geo::AngleDisplayRole poseRole = cad::geo::AngleDisplayRole::WorldDirection;
};

/// 约束模式标签
QString constraintModeLabel(RotateConstraintMode mode);

/// 构造状态提示文案
QString buildStatusHint(const RotateHintSnapshot& snap);

/// 锚心锁定提示文案
QString buildAnchorLockedReason();

/// 弹出线段属性对话框 (消除 ToolRotate 对 ui/LinePropertyDialog.h 的直接依赖)
bool openRotateLinePropertyDialog(CanvasScene* scene,
                                  cad::param::ParamDocument* doc,
                                  const QPointF& scenePos);

} // namespace cad::tools
