#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <QList>
#include <vector>

#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Condition.h"

namespace cad::param {

struct EvalContext;  // per-pass formula result memo (ExpressionEvaluator.h)

/// Frame-level cache of a curve segment's Bézier spans (local coordinates).
/// Rebuilt once at the end of Block::resolve(), so every consumer within the
/// same frame — canvas cache rebuild, snap engine projection, tangent-handle
/// display, attachment settling — shares a SINGLE C2 solve per curve instead
/// of each rebuilding the spans independently (the old hot-path repeated the
/// tridiagonal solve 3~5 times per curve per frame).
struct CurveSpanEntry {
    QUuid segmentId;
    std::vector<geo::BezierSpan> spans;  ///< Bézier spans (local coords).
    std::vector<geo::Vec2> anchors;      ///< Anchor sequence: start + passPoints + end.
    geo::Vec2 bboxMin;                   ///< Control-polygon bounding box (local) —
    geo::Vec2 bboxMax;                   ///< the curve lies inside it (convex hull
                                         ///< property), so it is safe for coarse
                                         ///< distance culling in hit tests / snaps.

    // ── Render cache (rebuilt once per resolve with the spans; LOCAL coords) ──
    // The canvas only applies rotation + Y-flip — it never re-flattens,
    // re-evaluates or re-integrates the curve on cache rebuild.
    std::vector<geo::Vec2> flatLocal;    ///< Flattened polyline (0.1 mm tolerance).
    geo::Vec2 labelLocal;                ///< Parametric midpoint t=0.5 (local).
    geo::Vec2 labelLocalDir;             ///< Unit tangent at t=0.5 (local).
    double    arcLengthMm = 0.0;         ///< Exact arc length (mm).
};

/// Rigid-body 2D transform: translation + rotation.
struct Transform2D {
    geo::Vec2 origin;         ///< World-coordinate position of local origin.
    double    rotation = 0.0; ///< Rotation in radians (CCW positive).

    /// Transform a local-coordinate point to world coordinates.
    [[nodiscard]] geo::Vec2 toWorld(const geo::Vec2& local) const;

    /// Transform a world-coordinate point to local coordinates.
    [[nodiscard]] geo::Vec2 toLocal(const geo::Vec2& world) const;
};

/// A Block is a rigid body containing parametric points and segments.
/// Dragging moves the whole Block (changes Transform);
/// editing shape means modifying internal parameters.
class Block
{
public:
    QUuid   id   = QUuid::createUuid();
    QString name;

    Transform2D transform;  ///< Rigid-body placement (drag changes this).

    /// Monotonically bumped whenever a resolve pass changes any point's
    /// resolvedPos (internal geometry). The canvas compares this against its
    /// last-seen value to distinguish transform-only moves (cheap setPos)
    /// from internal shape changes (full cache rebuild).
    quint64 geometryEpoch = 0;

    /// Canvas layer index (into ParamDocument::layers()). Pure selection /
    /// visibility filter — does not affect solving, measurements or links.
    int layer = 0;

    std::vector<ParamPoint> points;    ///< Internal points (local coordinates).
    std::vector<Segment>    segments;  ///< Internal segments referencing points.

    bool isClosed = false;  ///< Whether this block forms a closed contour.

    /// Endpoint aim constraint (终点指向): the block rotates so that its
    /// segment's END point aims at a target point on another block. The
    /// rotation is driven AFTER attachment settling, so this overrides any
    /// attachment-driven rotation when both are present. The line's LENGTH is
    /// unaffected — only the direction is constrained.
    QUuid  endTargetBlockId;    ///< Block containing the aim target (null = none).
    QUuid  endTargetPointId;    ///< The target point being aimed at.
    double endTargetOffset = 0; ///< Angular offset from exact aim direction (deg, CCW+).
    QString endTargetOffsetFormula;  ///< Optional formula overriding endTargetOffset.

