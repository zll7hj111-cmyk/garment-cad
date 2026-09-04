#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QString>

#include "parametric/Attachment.h"
#include "geometry/Vec2.h"
#include "document/commands/CommandIds.h"  // CommandId::SetFollowerAngle

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

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
                                 const QUuid& newRef2BlockId = QUuid(),
                                 const QUuid& newRef2PointId = QUuid(),
                                 QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newRefBlockId;
    QUuid m_newRefSegmentId;
    QUuid m_newRefPointId;
    QUuid m_newRef2BlockId;
    QUuid m_newRef2PointId;
    QUuid m_oldRefBlockId;
    QUuid m_oldRefSegmentId;
    QUuid m_oldRefPointId;
    QUuid m_oldRef2BlockId;
    QUuid m_oldRef2PointId;
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

/// 设置对齐点 (2026-09 设计修正): 本线段的哪个端点钉在目标点上 —— 只改
/// Attachment::fromPointId (同块点互换), 并按当前基准方向反算 followerAngle
/// 保零跳变。**与换向 (start/end 身份) 完全无关**: fromPointId 由连接语义
/// 决定, 不随 ReverseSegmentCommand 翻转。undo 恢复旧 fromPointId 与角度。
class SetAlignPointCommand : public QUndoCommand
{
public:
    SetAlignPointCommand(cad::param::ParamDocument* doc,
                         const QUuid& attId,
                         const QUuid& newFromPointId,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newFromPointId;
    QUuid m_oldFromPointId;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
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
    cad::param::Attachment m_newAtt;
    cad::param::Attachment m_oldAtt;
    cad::geo::Vec2 m_oldOrigin;
    double m_oldRotation = 0.0;
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
    int id() const override {
        return static_cast<int>(CommandId::SetFollowerAngle);
    }
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
