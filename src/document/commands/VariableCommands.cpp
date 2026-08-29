#include "VariableCommands.h"

#include "parametric/ParamDocument.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── AddVariableCommand ───

AddVariableCommand::AddVariableCommand(cad::param::ParamDocument* doc,
                                       cad::param::Variable var,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_var(std::move(var))
{
    setText(QStringLiteral("添加变量"));
}

void AddVariableCommand::redo() { m_doc->addVariable(m_var); }
void AddVariableCommand::undo() { m_doc->removeVariable(m_var.id); }

// ─── RemoveVariableCommand ───

RemoveVariableCommand::RemoveVariableCommand(cad::param::ParamDocument* doc,
                                             const QUuid& varId,
                                             QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("删除变量"));
    if (auto* v = doc->findVariable(varId))
        m_var = *v;
}

void RemoveVariableCommand::redo() { m_doc->removeVariable(m_var.id); }
void RemoveVariableCommand::undo() { m_doc->addVariable(m_var); }

// ─── SetVariableValueCommand ───

SetVariableValueCommand::SetVariableValueCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& varId,
                                                 double newValue,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_varId(varId), m_newValue(newValue)
    , m_oldValue(0.0)
{
    setText(QStringLiteral("修改变量值"));
    if (auto* v = doc->findVariable(varId))
        m_oldValue = v->value;
}

void SetVariableValueCommand::redo()
{
    if (auto* v = m_doc->findVariable(m_varId)) {
        v->value = m_newValue;
        m_doc->recomputeFormulas();
    }
}

void SetVariableValueCommand::undo()
{
    if (auto* v = m_doc->findVariable(m_varId)) {
        v->value = m_oldValue;
        m_doc->recomputeFormulas();
    }
}

bool SetVariableValueCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    const auto* cmd = dynamic_cast<const SetVariableValueCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2)
    if (cmd->m_varId != m_varId) return false;
    m_newValue = cmd->m_newValue;
    return true;
}

// ─── SetVariableCommand ───

