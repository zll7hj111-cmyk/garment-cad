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
/// 组件化组（路线B）: 成组/解散只维护成员关系与组件元数据, 不直接改动普通
/// attachment; 组件铰链由 SetComponentHingeCommand 单独管理. Redo: create the
/// group record. Undo: dissolve it.
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

/// Reorder the group registry (panel drag-sort). The order is persisted in
/// the file, so the move must be undoable to keep the dirty flag honest.
/// moveGroup removes @p fromIndex and inserts at @p toIndex; the reverse
/// move restores the original order.
class MoveGroupCommand : public QUndoCommand
{
public:
    MoveGroupCommand(cad::param::ParamDocument* doc, int fromIndex, int toIndex,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    int m_from = -1;
    int m_to = -1;
};

/// Toggle / set bounding box visibility on canvas.
class SetGroupBoundingBoxCommand : public QUndoCommand
{
public:
    SetGroupBoundingBoxCommand(cad::param::ParamDocument* doc,
                               const QUuid& groupId,
                               bool visible,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    bool m_newVisible;
    bool m_oldVisible = true;
    bool m_valid = false;
};

/// Set the component's single main hinge (路线B).
/// Redo: store the new hinge. Undo: restore the previous hinge state.
class SetComponentHingeCommand : public QUndoCommand
{
public:
    SetComponentHingeCommand(cad::param::ParamDocument* doc,
                             const QUuid& groupId,
                             const cad::param::ComponentHinge& hinge,
                             QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    bool m_valid = false;
    cad::param::ComponentHinge m_newHinge;
    bool m_oldHasHinge = false;
    cad::param::ComponentHinge m_oldHinge;
};

/// Clear the component's main hinge; component returns to ordinary group.
class ClearComponentHingeCommand : public QUndoCommand
{
public:
    ClearComponentHingeCommand(cad::param::ParamDocument* doc,
                               const QUuid& groupId,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    bool m_valid = false;
    bool m_oldHasHinge = false;
    cad::param::ComponentHinge m_oldHinge;
};

/// Add one or more existing ungrouped blocks to a group (加人, panel entry).
/// Redo adds each block via addGroupMember; undo restores the original group
/// record and member list wholesale (so member order, root and hinge state
/// are exactly preserved).
class AddGroupMembersCommand : public QUndoCommand
{
public:
    AddGroupMembersCommand(cad::param::ParamDocument* doc,
                           const QUuid& groupId,
                           const QList<QUuid>& memberIds,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    QList<QUuid> m_memberIds;
    bool m_valid = false;
    cad::param::Group m_group;         ///< Original record (undo snapshot).
    QList<QUuid> m_originalMembers;    ///< Original membership (undo snapshot).
};

/// Remove one or more members from a group (减人, panel entry). Removal may
/// auto-dissolve the group below two members; undo restores the original
/// group record and member list exactly.
class RemoveGroupMembersCommand : public QUndoCommand
{
public:
    RemoveGroupMembersCommand(cad::param::ParamDocument* doc,
                              const QUuid& groupId,
                              const QList<QUuid>& memberIds,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    QList<QUuid> m_memberIds;
    bool m_valid = false;
    cad::param::Group m_group;         ///< Original record (undo snapshot).
    QList<QUuid> m_originalMembers;    ///< Original membership (undo snapshot).
};
} // namespace cad::cmd