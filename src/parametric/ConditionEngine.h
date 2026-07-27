#pragma once

#include <QHash>
#include <QString>
#include <QList>

#include "parametric/Condition.h"
#include "parametric/ExpressionEvaluator.h"

namespace cad::param {

/// Condition-aware formula evaluation.
///
/// Semantics (no propagation): a formula's conditions take effect ONLY when the
/// formula is referenced *standalone* — i.e. the whole expression is exactly its
/// name. Inside a composite expression the formula contributes its base value
/// (conditions are NOT applied), which prevents cascading adjustment errors.
class ConditionEngine
{
public:
    /// Apply a list of conditions to a base value (everything in cm).
    /// @param baseCm      The un-adjusted result.
    /// @param conditions  Conditions to apply.
    /// @param baseValues  Current cm value of each watched variable/formula.
    [[nodiscard]] static double applyConditions(
        double baseCm,
        const QList<Condition>& conditions,
        const QHash<QString, double>& baseValues);

    /// Evaluate an expression with standalone-condition semantics.
    /// @param expr        Formula text.
    /// @param baseValues  name/refName/formulaName -> base value in cm
    ///                    (conditions NOT applied).
    /// @param condByName  formulaName -> its conditions (only formulas whose
    ///                    conditions are enabled and non-empty).
    [[nodiscard]] static ExpressionEvaluator::Result evaluate(
        const QString& expr,
        const QHash<QString, double>& baseValues,
        const QHash<QString, QList<Condition>>& condByName);
};

} // namespace cad::param
