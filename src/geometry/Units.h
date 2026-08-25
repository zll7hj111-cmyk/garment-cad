#pragma once

#include <QString>
#include <QPointF>

#include "Vec2.h"

namespace cad::geo {

/// Coordinate-system boundary helpers.
///
/// User coordinates  : +Y up   (engineering / DXF convention, stored in entities)
/// Scene coordinates : +Y down (Qt QGraphicsScene native)
///
/// Conversion is a pure Y-negation; X is unchanged.
/// Apply these ONLY at the boundary between the document/tool layer and the
/// Qt graphics layer — never inside either layer alone.
struct Coord {
    static constexpr double toSceneY(double userY) { return -userY; }
    static constexpr double toUserY(double sceneY) { return -sceneY; }

    static QPointF toScene(const Vec2& p) { return {p.x, -p.y}; }
    static QPointF toScene(double ux, double uy) { return {ux, -uy}; }

    static Vec2 toUser(const QPointF& p) { return {p.x(), -p.y()}; }
    static Vec2 toUser(double sx, double sy) { return {sx, -sy}; }
};

/// Unit conversion utilities.
/// Internal unit: millimeter (mm).
/// Display unit: centimeter (cm) by default.
struct Units {
    // --- Conversion factors (relative to mm) ---
    static constexpr double MM_PER_CM   = 10.0;
    static constexpr double MM_PER_INCH = 25.4;

    // --- mm <-> cm ---
    static constexpr double mmToCm(double mm) { return mm / MM_PER_CM; }
    static constexpr double cmToMm(double cm) { return cm * MM_PER_CM; }

    // --- mm <-> inch ---
    static constexpr double mmToInch(double mm) { return mm / MM_PER_INCH; }
    static constexpr double inchToMm(double inch) { return inch * MM_PER_INCH; }

    // --- Formatted display strings ---
    /// Format a mm value as cm string with given precision (default 2 decimal places = 0.01cm = 0.1mm).
    static QString formatLength(double mm, int precision = 2) {
        return QString::number(mmToCm(mm), 'f', precision) + QStringLiteral(" cm");
    }

    /// Format a coordinate pair (mm) as cm string.
    static QString formatPoint(double xMm, double yMm, int precision = 2) {
        return QStringLiteral("X: %1  Y: %2 cm")
            .arg(mmToCm(xMm), 0, 'f', precision)
            .arg(mmToCm(yMm), 0, 'f', precision);
    }

    /// Format an angle in degrees.
    static QString formatAngle(double degrees, int precision = 1) {
        return QString::number(degrees, 'f', precision) + QChar(0x00B0); // ° symbol
    }

    // --- Trimmed display strings (card value labels) ---

    /// Trim trailing zeros (and a trailing decimal point) from a formatted
    /// number string, e.g. "12.50" -> "12.5", "12.00" -> "12".
    static QString trimTrailingZeros(QString s) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
        return s;
    }

    /// Format a mm value as a trimmed cm string WITHOUT a unit suffix
    /// (e.g. "12.5"). Shared by the variable/measure card value labels.
    static QString formatCmTrimmed(double mm) {
        return trimTrailingZeros(QString::number(mmToCm(mm), 'f', 2));
    }

    /// Format a plain number with 2 decimals, trailing zeros trimmed
    /// (e.g. "12.5"). Shared by the formula card result label.
    static QString formatNumberTrimmed(double v) {
        return trimTrailingZeros(QString::number(v, 'f', 2));
    }

    /// Format a degree value with trailing zeros trimmed and a degree suffix
    /// (e.g. "12.5°"). Shared by the angle-measure card value label.
    static QString formatDegTrimmed(double deg) {
        return trimTrailingZeros(QString::number(deg, 'f', 1)) + QChar(0x00B0);
    }

    /// Format a degree value with 1 decimal, trailing ".0" trimmed, WITHOUT a
    /// degree suffix (e.g. "22" / "22.5"). Shared by the rotate HUD and the
    /// segment property dialogs (formerly local formatDeg/formatAngleDeg).
    static QString formatDegValue(double deg) {
        return trimTrailingZeros(QString::number(deg, 'f', 1));
    }
};

} // namespace cad::geo
