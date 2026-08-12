#include "parametric/GroupRegistry.h"

#include <algorithm>

#include "parametric/ParamDocument.h"

namespace cad::param {

GroupRegistry::GroupRegistry(ParamDocument* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
{
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
    m_groupMembers.insert(group.id, std::move(members));
    emit groupsChanged();
}

Group* GroupRegistry::findGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.end()) ? &(*it) : nullptr;
}

QUuid GroupRegistry::groupOfBlock(const QUuid& blockId) const
{
    return m_blockGroup.value(blockId);
}

QList<QUuid> GroupRegistry::blocksInGroup(const QUuid& groupId) const
{
    return m_groupMembers.value(groupId);
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

void GroupRegistry::restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup)
{
    m_groups = std::move(groups);
    m_blockGroup = std::move(blockGroup);
    m_groupMembers.clear();
    for (auto it = m_blockGroup.cbegin(); it != m_blockGroup.cend(); ++it)
        m_groupMembers[it.value()].push_back(it.key());
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
            for (auto mi = m_blockGroup.begin(); mi != m_blockGroup.end(); )
                mi = (mi.value() == gid) ? m_blockGroup.erase(mi) : std::next(mi);
            m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
                [&gid](const Group& g) { return g.id == gid; }),
                m_groups.end());
        }
    }
    emit groupsChanged();
}

void GroupRegistry::clear()
{
    m_groups.clear();
    m_blockGroup.clear();
    m_groupMembers.clear();
}

} // namespace cad::param
