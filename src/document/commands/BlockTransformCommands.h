#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QList>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "document/commands/CommandIds.h"
#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// 影子角度旋转提交 (拆开影子基准 R6/R8): 拆开态旋转改写影子块 transform
/// (冻结克隆自身姿态) 并同步回写跟随线 transform (绕 p3 原地转, Resolver
/// angleOnly 只写 rotation 不碰 origin)。redo/undo 双块 verbatim 还原 +
/// resolveAll —— 撤销后影子角度与跟随线姿态一起回拖前值。
class ShadowRotateCommand : public QUndoCommand
{
public:
    ShadowRotateCommand(cad::param::ParamDocument* doc,
                        const QUuid& shadowId,
                        const cad::param::Transform2D& shadowOld,
                        const cad::param::Transform2D& shadowNew,
                        const QUuid& followerId,
                        const cad::param::Transform2D& followerOld,
                        const cad::param::Transform2D& followerNew,
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_shadowId;
    QUuid m_followerId;
    cad::param::Transform2D m_shadowOld, m_shadowNew;
    cad::param::Transform2D m_followerOld, m_followerNew;
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
    int id() const override { return static_cast<int>(CommandId::MoveBlock); }
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
    int id() const override { return static_cast<int>(CommandId::RotateBlock); }
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

/// Rotate multiple blocks together (rigid body rotation around a pivot).
/// Snapshots old/new transforms and external attachments detached by the rotation.
class RotateBlocksCommand : public QUndoCommand
{
public:
    struct BlockTransformSnapshot {
        QUuid blockId;
        cad::param::Transform2D oldTf;
        cad::param::Transform2D newTf;
        QUuid oldEndTargetBlock;
        QUuid oldEndTargetPoint;
        QUuid newEndTargetBlock;
        QUuid newEndTargetPoint;
    };

    RotateBlocksCommand(cad::param::ParamDocument* doc,
                        std::vector<BlockTransformSnapshot> snapshots,
                        std::vector<cad::param::Attachment> releasedAttachments = {},
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    std::vector<BlockTransformSnapshot> m_snapshots;
    std::vector<cad::param::Attachment> m_releasedAttachments;
};

} // namespace cad::cmd
