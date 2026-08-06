#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QList>
#include <QString>
#include <vector>

#include "parametric/Group.h"
#include "parametric/Attachment.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Turn a confirmed selection into a user group (成组).
/// 组只是选择快捷方式 (2026-08-04 设计定稿): 成组/解散纯粹是成员关系
/// 标签, 绝不改动任何几何/连接状态 —— 不切断跨边界 attachment, 不清除
/// endTarget. Redo: create the group record. Undo: dissolve it.
class MakeGroupCommand : public QUndoCommand
{
public:
    MakeGroupCommand(cad::param::ParamDocument* doc,
                     const QList<QUuid>& memberIds,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QList<QUuid> m_memberIds;
    bool m_valid = false;                     ///< Pre-validated in the ctor.
    cad::param::Group m_group;                ///< Record captured on first redo.
};

/// Dissolve a user group (解散组): membership vanishes, member geometry and
/// connections stay untouched. Undo re-inserts the pristine record.
class UngroupCommand : public QUndoCommand
{
public:
    UngroupCommand(cad::param::ParamDocument* doc,
                   const QUuid& groupId,
                   QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    bool m_valid = false;
    cad::param::Group m_group;      ///< Pristine record snapshot.
    QList<QUuid> m_members;         ///< Membership snapshot.
};

/// Rename a user group (重命名组): a light undoable command so the rename
/// is not lost on Ctrl+Z (same treatment as the dissolve path).
class RenameGroupCommand : public QUndoCommand
{
public:
    RenameGroupCommand(cad::param::ParamDocument* doc,
                       const QUuid& groupId,
                       const QString& newName,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    QString m_newName;
    QString m_oldName;      ///< Captured at construction (undo restores it).
    bool m_valid = false;
};

} // namespace cad::cmd