SetVariableCommand::SetVariableCommand(cad::param::ParamDocument* doc,
                                       const cad::param::Variable& newVar,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newVar(newVar)
{
    setText(QStringLiteral("修改变量"));
    if (auto* v = doc->findVariable(newVar.id))
        m_oldVar = *v;
}

void SetVariableCommand::redo() { m_doc->updateVariable(m_newVar); }
void SetVariableCommand::undo() { m_doc->updateVariable(m_oldVar); }

// ─── AddFormulaCommand ───

AddFormulaCommand::AddFormulaCommand(cad::param::ParamDocument* doc,
                                     cad::param::FormulaVariable formula,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_formula(std::move(formula))
{
    setText(QStringLiteral("添加公式"));
}

void AddFormulaCommand::redo() { m_doc->addFormula(m_formula); }
void AddFormulaCommand::undo() { m_doc->removeFormula(m_formula.id); }

// ─── RemoveFormulaCommand ───

RemoveFormulaCommand::RemoveFormulaCommand(cad::param::ParamDocument* doc,
                                           const QUuid& formulaId,
                                           QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("删除公式"));
    if (auto* f = doc->findFormula(formulaId))
        m_formula = *f;
}

void RemoveFormulaCommand::redo() { m_doc->removeFormula(m_formula.id); }
void RemoveFormulaCommand::undo() { m_doc->addFormula(m_formula); }

// ─── SetFormulaCommand ───

SetFormulaCommand::SetFormulaCommand(cad::param::ParamDocument* doc,
                                     const cad::param::FormulaVariable& newFormula,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newFormula(newFormula)
{
    setText(QStringLiteral("修改公式"));
    if (auto* f = doc->findFormula(newFormula.id))
        m_oldFormula = *f;
}

void SetFormulaCommand::redo() { m_doc->updateFormula(m_newFormula); }
void SetFormulaCommand::undo() { m_doc->updateFormula(m_oldFormula); }

namespace {
/// Local (display) index of a formula among the members of its current group.
int localIndexOf(cad::param::ParamDocument* doc, const QUuid& formulaId,
                 const QUuid& groupId)
{
    int local = 0;
    for (const auto& f : doc->formulas()) {
        if (f.id == formulaId)
            return local;
        if (f.groupId == groupId)
            ++local;
    }
    return local;
}
} // namespace

// ─── AddFormulaGroupCommand ───

AddFormulaGroupCommand::AddFormulaGroupCommand(cad::param::ParamDocument* doc,
                                               cad::param::FormulaGroup group,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_group(std::move(group))
{
    setText(QStringLiteral("新建分组"));
}

void AddFormulaGroupCommand::redo() { m_doc->addFormulaGroup(m_group); }
void AddFormulaGroupCommand::undo() { m_doc->removeFormulaGroup(m_group.id); }

// ─── RemoveFormulaGroupCommand ───

RemoveFormulaGroupCommand::RemoveFormulaGroupCommand(cad::param::ParamDocument* doc,
                                                     const QUuid& groupId,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("解散分组"));
    const auto& groups = doc->formulaGroups();
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        if (groups[i].id == groupId) {
            m_group = groups[i];
            m_groupIndex = i;
            break;
        }
    }
    for (const auto& f : doc->formulas()) {
        if (f.groupId == groupId)
            m_memberIds.append(f.id);
    }
}

void RemoveFormulaGroupCommand::redo() { m_doc->removeFormulaGroup(m_group.id); }

void RemoveFormulaGroupCommand::undo()
{
    // Restore the group at its original registry position...
    const int pos = qBound(0, m_groupIndex,
                           static_cast<int>(m_doc->formulaGroups().size()));
    cad::param::RawModelAccess::insertFormulaGroupAt(*m_doc, pos, m_group);
    // ...and re-attach the former members (relative order is untouched by
    // dissolution, so restoring groupId is enough).
    for (const auto& id : m_memberIds) {
        if (auto* f = m_doc->findFormula(id))
            f->groupId = m_group.id;
    }
    emit m_doc->formulasChanged();
}

// ─── RenameFormulaGroupCommand ───

RenameFormulaGroupCommand::RenameFormulaGroupCommand(cad::param::ParamDocument* doc,
                                                     const QUuid& groupId,
                                                     const QString& newName,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_groupId(groupId), m_newName(newName)
{
    setText(QStringLiteral("重命名分组"));
    if (auto* g = doc->findFormulaGroup(groupId))
        m_oldName = g->name;
}

void RenameFormulaGroupCommand::redo() { m_doc->renameFormulaGroup(m_groupId, m_newName); }
void RenameFormulaGroupCommand::undo() { m_doc->renameFormulaGroup(m_groupId, m_oldName); }

// ─── MoveFormulaCommand ───

MoveFormulaCommand::MoveFormulaCommand(cad::param::ParamDocument* doc,
                                       const QUuid& formulaId,
                                       const QUuid& targetGroupId,
                                       int targetLocalIndex,
                                       QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_formulaId(formulaId)
    , m_newGroupId(targetGroupId), m_newLocalIndex(targetLocalIndex)
{
    setText(QStringLiteral("移动公式"));
    if (auto* f = doc->findFormula(formulaId)) {
        m_oldGroupId = f->groupId;
        m_oldLocalIndex = localIndexOf(doc, formulaId, f->groupId);
    }
}

void MoveFormulaCommand::redo() { m_doc->moveFormula(m_formulaId, m_newGroupId, m_newLocalIndex); }
void MoveFormulaCommand::undo() { m_doc->moveFormula(m_formulaId, m_oldGroupId, m_oldLocalIndex); }

// ─── MoveFormulaGroupCommand ───

MoveFormulaGroupCommand::MoveFormulaGroupCommand(cad::param::ParamDocument* doc,
                                                 int fromIndex, int toIndex,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_fromIndex(fromIndex), m_toIndex(toIndex)
{
    setText(QStringLiteral("移动分组"));
}

void MoveFormulaGroupCommand::redo() { m_doc->moveFormulaGroup(m_fromIndex, m_toIndex); }
void MoveFormulaGroupCommand::undo() { m_doc->moveFormulaGroup(m_toIndex, m_fromIndex); }

// ─── AddLinkedCommand ───

AddLinkedCommand::AddLinkedCommand(cad::param::ParamDocument* doc,
                                   cad::param::LinkedVariable lv,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_lv(std::move(lv))
{
    setText(QStringLiteral("发布关联参数"));
}

void AddLinkedCommand::redo() { m_doc->addLinked(m_lv); }
void AddLinkedCommand::undo() { m_doc->removeLinked(m_lv.id); }

// ─── RemoveLinkedCommand ───

RemoveLinkedCommand::RemoveLinkedCommand(cad::param::ParamDocument* doc,
                                         const QUuid& linkedId,
                                         QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("删除关联参数"));
    if (auto* lv = doc->findLinked(linkedId))
        m_lv = *lv;
}

void RemoveLinkedCommand::redo() { m_doc->removeLinked(m_lv.id); }
void RemoveLinkedCommand::undo() { m_doc->addLinked(m_lv); }

// ─── SetLinkedCommand ───

SetLinkedCommand::SetLinkedCommand(cad::param::ParamDocument* doc,
                                   const cad::param::LinkedVariable& newLv,
                                   QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newLv(newLv)
{
    setText(QStringLiteral("修改关联参数"));
    if (auto* lv = doc->findLinked(newLv.id))
        m_oldLv = *lv;
}

void SetLinkedCommand::redo() { m_doc->updateLinked(m_newLv); }
void SetLinkedCommand::undo() { m_doc->updateLinked(m_oldLv); }

// ─── AddMeasureCommand ───

AddMeasureCommand::AddMeasureCommand(cad::param::ParamDocument* doc,
                                     cad::param::MeasureVariable mv,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_mv(std::move(mv))
{
    setText(QStringLiteral("添加测量"));
}

void AddMeasureCommand::redo() { m_doc->addMeasure(m_mv); }
void AddMeasureCommand::undo() { m_doc->removeMeasure(m_mv.id); }

// ─── AddAngleMeasureCommand ───

AddAngleMeasureCommand::AddAngleMeasureCommand(
    cad::param::ParamDocument* doc, cad::param::AngleMeasureVariable am,
    QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_am(std::move(am))
{
    setText(QStringLiteral("添加角度测量"));
}

void AddAngleMeasureCommand::redo() { m_doc->addAngleMeasure(m_am); }
void AddAngleMeasureCommand::undo() { m_doc->removeAngleMeasure(m_am.id); }

// ─── RemoveMeasureCommand ───

RemoveMeasureCommand::RemoveMeasureCommand(cad::param::ParamDocument* doc,
                                           const QUuid& measureId,
                                           QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("删除测量变量"));
    if (auto* mv = doc->findMeasure(measureId))
        m_mv = *mv;
}

void RemoveMeasureCommand::redo() { m_doc->removeMeasure(m_mv.id); }
void RemoveMeasureCommand::undo() { m_doc->addMeasure(m_mv); }

// ─── SetMeasureCommand ───

SetMeasureCommand::SetMeasureCommand(cad::param::ParamDocument* doc,
                                     const cad::param::MeasureVariable& newMv,
                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newMv(newMv)
{
    setText(QStringLiteral("修改测量变量"));
    if (auto* mv = doc->findMeasure(newMv.id))
        m_oldMv = *mv;
}

void SetMeasureCommand::redo() { m_doc->updateMeasure(m_newMv); }
void SetMeasureCommand::undo() { m_doc->updateMeasure(m_oldMv); }

// ─── RemoveAngleMeasureCommand ───

RemoveAngleMeasureCommand::RemoveAngleMeasureCommand(cad::param::ParamDocument* doc,
                                                     const QUuid& angleId,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc)
{
    setText(QStringLiteral("删除角度测量变量"));
    if (auto* am = doc->findAngleMeasure(angleId))
        m_am = *am;
}

void RemoveAngleMeasureCommand::redo() { m_doc->removeAngleMeasure(m_am.id); }
void RemoveAngleMeasureCommand::undo() { m_doc->addAngleMeasure(m_am); }

// ─── SetAngleMeasureCommand ───

SetAngleMeasureCommand::SetAngleMeasureCommand(cad::param::ParamDocument* doc,
                                               const cad::param::AngleMeasureVariable& newAm,
                                               QUndoCommand* parent)
    : QUndoCommand(parent), m_doc(doc), m_newAm(newAm)
{
    setText(QStringLiteral("修改角度测量变量"));
    if (auto* am = doc->findAngleMeasure(newAm.id))
        m_oldAm = *am;
}

void SetAngleMeasureCommand::redo() { m_doc->updateAngleMeasure(m_newAm); }
void SetAngleMeasureCommand::undo() { m_doc->updateAngleMeasure(m_oldAm); }

} // namespace cad::cmd
