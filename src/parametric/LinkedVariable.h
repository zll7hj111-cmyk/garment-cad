#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

class Block;
struct Segment;

/// A linked variable whose value is automatically derived from a geometric
/// measurement (currently: the resolved length of a segment). Unlike plain
/// Variables (user-edited) or FormulaVariables (expression-computed), a
/// LinkedVariable is read-only — its value tracks the source geometry.
///
/// Once published, its refName enters the parameter map (cm domain) and can
/// be referenced by any lengthFormula / followerAngleFormula in the document.
struct LinkedVariable {
    QUuid   id = QUuid::createUuid();
    QString name;       ///< Display name (e.g. "肩线长")
    QString refName;    ///< Reference name for formulas (e.g. "Lk9x2bL1")

    // --- Measurement source ---
    QUuid sourceBlockId;     ///< Block containing the measured segment.
    QUuid sourceSegmentId;   ///< Segment whose resolved length is measured.

    double  value = 0;  ///< Cached measurement in mm (refreshed each resolve).
    QString comment;    ///< Optional annotation.

    /// True when the source block/segment no longer exists (dangling).
    /// The value freezes at the last successful measurement.
    bool dangling = false;

    /// Factory: build a LinkedVariable from a block + segment, measuring the
    /// current resolved length. Naming convention: refName = "L" + serial,
    /// name = (segName or serial) + "长".
    static LinkedVariable fromSegment(const Block& blk, const Segment& seg);
};

} // namespace cad::param
