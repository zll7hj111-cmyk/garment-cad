#include "ConditionEngine.h"

#include <cmath>

namespace cad::param {

double ConditionEngine::applyConditions(double baseCm,
                                        const QList<Condition>& conditions,
                                        const QHash<QString, double>& baseValues)
{
    double adjust = 0.0;
    for (const auto& c : conditions) {
        const auto it = baseValues.constFind(c.watchVar);
        if (it == baseValues.constEnd())
            continue;  // watched variable not resolvable -> skip this condition

        const double v = it.value();
        if (c.lowerOn && v < c.lower)
            continue;
        if (c.upperOn && v > c.upper)
            continue;

        if (c.mode == AdjustMode::Flat) {
            adjust += c.amount;
        } else {
            const double base = c.lowerOn ? c.lower : 0.0;
            if (c.step > 1e-12)
                adjust += std::floor((v - base) / c.step) * c.amount;
        }
    }
    return baseCm + adjust;
}

ExpressionEvaluator::Result ConditionEngine::evaluate(
    const QString& expr,
    const QHash<QString, double>& baseValues,
    const QHash<QString, QList<Condition>>& condByName)
{
    // Standalone reference to a conditioned formula -> use its adjusted value.
    const QString stand = ExpressionEvaluator::standaloneIdentifier(expr);
    if (!stand.isEmpty()) {
        const auto cit = condByName.constFind(stand);
        if (cit != condByName.constEnd()) {
            const auto bit = baseValues.constFind(stand);
            const double base = (bit != baseValues.constEnd()) ? bit.value() : 0.0;
            ExpressionEvaluator::Result r;
            r.ok = true;
            r.value = applyConditions(base, cit.value(), baseValues);
            return r;
        }
    }

    // Composite (or unconditioned) expression -> plain evaluation against base
    // values, so conditions never propagate into a larger expression.
    return ExpressionEvaluator::evaluate(expr, baseValues);
}

} // namespace cad::param
