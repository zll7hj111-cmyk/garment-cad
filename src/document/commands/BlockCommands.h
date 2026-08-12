#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"

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
    cad::param::Block m_block;                       ///< Saved block for undo.
    std::vector<cad::param::Block> m_bridges;        ///< Bridges pinned to it (pre-deletion state).
    std::vector<cad::param::Attachment> m_attachments; ///< Saved attachments for undo.
    std::vector<cad::param::LinkedVariable> m_linked;  ///< Linked vars sourced from the cascade set
                                                       ///< (auto-deleted with the block).
    std::vector<cad::param::MeasureVariable> m_measures; ///< Measure vars referencing the cascade set.
    std::vector<cad::param::Block> m_bakedConsumers;   ///< Blocks whose length formulas referenced
                                                       ///< those linked vars (baked to numbers by redo).
};

/// Move a group of blocks by a delta (supports mergeWith for continuous drag).
class MoveBlockCommand : public QUndoCommand
{
public:
    MoveBlockCommand(cad::param::ParamDocument* doc,
                     const QList<QUuid>& blockIds,
                     const cad::geo::Vec2& delta,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return 1001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QList<QUuid> m_blockIds;
    cad::geo::Vec2 m_delta;
};

/// Rotate a block to a new transform (supports mergeWith for continuous drag).
/// Also snapshots the endpoint-aim (endTarget) state: rotating a line that
/// aims at a point releases the aim (旋转 = 放弃终点指向), and undo restores
/// both the transform and the aim constraint.
///
/// Also snapshots an optional follower attachment RELEASED by the rotation
/// (旋转 = 放弃跟随): redo() detaches it, undo() restores it. Used when the
/// pivot was moved OFF the attachment point (anchor = the far endpoint).
class RotateBlockCommand : public QUndoCommand
{
public:
    RotateBlockCommand(cad::param::ParamDocument* doc,
                       const QUuid& blockId,
                       const cad::param::Transform2D& oldTf,
                       const cad::param::Transform2D& newTf,
                       const QUuid& oldEndTargetBlock = {},
                       const QUuid& oldEndTargetPoint = {},
                       const QUuid& newEndTargetBlock = {},
                       const QUuid& newEndTargetPoint = {},
                       const QUuid& releasedAttId = {},
                       const cad::param::Attachment& releasedAttBackup = {},
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return 1003; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    cad::param::Transform2D m_oldTf;
    cad::param::Transform2D m_newTf;
    QUuid m_oldEndTargetBlock;
    QUuid m_oldEndTargetPoint;
    QUuid m_newEndTargetBlock;
    QUuid m_newEndTargetPoint;
    QUuid m_releasedAttId;                      ///< Attachment detached by redo().
    cad::param::Attachment m_releasedAttBackup; ///< Snapshot restored by undo().
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

/// Set a segment's visual/semantic properties.
class SetSegmentPropertyCommand : public QUndoCommand
{
public:
    struct Props {
        QString name;
        cad::param::SegmentRole role;
        cad::param::LineStyle lineStyle;
        QColor color;
        double weight;
        bool visible;
        bool showName;
        bool showLength;
        QString lengthFormula;
    };

    SetSegmentPropertyCommand(cad::param::ParamDocument* doc,
                              const QUuid& blockId, const QUuid& segmentId,
                              const Props& newProps,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    Props m_oldProps;
    Props m_newProps;
};

/// Add an auxiliary (Interpolated) point to a host segment.
/// Pushed as its OWN undo step, deliberately NOT merged with the line that
/// borrows the point: the aux point belongs to the host segment (辅助点归
/// 宿主线段所有), so undoing / deleting the borrowing line must never
/// remove it — the first Ctrl+Z removes the line, the second removes the point.
class AddAuxPointCommand : public QUndoCommand
{
public:
    AddAuxPointCommand(cad::param::ParamDocument* doc,
                       const QUuid& blockId, const QUuid& segmentId,
                       cad::param::ParamPoint pt,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamPoint m_pt;
};

/// Move a single parametric point (curve anchor drag).
/// Stores old/new freePos; on redo applies new, on undo restores old.
class MovePointCommand : public QUndoCommand
{
public:
    MovePointCommand(cad::param::ParamDocument* doc,
                     const QUuid& blockId, const QUuid& pointId,
                     const cad::geo::Vec2& oldPos, const cad::geo::Vec2& newPos,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    cad::geo::Vec2 m_oldPos;
    cad::geo::Vec2 m_newPos;
};

/// Add a curve pass-point (曲线点) to a segment. The point is a CurveAnchor
/// constraint; the segment is promoted to a Bézier curve (type + passPointIds).
/// Undo removes the point and restores the segment's previous type (straight
/// line when this was the only curve point).
class AddCurvePointCommand : public QUndoCommand
{
public:
    AddCurvePointCommand(cad::param::ParamDocument* doc,
                         const QUuid& blockId, const QUuid& segmentId,
                         cad::param::ParamPoint pt,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamPoint m_pt;
    cad::param::SegmentType m_oldType;
};

/// Remove a curve pass-point from a segment. When the last curve point is
/// removed the segment reverts to a straight line. Undo restores the point,
/// its position in passPointIds and the segment type.
class RemoveCurvePointCommand : public QUndoCommand
{
public:
    RemoveCurvePointCommand(cad::param::ParamDocument* doc,
                            const QUuid& blockId, const QUuid& segmentId,
                            const QUuid& pointId,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_pointId;
    cad::param::ParamPoint m_pt;      ///< Saved copy of the removed point.
    int m_index = 0;                  ///< Position within passPointIds.
    cad::param::SegmentType m_oldType;
};

/// Move a curve anchor by (percent, offset) along its host chord. Pushed once
/// per drag stroke (on mouse release) — each stroke is its own undo step.
/// Also records the anchor's follow connection before/after the stroke: a drag
/// detaches an existing follow on its first move and may snap-connect a new
/// one on release, so undo must restore the complete connection state.
class MoveCurveAnchorCommand : public QUndoCommand
{
public:
    MoveCurveAnchorCommand(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& pointId,
                           double oldPercent, double oldOffset,
                           double newPercent, double newOffset,
                           const QUuid& oldFollowBlockId,
                           const QUuid& oldFollowPointId,
                           const cad::geo::Vec2& oldFollowOffset,
                           const QUuid& newFollowBlockId,
                           const QUuid& newFollowPointId,
                           const cad::geo::Vec2& newFollowOffset,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    double m_oldPercent, m_oldOffset;
    double m_newPercent, m_newOffset;
    QUuid m_oldFollowBlockId;
    QUuid m_oldFollowPointId;
    cad::geo::Vec2 m_oldFollowOffset;
    QUuid m_newFollowBlockId;
    QUuid m_newFollowPointId;
    cad::geo::Vec2 m_newFollowOffset;
};

/// Edit a curve anchor's tangent handles (manual Bézier handles). Stores the
/// before/after tangentIn/tangentOut/autoTangent so a handle drag undoes cleanly.
class SetCurveTangentCommand : public QUndoCommand
{
public:
    SetCurveTangentCommand(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& pointId,
                           const cad::geo::Vec2& oldTanIn, const cad::geo::Vec2& oldTanOut, bool oldAuto,
                           const cad::geo::Vec2& newTanIn, const cad::geo::Vec2& newTanOut, bool newAuto,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    cad::geo::Vec2 m_oldTanIn, m_oldTanOut;
    cad::geo::Vec2 m_newTanIn, m_newTanOut;
    bool m_oldAuto;
    bool m_newAuto;
};

/// Snapshot of the status-bar edit strip's mutable state (SegmentEditBar):
/// the segment's name / length fields, the endpoint's Polar / measure fields
/// and the follower attachment's angle / arc state. The strip edits the model
/// LIVE and pushes ONE command per commit, so undo/redo AND the undo stack's
/// dirty flag stay consistent (without this, edits after a save were silently
/// lost on close — the stack stayed clean).
class SegmentEditBarCommand : public QUndoCommand
{
public:
    struct State {
        QString segName;
        QString lengthFormula;
        QString endDistanceFormula;
        double endDistance = 0.0;
        double endAngle = 0.0;
        QString endAngleFormula;
        int endConstraint = 0;
        QUuid endRefPointId;
        // Follower attachment (null id = free line).
        QUuid attId;
        double followerAngle = 0.0;
        QString followerAngleFormula;
        double arcLength = 0.0;
        QString arcLengthFormula;
        int rotationMode = 0;
    };

    /// @p newState Target state; the OLD state is captured from the model at
    /// construction (push() then applies newState via redo()).
    SegmentEditBarCommand(cad::param::ParamDocument* doc,
                          const QUuid& blockId, const QUuid& segmentId,
                          State newState, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    State m_oldState;
    State m_newState;
};

} // namespace cad::cmd
