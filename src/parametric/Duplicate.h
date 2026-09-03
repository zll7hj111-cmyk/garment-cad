#pragma once

#include <QList>
#include <QUuid>
#include <optional>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/LinkedVariable.h"
#include "parametric/Component.h"

namespace cad::param {

class ParamDocument;

/// Result of duplicating a set of blocks (Ctrl+drag quick copy, 快捷复制).
struct DuplicateResult {
    std::vector<Block> blocks;              ///< Clones: fresh UUIDs + serials.
    std::vector<Attachment> attachments;    ///< Internal attachments, id-remapped.
    std::vector<LinkedVariable> newLinked;  ///< Bridge-length linked variables
                                            ///< auto-published for released
                                            ///< bridge copies (may be empty).
    std::vector<Component> components;      ///< Fully-enclosed components, id-remapped.
    [[nodiscard]] bool isEmpty() const { return blocks.empty(); }
};

/// Deep-copy the given blocks with all parametric information preserved
/// (除 ID 外信息全同): names, styles, roles, formulas, follower angles,
/// auxiliary points. Every UUID is regenerated and remapped; point/segment
/// serials are freshly assigned from the document counters.
///
/// Length linkage (长度关联, 角度保持数值):
///   - Formula-driven lengths are copied as-is (they already track their
///     variables).
///   - Plain NUMERIC lengths are replaced by the ORIGINAL segment's linked
///     variable (published on demand into newLinked), so editing the
///     original's length updates every copy.
///
/// Connection rules (以"是否在复制集合内"为界):
///   - Attachment with BOTH endpoints inside the set → cloned + remapped,
///     so the copied group keeps its internal structure (and its bridges).
///   - Follower attachment to an OUTSIDE leader → dropped; the clone keeps
///     its current world pose as an independent line.
///   - Bridge (Block::isBridge) losing at least one outside pin → released:
///     the stretched geometry is frozen (Block::freezeSegmentGeometry), the
///     length formula is set to the ORIGINAL bridge segment's linked variable
///     (published on demand into newLinked) so the copy keeps tracking the
///     bridge's passive length, and a surviving inside pin becomes a normal
///     follower attachment preserving the current direction.
///
/// The document is NOT modified (serial counters advance only); the caller
/// adds the result via DuplicateBlocksCommand or directly for live preview.
///
[[nodiscard]] DuplicateResult duplicateBlocks(ParamDocument& doc,
                                              const QList<QUuid>& blockIds);

} // namespace cad::param
