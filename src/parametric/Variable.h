#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A named parameter variable used in formulas and constraints.
struct Variable {
    QUuid   id    = QUuid::createUuid();
    QString name;       ///< Display name (e.g. "胸围")
    QString refName;    ///< Reference name for formulas (e.g. "b")
    double  value = 0;  ///< Numeric value in mm
    QString comment;    ///< Description / annotation
};

} // namespace cad::param
