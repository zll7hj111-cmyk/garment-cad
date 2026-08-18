#include "parametric/GroupRegistry.h"

#include <algorithm>

#include "parametric/ParamDocument.h"

namespace cad::param {

GroupRegistry::GroupRegistry(ParamDocument* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
{
}

void GroupRegistry::syncRootForGroup(Group& group)
{
    const QList<QUuid> members = m_groupMembers.value(group.id);
    QUuid root = group.componentRootBlockId;
    if (root.isNull() || !members.contains(root))
        root = members.isEmpty() ? QUuid() : members.first();
    group.componentRootBlockId = root;
    m_groupRoot.insert(group.id, root);
}

QUuid GroupRegistry::createGroup(const QList<QUuid>& memberIds, const QString& name)
{
    if (memberIds.size() < 2) return QUuid();

    // Validate: members exist, share one layer, none already grouped
    // (第一版禁嵌套、组成员必须同层).
    QUuid layer;
    for (const QUuid& id : memberIds) {
        const Block* b = m_doc->blockById(id);
        if (!b) return QUuid();
        if (m_blockGroup.contains(id)) return QUuid();
        if (layer.isNull()) layer = b->layer;
        else if (b->layer != layer) return QUuid();
    }

    Group g;
    g.serial = m_doc->newGroupSerial();
    g.name = name;
    const QUuid gid = g.id;
    m_groups.push_back(std::move(g));
    m_groupMembers.insert(gid, memberIds);
    for (const QUuid& id : memberIds)
        m_blockGroup.insert(id, gid);
    if (Group* stored = findGroup(gid))
        syncRootForGroup(*stored);
    emit groupsChanged();
    return gid;
}

void GroupRegistry::dissolveGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    if (it == m_groups.end()) return;
    m_groups.erase(it);
    m_groupMembers.remove(groupId);
    m_groupRoot.remove(groupId);
    for (auto i = m_blockGroup.begin(); i != m_blockGroup.end(); )
        i = (i.value() == groupId) ? m_blockGroup.erase(i) : std::next(i);
    emit groupsChanged();
}

void GroupRegistry::moveGroup(int fromIndex, int toIndex)
{
    const int n = static_cast<int>(m_groups.size());
    if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n)
        return;
    if (fromIndex == toIndex) return;
    Group g = std::move(m_groups[static_cast<size_t>(fromIndex)]);
    m_groups.erase(m_groups.begin() + fromIndex);
    m_groups.insert(m_groups.begin() + toIndex, std::move(g));
    emit groupsChanged();
}

void GroupRegistry::restoreGroup(Group group, const QList<QUuid>& memberIds)
{
    // Replace any record with the same id (idempotent undo replay).
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&group](const Group& g) { return g.id == group.id; });
    if (it != m_groups.end()) *it = group;
    else m_groups.push_back(group);
    // Replace the membership index entry wholesale (idempotent replay must
    // not duplicate members).
    QList<QUuid> members;
    for (const QUuid& id : memberIds)
        if (m_doc->blockById(id)) {              // skip members not (yet) restored
            members.push_back(id);
            m_blockGroup.insert(id, group.id);
        }
    // Remove stale block→group entries for this group that are no longer in
    // the restored member list (add/remove-member undo replay).
    for (auto mi = m_blockGroup.begin(); mi != m_blockGroup.end(); ) {
        if (mi.value() == group.id && !members.contains(mi.key()))
            mi = m_blockGroup.erase(mi);
        else
            ++mi;
    }
    m_groupMembers.insert(group.id, std::move(members));
    if (Group* stored = findGroup(group.id))
        syncRootForGroup(*stored);
    emit groupsChanged();
}

Group* GroupRegistry::findGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.end()) ? &(*it) : nullptr;
}

