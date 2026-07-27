#pragma once

#include <QUuid>

namespace cad::param {

/// Defines a snapping/attachment relationship between two Blocks.
/// The "from" Block's point is constrained to coincide with the "to" Block's point,
/// and the from Block's rotation is driven so that its attached segment makes the
/// construction angle (angleOffset) relative to the leader segment's direction.
struct Attachment {
    QUuid id = QUuid::createUuid();

    QUuid fromBlockId;  ///< The Block being attached (follower).
    QUuid fromPointId;  ///< Point on the from-Block that snaps.

    QUuid toBlockId;    ///< The target Block (leader).
    QUuid toPointId;    ///< Point on the to-Block to snap to.

    double angleOffset = 0.0;  ///< Construction angle in degrees, measured from the
                               ///< leader segment's direction (start->end).
                               ///< 0 = continue straight along the leader; CCW positive.
};

} // namespace cad::param
