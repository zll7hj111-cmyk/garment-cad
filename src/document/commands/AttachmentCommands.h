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

/// 位置吸附保持、角度独立 (用户新需求 2026): toggles an attachment between
/// normal angle-following and independent-angle mode. In independent mode the
/// from-point stays pinned to the leader, but the follower's own rotation is
/// preserved. Turning OFF back-solves the follower angle from the current world
/// direction so there is no visual jump when angle-following resumes.
class SetAttachmentAngleIndependentCommand : public QUndoCommand
{
public:
    SetAttachmentAngleIndependentCommand(cad::param::ParamDocument* doc,
                                         const QUuid& attId, bool angleIndependent,
                                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newIndependent;
    bool m_oldIndependent;
    bool m_oldAngleOnly = false;
    cad::param::SlideMode m_oldSlideMode = cad::param::SlideMode::None;
    bool m_oldLocked = false;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
};


/// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): switches an attachment between
/// 普通全连接 (None) and the one-axis slide modes (沿线滑动 AlongLeader /

/// 位置锚点与角度基准分离 (用户需求 2026): sets a separate angle-reference
/// block/segment on an attachment. The follower's position stays pinned to
/// toBlock, but its followerAngle is measured against the chosen angle-ref
/// segment instead of the position leader. Pass null to restore the default
/// (angle follows the position leader). Changing the ref back-solves the
/// current world direction into followerAngle so there is no visual jump.
class SetAttachmentAngleRefCommand : public QUndoCommand
{
public:
    SetAttachmentAngleRefCommand(cad::param::ParamDocument* doc,
                                 const QUuid& attId,
                                 const QUuid& newRefBlockId,
                                 const QUuid& newRefSegmentId,
                                 const QUuid& newRefPointId = QUuid(),
                                 QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newRefBlockId;
    QUuid m_newRefSegmentId;
    QUuid m_newRefPointId;
    QUuid m_oldRefBlockId;
    QUuid m_oldRefSegmentId;
    QUuid m_oldRefPointId;
    bool m_oldAngleIndependent = false;
    bool m_oldAngleOnly = false;
    cad::param::SlideMode m_oldSlideMode = cad::param::SlideMode::None;
    bool m_oldLocked = false;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
};

/// 重新挂接 (用户需求 2026): 解焊后把跟随线拖到新的位置宿主 A, 同时保留
/// 原角度基准 B —— 形成“位置挂 A、角度跟 B”的双基准连接。
/// 若原连接没有独立角度基准, 自动把旧位置宿主设为新的角度基准 (B=旧A)。
/// Undo 恢复旧 attachment 与拖前 transform。
class ReattachAttachmentCommand : public QUndoCommand
{
public:
    ReattachAttachmentCommand(cad::param::ParamDocument* doc,
                              const QUuid& attId,
                              const QUuid& newToBlockId,
                              const QUuid& newToPointId,
                              const QUuid& newToSegmentId,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newToBlockId;
    QUuid m_newToPointId;
    QUuid m_newToSegmentId;
    cad::param::Attachment m_oldAtt;
    bool m_hasOldAtt = false;
    cad::geo::Vec2 m_oldOrigin;
    double m_oldRotation = 0.0;
};

/// 仅角度线拖端点重挂 (用户报告 2026-12: 使用了引用线段但无连接线段的线,
/// 拖动端点无吸附反应): a line in angle-only mode (位置自由、角度随基准线)
/// is reconnected onto a new leader — 旧角度基准保留为独立角度基准 (双基准),
/// 位置挂到新端点, 恢复完整连接并重新焊接. redo re-applies the reattached
/// state (含 HUD 角度调整后的新态), undo restores the old angle-only
/// attachment + pre-drag transform — the WHOLE reattach is ONE undo step
/// (与普通连接的"建立连接"宏单步撤销体验一致).
class ReconnectAttachmentCommand : public QUndoCommand
{
public:
    ReconnectAttachmentCommand(cad::param::ParamDocument* doc,
                               const QUuid& attId,
                               const cad::param::Attachment& newAtt,
                               const cad::param::Attachment& oldAtt,
                               const cad::geo::Vec2& oldOrigin,
                               double oldRotation,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    cad::param::Attachment m_newAtt;  ///< 重挂后状态 (含 HUD 角度调整).
    cad::param::Attachment m_oldAtt;  ///< 重挂前仅角度态 (undo 恢复).
    cad::geo::Vec2 m_oldOrigin;       ///< 拖前 transform (undo 恢复).
    double m_oldRotation = 0.0;
};


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

/// 拖动保护开关 (焊接语义, 用户拍板 2026-09 补撤销): protected connections
/// are welded — dragging cannot tear them apart, dragging either side moves
/// the whole pair. Undo restores the previous locked state (此前面板直接改
/// 模型、Ctrl+Z 不回退, 与 angleOnly/slide 开关不一致).
class SetAttachmentLockedCommand : public QUndoCommand
{
public:
    SetAttachmentLockedCommand(cad::param::ParamDocument* doc,
                               const QUuid& attId, bool locked,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newLocked;
    bool m_oldLocked;
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
