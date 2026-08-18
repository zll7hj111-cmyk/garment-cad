#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add an attachment between two blocks.
class AddAttachmentCommand : public QUndoCommand
{
public:
    AddAttachmentCommand(cad::param::ParamDocument* doc,
                         cad::param::Attachment att,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;
};

/// Remove an attachment by ID.
class RemoveAttachmentCommand : public QUndoCommand
{
public:
    RemoveAttachmentCommand(cad::param::ParamDocument* doc,
                            const QUuid& attId,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;  ///< Saved for undo.
    /// Removing a bridge pin releases the bridge (it becomes an independent
    /// segment) — snapshot its pre-removal state + attachments for undo.
    cad::param::Block m_bridge;
    std::vector<cad::param::Attachment> m_bridgeAtts;
    bool m_hasBridge = false;
};

/// 拆开保留角度 (detach position, keep angle; 用户拍板 2026-08): converts an
/// attachment to angle-only mode — the follower's rotation keeps being driven
/// by the leader's direction + followerAngle, while the position constraint is
/// released (the line moves freely). Conversion also clears 拖动保护 (isLocked)
/// and 滑轨 (slideMode, 与 angleOnly 互斥).
/// Undo restores the full connection (position re-snaps to the leader point).
class SetAttachmentAngleOnlyCommand : public QUndoCommand
{
public:
    SetAttachmentAngleOnlyCommand(cad::param::ParamDocument* doc,
                                  const QUuid& attId, bool angleOnly,
                                  QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newAngleOnly;
    bool m_oldAngleOnly;
    bool m_oldLocked;  ///< 拖动保护 snapshot (拆开时清除).
    cad::param::SlideMode m_oldSlideMode;  ///< 拆开时清除滑轨 (互斥).
};

/// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): switches an attachment between
/// 普通全连接 (None) and the one-axis slide modes (沿线滑动 AlongLeader /
/// 垂直拉出 PerpLeader). Entering a slide mode snapshots the locked-axis
/// coordinate from the current geometry (via ParamDocument), clears angleOnly
/// and unlocks (位置必须可滑动); switching back to None restores the full
/// connection (re-welded). Undo restores the previous mode, lock-axis
/// snapshots, angleOnly and isLocked verbatim.
class SetAttachmentSlideModeCommand : public QUndoCommand
{
public:
    SetAttachmentSlideModeCommand(cad::param::ParamDocument* doc,
                                  const QUuid& attId,
                                  cad::param::SlideMode mode,
                                  QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    cad::param::SlideMode m_newMode;
    cad::param::SlideMode m_oldMode;
    double m_oldAlongMm = 0.0;
    double m_oldPerpMm = 0.0;
    bool m_oldAngleOnly = false;
    bool m_oldLocked = false;
};

/// 滑轨拖动回写 (抽屉式滑动, 用户拍板 2026-08): records the free-axis
/// coordinate change (old → new) caused by dragging a slide-mode follower,
/// so the drag's undo step restores the pre-drag rail position. Pushed in
/// the SAME macro as the enclosing MoveBlockCommand. Asymmetric resolve:
/// redo() writes the coordinates WITHOUT resolving (the move command's redo
/// settles the rail — resolving first would double-shift the block); undo()
/// DOES resolve (the move command's undo has already settled with the NEW
/// coordinates, so the final restore needs one more settle).
class SetSlideOffsetsCommand : public QUndoCommand
{
public:
    SetSlideOffsetsCommand(cad::param::ParamDocument* doc, const QUuid& attId,
                           double oldAlong, double oldPerp,
                           double newAlong, double newPerp,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    double m_oldAlong, m_oldPerp, m_newAlong, m_newPerp;
};

/// Set the follower angle (followerAngle) and/or arc-length rotation state
/// of an attachment. Supports both angle and arc-length modes.
class SetFollowerAngleCommand : public QUndoCommand
{
public:
    SetFollowerAngleCommand(cad::param::ParamDocument* doc,
                          const QUuid& attId, double newAngle,
                          const QString& newFormula = QString(),
                          cad::param::RotationMode newMode = cad::param::RotationMode::Angle,
                          double newArcLength = 0.0,
                          const QString& newArcFormula = QString(),
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return 1002; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    double m_oldAngle;
    double m_newAngle;
    QString m_oldFormula;
    QString m_newFormula;
    cad::param::RotationMode m_oldMode;
    cad::param::RotationMode m_newMode;
    double m_oldArcLength;
    double m_newArcLength;
    QString m_oldArcFormula;
    QString m_newArcFormula;
};

} // namespace cad::cmd
