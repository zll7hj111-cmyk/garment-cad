#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"

namespace cad::param {
class ParamDocument;
}

namespace cad::cmd {

/// D14 解除圆约束：把 `FitKind::Circle` 段冻结成普通曲线
/// （docs/design/CIRCLE_TOOL_DESIGN.md §4.4 / §16 / §17）。
///
/// 为什么必须是**一条命令**而不是就地降级：清掉 fitKind 的瞬间，象限锚会从
/// `resolveCurveAnchorPoint` 的弦分支取位置，立刻跳到弦上——圆当场变形。
/// 所以这里把每个锚点的当前**解算位置**写进 `freePos` 并把约束改成 `Free`，
/// 圆因此原地冻结。
///
/// 与设计稿 D14 原文的一处**有意偏离**：原文写 `autoTangent = true`（回到
/// Hobby 自动切向）。实测那样做会在解除瞬间重解切向 → 形状跳变，违反 §11
/// 测试 6d「解除瞬间形状冻结」。故 `autoTangent` / `tangentIn` / `tangentOut`
/// / `tangentLocked` 一律保持原值（拟合切向就是当前形状的切向）。
///
/// 圆心点**不动也不删**：它本来就是 `Free`，且块内可能有其它段引用它。
/// 命令只处理本段的起点 / 终点 / 全部分段锚。
///
/// 冻结的核心变换抽成 `detachCircleInPlace()`，供无撤销栈的路径复用
/// （面板在没有 undoStack 时直接调它），避免两套逻辑漂移。
void detachCircleInPlace(cad::param::ParamDocument* doc,
                         const QUuid& blockId, const QUuid& segmentId);

/// 可撤销的「解除圆约束」命令。undo 用构造时的完整点快照还原
/// （约束 / freePos / 距离 / 角度 / 公式 / 切向全部回滚）。
class DetachCircleCommand : public QUndoCommand
{
public:
    DetachCircleCommand(cad::param::ParamDocument* doc,
                        const QUuid& blockId, const QUuid& segmentId,
                        QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::FitKind m_oldFitKind = cad::param::FitKind::None;
    std::vector<cad::param::ParamPoint> m_oldPoints;  ///< 起点/终点/象限锚完整快照
};

} // namespace cad::cmd
