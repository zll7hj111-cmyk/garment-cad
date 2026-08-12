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
    if (m_layerId.isNull()) {
        m_layerId = m_doc->addLayer(m_name);
    } else {
        // Re-add after undo: re-insert the record under its ORIGINAL stable
        // id (快照完整性 — nothing else may reference a brand-new id).
        cad::param::Layer l;
        l.id = m_layerId;
        l.name = m_name;
        m_doc->insertLayerAt(m_doc->layerCount(), std::move(l));
    }
    // A newly created layer becomes the active one. Doing it inside redo()
    // (rather than only in the UI slot) means a Ctrl+Y redo of "新建图层"
    // re-activates the layer, so subsequently drawn lines land on it.
    m_doc->setActiveLayer(m_layerId);
}

void AddLayerCommand::undo()
{
    m_doc->removeLayer(m_layerId);
    // removeLayer() re-targets the active layer on its own, but that does NOT
    // bring back the pre-command active layer — restore the snapshot
    // (快照完整性). The snapshot id is still valid (adding never removes).
    m_doc->setActiveLayer(m_oldActive);
}

// ─── RemoveLayerCommand ───

RemoveLayerCommand::RemoveLayerCommand(cad::param::ParamDocument* doc, int row,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_row(row)
{
    setText(QStringLiteral("删除图层"));
    if (!doc)
        return;
    m_oldActive = doc->activeLayer();
    const auto& layers = doc->layers();
    if (row >= 0 && row < static_cast<int>(layers.size())) {
        m_layer = layers[static_cast<size_t>(row)];
        for (const auto& b : doc->blocks()) {
            if (b.layer == m_layer.id)
                m_memberIds.append(b.id);
        }
    }
}

void RemoveLayerCommand::redo() { m_doc->removeLayer(m_layer.id); }

void RemoveLayerCommand::undo()
{
    // Re-insert the record at its original display row (stable id kept) —
    // no other block reference was disturbed by the removal, so ONLY the
    // former members need to move back into the restored layer.
    m_doc->insertLayerAt(m_row, m_layer);
    for (const auto& id : m_memberIds) {
        if (auto* b = m_doc->findBlock(id))
            b->layer = m_layer.id;
    }

    emit m_doc->layersChanged();

    // redo's removeLayer() already re-targeted the active layer for the
    // shrunk list; put it back to the pre-removal snapshot (快照完整性).
    m_doc->setActiveLayer(m_oldActive);
}

// ─── RenameLayerCommand ───

RenameLayerCommand::RenameLayerCommand(cad::param::ParamDocument* doc, int row,
                                       const QString& newName, QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newName(newName)
{
    setText(QStringLiteral("重命名图层"));
    const auto& layers = doc->layers();
    if (row >= 0 && row < static_cast<int>(layers.size())) {
        m_layerId = layers[static_cast<size_t>(row)].id;
        m_oldName = layers[static_cast<size_t>(row)].name;
    }
}

void RenameLayerCommand::redo() { m_doc->renameLayer(m_layerId, m_newName); }
void RenameLayerCommand::undo() { m_doc->renameLayer(m_layerId, m_oldName); }

// ─── MoveBlockToLayerCommand ───

MoveBlockToLayerCommand::MoveBlockToLayerCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId, int row,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_blockId(blockId)
{
    setText(QStringLiteral("移动到其他图层"));
    const auto& layers = doc->layers();
    if (row >= 0 && row < static_cast<int>(layers.size()))
        m_newLayer = layers[static_cast<size_t>(row)].id;
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

// ─── SetLayerVisibleCommand ───

SetLayerVisibleCommand::SetLayerVisibleCommand(cad::param::ParamDocument* doc,
                                               const QUuid& layerId, bool visible,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_layerId(layerId), m_newVisible(visible)
{
    setText(visible ? QStringLiteral("显示图层") : QStringLiteral("隐藏图层"));
    m_oldVisible = doc->layerVisible(layerId);
}

void SetLayerVisibleCommand::redo()
{ m_doc->setLayerVisible(m_layerId, m_newVisible); }

void SetLayerVisibleCommand::undo()
{ m_doc->setLayerVisible(m_layerId, m_oldVisible); }

} // namespace cad::cmd
