#include "Block.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "geometry/CurveMath.h"
#include "geometry/Epsilon.h"

namespace cad::param {

namespace {
/// Hash one double into a running fingerprint (boost-style combine on the
/// bit pattern). Used by Block::spansForSegment's memo key.
inline quint64 fpMix(quint64 h, double v)
{
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(v));
    h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

/// Fingerprint of everything that determines a curve's Bézier spans: anchor
/// positions, stored tangents, auto flags and the segment tension. Any
/// change to the curve shape changes the key — so a memo hit is ALWAYS the
/// same spans a fresh Hobby solve would produce, mid-fixpoint included.
quint64 curveAnchorFingerprint(const std::vector<geo::Vec2>& pts,
                               const std::vector<geo::Vec2>& tIn,
                               const std::vector<geo::Vec2>& tOut,
                               const std::vector<bool>& autoTan,
                               double tension)
{
    quint64 h = 0xcbf29ce484222325ULL;
    h = fpMix(h, static_cast<double>(pts.size()));
    h = fpMix(h, tension);
    for (size_t i = 0; i < pts.size(); ++i) {
        h = fpMix(h, pts[i].x);   h = fpMix(h, pts[i].y);
        h = fpMix(h, tIn[i].x);   h = fpMix(h, tIn[i].y);
        h = fpMix(h, tOut[i].x);  h = fpMix(h, tOut[i].y);
        h = fpMix(h, (i < autoTan.size() && autoTan[i]) ? 1.0 : 0.0);
    }
    return h;
}

} // namespace
bool Block::collectCurveAnchors(const Segment& seg,
                                std::vector<geo::Vec2>& pts,
                                std::vector<geo::Vec2>& tIn,
                                std::vector<geo::Vec2>& tOut,
                                std::vector<bool>& autoTan,
                                bool skipUnresolvedPass,
                                bool tolerateStaleEndpoints) const
{
    pts.clear(); tIn.clear(); tOut.clear(); autoTan.clear();

    const ParamPoint* sp = findPoint(seg.startPointId);
    const ParamPoint* ep = findPoint(seg.endPointId);
    if (!sp || !ep) return false;
    if (!tolerateStaleEndpoints && (!sp->resolved || !ep->resolved))
        return false;
    // Tolerant mode: an endpoint mid-cycle in the fixpoint keeps its CACHED
    // resolvedPos — the fixpoint converges once the endpoint resolves
    // (endpoint↔query dependency cycle, e.g. break endpoints).
    if (!sp->resolved || !ep->resolved) {
        // Never fabricate geometry from a never-resolved (zero) endpoint.
        if (sp->resolvedPos.lengthSquared() < cad::geo::kGeomEpsTight &&
            ep->resolvedPos.lengthSquared() < cad::geo::kGeomEpsTight)
            return false;
    }

    pts.push_back(sp->resolvedPos);
    tIn.push_back(sp->tangentIn);  tOut.push_back(sp->tangentOut);  autoTan.push_back(sp->autoTangent);
    for (const auto& ppId : seg.passPointIds) {
        const ParamPoint* pp = findPoint(ppId);
        if (!pp || !pp->resolved) {
            if (skipUnresolvedPass) continue;  // mid-fixpoint tolerance
            return false;
        }
        pts.push_back(pp->resolvedPos);
        tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
    }
    pts.push_back(ep->resolvedPos);
    tIn.push_back(ep->tangentIn);  tOut.push_back(ep->tangentOut);  autoTan.push_back(ep->autoTangent);
    return true;
}

std::vector<geo::BezierSpan> Block::spansForSegment(
    const Segment& seg, bool skipUnresolvedPassPoints,
    bool tolerateStaleEndpoints) const
{
    if (!seg.isCurve()) return {};

    std::vector<geo::Vec2> pts, tIn, tOut;
    std::vector<bool> autoTan;
    if (!collectCurveAnchors(seg, pts, tIn, tOut, autoTan,
                             skipUnresolvedPassPoints, tolerateStaleEndpoints))
        return {};
    if (pts.size() < 2) return {};

    // Memo by anchor fingerprint: within one settle sweep the anchors do not
    // move (aux/intersection points never shape the curve), so N consumers
    // on the same curve share ONE Hobby solve. The fingerprint — not the
    // geometry epoch — is the validity key, because mid-fixpoint positions
    // change BEFORE the epoch bump (Block::resolve bumps it after the
    // fixpoint), so an epoch check would serve stale spans exactly when the
    // resolve-time callers run.
    const quint64 fp = curveAnchorFingerprint(pts, tIn, tOut, autoTan, seg.tension);
    const auto it = m_spanMemo.constFind(seg.id);
    if (it != m_spanMemo.constEnd() && it->fingerprint == fp)
        return it->spans;

    auto spans = geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                       geo::AutoCurveMode::Hobby);
    m_spanMemo.insert(seg.id, CurveSpanMemo{fp, spans});
    return spans;
}

