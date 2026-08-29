#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "document/commands/CommandIds.h"  // central merge-id enum (P0-2)

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add a plain variable.
class AddVariableCommand : public QUndoCommand
{
public:
    AddVariableCommand(cad::param::ParamDocument* doc,
                       cad::param::Variable var,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Variable m_var;
};

/// Remove a plain variable.
class RemoveVariableCommand : public QUndoCommand
{
public:
    RemoveVariableCommand(cad::param::ParamDocument* doc,
                          const QUuid& varId,
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Variable m_var;  ///< Saved for undo.
};

/// Set a variable's value (supports mergeWith for continuous slider/spinbox).
class SetVariableValueCommand : public QUndoCommand
{
public:
    SetVariableValueCommand(cad::param::ParamDocument* doc,
                            const QUuid& varId, double newValue,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return static_cast<int>(CommandId::SetVariableValue); }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_varId;
    double m_oldValue;
    double m_newValue;
};

/// Update a variable's full state (name/refName/value).
class SetVariableCommand : public QUndoCommand
{
public:
    SetVariableCommand(cad::param::ParamDocument* doc,
                       const cad::param::Variable& newVar,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Variable m_oldVar;
    cad::param::Variable m_newVar;
};

/// Add a formula variable.
class AddFormulaCommand : public QUndoCommand
{
public:
    AddFormulaCommand(cad::param::ParamDocument* doc,
                      cad::param::FormulaVariable formula,
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::FormulaVariable m_formula;
};

/// Remove a formula variable.
class RemoveFormulaCommand : public QUndoCommand
{
public:
    RemoveFormulaCommand(cad::param::ParamDocument* doc,
                         const QUuid& formulaId,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::FormulaVariable m_formula;  ///< Saved for undo.
};

/// Update a formula's expression/name/conditions.
class SetFormulaCommand : public QUndoCommand
{
public:
    SetFormulaCommand(cad::param::ParamDocument* doc,
                      const cad::param::FormulaVariable& newFormula,
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::FormulaVariable m_oldFormula;
    cad::param::FormulaVariable m_newFormula;
};

/// Add an (empty) formula group.
class AddFormulaGroupCommand : public QUndoCommand
{
public:
    AddFormulaGroupCommand(cad::param::ParamDocument* doc,
                           cad::param::FormulaGroup group,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::FormulaGroup m_group;
};

/// Dissolve a formula group (members become ungrouped).
class RemoveFormulaGroupCommand : public QUndoCommand
{
public:
    RemoveFormulaGroupCommand(cad::param::ParamDocument* doc,
                              const QUuid& groupId,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::FormulaGroup m_group;   ///< Saved for undo.
    int m_groupIndex = -1;              ///< Position in the group registry.
    QList<QUuid> m_memberIds;           ///< Members at removal time (display order).
};

/// Rename a formula group.
class RenameFormulaGroupCommand : public QUndoCommand
{
public:
    RenameFormulaGroupCommand(cad::param::ParamDocument* doc,
                              const QUuid& groupId, const QString& newName,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_groupId;
    QString m_oldName;
    QString m_newName;
};

/// Move a formula to a (possibly different) group / local position.
class MoveFormulaCommand : public QUndoCommand
{
public:
    MoveFormulaCommand(cad::param::ParamDocument* doc,
                       const QUuid& formulaId,
                       const QUuid& targetGroupId, int targetLocalIndex,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_formulaId;
    QUuid m_oldGroupId;
    int   m_oldLocalIndex = 0;
    QUuid m_newGroupId;
    int   m_newLocalIndex = 0;
};

/// Reorder formula groups.
class MoveFormulaGroupCommand : public QUndoCommand
{
public:
    MoveFormulaGroupCommand(cad::param::ParamDocument* doc,
                            int fromIndex, int toIndex,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    int m_fromIndex;
    int m_toIndex;
};

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

/// Add a length measure variable (undoable counterpart of addMeasure).
class AddMeasureCommand : public QUndoCommand
{
public:
    AddMeasureCommand(cad::param::ParamDocument* doc,
                      cad::param::MeasureVariable mv,
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::MeasureVariable m_mv;
};

/// Add an angle measure variable (undoable counterpart of addAngleMeasure).
class AddAngleMeasureCommand : public QUndoCommand
{
public:
    AddAngleMeasureCommand(cad::param::ParamDocument* doc,
                           cad::param::AngleMeasureVariable am,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::AngleMeasureVariable m_am;
};

/// Remove a length measure variable.
class RemoveMeasureCommand : public QUndoCommand
{
public:
    RemoveMeasureCommand(cad::param::ParamDocument* doc,
                         const QUuid& measureId,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::MeasureVariable m_mv;  ///< Saved for undo.
};

/// Update a length measure variable's name/comment.
class SetMeasureCommand : public QUndoCommand
{
public:
    SetMeasureCommand(cad::param::ParamDocument* doc,
                      const cad::param::MeasureVariable& newMv,
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::MeasureVariable m_oldMv;
    cad::param::MeasureVariable m_newMv;
};

/// Remove an angle measure variable.
class RemoveAngleMeasureCommand : public QUndoCommand
{
public:
    RemoveAngleMeasureCommand(cad::param::ParamDocument* doc,
                              const QUuid& angleId,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::AngleMeasureVariable m_am;  ///< Saved for undo.
};

/// Update an angle measure variable's name/comment.
class SetAngleMeasureCommand : public QUndoCommand
{
public:
    SetAngleMeasureCommand(cad::param::ParamDocument* doc,
                           const cad::param::AngleMeasureVariable& newAm,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::AngleMeasureVariable m_oldAm;
    cad::param::AngleMeasureVariable m_newAm;
};

} // namespace cad::cmd
