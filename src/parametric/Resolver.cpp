#include "Resolver.h"

#include <algorithm>
#include <cmath>

#include <QHash>

#include "Block.h"
#include "Attachment.h"

namespace cad::param {

void Resolver::resolveAll(std::vector<Block>& blocks,
                          const std::vector<Attachment>& attachments,
                          const QHash<QString, double>& params,
                          const QHash<QString, QList<Condition>>& conditioned)
{
    // Step 1: Resolve each block's internal points independently.
    for (auto& block : blocks) {
        block.resolve(params, conditioned);
    }

    // Step 2: Build O(1) index for attachment resolution.
    QHash<QUuid, int> blockIndex;
    blockIndex.reserve(static_cast<int>(blocks.size()));
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        blockIndex.insert(blocks[i].id, i);

    // Step 3: Process attachments iteratively until stable.
    bool progress = true;
    int maxIter = static_cast<int>(attachments.size()) + 1;
    while (progress && maxIter-- > 0) {
        progress = false;
        for (const auto& att : attachments) {
            auto fromIt = blockIndex.find(att.fromBlockId);
            auto toIt   = blockIndex.find(att.toBlockId);
            if (fromIt == blockIndex.end() || toIt == blockIndex.end()) continue;

            applyAttachment(blocks[fromIt.value()], att, blocks[toIt.value()]);
            progress = true;
        }
    }
}

void Resolver::applyAttachment(Block& from, const Attachment& att,
                               const Block& to)
{
    // Get the target point's world position on the "to" block.
    geo::Vec2 targetWorldPos = to.worldPos(att.toPointId);

    // Reference direction: the leader segment's world direction (start->end).
    // World direction = leader block rotation + local segment direction.
    double refWorld = to.transform.rotation + to.directionAtPoint(att.toPointId);

    // Local direction of the follower's attached segment (the one anchored at
    // fromPointId). The follower's world segment direction equals
    // from.transform.rotation + localDir, so to achieve the desired world
    // direction (refWorld + angleOffset) we set:
    //     rotation = refWorld + angleOffset - localDir
    // This keeps angleOffset == 0 meaning "continue straight along the leader".
    double localDir = from.directionAtPoint(att.fromPointId);
    double newRotation = refWorld + att.angleOffset * M_PI / 180.0 - localDir;
    from.transform.rotation = newRotation;

    // Now position the from-block so that its from-point lands on targetWorldPos.
    // from-point in local coords:
    const ParamPoint* fromPt = from.findPoint(att.fromPointId);
    if (!fromPt || !fromPt->resolved) return;

    geo::Vec2 localOffset = fromPt->resolvedPos;

    // Rotate localOffset by the new rotation
    double c = std::cos(newRotation);
    double s = std::sin(newRotation);
    geo::Vec2 rotatedOffset{
        localOffset.x * c - localOffset.y * s,
        localOffset.x * s + localOffset.y * c
    };

    // origin = targetWorldPos - rotatedOffset
    from.transform.origin = targetWorldPos - rotatedOffset;
}

} // namespace cad::param