void Block::rebuildCurveCache()
{
    ++curveCacheBuilds;               // telemetry: rigid drags must NOT grow this
    m_curveCacheEpoch = m_geometryEpoch;
    m_curveSpans.clear();
    m_curveSpans.reserve(segments.size());
    m_curveSpanIndex.clear();
    m_curveSpanIndex.reserve(segments.size());
    m_spanMemo.clear();  // repopulated below with the canonical spans

    for (const auto& seg : segments) {
        if (!seg.isCurve()) continue;

        std::vector<geo::Vec2> pts, tIn, tOut;
        std::vector<bool> autoTan;
        if (!collectCurveAnchors(seg, pts, tIn, tOut, autoTan)) continue;

        std::vector<geo::BezierSpan> spans =
            geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                  geo::AutoCurveMode::Hobby);
        if (spans.empty()) continue;

        // Prime the spansForSegment memo with the canonical spans so
        // post-resolve consumers (tools, commands) reuse this solve.
        m_spanMemo.insert(seg.id, CurveSpanMemo{
            curveAnchorFingerprint(pts, tIn, tOut, autoTan, seg.tension), spans});

        // Control-polygon bbox (local): the curve lies inside it (convex hull
        // property of Béziers), safe for coarse distance culling.
        geo::Vec2 lo = pts[0], hi = pts[0];
        const auto grow = [&lo, &hi](const geo::Vec2& p) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        };
        for (const auto& s : spans) {
            grow(s.ctrl1); grow(s.ctrl2); grow(s.p3);
        }

        // Render cache (local coords): flatten, label midpoint/tangent and
        // exact arc length are computed ONCE per resolve here — the canvas
        // cache rebuild (BlockItem) only applies rotation + Y-flip to these,
        // never re-flattens / re-integrates the curve per frame.
        CurveSpanEntry entry;
        entry.segmentId = seg.id;
        entry.spans = std::move(spans);
        entry.anchors = std::move(pts);
        entry.bboxMin = lo;
        entry.bboxMax = hi;
        entry.flatLocal    = geo::flattenBezierSpans(entry.spans, 0.1);
        entry.arcLengthMm  = geo::totalArcLength(entry.spans);
        // Per-span cumulative arc-length table, built once here so the
        // hot-path consumers (SnapEngine projection, interpolated-point
        // arc-length lookup) reuse it instead of re-integrating every span
        // per call.
        entry.cumArcLengthMm = geo::buildCumulativeArcLength(entry.spans);
        // Label anchor: the ARC-LENGTH midpoint of the whole curve. The old
        // evalCurve(spans, 0.5) was the parametric middle of span 0 — on
        // multi-anchor curves (3+ spans) the name/length labels drifted
        // toward the start of the curve instead of sitting at its middle.
        const double tMid = geo::arcLengthToParam(entry.spans, entry.arcLengthMm * 0.5,
                                                  &entry.cumArcLengthMm);
        entry.labelLocal   = geo::evalCurve(entry.spans, tMid);
        entry.labelLocalDir = geo::evalCurveTangent(entry.spans, tMid);
        m_curveSpanIndex.insert(seg.id, static_cast<int>(m_curveSpans.size()));
        m_curveSpans.push_back(std::move(entry));
    }
}

const CurveSpanEntry* Block::curveSpanEntry(const QUuid& segmentId) const
{
    // Indexed lookup: this accessor runs per curve per snap query / per canvas
    // cache rebuild — a linear scan is O(k^2) per block with k curves.
    const auto it = m_curveSpanIndex.constFind(segmentId);
    if (it == m_curveSpanIndex.constEnd())
        return nullptr;
    const int idx = it.value();
    if (idx < 0 || idx >= static_cast<int>(m_curveSpans.size()))
        return nullptr;
    const auto& e = m_curveSpans[static_cast<size_t>(idx)];
    // idempotency guard: the index and spans are rebuilt together, so a stale
    // entry can only appear via direct vector mutation — verify before use.
    return e.segmentId == segmentId ? &e : nullptr;
}


} // namespace cad::param
