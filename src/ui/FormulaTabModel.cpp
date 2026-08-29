#include "FormulaTabModel.h"

#include "parametric/ParamDocument.h"
#include "document/commands/VariableCommands.h"

namespace cad::ui {

FormulaTabModel::FormulaTabModel(cad::param::ParamDocument* doc)
    : m_doc(doc)
{
}

void FormulaTabModel::rebuild()
{
    const auto& formulas = m_doc->formulas();
    const auto& groups = m_doc->formulaGroups();

    // Display order: ungrouped section first, then per-group header + members.
    // Members of collapsed groups get no rows at all (virtualization makes
    // hiding them free — no widgets, no visibility bookkeeping).
    QVector<Row> rows;
    rows.reserve(static_cast<int>(formulas.size() + groups.size()));

    int local = 0;
    for (const auto& f : formulas) {
        if (f.groupId.isNull()) {
            rows.append({false, f.id, QUuid(), local++});
        }
    }
    for (const auto& g : groups) {
        rows.append({true, g.id, QUuid(), 0});
        if (g.collapsed)
            continue;
        local = 0;
        for (const auto& f : formulas) {
            if (f.groupId == g.id) {
                rows.append({false, f.id, g.id, local++});
            }
        }
    }
    m_rows = std::move(rows);
}

QVector<QUuid> FormulaTabModel::keys() const
{
    QVector<QUuid> keys;
    keys.reserve(m_rows.size());
    for (const auto& r : m_rows)
        keys.append(r.id);
    return keys;
}

void FormulaTabModel::toggleCollapsed(const QUuid& groupId)
{
    // View state: not undoable, but persisted with the document.
    if (const auto* g = m_doc->variablesView().groupById(groupId))
        m_doc->setFormulaGroupCollapsed(groupId, !g->collapsed);
}

void FormulaTabModel::rename(const QUuid& groupId, const QString& newName)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RenameFormulaGroupCommand(m_doc, groupId, newName));
    else
        m_doc->renameFormulaGroup(groupId, newName);
}

void FormulaTabModel::dissolve(const QUuid& groupId)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveFormulaGroupCommand(m_doc, groupId));
    else
        m_doc->removeFormulaGroup(groupId);
}

void FormulaTabModel::moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                                  int targetLocalIndex)
{
    const auto* f = m_doc->variablesView().formulaById(formulaId);
    if (!f)
        return;

    // Current local index within its own group.
    int curLocal = 0;
    for (const auto& other : m_doc->formulas()) {
        if (other.id == formulaId)
            break;
        if (other.groupId == f->groupId)
            ++curLocal;
    }

    if (f->groupId == targetGroupId) {
        // Drop slots are pre-removal; account for the card leaving its slot.
        if (targetLocalIndex > curLocal)
            --targetLocalIndex;
        if (targetLocalIndex == curLocal)
            return;  // No-op drop.
    }

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::MoveFormulaCommand(
            m_doc, formulaId, targetGroupId, targetLocalIndex));
    else
        m_doc->moveFormula(formulaId, targetGroupId, targetLocalIndex);
}

int FormulaTabModel::formulaCountIn(const QUuid& groupId) const
{
    int count = 0;
    for (const auto& f : m_doc->formulas())
        if (f.groupId == groupId)
            ++count;
    return count;
}

} // namespace cad::ui
