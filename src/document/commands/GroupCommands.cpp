#include "GroupCommands.h"

#include <QSet>


#include "parametric/ParamDocument.h"

namespace cad::cmd {

// ─── MakeGroupCommand ───

MakeGroupCommand::MakeGroupCommand(cad::param::ParamDocument* doc,
                                   const QList<QUuid>& memberIds,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_memberIds(memberIds)
{
    setText(QStringLiteral("\xe6\x88\x90\xe7\xbb\x84"));  // 成组
    if (!doc) return;

    // Pre-validate (same rules as createGroup): >= 2 members, all present,
    // same layer, none already grouped. Invalid → redo/undo are no-ops.
    if (memberIds.size() < 2) return;
    QUuid layer;
    for (const QUuid& id : memberIds) {
        const auto* b = doc->findBlock(id);
        if (!b) return;
        if (!doc->groupOfBlock(id).isNull()) return;
        if (layer.isNull()) layer = b->layer;
        else if (b->layer != layer) return;
    }
    m_valid = true;
}

void MakeGroupCommand::redo()
{
    if (!m_valid) return;

    // Create the group (first redo) or re-insert the captured record
    // (replay after undo — the serial must not advance again).
    if (m_group.id.isNull()) {
        const QUuid gid = m_doc->createGroup(m_memberIds);
        if (const auto* g = m_doc->findGroup(gid))
            m_group = *g;
    } else {
        m_doc->restoreGroup(m_group, m_memberIds);
    }
}

void MakeGroupCommand::undo()
{
    if (!m_valid) return;

    // Dissolve the group — 组零限制: 成员几何与连接在成组时从未被改动,
    // 解散也无需恢复任何东西.
    if (!m_group.id.isNull())
        m_doc->dissolveGroup(m_group.id);
}

// ─── UngroupCommand ───

UngroupCommand::UngroupCommand(cad::param::ParamDocument* doc,
                               const QUuid& groupId,
                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
{
    setText(QStringLiteral("\xe8\xa7\xa3\xe6\x95\xa3\xe7\xbb\x84"));  // 解散组
    if (!doc) return;
    const auto* g = doc->findGroup(groupId);
    if (!g) return;
    m_group = *g;
    m_members = doc->blocksInGroup(groupId);
    m_valid = true;
}

void UngroupCommand::redo()
{
    if (m_valid)
        m_doc->dissolveGroup(m_groupId);
}

void UngroupCommand::undo()
{
    if (m_valid)
        m_doc->restoreGroup(m_group, m_members);
}

// ─── RenameGroupCommand ───

RenameGroupCommand::RenameGroupCommand(cad::param::ParamDocument* doc,
                                       const QUuid& groupId,
                                       const QString& newName,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
    , m_newName(newName)
{
    setText(QStringLiteral("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"));  // 重命名组
    if (!doc) return;
    if (const auto* g = doc->findGroup(groupId)) {
        m_oldName = g->name;
        m_valid = true;
    }
}

void RenameGroupCommand::redo()
{
    if (m_valid && m_doc)
        m_doc->setGroupName(m_groupId, m_newName);
}

void RenameGroupCommand::undo()
{
    if (m_valid && m_doc)
        m_doc->setGroupName(m_groupId, m_oldName);
}

// ─── MoveGroupCommand ───

MoveGroupCommand::MoveGroupCommand(cad::param::ParamDocument* doc,
                                   int fromIndex, int toIndex,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_from(fromIndex)
    , m_to(toIndex)
{
    setText(QStringLiteral("\xe7\xa7\xbb\xe5\x8a\xa8\xe5\x88\x86\xe7\xbb\x84"));  // 移动分组
}

void MoveGroupCommand::redo()
{
    if (m_doc && m_from >= 0 && m_to >= 0)
        m_doc->moveGroup(m_from, m_to);
}

void MoveGroupCommand::undo()
{
    if (m_doc && m_from >= 0 && m_to >= 0)
        m_doc->moveGroup(m_to, m_from);
}

// ─── SetGroupBoundingBoxCommand ───

SetGroupBoundingBoxCommand::SetGroupBoundingBoxCommand(cad::param::ParamDocument* doc,
                                                       const QUuid& groupId,
                                                       bool visible,
                                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
    , m_newVisible(visible)
{
    setText(visible ? QStringLiteral("\xe6\x98\xbe\xe7\xa4\xba\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86")   // 显示包围框
                    : QStringLiteral("\xe9\x9a\x90\xe8\x97\x8f\xe5\x8c\x85\xe5\x9b\xb4\xe6\xa1\x86"));  // 隐藏包围框
    if (!doc) return;
    if (const auto* g = doc->findGroup(groupId)) {
        m_oldVisible = g->showBoundingBox;
        m_valid = true;
    }
}

void SetGroupBoundingBoxCommand::redo()
{
    if (m_valid && m_doc)
        m_doc->setGroupBoundingBoxVisible(m_groupId, m_newVisible);
}

void SetGroupBoundingBoxCommand::undo()
{
    if (m_valid && m_doc)
        m_doc->setGroupBoundingBoxVisible(m_groupId, m_oldVisible);
}


// ─── SetComponentHingeCommand ───

SetComponentHingeCommand::SetComponentHingeCommand(cad::param::ParamDocument* doc,
                                                   const QUuid& groupId,
                                                   const cad::param::ComponentHinge& hinge,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
    , m_newHinge(hinge)
{
    setText(QStringLiteral("\xe8\xae\xbe\xe7\xbd\xae\xe7\xbb\x84\xe4\xbb\xb6\xe4\xb8\xbb\xe8\xbf\x9e\xe6\x8e\xa5"));  // 设置组件主连接
    if (!doc) return;
    if (const auto* g = doc->findGroup(groupId)) {
        m_oldHasHinge = g->hasHinge;
        m_oldHinge = g->hinge;
        m_valid = true;
    }
}

void SetComponentHingeCommand::redo()
{
    if (m_valid && m_doc) {
        if (m_doc->hasComponentHinge(m_groupId))
            m_doc->clearComponentHinge(m_groupId);
        m_doc->setComponentHinge(m_groupId, m_newHinge);
    }
}

void SetComponentHingeCommand::undo()
{
    if (m_valid && m_doc) {
        m_doc->clearComponentHinge(m_groupId);
        if (m_oldHasHinge)
            m_doc->setComponentHinge(m_groupId, m_oldHinge);
    }
}

// ─── ClearComponentHingeCommand ───

ClearComponentHingeCommand::ClearComponentHingeCommand(cad::param::ParamDocument* doc,
                                                       const QUuid& groupId,
                                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
{
    setText(QStringLiteral("\xe6\xb8\x85\xe9\x99\xa4\xe7\xbb\x84\xe4\xbb\xb6\xe4\xb8\xbb\xe8\xbf\x9e\xe6\x8e\xa5"));  // 清除组件主连接
    if (!doc) return;
    if (const auto* g = doc->findGroup(groupId)) {
        m_oldHasHinge = g->hasHinge;
        m_oldHinge = g->hinge;
        m_valid = true;
    }
}

void ClearComponentHingeCommand::redo()
{
    if (m_valid && m_doc)
        m_doc->clearComponentHinge(m_groupId);
}

void ClearComponentHingeCommand::undo()
{
    if (m_valid && m_doc) {
        if (m_oldHasHinge)
            m_doc->setComponentHinge(m_groupId, m_oldHinge);
    }
}
// ─── AddGroupMembersCommand ───

AddGroupMembersCommand::AddGroupMembersCommand(cad::param::ParamDocument* doc,
                                               const QUuid& groupId,
                                               const QList<QUuid>& memberIds,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
    , m_memberIds(memberIds)
{
    setText(QStringLiteral("\xe5\x90\x91\xe7\xbb\x84\xe6\xb7\xbb\xe5\x8a\xa0\xe6\x88\x90\xe5\x91\x98"));  // 向组添加成员
    if (!doc) return;
    const auto* g = doc->findGroup(groupId);
    if (!g) return;
    m_group = *g;
    m_originalMembers = doc->blocksInGroup(groupId);
    if (m_memberIds.isEmpty()) return;

    // New members must exist, be ungrouped and share the group's layer
    // (same invariants as createGroup).
    const QList<QUuid> existing = m_originalMembers;
    const cad::param::Block* firstMember = existing.isEmpty() ? nullptr : doc->findBlock(existing.first());
    const QUuid layer = firstMember ? firstMember->layer : QUuid();
    for (const QUuid& id : m_memberIds) {
        const auto* b = doc->findBlock(id);
        if (!b) return;
        if (!doc->groupOfBlock(id).isNull()) return;
        if (!layer.isNull() && b->layer != layer) return;
    }
    m_valid = true;
}

void AddGroupMembersCommand::redo()
{
    if (!m_valid || !m_doc) return;
    for (const QUuid& id : m_memberIds)
        m_doc->addGroupMember(m_groupId, id);
}

void AddGroupMembersCommand::undo()
{
    if (m_valid && m_doc)
        m_doc->restoreGroup(m_group, m_originalMembers);
}

// ─── RemoveGroupMembersCommand ───

RemoveGroupMembersCommand::RemoveGroupMembersCommand(cad::param::ParamDocument* doc,
                                                     const QUuid& groupId,
                                                     const QList<QUuid>& memberIds,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_groupId(groupId)
    , m_memberIds(memberIds)
{
    setText(QStringLiteral("\xe4\xbb\x8e\xe7\xbb\x84\xe7\xa7\xbb\xe9\x99\xa4\xe6\x88\x90\xe5\x91\x98"));  // 从组移除成员
    if (!doc) return;
    const auto* g = doc->findGroup(groupId);
    if (!g) return;
    m_group = *g;
    m_originalMembers = doc->blocksInGroup(groupId);
    if (m_memberIds.isEmpty()) return;
    const QSet<QUuid> current(m_originalMembers.begin(), m_originalMembers.end());
    for (const QUuid& id : m_memberIds)
        if (!current.contains(id)) return;
    m_valid = true;
}

void RemoveGroupMembersCommand::redo()
{
    if (!m_valid || !m_doc) return;
    for (const QUuid& id : m_memberIds)
        m_doc->removeGroupMember(m_groupId, id);
}

void RemoveGroupMembersCommand::undo()
{
    if (m_valid && m_doc)
        m_doc->restoreGroup(m_group, m_originalMembers);
}

} // namespace cad::cmd
