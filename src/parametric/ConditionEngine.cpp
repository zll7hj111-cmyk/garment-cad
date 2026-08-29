#include "ConditionEngine.h"

#include <cmath>

#include "geometry/Units.h"

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
    const QHash<QString, QList<Condition>>& condByName,
    EvalContext* ctx)
{
    // Per-pass memo: within one resolve pass the variable map is constant,
    // so identical expression texts always yield identical results. This
    // deduplicates the many points/attachments sharing the same formula.
    if (ctx) {
        auto mit = ctx->memo.constFind(expr);
        if (mit != ctx->memo.constEnd())
            return mit.value();

        // Lazy case-folding index (lower -> exact key) over the pass's
        // variable map: exact-name misses then resolve in O(1) instead of a
        // linear scan. Built once; a lookup miss falls back to the scan, so
        // keys added mid-pass (e.g. by measurements) stay resolvable.
        if (!ctx->normBuilt) {
            ctx->normBuilt = true;
            for (auto it = baseValues.cbegin(); it != baseValues.cend(); ++it) {
                const QString lower = it.key().toLower();
                if (!ctx->normLookup.contains(lower))
                    ctx->normLookup.insert(lower, it.key());
            }
        }
    }

    ExpressionEvaluator::Result r;

    // Standalone reference to a conditioned formula -> use its adjusted value.
    // Bytecode comes from the pass's own compile cache (document-owned) when
    // one was threaded through EvalContext, else the thread-local fallback.
    const auto& ce = cacheFor(ctx).compiled(expr);
    if (ce.ok && ce.isStandalone) {
        const auto cit = condByName.constFind(ce.standaloneName);
        if (cit != condByName.constEnd()) {
            const auto bit = baseValues.constFind(ce.standaloneName);
            const double base = (bit != baseValues.constEnd()) ? bit.value() : 0.0;
            r.ok = true;
            r.value = applyConditions(base, cit.value(), baseValues);
        }
    }

    // Composite (or unconditioned) expression -> plain evaluation against base
    // values, so conditions never propagate into a larger expression.
    if (!r.ok)
        r = ExpressionEvaluator::execute(ce, baseValues,
                                         ctx ? &ctx->normLookup : nullptr);

    if (ctx)
        ctx->memo.insert(expr, r);
    return r;
}

bool ConditionEngine::evaluateLengthMm(
    const QString& formulaCm,
    const QHash<QString, double>& baseValues,
    const QHash<QString, QList<Condition>>& condByName,
    double& outMm,
    EvalContext* ctx)
{
    if (formulaCm.isEmpty()) return false;
    const auto r = evaluate(formulaCm, baseValues, condByName, ctx);
    if (!r.ok) return false;
    outMm = geo::Units::cmToMm(r.value);
    return true;
}

} // namespace cad::param
