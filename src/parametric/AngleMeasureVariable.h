#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A measurement variable whose value is the relative (directed) angle between
/// two segments — possibly on different blocks. Mirrors MeasureVariable (which
/// captures a two-point distance), but for angles.
///
/// Angle semantics follow the follower angle (跟随角度): the directed angle
/// from segment A's world direction (start→end) to segment B's world direction,
/// normalized to (-180, 180]. Refreshed on every resolve pass and published to
/// the parameter table in the degree domain.
///
/// Typical use: an angle-measure tool picks two lines and publishes their
/// relative angle as a formula-consumable parameter.
struct AngleMeasureVariable {
    QUuid   id = QUuid::createUuid();
    QString name;       ///< Display name (optional, no reference effect).
    QString refName;    ///< Reference name for formulas (e.g. "MA_a3kx9").

    // --- Measurement source: two segments (may be on different blocks) ---
    QUuid blockA;       ///< Block containing segment A (reference line).
    QUuid segmentA;     ///< Reference segment (its start→end is the base direction).
    QUuid blockB;       ///< Block containing segment B (target line).
    QUuid segmentB;     ///< Target segment.

    double  value = 0;  ///< Cached angle in degrees, (-180, 180] (refreshed each resolve).
    QString comment;    ///< Optional annotation.

    /// True when either source block/segment no longer exists (dangling).
    /// The value freezes at the last successful measurement.
    bool dangling = false;
};

} // namespace cad::param
