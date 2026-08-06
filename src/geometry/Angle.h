#pragma once

#include <cmath>
#include <numbers>

namespace cad::geo {

inline constexpr double kPi = std::numbers::pi;

// ─── Degree / Radian conversion ───────────────────────────────────────────────

/// Convert degrees to radians.
constexpr double degToRad(double deg) { return deg * kPi / 180.0; }

/// Convert radians to degrees.
constexpr double radToDeg(double rad) { return rad * 180.0 / kPi; }

// ─── Angle normalization ──────────────────────────────────────────────────────

/// Normalize an angle in degrees to the range (-180, 180].
/// Correctly maps +180 → +180 (not -180).
inline double normalizeDeg180(double deg)
{
    while (deg >  180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}

/// Normalize an angle in radians to the range (-π, π].
inline double normalizeRad(double rad)
{
    while (rad >  kPi) rad -= 2.0 * kPi;
    while (rad <= -kPi) rad += 2.0 * kPi;
    return rad;
}

} // namespace cad::geo
