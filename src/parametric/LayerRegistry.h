#pragma once

#include <QObject>
#include <QUuid>
#include <vector>

#include "parametric/Layer.h"

namespace cad::param {

/// Canvas layer registry (selection/visibility filter + auxiliary
/// calculation layer). Owns the layer list, the active layer and the
/// layered dirty-marking flags consumed by the resolve pipeline.
/// Pure registry: cross-domain effects (e.g. shifting blocks between
/// layers on removal) are orchestrated by ParamDocument.
///
/// Identity model: every layer carries a STABLE Layer::id; blocks reference
/// layers by id (Block::layer), so removal/reordering of other layers never
/// invalidates block references. Display order = vector order; row indices
/// are a VIEW concern (LayerPanel) and never stored in the model.
class LayerRegistry : public QObject
{
    Q_OBJECT

public:
    explicit LayerRegistry(QObject* parent = nullptr);

    /// The layer registry in DISPLAY ORDER. Element 0 is always the single
    /// auxiliary calculation layer; the rest are working layers. Always
    /// contains at least two layers. Read-only view — mutation goes through
    /// addLayer/removeLayerRaw/insertLayerAt (which keep the invariants and
    /// emit layersChanged()).
    [[nodiscard]] const std::vector<Layer>& layers() const { return m_layers; }
    [[nodiscard]] int layerCount() const { return static_cast<int>(m_layers.size()); }

    // --- Id/index mapping ---
    /// Display row of @p layerId (-1 when absent).
    [[nodiscard]] int indexOf(const QUuid& layerId) const;
    /// Layer with @p layerId (nullptr when absent).
    [[nodiscard]] const Layer* layerById(const QUuid& layerId) const;
    /// Id of the (single) auxiliary calculation layer.
    [[nodiscard]] QUuid auxLayerId() const;
    /// Id of the first working layer (always present).
    [[nodiscard]] QUuid firstWorkingLayerId() const;

    /// Append a WORKING layer and return its id. Emits layersChanged().
    QUuid addLayer(const QString& name);
    /// Registry-only removal: drops the layer and fixes the active layer.
    /// Rejections (aux layer, last working layer) apply here; block
    /// re-layering is orchestrated by ParamDocument::removeLayer.
    void removeLayerRaw(const QUuid& layerId);
    /// Re-insert a layer at display row @p index (clamped to range) — the
    /// undo half of removeLayerRaw. The record keeps its original id.
    void insertLayerAt(int index, Layer layer);
    /// Replace the ENTIRE layer list verbatim (deserializer batch restore).
    /// Silent — no signals; the caller's finishRestore() pipeline refreshes.
    void replaceLayersRaw(std::vector<Layer> layers);
    void renameLayer(const QUuid& layerId, const QString& name);  ///< Emits layersChanged().
    /// Toggle layer visibility. Hiding the active layer auto-switches the
    /// active layer to the nearest visible one. Emits layersChanged() (and
    /// activeLayerChanged() if the active layer moved).
    void setLayerVisible(const QUuid& layerId, bool visible);
    [[nodiscard]] bool layerVisible(const QUuid& layerId) const;
    [[nodiscard]] QUuid activeLayer() const { return m_activeLayer; }
    void setActiveLayer(const QUuid& layerId);   ///< Emits activeLayerChanged().

    /// True when @p layerId is the auxiliary calculation layer.
    [[nodiscard]] bool isAuxLayer(const QUuid& layerId) const;

    // --- Layered dirty marking (resolve pipeline optimisation) ---
    /// Mark the layer group (aux vs working) containing @p layerId as
    /// needing re-resolution. Accumulates until consumed by resolveAll().
    void invalidateLayer(const QUuid& layerId);
    /// Mark every layer group dirty (variable edits, structural changes).
    void invalidateAllLayers();
    [[nodiscard]] bool auxDirty() const { return m_auxDirty; }
    [[nodiscard]] bool workingDirty() const { return m_workingDirty; }
    /// True when the caller narrowed the scope via invalidateLayer() /
    /// invalidateAllLayers(); false = conservative full re-resolve.
    [[nodiscard]] bool dirtyAnnotated() const { return m_dirtyAnnotated; }
    /// Clear the annotation flag after the resolve pass consumed it.
    void clearDirtyAnnotation() { m_dirtyAnnotated = false; }
    /// Store the post-pass dirty flags (the pass consumed the annotation).
    void setAuxDirty(bool v) { m_auxDirty = v; }
    void setWorkingDirty(bool v) { m_workingDirty = v; }

    /// Reset to the default two-layer state (aux + one working layer, fresh
    /// ids), all dirty. No signals emitted (used by document reset).
    void reset();

signals:
    void layersChanged();
    void activeLayerChanged(const QUuid& layerId);

private:
    std::vector<Layer> m_layers;   ///< Layer registry (display order).
    QUuid              m_activeLayer;  ///< Active layer id (first working layer).
    bool m_auxDirty = true;        ///< Aux layer needs re-resolution.
    bool m_workingDirty = true;    ///< Working layers need re-resolution.
    bool m_dirtyAnnotated = false; ///< Narrowed via invalidate*() by the caller.

    /// Build the default two-layer state and point m_activeLayer at the
    /// working layer. Shared by the constructor and reset().
    void initDefaultLayers();
};

} // namespace cad::param
