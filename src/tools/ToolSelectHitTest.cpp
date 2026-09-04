#include "ToolSelect.h"

#include <vector>
#include <utility>

#include "canvas/CanvasScene.h"
#include "HitTester.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "CurveAnchorDragSession.h"
#include "ConnectGesture.h"

namespace cad::tools {
// ═══════════════════════════════════════════════════════════════════════════════
// Hit testing / edit target
// ═══════════════════════════════════════════════════════════════════════════════

QUuid ToolSelect::hitBlock(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene || !m_paramDoc) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().blockId;
}

QUuid ToolSelect::hitSegmentAt(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene || !m_paramDoc) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().segmentId;
}

void ToolSelect::notifyEditTarget()
{
    if (!m_host) return;
    QUuid blockId, segId;
    if (m_selection.size() == 1) {
        blockId = *m_selection.begin();
        segId = m_lastHitSegmentId;
        if (segId.isNull() && m_paramDoc) {
            if (const auto* blk = m_paramDoc->blocksView().byId(blockId); blk && !blk->segments.empty())
                segId = blk->segments.front().id;
        }
    }
    reportPinnedTarget(blockId, segId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Point-level press dispatch (curve anchor / connect gesture)
// ═══════════════════════════════════════════════════════════════════════════════

bool ToolSelect::tryPointOperation(const cad::geo::Vec2& pos)
{
    if (m_selection.isEmpty()) return false;

    // Curve anchor first: press near a pass point of any SELECTED curve.
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    const auto anchorHit = m_anchorDrag->hitAt(pos, zoom, m_selection);
    if (anchorHit) {
        m_anchorDrag->begin(anchorHit->first, anchorHit->second);
        return true;
    }

    if (!m_connectGesture || !m_paramDoc) return false;

    const auto cands = m_connectGesture->hitPointCandidates(pos);
    if (cands.empty()) return false;

    std::vector<SnapResult> validCands;
    for (const auto& c : cands) {
        if (!m_selection.contains(c.blockId))
            continue;
        const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
        if (!blk || blk->isBridge) continue;
        if (blk->layer != m_paramDoc->layersView().activeLayer()) continue;

        if (!m_paramDoc->componentsView().ofBlock(c.blockId)) {
            bool externalWelded = false;
            for (const auto& att : m_paramDoc->attachments()) {
                if (!att.isLocked) continue;
                if (att.fromBlockId == c.blockId
                    || (att.toBlockId == c.blockId && att.toPointId == c.pointId)) {
                    externalWelded = true;
                    break;
                }
            }
            if (externalWelded) continue;

            bool isFollower = false;
            bool angleOnlyFree = false;
            for (const auto& att : m_paramDoc->attachments())
                if (att.fromBlockId == c.blockId) {
                    isFollower = true;
                    if (!att.isPin && att.angleOnly) angleOnlyFree = true;
                    break;
                }
            if (isFollower && !angleOnlyFree) continue;
        }

        validCands.push_back(c);
    }

    if (validCands.size() >= 2) {
        const cad::geo::Vec2 refPos = validCands.front().worldPos;
        std::vector<SnapResult> overlap;
        for (const auto& c : validCands)
            if (c.worldPos.distanceTo(refPos) < kSnapOverlapEps)
                overlap.push_back(c);

        if (overlap.size() > 1) {
            std::vector<ConfirmCandidate> sourceCandidates;
            for (const auto& c : overlap) {
                const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
                if (!blk) continue;
                for (const auto& seg : blk->segments) {
                    if (seg.startPointId == c.pointId || seg.endPointId == c.pointId)
                        sourceCandidates.push_back({c.blockId, seg.id, c.pointId});
                }
            }
            if (!sourceCandidates.empty()) {
                m_connectGesture->beginSourceConfirm(std::move(sourceCandidates), pos);
                return true;
            }
        }
    }

    if (validCands.empty()) return false;

    const SnapResult& pickedCand = validCands.front();
    m_connectGesture->beginConnect(pickedCand.blockId, pickedCand.pointId, pos);
    return true;
}

} // namespace cad::tools
