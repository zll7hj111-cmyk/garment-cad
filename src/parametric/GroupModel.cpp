#include "GroupModel.h"

#include <QSet>

#include "parametric/Attachment.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

namespace {

GroupNode buildSubtree(const ParamDocument& doc, const QUuid& blockId,
                       double angleOffset, bool isRoot, QSet<QUuid>& visited)
{
    GroupNode node;
    node.blockId    = blockId;
    node.angleOffset = angleOffset;
    node.isRoot     = isRoot;
    visited.insert(blockId);

    if (const Block* b = doc.findBlock(blockId)) {
        if (!b->segments.empty()) {
            node.segmentId = b->segments.front().id;
            node.serial    = b->segments.front().serial;
            node.name      = b->segments.front().name;
        }
        if (node.name.isEmpty())
            node.name = b->name;
    }

    // Children = blocks attached to this one (this block is their leader).
    for (const Attachment& att : doc.attachments()) {
        if (att.toBlockId == blockId && !visited.contains(att.fromBlockId))
            node.children.push_back(
                buildSubtree(doc, att.fromBlockId, att.angleOffset, false, visited));
    }
    return node;
}

} // namespace

QUuid findGroupRoot(const ParamDocument& doc, const QUuid& seedBlockId)
{
    QSet<QUuid> seen;
    QUuid cur = seedBlockId;
    while (!seen.contains(cur)) {
        seen.insert(cur);
        QUuid leader;
        for (const Attachment& att : doc.attachments()) {
            if (att.fromBlockId == cur) {
                leader = att.toBlockId;
                break;
            }
        }
        if (leader.isNull())
            return cur;
        cur = leader;
    }
    return cur;  // cycle guard
}

GroupNode buildGroupTree(const ParamDocument& doc, const QUuid& seedBlockId)
{
    const QUuid root = findGroupRoot(doc, seedBlockId);
    QSet<QUuid> visited;
    return buildSubtree(doc, root, 0.0, true, visited);
}

std::vector<GroupNode> buildAllGroupTrees(const ParamDocument& doc)
{
    QSet<QUuid> hasLeader;
    for (const Attachment& att : doc.attachments())
        hasLeader.insert(att.fromBlockId);

    std::vector<GroupNode> roots;
    QSet<QUuid> visited;
    for (const Block& b : doc.blocks()) {
        if (!hasLeader.contains(b.id) && !visited.contains(b.id))
            roots.push_back(buildSubtree(doc, b.id, 0.0, true, visited));
    }
    return roots;
}

QList<QUuid> collectGroupBlockIds(const ParamDocument& doc, const QUuid& seedBlockId)
{
    QList<QUuid> result;
    QSet<QUuid> visited;
    QList<QUuid> stack;
    stack.push_back(seedBlockId);
    visited.insert(seedBlockId);
    while (!stack.isEmpty()) {
        const QUuid cur = stack.takeLast();
        result.push_back(cur);
        for (const Attachment& att : doc.attachments()) {
            if (att.fromBlockId == cur && !visited.contains(att.toBlockId)) {
                visited.insert(att.toBlockId);
                stack.push_back(att.toBlockId);
            } else if (att.toBlockId == cur && !visited.contains(att.fromBlockId)) {
                visited.insert(att.fromBlockId);
                stack.push_back(att.fromBlockId);
            }
        }
    }
    return result;
}

std::vector<QList<QUuid>> findAllComponents(const ParamDocument& doc)
{
    // Build undirected adjacency from attachments.
    QSet<QUuid> blockSet;
    for (const auto& b : doc.blocks())
        blockSet.insert(b.id);

    QHash<QUuid, QSet<QUuid>> adj;
    for (const auto& att : doc.attachments()) {
        if (blockSet.contains(att.fromBlockId) && blockSet.contains(att.toBlockId)) {
            adj[att.fromBlockId].insert(att.toBlockId);
            adj[att.toBlockId].insert(att.fromBlockId);
        }
    }

    // BFS to find connected components.
    QSet<QUuid> visited;
    std::vector<QList<QUuid>> components;
    for (const auto& b : doc.blocks()) {
        if (visited.contains(b.id))
            continue;
        QList<QUuid> comp;
        QList<QUuid> queue;
        queue.push_back(b.id);
        visited.insert(b.id);
        while (!queue.isEmpty()) {
            const QUuid cur = queue.takeFirst();
            comp.push_back(cur);
            for (const QUuid& nb : adj.value(cur)) {
                if (!visited.contains(nb)) {
                    visited.insert(nb);
                    queue.push_back(nb);
                }
            }
        }
        if (comp.size() >= 2)
            components.push_back(std::move(comp));
    }
    return components;
}

} // namespace cad::param
