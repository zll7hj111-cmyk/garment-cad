#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QList>
#include <QString>

#include "parametric/Layer.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Append a new (empty) layer.
class AddLayerCommand : public QUndoCommand
{
public:
    AddLayerCommand(cad::param::ParamDocument* doc, const QString& name,
                    QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QString m_name;
    int m_index = -1;  ///< Index assigned on (re)do.
    int m_oldActive = 0;  ///< Active-layer snapshot pre-command (undo restores it).
};

/// Remove a layer: its blocks fall to the layer below, higher layers shift down.
class RemoveLayerCommand : public QUndoCommand
{
public:
    RemoveLayerCommand(cad::param::ParamDocument* doc, int index,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    int m_index = -1;
    cad::param::Layer m_layer;      ///< Removed layer data (for undo).
    QList<QUuid> m_memberIds;       ///< Blocks that lived in the layer at removal.
    int m_oldActive = 0;            ///< Active-layer snapshot pre-command (undo restores it).
};

/// Rename a layer.
class RenameLayerCommand : public QUndoCommand
{
public:
    RenameLayerCommand(cad::param::ParamDocument* doc, int index,
                       const QString& newName, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    int m_index;
    QString m_oldName;
    QString m_newName;
};

/// Move a block to a different layer.
class MoveBlockToLayerCommand : public QUndoCommand
{
public:
    MoveBlockToLayerCommand(cad::param::ParamDocument* doc, const QUuid& blockId,
                            int targetLayer, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    int m_oldLayer = 0;
    int m_newLayer = 0;
};

} // namespace cad::cmd
