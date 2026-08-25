#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QList>
#include <QHash>
#include <vector>

#include "parametric/Component.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// 创建组件: package the given blocks into a Component. The component does NOT
/// freeze member relations (参数化活性铁律) — it only records the creation-time
/// overall orientation (defaultAngleDeg = first member's rotation).
class MakeComponentCommand : public QUndoCommand
{
public:
    MakeComponentCommand(cad::param::ParamDocument* doc,
                         const QList<QUuid>& memberBlockIds,
                         const QString& name,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QList<QUuid> m_memberBlockIds;
    QString m_name;
    QUuid m_componentId;
    double m_defaultAngleDeg = 0.0;
};

/// 解散组件: remove the component record only; members (and their internal
/// parametric relations) are untouched.
class DissolveComponentCommand : public QUndoCommand
{
public:
    DissolveComponentCommand(cad::param::ParamDocument* doc, const QUuid& componentId,
                             QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Component m_component;
};

/// 删除组件: remove the component record AND every member segment (with the
/// full removeBlock cascade, deduped across members for undo).
class DeleteComponentCommand : public QUndoCommand
{
public:
    DeleteComponentCommand(cad::param::ParamDocument* doc, const QUuid& componentId,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Component m_component;
    std::vector<cad::param::Block> m_blocks;      ///< member snapshots (undo).
    std::vector<cad::param::Block> m_bridges;     ///< bridges pinned to members.
    std::vector<cad::param::Attachment> m_attachments; ///< touching the cascade set (deduped).
    std::vector<cad::param::LinkedVariable> m_linked;
    std::vector<cad::param::MeasureVariable> m_measures;
    std::vector<cad::param::Block> m_bakedConsumers;
};

/// Set component metadata (name / bounding box visibility / 原始角度
/// defaultAngleDeg — the 回到默认角度 target). Pure metadata — geometry
/// unchanged, no resolve.
class SetComponentPropertyCommand : public QUndoCommand
{
public:
    SetComponentPropertyCommand(cad::param::ParamDocument* doc, const QUuid& componentId,
                                const QString& name, bool showBoundingBox,
                                double defaultAngleDeg,
                                const QString& defaultAngleFormula = QString(),
                                QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Component m_old;
    cad::param::Component m_new;
};

/// 回到初始状态 (回正, 用户拍板 2026-09): reset the WHOLE component to its
/// initial state — (1) cancel any external follow (component-level attachment),
/// (2) rotate every member about the bbox center so the overall orientation
/// returns to 原始角 (defaultAngleDeg; the formula, if any, is evaluated once
/// and baked into the numeric value), (3) clear the 原始角 formula.
class ResetComponentAngleCommand : public QUndoCommand
{
public:
    ResetComponentAngleCommand(cad::param::ParamDocument* doc, const QUuid& componentId,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Component m_oldComponent;   ///< 组件快照 (undo 恢复公式/数值).
    cad::param::Component m_newComponent;   ///< 清除公式 + 烘焙数值.
    cad::param::Attachment m_att;           ///< 被取消的对接 (undo 恢复).
    bool m_hadAttachment = false;
    QHash<QUuid, cad::param::Transform2D> m_oldTransforms;
    QHash<QUuid, cad::param::Transform2D> m_newTransforms;
};

 
/// 组件整组旋转快照字段：被释放的外部 endTarget（成员指向组外点）。
struct AimRelease
{
    QUuid blockId;
    QUuid endTargetBlockId;
    QUuid endTargetPointId;
    double endTargetOffset = 0.0;
    QString endTargetOffsetFormula;
};

/// 组件整组旋转快照字段：被降级/释放的省道线（start/ref 引用组外点）。
struct DartRelease
{
    QUuid blockId;
    QUuid dartStartBlockId;
    QUuid dartStartPointId;
    QUuid dartRefBlockId;
    QUuid dartRefPointId;
    QUuid dartRefSegmentId;
    double dartOffsetMm = 0.0;
    QString dartOffsetFormula;
    double dartAngleDeg = 90.0;
    QString dartAngleFormula;
};

/// 组件整组旋转 (旋转工具 W 键, 2026-12 用户拍板): 全体成员绕任意锚点 rigid
/// 旋转 (rotation += delta, origin = pivot + (origin - pivot).rotated(delta)).
/// 整组旋转开始前释放的外部约束 (组件级 attachment / 成员线对组外 leader 的
/// attachment / 成员指向组外点的 endTarget / 引用组外点的省道线) 由命令
/// restore-then-replay: undo 原样恢复 (快照完整性铁律).
class RotateComponentCommand : public QUndoCommand
{
public:
    RotateComponentCommand(cad::param::ParamDocument* doc,
                           const QUuid& componentId,
                           const QHash<QUuid, cad::param::Transform2D>& oldTf,
                           const QHash<QUuid, cad::param::Transform2D>& newTf,
                           const std::vector<cad::param::Attachment>& releasedAtts,
                           const std::vector<AimRelease>& releasedTargets,
                           const std::vector<DartRelease>& releasedDarts,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_componentId;
    QHash<QUuid, cad::param::Transform2D> m_oldTf;
    QHash<QUuid, cad::param::Transform2D> m_newTf;
    std::vector<cad::param::Attachment> m_releasedAtts;
    std::vector<AimRelease> m_releasedTargets;
    std::vector<DartRelease> m_releasedDarts;
};

} // namespace cad::cmd
