#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// How a condition adjusts the formula result.
enum class AdjustMode {
    Flat    = 0,  ///< Add `amount` once when the value falls in range.
    PerStep = 1,  ///< Add `amount` for every `step` increment within range.
};

/// A conditional adjustment applied to a formula variable's result.
///
/// The watched variable (`watchVar`) MUST be referenced by the owning formula's
/// expression. All numeric fields are expressed in cm (the formula domain unit).
///
/// Examples:
///   Flat:    B in [84, 90] -> +0.5
///   PerStep: B in [84, 90], every 1 -> +0.1  (B=85 -> +0.1, B=86 -> +0.2 ...)
struct Condition {
    QUuid    id = QUuid::createUuid();
    QString  watchVar;          ///< Identifier (name or refName) used in the expression.
    bool     lowerOn = true;    ///< Whether the lower bound is active.
    double   lower   = 0.0;     ///< Lower bound (cm).
    bool     upperOn = true;    ///< Whether the upper bound is active.
    double   upper   = 0.0;     ///< Upper bound (cm).
    AdjustMode mode = AdjustMode::Flat;
    double   step   = 1.0;      ///< PerStep: size of one increment (cm).
    double   amount = 0.0;      ///< Adjustment per hit / per step (cm, signed).
};

} // namespace cad::param
