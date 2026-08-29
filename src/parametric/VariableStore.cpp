#include "parametric/VariableStore.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ExpressionEvaluator.h"
#include "geometry/Units.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::param {

VariableStore::VariableStore(ParamDocument* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
{
}

// --- Variables ---

void VariableStore::addVariable(Variable var)
{
    m_variables.push_back(std::move(var));
    emit variablesChanged();
    recomputeFormulas();
}

void VariableStore::addVariableRaw(Variable var)
{
    m_variables.push_back(std::move(var));
}

void VariableStore::removeVariable(const QUuid& id)
{
    auto it = std::find_if(m_variables.begin(), m_variables.end(),
        [&id](const Variable& v) { return v.id == id; });
    if (it != m_variables.end()) {
        m_variables.erase(it);
        emit variablesChanged();
        recomputeFormulas();
    }
}

void VariableStore::updateVariable(const Variable& var)
{
    for (auto& v : m_variables) {
        if (v.id == var.id) {
            v = var;
            break;
        }
    }
    emit variablesChanged();
    recomputeFormulas();
}

Variable* VariableStore::findVariable(const QUuid& id)
{
    auto it = std::find_if(m_variables.begin(), m_variables.end(),
        [&id](const Variable& v) { return v.id == id; });
    return (it != m_variables.end()) ? &(*it) : nullptr;
}

const Variable* VariableStore::findVariable(const QUuid& id) const
{
    auto it = std::find_if(m_variables.begin(), m_variables.end(),
        [&id](const Variable& v) { return v.id == id; });
    return (it != m_variables.end()) ? &(*it) : nullptr;
}

// --- Formula variables ---

void VariableStore::addFormula(FormulaVariable formula)
{
    m_formulas.push_back(std::move(formula));
    m_formulaDepsDirty = true;  // new formula may be referenced by others
    emit formulasChanged();
    recomputeFormulas();
}

void VariableStore::addFormulaRaw(FormulaVariable formula)
{
    m_formulas.push_back(std::move(formula));
    m_formulaDepsDirty = true;
}

void VariableStore::removeFormula(const QUuid& id)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&id](const FormulaVariable& f) { return f.id == id; });
    if (it != m_formulas.end()) {
        m_formulas.erase(it);
        m_formulaDepsDirty = true;  // references to the removed name now dangle
        emit formulasChanged();
        recomputeFormulas();
    }
}

void VariableStore::updateFormula(const FormulaVariable& formula)
{
    for (auto& f : m_formulas) {
        if (f.id == formula.id) {
            f.name = formula.name;
            f.expression = formula.expression;
            f.actualValueCm = formula.actualValueCm;
            f.comment = formula.comment;
            f.conditions = formula.conditions;
            f.conditionsEnabled = formula.conditionsEnabled;
            m_formulaDepsDirty = true;  // name / expression may have changed
            break;
        }
    }
    emit formulasChanged();
    recomputeFormulas();
}

FormulaVariable* VariableStore::findFormula(const QUuid& id)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&id](const FormulaVariable& f) { return f.id == id; });
    return (it != m_formulas.end()) ? &(*it) : nullptr;
}

const FormulaVariable* VariableStore::findFormula(const QUuid& id) const
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&id](const FormulaVariable& f) { return f.id == id; });
    return (it != m_formulas.end()) ? &(*it) : nullptr;
}

// ============================================================
// Formula groups (panel folders)
// ============================================================

void VariableStore::addFormulaGroup(FormulaGroup group)
{
    m_formulaGroups.push_back(std::move(group));
    emit formulaGroupsChanged();
}

void VariableStore::addFormulaGroupRaw(FormulaGroup group)
{
    m_formulaGroups.push_back(std::move(group));
}

