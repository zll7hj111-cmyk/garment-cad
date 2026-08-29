#pragma once

/// Narrow domain view over the ParamDocument canvas-layer domain (B3,
/// 门面按域分组 — see BlockView.h for the pattern established in B1).
///
/// Covers the layer registry (selection/visibility filter + the auxiliary
/// calculation layer) and the layered dirty-marking query. Method names
/// mirror the facade 1:1 (except layers()→all() / layerById()→byId()) so a
/// facade→view mapping stays greppable.
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. Writes (addLayer / removeLayer / insertLayerAt /
///     renameLayer / setLayerVisible / setActiveLayer) and the dirty-marking
///     writes (invalidateLayer / invalidateAllLayers) stay on the facade:
///     they emit layersChanged() / activeLayerChanged() and maintain the
///     aux/working invariants.
///   * Stateless — holds a `const ParamDocument*`, always reflects the live
///     document.
///
/// Usage:  for (const Layer& l : doc.layersView().all()) { ... }
///         if (!doc.layersView().layerSnappable(id)) { ... }

#include <QUuid>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Layer.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

class LayersView
{
public:
    explicit LayersView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// The layer registry in DISPLAY ORDER; element 0 is always the single
    /// auxiliary calculation layer (read-only container view).
    [[nodiscard]] const std::vector<Layer>& all() const { return m_doc->layers(); }

    /// Layer with @p layerId (nullptr when absent, read-only overload).
    [[nodiscard]] const Layer* byId(const QUuid& layerId) const { return m_doc->layerById(layerId); }

    /// Display row of @p layerId (-1 when absent).
    [[nodiscard]] int layerIndex(const QUuid& layerId) const { return m_doc->layerIndex(layerId); }

    /// Number of layers (always ≥ 2: aux + at least one working layer).
    [[nodiscard]] int layerCount() const { return m_doc->layerCount(); }

    /// Id of the (single) auxiliary calculation layer.
    [[nodiscard]] QUuid auxLayerId() const { return m_doc->auxLayerId(); }

    /// Id of the first working layer (always present).
    [[nodiscard]] QUuid firstWorkingLayerId() const { return m_doc->firstWorkingLayerId(); }

    /// Id of the active layer.
    [[nodiscard]] QUuid activeLayer() const { return m_doc->activeLayer(); }

    /// Manual visibility flag (hidden layers are not rendered at all).
    [[nodiscard]] bool layerVisible(const QUuid& layerId) const { return m_doc->layerVisible(layerId); }

    /// True when @p layerId is the auxiliary calculation layer.
    [[nodiscard]] bool isAuxLayer(const QUuid& layerId) const { return m_doc->isAuxLayer(layerId); }

    /// True when the block lives on the auxiliary layer.
    [[nodiscard]] bool isAuxBlock(const Block& b) const { return m_doc->isAuxBlock(b); }

    /// Effective visibility for RENDERING: non-active layers render grayed,
    /// only manually hidden layers are not rendered.
    [[nodiscard]] bool layerEffectivelyVisible(const QUuid& layerId) const
    { return m_doc->layerEffectivelyVisible(layerId); }

    /// Whether the layer's points/segments may be SNAP targets (the aux layer
    /// only while it is the active layer — snap targets must never be
    /// connectable).
    [[nodiscard]] bool layerSnappable(const QUuid& layerId) const
    { return m_doc->layerSnappable(layerId); }

    /// True when the document contains at least one cross-layer attachment
    /// (aux follower → working leader); resolve frames short-circuit the
    /// Phase 3 settle at zero cost otherwise.
    [[nodiscard]] bool hasCrossLayerAttachments() const
    { return m_doc->hasCrossLayerAttachments(); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline LayersView ParamDocument::layersView() const noexcept
{
    return LayersView(*this);
}

} // namespace cad::param
