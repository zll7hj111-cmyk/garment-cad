#pragma once

#include <QList>

#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"

namespace cad::demo {

/// Return the default set of demo variables (values in mm).
[[nodiscard]] QList<cad::param::Variable> defaultVariables();

/// Return the default set of demo formula variables.
[[nodiscard]] QList<cad::param::FormulaVariable> defaultFormulas();

} // namespace cad::demo
