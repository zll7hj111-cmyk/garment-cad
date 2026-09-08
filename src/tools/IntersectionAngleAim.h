#pragma once

#include "geometry/Vec2.h"
#include "geometry/Angle.h"
#include <optional>
#include <cmath>

namespace cad::tools {

struct AimAngleResult {
    double displayDeg = 0.0;
    double storageDeg = 0.0;
};

inline AimAngleResult computeAimAngles(
    const cad::geo::Vec2& originPos,
    const cad::geo::Vec2& cursorPos,
    const std::optional<cad::geo::Vec2>& aimPos,
    double segAngleRad,
    bool worldAngleMode,
    bool angleSnap)
{
    // Aim direction (world, degrees): borrowed point or free cursor aiming.
    double worldDeg = 0.0;
    if (aimPos) {
        cad::geo::Vec2 toAim = *aimPos - originPos;
        worldDeg = cad::geo::radToDeg(std::atan2(toAim.y, toAim.x));
    } else {
        cad::geo::Vec2 toCursor = cursorPos - originPos;
        worldDeg = cad::geo::radToDeg(std::atan2(toCursor.y, toCursor.x));
    }
    double segAngleDeg = cad::geo::radToDeg(segAngleRad);

    // Display angle depends on the aiming mode; Shift snaps in that mode
    // (never while borrowing a point — the direction must stay exact).
    double displayDeg = 0.0;
    if (worldAngleMode) {
        displayDeg = worldDeg;
        if (angleSnap && !aimPos) displayDeg = std::round(displayDeg / 45.0) * 45.0;
    } else {
        displayDeg = worldDeg - segAngleDeg;
        if (angleSnap && !aimPos) displayDeg = std::round(displayDeg / 45.0) * 45.0;
    }
    displayDeg = cad::geo::normalizeDeg360(displayDeg);

    // Storage is ALWAYS the segment-relative angle (back-calculate from world).
    double relDeg = worldAngleMode ? (displayDeg - segAngleDeg) : displayDeg;
    relDeg = cad::geo::normalizeDeg360(relDeg);

    return {displayDeg, relDeg};
}

} // namespace cad::tools