void VariableStore::insertFormulaGroupAt(int index, FormulaGroup group)
{
    const int n = static_cast<int>(m_formulaGroups.size());
    if (index < 0 || index > n)
        index = n;  // Clamp to append.
    m_formulaGroups.insert(m_formulaGroups.begin() + index, std::move(group));
    emit formulaGroupsChanged();
}

void VariableStore::removeFormulaGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_formulaGroups.begin(), m_formulaGroups.end(),
        [&groupId](const FormulaGroup& g) { return g.id == groupId; });
    if (it == m_formulaGroups.end())
        return;

    // Dissolve: members fall back to the ungrouped section.
    bool membersChanged = false;
    for (auto& f : m_formulas) {
        if (f.groupId == groupId) {
            f.groupId = QUuid();
            membersChanged = true;
        }
    }
    m_formulaGroups.erase(it);

    emit formulaGroupsChanged();
    if (membersChanged)
        emit formulasChanged();
}

void VariableStore::renameFormulaGroup(const QUuid& groupId, const QString& name)
{
    if (auto* g = findFormulaGroup(groupId); g && g->name != name) {
        g->name = name;
        emit formulaGroupsChanged();
    }
}

void VariableStore::setFormulaGroupCollapsed(const QUuid& groupId, bool collapsed)
{
    if (auto* g = findFormulaGroup(groupId); g && g->collapsed != collapsed) {
        g->collapsed = collapsed;
        emit formulaGroupsChanged();
    }
}

void VariableStore::moveFormulaGroup(int fromIndex, int toIndex)
{
    const int n = static_cast<int>(m_formulaGroups.size());
    if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n
        || fromIndex == toIndex)
        return;

    FormulaGroup g = std::move(m_formulaGroups[static_cast<size_t>(fromIndex)]);
    m_formulaGroups.erase(m_formulaGroups.begin() + fromIndex);
    m_formulaGroups.insert(m_formulaGroups.begin() + toIndex, std::move(g));
    emit formulaGroupsChanged();
}

void VariableStore::moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                                int targetLocalIndex)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&formulaId](const FormulaVariable& f) { return f.id == formulaId; });
    if (it == m_formulas.end())
        return;

    FormulaVariable moved = std::move(*it);
    m_formulas.erase(it);
    moved.groupId = targetGroupId;

    // Global insert position = slot of the targetLocalIndex-th member of the
    // target group (after removal). Only the relative order within a group
    // matters for display, so falling back to "after the last member" (or
    // vector end for an empty group) is sufficient.
    auto insertPos = m_formulas.size();
    bool placed = false;
    int local = 0;
    for (std::size_t i = 0; i < m_formulas.size(); ++i) {
        if (m_formulas[i].groupId == targetGroupId) {
            if (local == targetLocalIndex) { insertPos = i; placed = true; break; }
            ++local;
        }
    }
    if (!placed) {
        for (std::size_t i = m_formulas.size(); i > 0; --i) {
            if (m_formulas[i - 1].groupId == targetGroupId) { insertPos = i; break; }
        }
    }

    m_formulas.insert(m_formulas.begin() + static_cast<std::ptrdiff_t>(insertPos),
                      std::move(moved));
    emit formulasChanged();
}

FormulaGroup* VariableStore::findFormulaGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_formulaGroups.begin(), m_formulaGroups.end(),
        [&groupId](const FormulaGroup& g) { return g.id == groupId; });
    return (it != m_formulaGroups.end()) ? &(*it) : nullptr;
}

const FormulaGroup* VariableStore::findFormulaGroup(const QUuid& groupId) const
{
    auto it = std::find_if(m_formulaGroups.begin(), m_formulaGroups.end(),
        [&groupId](const FormulaGroup& g) { return g.id == groupId; });
    return (it != m_formulaGroups.end()) ? &(*it) : nullptr;
}