const Group* GroupRegistry::findGroup(const QUuid& groupId) const
{
    auto it = std::find_if(m_groups.cbegin(), m_groups.cend(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.cend()) ? &(*it) : nullptr;
}

QUuid GroupRegistry::groupOfBlock(const QUuid& blockId) const
{
    return m_blockGroup.value(blockId);
}

QList<QUuid> GroupRegistry::blocksInGroup(const QUuid& groupId) const
{
    return m_groupMembers.value(groupId);
}
bool GroupRegistry::addGroupMember(const QUuid& groupId, const QUuid& blockId)
{
    if (blockId.isNull()) return false;
    Group* g = findGroup(groupId);
    if (!g) return false;
    if (!m_blockGroup.value(blockId).isNull()) return false;   // nesting forbidden
    const Block* b = m_doc->blockById(blockId);
    if (!b) return false;

    const QList<QUuid> members = m_groupMembers.value(groupId);
    if (!members.isEmpty()) {
        const Block* first = m_doc->blockById(members.first());
        if (!first || first->layer != b->layer) return false;  // same-layer rule
    }

    auto& list = m_groupMembers[groupId];
    if (list.contains(blockId)) return false;
    list.push_back(blockId);
    m_blockGroup.insert(blockId, groupId);
    syncRootForGroup(*g);
    emit groupsChanged();
    return true;
}

bool GroupRegistry::removeGroupMember(const QUuid& groupId, const QUuid& blockId)
{
    Group* g = findGroup(groupId);
    if (!g) return false;
    auto it = m_groupMembers.find(groupId);
    if (it == m_groupMembers.end()) return false;
    auto& list = *it;
    auto rit = std::find(list.begin(), list.end(), blockId);
    if (rit == list.end()) return false;

    list.erase(rit);
    m_blockGroup.remove(blockId);
    if (list.size() < 2) {
        // Auto-dissolve below two members (same rule as purgeBlock).
        m_groupMembers.erase(it);
        m_groupRoot.remove(groupId);
        for (auto mi = m_blockGroup.begin(); mi != m_blockGroup.end(); )
            mi = (mi.value() == groupId) ? m_blockGroup.erase(mi) : std::next(mi);
        m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
            [&groupId](const Group& gr) { return gr.id == groupId; }),
            m_groups.end());
    } else {
        // If the hinge's member endpoint was removed, the hinge is no longer
        // a valid component connection — clear it and re-pin the root.
        if (g->hasHinge && g->hinge.memberBlockId == blockId) {
            g->hasHinge = false;
            g->hinge = ComponentHinge{};
        }
        syncRootForGroup(*g);
    }
    emit groupsChanged();
    return true;
}

void GroupRegistry::setGroupName(const QUuid& groupId, const QString& name)
{
    if (Group* g = findGroup(groupId)) {
        if (g->name != name) {
            g->name = name;
            emit groupsChanged();
        }
    }
}

void GroupRegistry::setGroupBoundingBoxVisible(const QUuid& groupId, bool visible)
{
    if (Group* g = findGroup(groupId)) {
        if (g->showBoundingBox != visible) {
            g->showBoundingBox = visible;
            emit groupsChanged();
        }
    }
}

bool GroupRegistry::isGroupBoundingBoxVisible(const QUuid& groupId) const
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.end()) ? it->showBoundingBox : true;
}

// --- Component (组件) API ---

void GroupRegistry::ensureComponentRoot(const QUuid& groupId)
{
    if (Group* g = findGroup(groupId))
        syncRootForGroup(*g);
}

QUuid GroupRegistry::componentRootBlockId(const QUuid& groupId) const
{
    return m_groupRoot.value(groupId);
}

bool GroupRegistry::hasComponentHinge(const QUuid& groupId) const
{
    const Group* g = findGroup(groupId);
    return g && g->hasHinge && g->hinge.isValid();
}

