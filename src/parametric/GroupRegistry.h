#pragma once

#include <QObject>
#include <QUuid>
#include <QHash>
#include <QList>
#include <vector>

#include "parametric/Group.h"

namespace cad::param {

class ParamDocument;

/// User-group registry (成组: authored protection locks). Groups are
/// USER-authored protection units — NOT derived from the attachment graph.
/// Members are locked against structural operations (single-line delete /
/// break / internal detach) until dissolved; the guard itself lives at the
/// tool layer, the model only keeps membership.
class GroupRegistry : public QObject
{
    Q_OBJECT

public:
    explicit GroupRegistry(ParamDocument* doc, QObject* parent = nullptr);

    /// Create a group over the given blocks. Members must exist (>= 2), share
    /// one layer and not already belong to a group (nesting forbidden).
    /// Returns the new group's id, or a null QUuid when rejected.
    /// Emits groupsChanged().
    QUuid createGroup(const QList<QUuid>& memberIds, const QString& name = QString());
    /// Dissolve a group: the record and membership vanish; member geometry
    /// and connections stay untouched. Emits groupsChanged().
    void dissolveGroup(const QUuid& groupId);
    /// Reorder groups within the registry (panel drag-sort). View-level
    /// ordering — not undoable; persisted via the groups array order.
    /// Emits groupsChanged().
    void moveGroup(int fromIndex, int toIndex);
    /// Re-insert a group record + member mapping verbatim (undo replay /
    /// batch restore). A record with the same id is replaced; members not
    /// present in the document are skipped. Emits groupsChanged().
    void restoreGroup(Group group, const QList<QUuid>& memberIds);
    [[nodiscard]] const std::vector<Group>& groups() const { return m_groups; }
    [[nodiscard]] Group* findGroup(const QUuid& groupId);
    /// Group id the block currently belongs to (null if ungrouped).
    [[nodiscard]] QUuid groupOfBlock(const QUuid& blockId) const;
    /// All block ids belonging to a group.
    [[nodiscard]] QList<QUuid> blocksInGroup(const QUuid& groupId) const;
    /// Rename a group (emits groupsChanged()).
    void setGroupName(const QUuid& groupId, const QString& name);

    /// Batch-restore groups and block-group mapping (used by deserializer).
    void restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup);

    /// Cascade for removeBlock(): drop the block from its group; a group
    /// that shrinks below two members dissolves automatically
    /// (组成员删到不足两条时自动解散). Emits groupsChanged() when the
    /// registry changed.
    void purgeBlock(const QUuid& blockId);

    /// Clear all registries (document reset). No signals emitted.
    void clear();

signals:
    void groupsChanged();

private:
    ParamDocument* m_doc = nullptr;
    std::vector<Group>   m_groups;       ///< Active groups (each has >= 2 members).
    QHash<QUuid, QUuid>  m_blockGroup;   ///< blockId -> groupId.
    /// Reverse membership index (groupId -> member block ids, insertion
    /// order). Kept in sync with m_blockGroup so blocksInGroup() is
    /// O(members) instead of a full-table scan.
    QHash<QUuid, QList<QUuid>> m_groupMembers;
};

} // namespace cad::param
