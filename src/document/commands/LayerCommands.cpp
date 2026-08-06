#include "LayerCommands.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::cmd {

// ─── AddLayerCommand ───

AddLayerCommand::AddLayerCommand(cad::param::ParamDocument* doc, const QString& name,
                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_name(name)
{
    setText(QStringLiteral("新建图层"));
    if (doc)
        m_oldActive = doc->activeLayer();
}

void AddLayerCommand::redo()
{
    m_index = m_doc->addLayer(m_name);
    // A newly created layer becomes the active one. Doing it inside redo()
    // (rather than only in the UI slot) means a Ctrl+Y redo of "新建图层"
    // re-activates the layer, so subsequently drawn lines land on it.
    m_doc->setActiveLayer(m_index);
}

void AddLayerCommand::undo()
{
    m_doc->removeLayer(m_index);
    // removeLayer() clamps/shifts m_activeLayer on its own, but that does NOT
    // bring back the pre-command active layer (e.g. removing the just-added
    // top layer clamps to the new top instead of the original) — restore the
    // snapshot (快照完整性).
    m_doc->setActiveLayer(qBound(0, m_oldActive, m_doc->layerCount() - 1));
}

// ─── RemoveLayerCommand ───

RemoveLayerCommand::RemoveLayerCommand(cad::param::ParamDocument* doc, int index,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_index(index)
{
    setText(QStringLiteral("删除图层"));
    if (doc)
        m_oldActive = doc->activeLayer();
    const auto& layers = doc->layers();
    if (index >= 0 && index < static_cast<int>(layers.size()))
        m_layer = layers[index];
    for (const auto& b : doc->blocks()) {
        if (b.layer == index)
            m_memberIds.append(b.id);
    }
}

void RemoveLayerCommand::redo() { m_doc->removeLayer(m_index); }

void RemoveLayerCommand::undo()
{
    auto& layers = m_doc->layers();
    const int pos = qBound(0, m_index, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + pos, m_layer);

    // Shift blocks at/above the re-inserted slot up by one, then put the
    // former members back into their original layer.
    for (auto& b : m_doc->blocks()) {
        if (b.layer >= pos)
            ++b.layer;
    }
    for (const auto& id : m_memberIds) {
        if (auto* b = m_doc->findBlock(id))
            b->layer = pos;
    }

    emit m_doc->layersChanged();

    // redo's removeLayer() already re-adjusted m_activeLayer for the shrunk
    // list; undo re-inserted the layer, so put the active layer back to the
    // pre-removal snapshot (快照完整性 — the model-level adjustment does not
    // reverse to the original index in general).
    m_doc->setActiveLayer(qBound(0, m_oldActive, m_doc->layerCount() - 1));
}

// ─── RenameLayerCommand ───

RenameLayerCommand::RenameLayerCommand(cad::param::ParamDocument* doc, int index,
                                       const QString& newName, QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_index(index), m_newName(newName)
{
    setText(QStringLiteral("重命名图层"));
    const auto& layers = doc->layers();
    if (index >= 0 && index < static_cast<int>(layers.size()))
        m_oldName = layers[index].name;
}

void RenameLayerCommand::redo() { m_doc->renameLayer(m_index, m_newName); }
void RenameLayerCommand::undo() { m_doc->renameLayer(m_index, m_oldName); }

// ─── MoveBlockToLayerCommand ───

MoveBlockToLayerCommand::MoveBlockToLayerCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId, int targetLayer,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_blockId(blockId), m_newLayer(targetLayer)
{
    setText(QStringLiteral("移动到其他图层"));
    if (const auto* b = doc->findBlock(blockId))
        m_oldLayer = b->layer;
}

void MoveBlockToLayerCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->layer = m_newLayer;
        emit m_doc->layersChanged();
    }
}

void MoveBlockToLayerCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->layer = m_oldLayer;
        emit m_doc->layersChanged();
    }
}

} // namespace cad::cmd