bool GroupRegistry::setComponentHinge(const QUuid& groupId, const ComponentHinge& hinge)
{
    Group* g = findGroup(groupId);
    if (!g) return false;
    if (g->hasHinge && g->hinge.isValid())
        return false;   // 单一主连接铰链：已有铰链时不再创建第二个

    const QList<QUuid> members = m_groupMembers.value(groupId);
    if (!members.contains(hinge.memberBlockId))
        return false;

    const Block* member = m_doc->blockById(hinge.memberBlockId);
    if (!member || !member->findPoint(hinge.memberPointId))
        return false;
    const Block* leader = m_doc->blockById(hinge.leaderBlockId);
    if (!leader || !leader->findPoint(hinge.leaderPointId))
        return false;

    ensureComponentRoot(groupId);
    g->hasHinge = true;
    g->hinge = hinge;
    emit groupsChanged();
    return true;
}

void GroupRegistry::clearComponentHinge(const QUuid& groupId)
{
    Group* g = findGroup(groupId);
    if (!g || !g->hasHinge) return;
    g->hasHinge = false;
    g->hinge = ComponentHinge{};
    emit groupsChanged();
}

bool GroupRegistry::updateComponentHinge(const QUuid& groupId, const ComponentHinge& hinge)
{
    Group* g = findGroup(groupId);
    if (!g) return false;
    const QList<QUuid> members = m_groupMembers.value(groupId);
    if (!members.contains(hinge.memberBlockId))
        return false;
    const Block* member = m_doc->blockById(hinge.memberBlockId);
    if (!member || !member->findPoint(hinge.memberPointId))
        return false;
    const Block* leader = m_doc->blockById(hinge.leaderBlockId);
    if (!leader || !leader->findPoint(hinge.leaderPointId))
        return false;
    ensureComponentRoot(groupId);
    g->hasHinge = true;
    g->hinge = hinge;
    emit groupsChanged();
    return true;
}
const ComponentHinge* GroupRegistry::componentHinge(const QUuid& groupId) const
{
    const Group* g = findGroup(groupId);
    if (!g || !g->hasHinge || !g->hinge.isValid())
        return nullptr;
    return &g->hinge;
}

std::vector<Component> GroupRegistry::components() const
{
    std::vector<Component> result;
    result.reserve(m_groups.size());
    for (const Group& g : m_groups) {
        Component c;
        c.groupId = g.id;
        c.rootBlockId = m_groupRoot.value(g.id);
        c.memberIds = m_groupMembers.value(g.id);
        c.hasHinge = g.hasHinge && g.hinge.isValid();
        c.hinge = g.hinge;
        result.push_back(std::move(c));
    }
    return result;
}

void GroupRegistry::restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup)
{
    m_groups = std::move(groups);
    m_blockGroup = std::move(blockGroup);
    m_groupMembers.clear();
    m_groupRoot.clear();
    for (auto it = m_blockGroup.cbegin(); it != m_blockGroup.cend(); ++it)
        m_groupMembers[it.value()].push_back(it.key());
    for (Group& g : m_groups)
        syncRootForGroup(g);
}

void GroupRegistry::purgeBlock(const QUuid& blockId)
{
    const QUuid gid = m_blockGroup.value(blockId);
    if (gid.isNull()) return;
    m_blockGroup.remove(blockId);
    auto it = m_groupMembers.find(gid);
    if (it != m_groupMembers.end()) {
        it->erase(std::remove(it->begin(), it->end(), blockId), it->end());
        if (it->size() < 2) {
            m_groupMembers.erase(it);
            m_groupRoot.remove(gid);
            for (auto mi = m_blockGroup.begin(); mi != m_blockGroup.end(); )
                mi = (mi.value() == gid) ? m_blockGroup.erase(mi) : std::next(mi);
            m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
                [&gid](const Group& g) { return g.id == gid; }),
                m_groups.end());
        } else if (Group* g = findGroup(gid)) {
            syncRootForGroup(*g);
        }
    }
    emit groupsChanged();
}

void GroupRegistry::clear()
{
    m_groups.clear();
    m_blockGroup.clear();
    m_groupMembers.clear();
    m_groupRoot.clear();
}

} // namespace cad::param
