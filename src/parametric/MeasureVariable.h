#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A measurement variable whose value is the distance between two arbitrary
/// points (possibly on different blocks). Unlike LinkedVariable (which tracks
/// a segment's own length), a MeasureVariable captures the spatial distance
/// between any two resolved points — refreshed on every resolve pass.
///
/// Typical use: bridge line creation measures |P1-P2| and publishes the result
/// as a formula-consumable parameter (cm domain).
struct MeasureVariable {
    QUuid   id = QUuid::createUuid();
    QString name;       ///< Display name (optional, no reference effect).
    QString refName;    ///< Reference name for formulas (e.g. "M_a3kx9").

    // --- Measurement source: two points (may be on different blocks) ---
    QUuid blockA;       ///< Block containing point A.
    QUuid pointA;       ///< First point.
    QUuid blockB;       ///< Block containing point B.
    QUuid pointB;       ///< Second point.

    /// The block (bridge / measure line) that owns this measurement, created
    /// together with it (e.g. SmartPen bridge). Null for standalone
    /// measurements made with the measure tool. When the owner block is
    /// deleted the measurement variable is deleted with it (测量线删除时同步
    /// 删除测量变量); clicking the card highlights the owner rather than the
    /// source base line.
    QUuid ownerBlockId;

    double  value = 0;  ///< Cached measurement in mm (refreshed each resolve).
    QString comment;    ///< Optional annotation.

    /// True when either source block/point no longer exists (dangling).
    /// The value freezes at the last successful measurement.
    bool dangling = false;
};

} // namespace cad::param