    /// Bridge line (桥接线): BOTH endpoints are pinned to points on other
    /// blocks via two attachments (pure position pins — no follower angle,
    /// no leader segment). Length/direction are fully passive (derived from
    /// the two pinned points), so the property dialog shows them read-only.
    /// Invariants enforced by ParamDocument:
    ///   - a bridge is the follower of exactly TWO attachments;
    ///   - the pinned endpoints are pure leaves (not snappable, cannot anchor
    ///     followers) and bridge-to-bridge pins are forbidden;
    ///   - EXCEPTION: an AUXILIARY point on a bridge may anchor a regular
    ///     follower (the Resolver settles bridge followers after the bridge);
    ///   - losing a pin (detach / host deletion) RELEASES the bridge as an
    ///     independent segment: its stretched geometry is frozen into its own
    ///     length + world angle, and a surviving pin becomes a normal follower
    ///     attachment preserving the current direction.
    bool isBridge = false;

    /// Resolve all internal point positions based on constraint chain.
    /// @param params       Variable name→value map (cm) for formula evaluation.
    /// @param conditioned  formulaName→conditions for standalone-condition
    ///                     semantics (see ConditionEngine). May be empty.
    /// @param ctx          Optional per-pass memo so identical formula texts
    ///                     shared by many points execute only once per pass.
    void resolve(const QHash<QString, double>& params = {},
                 const QHash<QString, QList<Condition>>& conditioned = {},
                 EvalContext* ctx = nullptr);

    /// Resolve ONLY the still-unresolved points, without resetting anything.
    /// Called by the Resolver AFTER the cross-block passes (Step 6/6b) so a
    /// Polar endpoint whose ref (e.g. an interpolated point referencing a
    /// cross-block intersection) was resolved there can converge inside the
    /// same outer iteration.
    void resolveUnresolved(const QHash<QString, double>& params = {},
                           const QHash<QString, QList<Condition>>& conditioned = {},
                           EvalContext* ctx = nullptr);

    /// Number of points whose constraint did not resolve yet (current pass).
    /// Used by the Resolver to detect progress inside the cross-block fixpoint.
    [[nodiscard]] int unresolvedCount() const
    {
        int n = 0;
        for (const auto& p : points)
            if (!p.resolved) ++n;
        return n;
    }

    /// Re-resolve ONLY the Interpolated (auxiliary) points, using the CURRENT
    /// resolvedPos of their host-segment endpoints. Called by the Resolver
    /// after a bridge's pinned endpoints have been placed on their hosts (the
    /// segment "stretches"), so aux points track the stretched geometry rather
    /// than the pre-stretch local construction.
    void resolveInterpolatedPoints(const QHash<QString, double>& params = {},
                                   const QHash<QString, QList<Condition>>& conditioned = {},
                                   EvalContext* ctx = nullptr);

    /// Get the world-coordinate position of a point by ID.
    [[nodiscard]] geo::Vec2 worldPos(const QUuid& pointId) const;

    /// Find a mutable point by ID (nullptr if not found). O(1) via index.
    [[nodiscard]] ParamPoint* findPoint(const QUuid& pointId);

    /// Find a const point by ID (nullptr if not found). O(1) via index.
    [[nodiscard]] const ParamPoint* findPoint(const QUuid& pointId) const;

    /// Find a segment by ID (nullptr if not found). O(1) via index.
    [[nodiscard]] Segment* findSegment(const QUuid& segmentId);
    [[nodiscard]] const Segment* findSegment(const QUuid& segmentId) const;

    /// Direction (radians, local coordinates, start->end) of the segment that
    /// has the given point as one of its endpoints. Returns 0 if no such
    /// segment exists or its endpoints are unresolved.
    [[nodiscard]] double directionAtPoint(const QUuid& pointId) const;

