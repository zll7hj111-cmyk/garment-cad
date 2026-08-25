#include "ParamDocument.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QDebug>

#include "parametric/Resolver.h"
#include "parametric/Serial.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "parametric/PerfProbe.h"
#include "parametric/IntersectDebug.h"
#include "parametric/LayerRegistry.h"
#include "parametric/VariableStore.h"
#include "parametric/MeasurementStore.h"

namespace cad::param {

// 参数发布与公式同步 (2026-08 拆分)

void ParamDocument::setParameter(const QString& name, double value)
{
    m_parameters[name] = value;
    resolveAll();
}

void ParamDocument::setParameters(const QHash<QString, double>& nameValues)
{
    for (auto it = nameValues.cbegin(); it != nameValues.cend(); ++it)
        m_parameters[it.key()] = it.value();
    resolveAll();
}

void ParamDocument::removeParameter(const QString& name)
{
    if (m_parameters.remove(name))
        resolveAll();
}

void ParamDocument::syncFormulaParameters(const QHash<QString, double>& cmValues)
{
    // Remove stale formula-derived parameters from the previous sync.
    for (const QString& old : std::as_const(m_formulaParamNames)) {
        if (!cmValues.contains(old))
            m_parameters.remove(old);
    }
    m_formulaParamNames = QSet<QString>(cmValues.keyBegin(), cmValues.keyEnd());

    // Insert / update current formula parameters (cm).
    for (auto it = cmValues.cbegin(); it != cmValues.cend(); ++it)
        m_parameters[it.key()] = it.value();

    resolveAll();
}

void ParamDocument::syncFormulaConditions(const QHash<QString, QList<Condition>>& conditioned)
{
    m_conditioned = conditioned;
}

double ParamDocument::parameter(const QString& name, double defaultVal) const
{
    return m_parameters.value(name, defaultVal);
}

// --- Internal sub-domain hooks ---

void ParamDocument::publishParameter(const QString& name, double cmValue)
{
    m_parameters[name] = cmValue;
}

void ParamDocument::removeParameterEntry(const QString& name)
{
    m_parameters.remove(name);
}

void ParamDocument::publishParamsRaw(const QHash<QString, double>& cmValues)
{
    for (auto it = cmValues.cbegin(); it != cmValues.cend(); ++it)
        m_parameters[it.key()] = it.value();
}

} // namespace cad::param