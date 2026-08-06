#pragma once

#include "geometry/Angle.h"

namespace cad::param {

/// Back-solve the follower angle (followerAngle, degrees) that preserves the
/// follower's current world direction when attaching to a leader.
///
/// The Resolver drives:  rotation = refWorld + angle·π/180 − localDir
/// so:                   angle = (rotation + localDir − refWorld)·180/π
///
/// @param followerRotRad  Follower block's transform.rotation (radians).
/// @param localDirRad     Follower's exit direction at the attach point (radians,
///                        from Block::directionAtPoint).
/// @param refWorldRad     Leader's world reference direction (radians), i.e.
///                        leader.transform.rotation + leader.exitDirectionAtPoint(...).
/// @return Follower angle in degrees, normalized to (-180, 180].
inline double backSolveFollowerAngle(double followerRotRad,
                                         double localDirRad,
                                         double refWorldRad)
{
    return cad::geo::normalizeDeg180(
        cad::geo::radToDeg(followerRotRad + localDirRad - refWorldRad));
}

} // namespace cad::param
