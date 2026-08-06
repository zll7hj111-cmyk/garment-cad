#pragma once

#include <QUuid>
#include <QString>
#include <QList>
#include <optional>

#include "parametric/Condition.h"

namespace cad::param {

/// A computed variable defined by an arithmetic expression.
/// The expression may reference plain variables (and other formulas)
/// by display name ("胸围") or reference name ("b").
struct FormulaVariable {
    QUuid   id = QUuid::createUuid();
    QString name;        ///< Display name (e.g. "胸宽")
    QString expression;  ///< e.g. "胸围/2+6" or "b/2+6" (operands in cm)
    std::optional<double> actualValueCm; ///< User-provided actual value (cm).
                                         ///< When set, overrides the expression.
    double  value = 0;   ///< Cached FINAL result in mm (conditions applied, for display)
    double  baseValue = 0; ///< Base result in mm (conditions NOT applied, for propagation)
    QString comment;     ///< Description / annotation
    bool    valid = false;
    QString error;       ///< Last evaluation error, if any

    QList<Condition> conditions;      ///< Conditional adjustments (cm domain).
    bool conditionsEnabled = true;    ///< Master switch (card checkbox).

    QUuid groupId;  ///< Owning FormulaGroup id (isNull = ungrouped).
};

} // namespace cad::param
