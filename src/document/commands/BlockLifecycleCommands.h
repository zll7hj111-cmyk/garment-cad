#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add a block to the document.
class AddBlockCommand : public QUndoCommand
{
public:
    AddBlockCommand(cad::param::ParamDocument* doc, cad::param::Block block,
                    QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;
};

/// Remove a block (and its attachments) from the document.
class RemoveBlockCommand : public QUndoCommand
{
public:
    RemoveBlockCommand(cad::param::ParamDocument* doc, const QUuid& blockId,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;                         ///< Saved block for undo.
    std::vector<cad::param::Block> m_bridges;          ///< Bridges pinned to it (pre-deletion state).
    std::vector<cad::param::Block> m_shadows;          ///< 影子块级联删除的 pre-deletion 快照
                                                       ///< (本体/跟随线被删时随删, DETACH_SHADOW_DESIGN ⑥)。
    std::vector<cad::param::Attachment> m_attachments; ///< Saved attachments for undo.
    std::vector<cad::param::LinkedVariable> m_linked;  ///< Linked vars sourced from the cascade set
                                                       ///< (auto-deleted with the block).
    std::vector<cad::param::MeasureVariable> m_measures; ///< Measure vars referencing the cascade set.
    std::vector<cad::param::Block> m_bakedConsumers;   ///< Blocks whose length formulas referenced
                                                       ///< those linked vars (baked to numbers by redo).
};

/// Add the clones produced by a Ctrl+drag quick copy (快捷复制) as ONE
/// undo step: auto-published bridge-length linked variables first (so their
/// refName resolves), then blocks, then the cloned internal attachments.
class DuplicateBlocksCommand : public QUndoCommand
{
public:
    DuplicateBlocksCommand(cad::param::ParamDocument* doc,
                           cad::param::DuplicateResult result,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::DuplicateResult m_result;
};

/// Rotate-copy (旋转复制, Ctrl+drag in ToolRotate): ONE undo step that
/// creates a clone of the target block and attaches it BACK to the original
/// (follower = clone, leader = original), with the given follower angle
/// measured RELATIVE to the original's current direction.
class RotateCopyCommand : public QUndoCommand
{
public:
    RotateCopyCommand(cad::param::ParamDocument* doc,
                      cad::param::DuplicateResult result,
                      const QUuid& originalBlockId,
                      const QUuid& pivotPointId,
                      const QUuid& clonePivotPointId,
                      const QUuid& leaderSegmentId,
                      double followerAngle,
                      const QString& followerAngleFormula = {},
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::DuplicateResult m_result;
    QUuid m_originalBlockId;     ///< Leader (the rotated original line).
    QUuid m_pivotPointId;        ///< Leader-side attachment point (pivot).
    QUuid m_clonePivotPointId;   ///< Clone-side attachment point.
    QUuid m_leaderSegmentId;     ///< Leader segment whose exit direction is the
                                 ///< follower-angle reference.
    cad::param::Attachment m_att;  ///< Clone→original attachment (id snapshot).
};

} // namespace cad::cmd
