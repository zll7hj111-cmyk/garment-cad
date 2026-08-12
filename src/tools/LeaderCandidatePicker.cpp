#include "LeaderCandidatePicker.h"

#include <algorithm>
#include <cmath>

#include <QSet>
#include <QGraphicsView>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"

namespace cad::tools {

LeaderCandidatePicker::LeaderCandidatePicker(CanvasScene* scene,
                                             cad::param::ParamDocument* doc)
    : m_scene(scene)
    , m_paramDoc(doc)
{
}

void LeaderCandidatePicker::collect(const SnapResult& snap)
{
    clear();
    if (!m_paramDoc || !m_scene) return;

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    // Same reach as the snap itself: what looks like one point may be several
    // coincident points from different blocks stacked on the same spot, and
    // every segment incident to any of them is a valid angle reference.
    const double tol = m_snapEngine.snapRadius / zoom;

    struct Ranked {
        LeaderCandidate cand;
        int rank;   ///< Lower = preferred for the auto-pick.
    };
    std::vector<Ranked> ranked;
    QSet<QUuid> seenSegments;

    for (const auto& block : m_paramDoc->blocks()) {
        for (const auto& pt : block.points) {
            if (!pt.resolved) continue;
            const Vec2 wp = block.transform.toWorld(pt.resolvedPos);
            if (wp.distanceTo(snap.worldPos) > tol) continue;

            for (const auto& seg : block.segments) {
                const bool isEndpoint = (seg.startPointId == pt.id
                                         || seg.endPointId == pt.id);
                const bool isHost =
                    (pt.constraint == cad::param::PointConstraint::Interpolated
                     && pt.hostSegmentId == seg.id);
                if (!isEndpoint && !isHost) continue;
                if (seenSegments.contains(seg.id)) continue;
                seenSegments.insert(seg.id);

                // Auto-pick order: endpoint segments before host segments,
                // then Outline < Internal < Auxiliary, then creation order
                // (stable sort keeps document iteration order within a rank).
                int rank = isEndpoint ? 0 : 100;
                switch (seg.role) {
                case cad::param::SegmentRole::Outline:   rank += 0; break;
                case cad::param::SegmentRole::Internal:  rank += 1; break;
                case cad::param::SegmentRole::Auxiliary: rank += 2; break;
                }
                ranked.push_back({{block.id, pt.id, seg.id}, rank});
            }
        }
    }

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) {
                         return a.rank < b.rank;
                     });

    m_leaderCandidates.reserve(ranked.size());
    for (const auto& r : ranked)
        m_leaderCandidates.push_back(r.cand);
}

void LeaderCandidatePicker::setIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_leaderCandidates.size()))
        return;

    // Move the teal highlight to the new candidate's block item.
    if (m_scene && !m_highlightBlockId.isNull()) {
        if (auto* item = m_scene->findBlockItem(m_highlightBlockId))
            item->setLeaderHighlight(QUuid());
    }

    m_leaderIndex = index;
    const LeaderCandidate& cand = m_leaderCandidates[static_cast<size_t>(index)];
    m_highlightBlockId = cand.blockId;
    if (m_scene) {
        if (auto* item = m_scene->findBlockItem(cand.blockId))
            item->setLeaderHighlight(cand.segmentId);
    }

    // Re-anchor construction-angle space (HUD / Shift snap / final followerAngle)
    // on the new leader's exit direction.
    if (m_paramDoc) {
        if (const auto* lb = m_paramDoc->findBlock(cand.blockId))
            m_refDirDeg = (lb->transform.rotation
                           + lb->exitDirectionAtPoint(cand.pointId, cand.segmentId))
                          * 180.0 / M_PI;
    }
}

void LeaderCandidatePicker::clear()
{
    if (m_scene && !m_highlightBlockId.isNull()) {
        if (auto* item = m_scene->findBlockItem(m_highlightBlockId))
            item->setLeaderHighlight(QUuid());
    }
    m_highlightBlockId = QUuid();
    m_leaderCandidates.clear();
    m_leaderIndex = -1;
}

int LeaderCandidatePicker::candidateAt(const Vec2& worldPos, double zoom) const
{
    if (!m_paramDoc) return -1;
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    // Same pick tolerance as segment hover — the two gestures should feel
    // identical (hoverRadiusPx is the shared interaction token).
    const double tol = (m_scene ? m_scene->style()->hoverRadiusPx() : 8.0) / zoom;

    int best = -1;
    double bestDist = tol;
    for (int i = 0; i < static_cast<int>(m_leaderCandidates.size()); ++i) {
        const LeaderCandidate& cand = m_leaderCandidates[static_cast<size_t>(i)];
        const auto* block = m_paramDoc->findBlock(cand.blockId);
        if (!block) continue;
        const auto* seg = block->findSegment(cand.segmentId);
        if (!seg) continue;
        const auto* a = block->findPoint(seg->startPointId);
        const auto* b = block->findPoint(seg->endPointId);
        if (!a || !b || !a->resolved || !b->resolved) continue;
        const Vec2 wa = block->transform.toWorld(a->resolvedPos);
        const Vec2 wb = block->transform.toWorld(b->resolvedPos);
        const double d = Vec2::distanceToSegment(worldPos, wa, wb);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

} // namespace cad::tools
