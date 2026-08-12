#include "parametric/LayerRegistry.h"

namespace cad::param {

LayerRegistry::LayerRegistry(QObject* parent)
    : QObject(parent)
{
    // Always an auxiliary calculation layer + one working layer.
    initDefaultLayers();
}

void LayerRegistry::initDefaultLayers()
{
    m_layers.clear();
    Layer aux;
    aux.name = QStringLiteral("辅助层");
    aux.type = LayerType::Auxiliary;
    m_layers.push_back(std::move(aux));
    Layer working;
    working.name = QStringLiteral("图层 1");
    m_layers.push_back(std::move(working));
    m_activeLayer = m_layers.back().id;
}

int LayerRegistry::indexOf(const QUuid& layerId) const
{
    for (int i = 0; i < layerCount(); ++i)
        if (m_layers[static_cast<size_t>(i)].id == layerId)
            return i;
    return -1;
}

const Layer* LayerRegistry::layerById(const QUuid& layerId) const
{
    const int i = indexOf(layerId);
    return i < 0 ? nullptr : &m_layers[static_cast<size_t>(i)];
}

QUuid LayerRegistry::auxLayerId() const
{
    return m_layers.empty() ? QUuid() : m_layers.front().id;
}

QUuid LayerRegistry::firstWorkingLayerId() const
{
    for (const auto& l : m_layers)
        if (l.type == LayerType::Working)
            return l.id;
    return QUuid();
}

QUuid LayerRegistry::addLayer(const QString& name)
{
    Layer l;
    l.name = name;
    m_layers.push_back(std::move(l));
    emit layersChanged();
    return m_layers.back().id;
}

void LayerRegistry::removeLayerRaw(const QUuid& layerId)
{
    const int n = layerCount();
    if (n <= 2)
        return;  // Need at least aux + one working layer.
    const int index = indexOf(layerId);
    if (index < 0)
        return;
    if (isAuxLayer(layerId))
        return;  // The auxiliary calculation layer cannot be removed.

    m_layers.erase(m_layers.begin() + index);

    // Removed the active layer: fall back to the nearest working layer.
    if (m_activeLayer == layerId) {
        m_activeLayer = firstWorkingLayerId();
        emit activeLayerChanged(m_activeLayer);
    }

    emit layersChanged();
}

void LayerRegistry::insertLayerAt(int index, Layer layer)
{
    const int n = layerCount();
    if (index < 0 || index > n)
        index = n;  // Clamp to append.
    m_layers.insert(m_layers.begin() + index, std::move(layer));
    emit layersChanged();
}

void LayerRegistry::replaceLayersRaw(std::vector<Layer> layers)
{
    m_layers = std::move(layers);
}

void LayerRegistry::renameLayer(const QUuid& layerId, const QString& name)
{
    const int index = indexOf(layerId);
    if (index < 0)
        return;
    if (m_layers[static_cast<size_t>(index)].name != name) {
        m_layers[static_cast<size_t>(index)].name = name;
        emit layersChanged();
    }
}

void LayerRegistry::setLayerVisible(const QUuid& layerId, bool visible)
{
    const int index = indexOf(layerId);
    if (index < 0)
        return;
    if (m_layers[static_cast<size_t>(index)].visible == visible)
        return;
    m_layers[static_cast<size_t>(index)].visible = visible;
    emit layersChanged();

    // Hiding the active layer: switch to the nearest visible layer so the
    // user always has an editable layer.
    if (!visible && layerId == m_activeLayer) {
        for (int d = 1; d < layerCount(); ++d) {
            if (index - d >= 0 && m_layers[static_cast<size_t>(index - d)].visible) {
                setActiveLayer(m_layers[static_cast<size_t>(index - d)].id);
                return;
            }
            if (index + d < layerCount() && m_layers[static_cast<size_t>(index + d)].visible) {
                setActiveLayer(m_layers[static_cast<size_t>(index + d)].id);
                return;
            }
        }
    }
}

bool LayerRegistry::layerVisible(const QUuid& layerId) const
{
    const Layer* l = layerById(layerId);
    if (!l)
        return true;  // Unknown layer treated as visible (defensive).
    return l->visible;
}

void LayerRegistry::setActiveLayer(const QUuid& layerId)
{
    if (indexOf(layerId) < 0 || layerId == m_activeLayer)
        return;
    m_activeLayer = layerId;
    emit activeLayerChanged(layerId);
}

bool LayerRegistry::isAuxLayer(const QUuid& layerId) const
{
    const Layer* l = layerById(layerId);
    return l && l->type == LayerType::Auxiliary;
}

void LayerRegistry::invalidateLayer(const QUuid& layerId)
{
    if (isAuxLayer(layerId)) m_auxDirty = true;
    else m_workingDirty = true;
    m_dirtyAnnotated = true;
}

void LayerRegistry::invalidateAllLayers()
{
    m_auxDirty = m_workingDirty = true;
    m_dirtyAnnotated = true;
}

void LayerRegistry::reset()
{
    initDefaultLayers();
    m_auxDirty = true;
    m_workingDirty = true;
    m_dirtyAnnotated = false;
}

} // namespace cad::param
