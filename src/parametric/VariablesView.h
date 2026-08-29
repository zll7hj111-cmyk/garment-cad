#pragma once

/// Narrow domain view over the ParamDocument variable domain (B3,
/// 门面按域分组 — see BlockView.h for the pattern established in B1).
///
/// Covers the three variable registries: plain value variables (变量),
/// formula variables (公式变量) and formula groups (公式组, panel folders).
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. add/remove/update, recomputeFormulas() and the group
///     reorder/move operations stay on the facade (they emit
///     variablesChanged() / formulasChanged() / formulaGroupsChanged() and
///     resolve).
///   * The mutable find* overloads stay facade-only — they are the
///     authorized in-place edit channels (commands / cards) and must stay
///     greppable as edits. The view's byId*() wrap the const overloads
///     added alongside this view (B3).
///   * Stateless — holds a `const ParamDocument*`, always reflects the live
///     document.
///
/// Usage:  for (const auto& f : doc.variablesView().formulas()) { ... }
///         if (const auto* v = doc.variablesView().byId(id)) { ... }

#include <QUuid>
#include <vector>

#include "parametric/FormulaGroup.h"
#include "parametric/FormulaVariable.h"
#include "parametric/ParamDocument.h"
#include "parametric/Variable.h"

namespace cad::param {

class VariablesView
{
public:
    explicit VariablesView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// All plain value variables in registry order.
    [[nodiscard]] const std::vector<Variable>& all() const { return m_doc->variables(); }

    /// Lookup a value variable by id; nullptr when absent (read-only overload).
    [[nodiscard]] const Variable* byId(const QUuid& id) const { return m_doc->findVariable(id); }

    /// All formula variables in registry order.
    [[nodiscard]] const std::vector<FormulaVariable>& formulas() const { return m_doc->formulas(); }

    /// Lookup a formula variable by id; nullptr when absent (read-only overload).
    [[nodiscard]] const FormulaVariable* formulaById(const QUuid& id) const
    { return m_doc->findFormula(id); }

    /// All formula groups (panel folders) in display order.
    [[nodiscard]] const std::vector<FormulaGroup>& groups() const { return m_doc->formulaGroups(); }

    /// Lookup a formula group by id; nullptr when absent (read-only overload).
    [[nodiscard]] const FormulaGroup* groupById(const QUuid& groupId) const
    { return m_doc->findFormulaGroup(groupId); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline VariablesView ParamDocument::variablesView() const noexcept
{
    return VariablesView(*this);
}

} // namespace cad::param
