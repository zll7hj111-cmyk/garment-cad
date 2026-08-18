#pragma once

#include <QObject>
#include <QUuid>
#include <QHash>
#include <QList>
#include <vector>

#include "parametric/Group.h"

namespace cad::param {

class ParamDocument;

/// User-group registry (成组/组件): stores user-authored membership and the
/// component metadata needed by Resolver. The model layer keeps membership
/// and, when a main hinge exists (hasHinge), a component participates in
/// geometry solving through Resolver's component pass (路线B, 2026-08).
/// The tool layer owns whole-group selection/drag/rotate and structural-
/// operation guards (break/intersection/curve point/aux point/end-aim) until
/// dissolved. Delete is NOT guarded; deleting members shrinks the group and
/// auto-dissolves below 2 members.
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
/// Add a block to an existing group. The block must exist, must not
    /// already belong to any group (nesting forbidden), and must share the
    /// group's layer. A group always keeps its existing members. Returns
    /// false (with no mutation) when the group or block is invalid/rejected.
    /// Emits groupsChanged() on success.
    bool addGroupMember(const QUuid& groupId, const QUuid& blockId);
    /// Remove a member from a group. When the group shrinks below 2 members
    /// it auto-dissolves (same rule as purgeBlock); otherwise the record stays
    /// and the component root/hinge are re-synced. Returns false when the
    /// group or block is not a current member. Emits groupsChanged() on
    /// success (including auto-dissolve).
    bool removeGroupMember(const QUuid& groupId, const QUuid& blockId);
    /// Rename a group (emits groupsChanged()).
    void setGroupName(const QUuid& groupId, const QString& name);
    /// Toggle or set bounding box visibility on canvas (emits groupsChanged()).
    void setGroupBoundingBoxVisible(const QUuid& groupId, bool visible);
    [[nodiscard]] bool isGroupBoundingBoxVisible(const QUuid& groupId) const;

    // --- Component (组件) API ---
    /// Ensure the group has a valid component root block. The root is the
    /// block whose Transform2D represents the component pose. This is pinned
    /// to the first surviving member on create/restore/purge; callers may
    /// force a specific root via restoreGroup's Group::componentRootBlockId.
    void ensureComponentRoot(const QUuid& groupId);
    /// Component root block id (null if the group has no valid root).
    [[nodiscard]] QUuid componentRootBlockId(const QUuid& groupId) const;
    /// True when the group is a component with a main hinge (Resolver drives
    /// the whole component from this hinge).
    [[nodiscard]] bool hasComponentHinge(const QUuid& groupId) const;
    /// Set the component's single main hinge. Returns false (and does nothing)
    /// when a hinge already exists, the member/point/leader references are not
    /// valid, or the group has no root. Only ONE hinge is supported.
    bool setComponentHinge(const QUuid& groupId, const ComponentHinge& hinge);
    /// Update the existing single main hinge (same validation, no
    /// single-hinge refusal — used for live rotation preview/commit).
    bool updateComponentHinge(const QUuid& groupId, const ComponentHinge& hinge);
    /// Remove the component's main hinge; the component returns to an ordinary
    /// group (no Resolver drive).
    void clearComponentHinge(const QUuid& groupId);
    /// Read-only access to the current hinge (nullptr when absent).
    [[nodiscard]] const ComponentHinge* componentHinge(const QUuid& groupId) const;
    /// Build Resolver-facing component snapshots for every active group.
    /// All groups are components; only those with hasHinge are driven by the
    /// Resolver, but every component carries its root/member list.
    [[nodiscard]] std::vector<Component> components() const;

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
    /// groupId -> component root block id. A cache built/kept in sync with
    /// m_groupMembers; Group::componentRootBlockId on the record is the
    /// authoritative value and this cache mirrors it for O(1) reads.
    QHash<QUuid, QUuid> m_groupRoot;

    /// Pin the root cache to the record's componentRootBlockId if that block
    /// is a member; otherwise pin it to the first member.
    void syncRootForGroup(Group& group);
    /// Const overload for read-only lookups.
    [[nodiscard]] const Group* findGroup(const QUuid& groupId) const;
};

} // namespace cad::param
