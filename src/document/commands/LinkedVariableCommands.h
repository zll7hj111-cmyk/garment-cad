#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "parametric/LinkedVariable.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add a linked variable (publish a geometric measurement).
class AddLinkedCommand : public QUndoCommand
{
public:
    AddLinkedCommand(cad::param::ParamDocument* doc,
                     cad::param::LinkedVariable lv,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::LinkedVariable m_lv;
};

/// Remove a linked variable.
class RemoveLinkedCommand : public QUndoCommand
{
public:
    RemoveLinkedCommand(cad::param::ParamDocument* doc,
                        const QUuid& linkedId,
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::LinkedVariable m_lv;  ///< Saved for undo.
};

/// Update a linked variable's name/comment.
class SetLinkedCommand : public QUndoCommand
{
public:
    SetLinkedCommand(cad::param::ParamDocument* doc,
                     const cad::param::LinkedVariable& newLv,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::LinkedVariable m_oldLv;
    cad::param::LinkedVariable m_newLv;
};

}  // namespace cad::cmd
