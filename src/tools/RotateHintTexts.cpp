#include "tools/RotateHintTexts.h"
#include "tools/ToolRotate.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "tools/HitTester.h"
#include "canvas/CanvasScene.h"
#include "ui/LinePropertyDialog.h"

#include <QGraphicsView>

namespace cad::tools {

QString constraintModeLabel(RotateConstraintMode mode)
{
    switch (mode) {
    case RotateConstraintMode::None:
        return QString::fromUtf8("[无约束 · Shift切换]");
    case RotateConstraintMode::Baseline:
        return QString::fromUtf8("[母线基准约束15° · Shift切换]");
    case RotateConstraintMode::World:
        return QString::fromUtf8("[世界角度约束15° · Shift切换]");
    }
    return QString();
}

QString buildStatusHint(const RotateHintSnapshot& snap)
{
    if (snap.isCopyGestureActive) {
        return QString::fromUtf8("旋转复制 %1°").arg(cad::geo::Units::formatDegValue(snap.copyRelativeAngle));
    }
    const QString modeStr = constraintModeLabel(snap.constraintMode);

    if (snap.state == RotateState::Idle) {
        return QString();
    }
    if (snap.state == RotateState::Rotating) {
        return QString::fromUtf8("旋转中 · 基准: %1° · 角度: %2° · %3 · 松手提交 · Esc 回位")
            .arg(cad::geo::Units::formatDegValue(
                     cad::geo::toDisplayDeg(snap.baseAngleDeg, cad::geo::AngleDisplayRole::WorldDirection)),
                 // 2026-09 统一 M3: 姿态角按物理量取显示域（连接段=折角, 自由段=世界向）
                 cad::geo::Units::formatDegValue(
                     cad::geo::toDisplayDeg(snap.currentAngleDeg, snap.poseRole)),
                 modeStr);
    }
    if (snap.state == RotateState::Ready) {
        if (!snap.selectionConfirmed) {
            if (snap.hasSingleBlock && snap.selectionSize <= 1) {
                return QString::fromUtf8("旋转：锚心 %1 · 右键或回车确认选区 | 点击切换端点 | %2")
                    .arg(snap.anchorTag, modeStr);
            }
            return QString::fromUtf8("已选 %1 条线段 · 右键或回车确认选区 | Shift加减选 | Esc清除")
                .arg(snap.selectionSize);
        }
        if (!snap.pivotPicked) {
            return QString::fromUtf8("【已确认】请指定旋转中心（锚点）：点击端点/圆心或画布任意位置 | Esc返回选区");
        }
        if (snap.isAngleLocked) {
            return QString::fromUtf8("旋转：锚心 %1 · 角度由变量/公式驱动，已锁定（移除公式后可旋转）")
                .arg(snap.anchorTag);
        }
        return QString::fromUtf8("已指定旋转中心 · 拖动旋转 · %1 | 长按Ctrl拖动复制 | Esc重选中心")
            .arg(modeStr);
    }
    return QString();
}

QString buildAnchorLockedReason()
{
    return QString::fromUtf8("已连接线段禁止切换锚心（先断开连接）");
}

bool openRotateLinePropertyDialog(CanvasScene* scene,
                                  cad::param::ParamDocument* doc,
                                  const QPointF& scenePos)
{
    if (!scene || !doc) return false;
    const auto hits = blockHitsAtScene(*scene, *doc, scenePos);
    if (hits.empty() || hits.front().segmentId.isNull()) return false;

    QWidget* parentWidget = scene->views().isEmpty() ? nullptr : scene->views().first();
    auto* dlg = new cad::ui::LinePropertyDialog(hits.front().blockId, hits.front().segmentId,
                                                doc, scene, parentWidget);
    dlg->show();
    return true;
}

} // namespace cad::tools
