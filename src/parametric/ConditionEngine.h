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
    /// @param ctx         Optional per-pass memo: when the variable map is
    ///                    constant (e.g. inside one Resolver pass), identical
    ///                    expression texts execute only once per pass.
    [[nodiscard]] static ExpressionEvaluator::Result evaluate(
        const QString& expr,
        const QHash<QString, double>& baseValues,
        const QHash<QString, QList<Condition>>& condByName,
        EvalContext* ctx = nullptr);

    /// Evaluate a length formula (cm domain) and convert the result to mm.
    /// Equivalent to the common idiom
    ///   `if (!f.isEmpty()) { auto r = evaluate(f, ...); if (r.ok) x = Units::cmToMm(r.value); }`
    /// in one call — 2026-08-28 收口 A2.
    /// @param formulaCm    Empty string → returns false, @p outMm untouched.
    /// @param baseValues   Same as evaluate().
    /// @param condByName   Same as evaluate().
    /// @param outMm        Written ONLY on success (mm value).
    /// @param ctx          Optional per-pass memo (same as evaluate()).
    /// @return true when @p formulaCm is non-empty and evaluates OK.
    [[nodiscard]] static bool evaluateLengthMm(
        const QString& formulaCm,
        const QHash<QString, double>& baseValues,
        const QHash<QString, QList<Condition>>& condByName,
        double& outMm,
        EvalContext* ctx = nullptr);
};

} // namespace cad::param
