#include "SnapEngine.h"

#include <cmath>
#include <limits>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::tools {

std::optional<SnapResult> SnapEngine::findSnap(
    const cad::geo::Vec2& worldPos,
    const cad::param::ParamDocument* paramDoc,
    double zoom) const
{
    if (!paramDoc) return std::nullopt;

    // Convert screen-space radius to world-space
    double worldRadius = (zoom > 1e-9) ? (snapRadius / zoom) : snapRadius;
    double bestDistSq = worldRadius * worldRadius;
    std::optional<SnapResult> best;

    for (const auto& block : paramDoc->blocks()) {
        for (const auto& pt : block.points) {
            // Skip invisible or non-selectable points (e.g. anchors)
            if (!pt.visible || !pt.selectable || !pt.resolved) continue;

            cad::geo::Vec2 wp = block.transform.toWorld(pt.resolvedPos);
            double distSq = worldPos.distanceSquaredTo(wp);

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                best = SnapResult{
                    .worldPos  = wp,
                    .blockId   = block.id,
                    .pointId   = pt.id,
                    .pointName = pt.name
                };
            }
        }
    }

    return best;
}

} // namespace cad::tools
