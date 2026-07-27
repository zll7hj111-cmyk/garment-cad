#pragma once

#include <QList>
#include <QString>
#include <QUuid>
#include <vector>

namespace cad::param {

class ParamDocument;

/// A node in the attachment group tree. Each node corresponds to one block
/// (currently one segment per block); children are blocks attached to it.
struct GroupNode {
    QUuid   blockId;
    QUuid   segmentId;          ///< First segment of the block (for editing).
    QString serial;             ///< Readable serial of the first segment (e.g. "k9x2bL1").
    QString name;               ///< Segment (or block) display name.
    double  angleOffset = 0.0;  ///< Construction angle relative to parent (deg).
    bool    isRoot = false;     ///< True for the top-most leader of the group.
    std::vector<GroupNode> children;
};

/// Find the root (top-most leader) of the group containing seedBlockId by
/// walking leader links upward. Cycle-safe.
[[nodiscard]] QUuid findGroupRoot(const ParamDocument& doc, const QUuid& seedBlockId);

/// Build the full group tree (rooted at the top-most leader) for the group
/// that contains seedBlockId.
[[nodiscard]] GroupNode buildGroupTree(const ParamDocument& doc, const QUuid& seedBlockId);

/// Build trees for every group (forest) in the document.
[[nodiscard]] std::vector<GroupNode> buildAllGroupTrees(const ParamDocument& doc);

/// Collect all block IDs in the connected component containing seedBlockId,
/// traversing attachments in both directions. Used for group drag/highlight.
[[nodiscard]] QList<QUuid> collectGroupBlockIds(const ParamDocument& doc, const QUuid& seedBlockId);

/// Find all connected components (groups of >= 2 blocks) in the attachment
/// graph. Each component is a list of block IDs. Singleton blocks are excluded.
[[nodiscard]] std::vector<QList<QUuid>> findAllComponents(const ParamDocument& doc);

} // namespace cad::param