    /// Direction (radians, local coordinates) that continues the segment
    /// straight past the given endpoint — i.e. the direction a new line would
    /// take to "keep going straight" if attached at that point.
    ///   - At the segment's END point:   start->end  (extend forward).
    ///   - At the segment's START point: end->start  (extend backward).
    /// This is the reference direction for follower angles, so that
    /// followerAngle == 0 always means "continue straight along the leader",
    /// regardless of which endpoint is snapped. Returns 0 if no such segment
    /// exists or its endpoints are unresolved.
    [[nodiscard]] double exitDirectionAtPoint(const QUuid& pointId) const;

    /// Overload preferring an explicit segment (Attachment::toSegmentId).
    /// If preferredSegmentId names a segment incident to pointId (as endpoint
    /// or as host of an interpolated aux point), its direction is used;
    /// otherwise falls back to the scan-based overload (legacy behaviour).
    [[nodiscard]] double exitDirectionAtPoint(const QUuid& pointId,
                                              const QUuid& preferredSegmentId) const;

    /// ID of the segment exitDirectionAtPoint(pointId) would use: the first
    /// segment having pointId as an endpoint, else the host segment of an
    /// interpolated aux point, else null. Used to record the auto-picked
    /// leader segment explicitly when creating an Attachment.
    [[nodiscard]] QUuid exitSegmentAtPoint(const QUuid& pointId) const;

    /// Frame-level Bézier cache accessor: returns the spans + anchors for a
    /// curve segment, or nullptr when the segment is not a curve / the cache
    /// is empty (points unresolved). Valid after resolve(); the cache is
    /// rebuilt at the end of every resolve() pass.
    [[nodiscard]] const CurveSpanEntry* curveSpanEntry(const QUuid& segmentId) const;

    /// Length (mm, local coordinates) of the segment incident to pointId.
    /// Returns the Euclidean distance between the segment's start and end
    /// resolved positions. Returns 0 if no segment is found or endpoints
    /// are unresolved.
    [[nodiscard]] double segmentLengthAtPoint(const QUuid& pointId) const;

    /// Freeze the current (resolved, possibly stretched) world geometry of
    /// this single-segment block into a self-contained local construction:
    /// origin at the start point, rotation along start→end, end point
    /// Polar(distance = frozen length, angle = 0). The resolved cache is
    /// synced so directionAtPoint() stays valid before the next resolve.
    /// Used when releasing a bridge (桥接线释放) and when duplicating one.
    /// Returns false if the segment/endpoints are missing or unresolved.
    bool freezeSegmentGeometry();

    /// Add a point and return its ID.
    QUuid addPoint(ParamPoint pt);

    /// Add a segment and return its ID.
    QUuid addSegment(Segment seg);

    /// Rebuild the internal point index (call after modifying points vector directly).
    void rebuildPointIndex();

    /// Rebuild the internal segment index (call after modifying the segments
    /// vector directly or regenerating segment IDs, e.g. duplicate clones).
    void rebuildSegmentIndex();

private:
    QHash<QUuid, int> m_pointIndex;  ///< pointId -> index in points vector
    QHash<QUuid, int> m_segmentIndex;  ///< segmentId -> index in segments vector

    std::vector<CurveSpanEntry> m_curveSpans;  ///< Frame cache, rebuilt in resolve().

    /// Collect the resolved anchor sequence (start + passPoints + end) of a
    /// curve segment. Returns false when any anchor is unresolved.
    [[nodiscard]] bool collectCurveAnchors(const Segment& seg,
                                           std::vector<geo::Vec2>& pts,
                                           std::vector<geo::Vec2>& tIn,
                                           std::vector<geo::Vec2>& tOut,
                                           std::vector<bool>& autoTan) const;

    /// Rebuild m_curveSpans from the CURRENT resolved positions (call at the
    /// end of resolve() — one C2 solve per curve segment per frame).
    void rebuildCurveCache();

    /// Resolve a single Interpolated (auxiliary) point from its host segment's
    /// current endpoint positions. Returns true if the point was resolved.
    bool resolveInterpolatedPoint(ParamPoint& pt,
                                  const QHash<QString, double>& params,
                                  const QHash<QString, QList<Condition>>& conditioned,
                                  EvalContext* ctx);
};

} // namespace cad::param