void VariableStore::clear()
{
    m_variables.clear();
    m_formulas.clear();
    m_formulaGroups.clear();
    m_formulaDepsDirty = true;
    m_formulaOrder.clear();
    m_formulaAcyclic = true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Formula dependency graph (拓扑序缓存)
// ═══════════════════════════════════════════════════════════════════════════════

void VariableStore::rebuildFormulaOrder() const
{
    m_formulaDepsDirty = false;
    const int n = static_cast<int>(m_formulas.size());
    m_formulaOrder.clear();
    m_formulaOrder.reserve(static_cast<size_t>(n));

    // Dependencies = identifiers matched case-insensitively against formula
    // names (same folding the evaluator's PushVar fallback uses, so the
    // graph and the evaluator agree on what resolves).
    QHash<QString, int> nameIndex;  // case-folded formula name -> index
    for (int i = 0; i < n; ++i)
        if (!m_formulas[static_cast<size_t>(i)].name.isEmpty())
            nameIndex.insert(m_formulas[static_cast<size_t>(i)].name.toLower(), i);

    std::vector<std::vector<int>> dependents(static_cast<size_t>(n));
    std::vector<int> inDegree(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const auto& f = m_formulas[static_cast<size_t>(i)];
        if (f.actualValueCm.has_value() || f.expression.isEmpty()) continue;
        const QStringList refs = ExpressionEvaluator::referencedNames(f.expression);
        QSet<QString> seen;
        for (const QString& ref : std::as_const(refs)) {
            const QString lower = ref.toLower();
            if (seen.contains(lower)) continue;
            seen.insert(lower);
            const auto it = nameIndex.constFind(lower);
            if (it == nameIndex.constEnd()) continue;  // variable / typo
            dependents[static_cast<size_t>(it.value())].push_back(i);
            ++inDegree[static_cast<size_t>(i)];
        }
    }

    // Kahn's algorithm; document order preserved among ready formulas.
    std::vector<int> queue;
    queue.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        if (inDegree[static_cast<size_t>(i)] == 0) queue.push_back(i);
    for (size_t head = 0; head < queue.size(); ++head) {
        const int cur = queue[head];
        m_formulaOrder.push_back(cur);
        for (const int d : dependents[static_cast<size_t>(cur)])
            if (--inDegree[static_cast<size_t>(d)] == 0) queue.push_back(d);
    }
    m_formulaAcyclic = static_cast<int>(m_formulaOrder.size()) == n;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Formulas
// ═══════════════════════════════════════════════════════════════════════════════

void VariableStore::recomputeFormulas()
{
    // Sync plain variable values into the parameter map (cm).
    QHash<QString, double> varCm;
    for (const auto& v : m_variables) {
        const double cm = geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            varCm.insert(v.name, cm);
        if (!v.refName.isEmpty())
            varCm.insert(v.refName, cm);
    }
    cad::param::RawModelAccess::publishParamsRaw(*m_doc, varCm);

    // Base value map (cm): variables under display name + reference name.
    QHash<QString, double> baseMap;
    for (const auto& v : m_variables) {
        const double cm = geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            baseMap.insert(v.name, cm);
        if (!v.refName.isEmpty())
            baseMap.insert(v.refName, cm);
    }

    // Condition table: formulaName -> conditions (enabled & non-empty only).
    QHash<QString, QList<Condition>> condByName;
    for (const auto& f : m_formulas) {
        if (f.conditionsEnabled && !f.conditions.isEmpty() && !f.name.isEmpty())
            condByName.insert(f.name, f.conditions);
    }

    // Topological single-pass evaluation (optimisation): formulas may
    // reference other formulas by name. The dependency order is CACHED and
    // rebuilt only when the formula set changes — variable edits reuse it.
    // For the (overwhelmingly common) acyclic case, evaluating in dependency
    // order makes every formula converge in ONE pass — the legacy bounded
    // fixpoint re-evaluated ALL formulas per pass, costing O(depth x count)
    // evaluations on deep reference chains (each early pass mostly failing
    // with "unknown variable" until its dependencies are ready). Cycles (an
    // authoring error) fall back to the original fixpoint, bit-for-bit
    // unchanged.
    if (m_formulaDepsDirty)
        rebuildFormulaOrder();

    if (m_formulaAcyclic) {
        // Acyclic: every formula evaluates exactly once, dependencies first.
        for (const int i : m_formulaOrder) {
            auto& f = m_formulas[static_cast<size_t>(i)];
            if (f.actualValueCm.has_value()) {
                // User-provided actual value overrides the expression.
                f.valid = true;
                f.error.clear();
                f.baseValue = geo::Units::cmToMm(*f.actualValueCm);
                if (!f.name.isEmpty())
                    baseMap.insert(f.name, *f.actualValueCm);
                continue;
            }
            const auto r = ConditionEngine::evaluate(
                f.expression, baseMap, condByName);
            if (r.ok) {
                f.valid = true;
                f.error.clear();
                f.baseValue = geo::Units::cmToMm(r.value);
                if (!f.name.isEmpty())
                    baseMap.insert(f.name, r.value);
            } else {
                f.valid = false;
                f.error = r.error;
            }
        }
    } else {
        // Cycle detected (a formula references itself or a cycle): fall back
        // to the legacy bounded fixpoint so values stay bit-for-bit identical.
        const int passes = qMax(1, static_cast<int>(m_formulas.size()));
        for (int pass = 0; pass < passes; ++pass) {
            bool progressed = false;
            for (auto& f : m_formulas) {
                // User-provided actual value overrides the expression entirely.
                if (f.actualValueCm.has_value()) {
                    f.valid = true;
                    f.error.clear();
                    f.baseValue = geo::Units::cmToMm(*f.actualValueCm);
                    if (!f.name.isEmpty()) {
                        auto it = baseMap.find(f.name);
                        if (it == baseMap.end()) {
                            baseMap.insert(f.name, *f.actualValueCm);
                            progressed = true;
                        } else if (qAbs(it.value() - *f.actualValueCm) > 1e-9) {
                            it.value() = *f.actualValueCm;
                            progressed = true;
                        }
                    }
                    continue;
                }
                const auto r = ConditionEngine::evaluate(
                    f.expression, baseMap, condByName);
                if (r.ok) {
                    f.valid = true;
                    f.error.clear();
                    f.baseValue = geo::Units::cmToMm(r.value);
                    if (!f.name.isEmpty()) {
                        auto it = baseMap.find(f.name);
                        if (it == baseMap.end()) {
                            baseMap.insert(f.name, r.value);
                            progressed = true;
                        } else if (qAbs(it.value() - r.value) > 1e-9) {
                            it.value() = r.value;
                            progressed = true;
                        }
                    }
                } else {
                    f.valid = false;
                    f.error = r.error;
                }
            }
            if (!progressed)
                break;
        }
    }

    // Final adjusted values (conditions applied) for display + standalone use.
    for (auto& f : m_formulas) {
        if (!f.valid) continue;
        // Actual value is a direct override: no condition adjustment.
        if (f.actualValueCm.has_value()) {
            f.value = f.baseValue;
            continue;
        }
        const double baseCm = geo::Units::mmToCm(f.baseValue);
        const double adjCm = condByName.contains(f.name)
            ? ConditionEngine::applyConditions(baseCm, f.conditions, baseMap)
            : baseCm;
        f.value = geo::Units::cmToMm(adjCm);
    }

    // Sync formula BASE values (conditions NOT applied) into parameters.
    QHash<QString, double> baseCm;
    for (const auto& f : m_formulas) {
        if (f.valid && !f.name.isEmpty())
            baseCm.insert(f.name, geo::Units::mmToCm(f.baseValue));
    }
    m_doc->syncFormulaConditions(condByName);
    m_doc->syncFormulaParameters(baseCm);  // triggers resolveAll()

    emit formulasChanged();
}

} // namespace cad::param
