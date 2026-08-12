#pragma once

#include <QObject>
#include <QUuid>
#include <QHash>
#include <vector>

#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Condition.h"

namespace cad::param {

class ParamDocument;

/// Variable / formula / formula-group sub-domain of the document.
/// Owns the three registries and the formula evaluation pipeline
/// (topological single-pass evaluation with a cached dependency order).
/// Published parameter values are pushed into ParamDocument through its
/// internal parameter hooks; this class knows nothing about geometry.
class VariableStore : public QObject
{
    Q_OBJECT

public:
    explicit VariableStore(ParamDocument* doc, QObject* parent = nullptr);

    // --- Variables (plain value variables) ---
    void addVariable(Variable var);
    void removeVariable(const QUuid& id);
    void updateVariable(const Variable& var);
    [[nodiscard]] const std::vector<Variable>& variables() const { return m_variables; }
    [[nodiscard]] Variable* findVariable(const QUuid& id);

    // --- Formula variables ---
    void addFormula(FormulaVariable formula);
    void removeFormula(const QUuid& id);
    void updateFormula(const FormulaVariable& formula);
    [[nodiscard]] const std::vector<FormulaVariable>& formulas() const { return m_formulas; }
    [[nodiscard]] FormulaVariable* findFormula(const QUuid& id);

    /// Re-evaluate all formulas against current variables, update cached values,
    /// and push the results into the document's parameter map + resolve.
    void recomputeFormulas();

    // --- Formula groups (panel folders for formula variables) ---
    void addFormulaGroup(FormulaGroup group);
    /// Dissolve a group: members become ungrouped, the group is removed.
    void removeFormulaGroup(const QUuid& groupId);
    void renameFormulaGroup(const QUuid& groupId, const QString& name);
    /// Collapse/expand toggle (view state, persisted; not undoable).
    void setFormulaGroupCollapsed(const QUuid& groupId, bool collapsed);
    /// Reorder groups within the registry.
    void moveFormulaGroup(int fromIndex, int toIndex);
    /// Move a formula to targetGroupId (may be null = ungrouped) at the given
    /// local index within that group's display sequence.
    void moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                     int targetLocalIndex);
    [[nodiscard]] const std::vector<FormulaGroup>& formulaGroups() const { return m_formulaGroups; }
    [[nodiscard]] FormulaGroup* findFormulaGroup(const QUuid& groupId);

    // --- Silent raw mutations (trusted callers only: deserializer / undo
    // replay). No signals, no recompute — the caller's pipeline owns the
    // eventual refresh (快照完整性 / 反序列化批量恢复).
    void addVariableRaw(Variable var);
    void addFormulaRaw(FormulaVariable formula);
    void addFormulaGroupRaw(FormulaGroup group);
    /// Re-insert a formula group at registry position @p index (undo replay).
    void insertFormulaGroupAt(int index, FormulaGroup group);

    /// Clear all registries (document reset). No signals emitted.
    void clear();

signals:
    void variablesChanged();
    void formulasChanged();
    void formulaGroupsChanged();

private:
    ParamDocument* m_doc = nullptr;

    std::vector<Variable>        m_variables;
    std::vector<FormulaVariable> m_formulas;
    std::vector<FormulaGroup>    m_formulaGroups;  ///< Panel folders for formulas.

    // ── Formula dependency graph cache (公式拓扑序) ──
    /// Topological evaluation order over m_formulas, rebuilt ONLY when the
    /// formula set/expressions change (variable edits reuse it — building
    /// the graph costs more than evaluating the formulas it orders).
    /// m_formulaAcyclic == false means a cycle was found: recomputeFormulas
    /// falls back to the legacy bounded fixpoint (bit-for-bit unchanged).
    mutable bool m_formulaDepsDirty = true;
    mutable std::vector<int> m_formulaOrder;
    mutable bool m_formulaAcyclic = true;

    /// Rebuild m_formulaOrder via Kahn's algorithm over formula-name
    /// references (case-insensitive, matching the evaluator's fallback).
    void rebuildFormulaOrder() const;
};

} // namespace cad::param
