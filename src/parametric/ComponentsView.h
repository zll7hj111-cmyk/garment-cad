#pragma once

/// Narrow domain view over the ParamDocument component domain (B2,
/// 门面按域分组 — see BlockView.h for the pattern established in B1).
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. Structural writes (addComponent / removeComponentRecord /
///     updateComponent) stay on the facade: they carry member validation,
///     the reverse block→component index and the componentsChanged() signal.
///   * Stateless — holds a `const ParamDocument*`, always reflects the live
///     document.
///
/// Usage:  if (const Component* c = doc.componentsView().ofBlock(blockId)) { ... }
///         BBox box = doc.componentsView().boundingBoxOf(compId);

#include <QList>
#include <QSet>
#include <QUuid>
#include <vector>

#include "parametric/Component.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

class ComponentsView
{
public:
    explicit ComponentsView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// All components in document order (read-only container view).
    [[nodiscard]] const std::vector<Component>& all() const { return m_doc->components(); }

    /// O(1)-ish lookup by id; nullptr when absent (read-only overload).
    [[nodiscard]] const Component* byId(const QUuid& id) const { return m_doc->findComponent(id); }

    /// Reverse lookup: the component @p blockId belongs to (nullptr if none).
    [[nodiscard]] const Component* ofBlock(const QUuid& blockId) const
    { return m_doc->componentOfBlock(blockId); }

    /// World-space AABB over every member's resolved geometry (points + curve hull).
    [[nodiscard]] BBox boundingBoxOf(const QUuid& componentId) const
    { return m_doc->boundingBoxOf(componentId); }

    /// World-space AABB over an arbitrary block set (旋转选集高亮).
    [[nodiscard]] BBox boundingBoxOfBlocks(const QList<QUuid>& blockIds) const
    { return m_doc->boundingBoxOfBlocks(blockIds); }

    /// Expand a seed set to the full component closure: every member of every
    /// component that intersects @p seed (整组拖动/旋转与脏传播共用).
    [[nodiscard]] QSet<QUuid> closure(const QSet<QUuid>& seed) const
    { return m_doc->componentClosure(seed); }

    /// Member block owning @p pointId inside @p comp (null id when none).
    [[nodiscard]] QUuid memberOwningPoint(const Component& comp, const QUuid& pointId) const
    { return m_doc->memberOwningPoint(comp, pointId); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline ComponentsView ParamDocument::componentsView() const noexcept
{
    return ComponentsView(*this);
}

} // namespace cad::param
