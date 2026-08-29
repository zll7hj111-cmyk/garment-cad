#pragma once

/// Narrow domain view over the ParamDocument blocks domain (B1 pilot,
/// 门面按域分组 — ARCHITECTURE_REVIEW).
///
/// ParamDocument is a facade whose public methods span 19 domains — the
/// "万能抽屉" problem. Domain views group that surface: a caller working
/// with blocks names the domain explicitly (`doc.blocksView().byId(id)`)
/// and this header documents the domain's contract in one place.
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. Structural writes stay on the facade (addBlock /
///     removeBlock carry validation + signals; a view cannot bypass them).
///   * Stateless — holds a `const ParamDocument*`, so a view is cheap to
///     pass around and always reflects the live document.
///   * Pointer lifetime follows the facade rule (P1-3): byId() returns a
///     pointer into the blocks vector, valid only until the next structural
///     mutation. Re-fetch per use; epoch() is the invalidation signal.
///     (Mid-term C-class handle work slots in HERE without touching call
///     sites again — that is the strategic point of this view.)
///
/// Usage:  const Block* b = doc.blocksView().byId(id);
///         for (const Block& b : doc.blocksView().all()) { ... }

#include <QUuid>
#include <vector>

#include "parametric/Block.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

class BlocksView
{
public:
    explicit BlocksView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// All blocks in document order (read-only container view).
    [[nodiscard]] const std::vector<Block>& all() const { return m_doc->blocks(); }

    /// O(1) lookup by id; nullptr when absent. The pointer dies with the
    /// next structural mutation — re-fetch per use (epoch() = invalidation).
    [[nodiscard]] const Block* byId(const QUuid& id) const { return m_doc->findBlock(id); }

    /// Bumped on every structural change of the block vector — the cache
    /// invalidation signal for "the set of blocks" (P1-3).
    [[nodiscard]] quint64 epoch() const noexcept { return m_doc->structureEpoch(); }

    /// Predicted removal consequences (删除影响报告, advisory read-only).
    [[nodiscard]] ParamDocument::DeleteImpact impactOf(const QUuid& id) const
    { return m_doc->deleteImpactReport(id); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline BlocksView ParamDocument::blocksView() const noexcept
{
    return BlocksView(*this);
}

} // namespace cad::param
