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
    QUuid m_layerId;            ///< Stable id assigned on (re)do.
    QUuid m_oldActive;          ///< Active-layer snapshot pre-command (undo restores it).
};

/// Remove a layer: its blocks fall to the layer below (stable ids — no
/// other layer reference is disturbed).
class RemoveLayerCommand : public QUndoCommand
{
public:
    /// @p row Display row of the layer to remove (resolved to its stable id
    ///        at construction; kept for the undo re-insert position).
    RemoveLayerCommand(cad::param::ParamDocument* doc, int row,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    int m_row = -1;             ///< Display row at removal (undo re-insert slot).
    cad::param::Layer m_layer;  ///< Removed layer record incl. stable id (for undo).
    QList<QUuid> m_memberIds;   ///< Blocks that lived in the layer at removal.
    QUuid m_oldActive;          ///< Active-layer snapshot pre-command (undo restores it).
};

/// Rename a layer.
class RenameLayerCommand : public QUndoCommand
{
public:
    /// @p row Display row of the layer (resolved to its stable id at
    ///        construction).
    RenameLayerCommand(cad::param::ParamDocument* doc, int row,
                       const QString& newName, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_layerId;
    QString m_oldName;
    QString m_newName;
};

/// Move a block to a different layer.
class MoveBlockToLayerCommand : public QUndoCommand
{
public:
    /// @p row Display row of the TARGET layer (resolved to its stable id at
    ///        construction).
    MoveBlockToLayerCommand(cad::param::ParamDocument* doc, const QUuid& blockId,
                            int row, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_oldLayer;
    QUuid m_newLayer;
};

/// Toggle a layer's visibility. The flag is persisted in the file, so a
/// non-undoable toggle would silently break the dirty flag (save → toggle →
/// close would lose the change without prompting).
class SetLayerVisibleCommand : public QUndoCommand
{
public:
    SetLayerVisibleCommand(cad::param::ParamDocument* doc, const QUuid& layerId,
                           bool visible, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_layerId;
    bool m_oldVisible = false;   ///< Captured at construction.
    bool m_newVisible = false;
};

} // namespace cad::cmd
