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

    /// Curve-cache rebuild counter (telemetry / 回归): every
    /// rebuildCurveCache() call increments this. A pure rigid drag (transform
    /// only) must NOT increase it — the drawn curve stays frozen (用户 2026-09:
    /// 跟随对象拖动抖动 = 每帧重算 + 余量; 拖动不改形状, 重算纯浪费).
    quint64 curveCacheBuilds = 0;

    /// Canvas layer — stable Layer::id reference (into ParamDocument::layers()).
    /// Pure selection / visibility filter — does not affect solving,
    /// measurements or links. Null only before assignment (addBlock clamps a
    /// null layer to the first working layer; the aux layer is never default).
    QUuid layer;

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

    /// Dart line (省道线, 用户拍板 2026-08): the block's START point is pinned
    /// to point A on another block, and its END point E is computed from a
    /// reference point B on another block's segment:
    ///     θ_B = refBlock.rotation + refBlock.exitDirectionAtPoint(B, dartRefSegmentId)
    ///     E   = B_world + d · (cos(θ_B + β), sin(θ_B + β))
    /// where d = dartOffsetMm (signed, mm; the sign picks the side — no
    /// horizontal/vertical mode needed) and β = dartAngleDeg (deg relative to
    /// the REFERENCE segment, default 90° — the angle reference is ALWAYS the
    /// offset point B's segment, so it rotates together with that segment).
    /// The Resolver drives origin = A_world, rotation = A→E and writes back
    /// the end point's Polar distance = |A−E| — length/direction are fully
    /// COMPUTED (线是算出来的), never freely editable. The relationship is
    /// one-way: the reference block neither displays nor modifies this
    /// connection (省道线主动挂上去的). Start A must be an attached point
    /// (自由起点禁止进入). All ids null = a plain line.
    QUuid  dartStartBlockId;      ///< Block containing the start pin point A.
    QUuid  dartStartPointId;      ///< Start point A (block origin pins here).
    QUuid  dartRefBlockId;        ///< Block containing the offset reference point B.
    QUuid  dartRefPointId;        ///< Offset reference point B.
    QUuid  dartRefSegmentId;      ///< Segment of B used as the direction basis.
    double dartOffsetMm   = 0.0;  ///< Offset distance d (signed mm; sign = side).
    QString dartOffsetFormula;    ///< Optional formula overriding dartOffsetMm (cm domain).
    double dartAngleDeg   = 90.0; ///< Angle β relative to the ref segment (deg, default 90).
    QString dartAngleFormula;     ///< Optional formula overriding dartAngleDeg.

    /// True when this block carries an active dart constraint (both the start
    /// pin and the reference point are set).
    [[nodiscard]] bool isDart() const
    {
        return !dartStartBlockId.isNull() && !dartRefBlockId.isNull();
    }

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

    /// Bootstrap seed for the "break-endpoint" cycle (3.gcad L68/P175/P176:
    /// a Polar ENDPOINT anchored to an aux point — Intersection/Interpolated —
    /// on its OWN segment). On a cold start the endpoint has no cached pose,
    /// the segment degenerates to zero length, and the intersection (which the
    /// endpoint depends on) can never fire: deadlock. This evaluates the
    /// endpoint's polar formula anchored at the SEGMENT START instead, giving
    /// the fixpoint a non-degenerate first pass; once the aux resolves in a
    /// later pass the endpoint re-anchors to the true reference.
    /// Returns true (and fills @p outLocal) when @p ep is such a Polar
    /// endpoint whose ref is an on-segment aux point of @p seg.
    [[nodiscard]] static bool polarEndpointCycleSeed(
        const ParamPoint& ep, const Block& block, const Segment& seg,
        const ParamPoint& sp, const QHash<QString, double>& params,
        const QHash<QString, QList<Condition>>& conditioned,
        EvalContext* ctx, geo::Vec2& outLocal);

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
    /// This is the reference direction for follower angles. 闭合基准（用户拍板
    /// 2026-08）：followerAngle 0° = 两线折叠重叠、180° = 沿 leader 延伸直行，
    /// 与起点/终点吸附无关。Returns 0 if no such segment
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

    // ── 端点延长线 (EXTEND_LINE_DESIGN.md) ─────────────────────────────────
    /// 端点的"有效位置"（本体 + 延长量 × 出方向）。所有按"点位置"定义的消费方
    /// （绘制/捕捉/附着/测量/交点/长度显示）应读取本值；按"本体长度/比例"定义
    /// 的内部计算（辅助点/曲线锚点）继续读 resolvedPos（本体）。
    /// 按当前 resolvedPos 即时计算（不依赖缓存），求解 pass 内也正确。
    [[nodiscard]] geo::Vec2 effectiveLocalPos(const QUuid& pointId) const;

    /// 本体段长（未延长）：直线 = 弦长；曲线 = 弧长（曲线不支持延长）。
    [[nodiscard]] double segmentBaseLength(const QUuid& segmentId) const;

    /// 有效段长（含延长量）：直线 = |有效终点 − 有效起点|；曲线 = 本体弧长。
    [[nodiscard]] double segmentEffectiveLength(const QUuid& segmentId) const;

    /// 端点延长量（已求值 mm；无延长 = 0）。
    [[nodiscard]] double segmentExtendStart(const QUuid& segmentId) const;
    [[nodiscard]] double segmentExtendEnd(const QUuid& segmentId) const;

    /// SnapEngine 的段身参数 t（沿有效段 0..1）是否落在本体范围内。
    /// 用于"尾巴上禁止建辅助点/打断"判定（EXTEND_LINE_DESIGN.md D7）。
    [[nodiscard]] bool segmentSnapWithinBase(const QUuid& segmentId, double t) const;

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

    /// Rebuild the internal point index (call after modifying points vector
    /// directly). Const: the index is a lazily-rebuilt mutable cache (same
    /// convention as rebuildSegmentIndex).
    void rebuildPointIndex() const;

    /// Rebuild the internal segment index (call after modifying the segments
    /// vector directly or regenerating segment IDs, e.g. duplicate clones).
    /// Const: the index is a lazily-rebuilt mutable cache.
    void rebuildSegmentIndex() const;

private:
    mutable QHash<QUuid, int> m_pointIndex;  ///< pointId -> index (mutable cache).
    mutable QHash<QUuid, int> m_segmentIndex;  ///< segmentId -> index (mutable cache).

    /// ── 端点延长线 (EXTEND_LINE_DESIGN.md) ──────────────────────────────
    /// Per-segment evaluated extension values (mm; filled at end of resolve).
    struct ExtendEval {
        double startMm = 0.0;
        double endMm   = 0.0;
    };
    /// pointId -> effective local position (本体 + 延长量 × 出方向).
    mutable QHash<QUuid, geo::Vec2> m_effectiveLocal;
    /// segmentId -> evaluated extension values (0-entries omitted).
    mutable QHash<QUuid, ExtendEval> m_extendEval;

    /// End-of-resolve pass: 用 m_extendEval（resolve 开头已求值）+ 当前本体位置
    /// 重建 m_effectiveLocal 缓存，并在可视尾巴变化时显式 +geometryEpoch
    /// （本体不变但视觉要重绘 —— 铁律: 只改显示/语义属性必须显式 +epoch）。
    void applyEffectivePositions();

    /// resolve() 开头：求值各段延长量（数值 mm / 公式 cm 域，防御性 clamp ≥0）
    /// 存入 m_extendEval —— 使求解 pass 内的 effectiveLocalPos（如交叉点宿主段）
    /// 即能看到本帧延长量。
    void evaluateExtendValues(const QHash<QString, double>& params = {},
                              const QHash<QString, QList<Condition>>& conditioned = {},
                              EvalContext* ctx = nullptr);

    std::vector<CurveSpanEntry> m_curveSpans;  ///< Frame cache, rebuilt in resolve().
    /// segmentId -> index into m_curveSpans (rebuilt together with the spans).
    /// curveSpanEntry() is on the per-frame snap/paint path, so a linear scan
    /// would make a block with k curves cost O(k^2) per mouse-move query.
    mutable QHash<QUuid, int> m_curveSpanIndex;
    /// geometryEpoch at the last curve-cache rebuild. The cache is stale only
    /// when the local geometry actually changed (epoch delta) or the cache is
    /// cold — a transform-only drag keeps the spans frozen, so no per-frame
    /// C2 solve / flatten / arc-length integration (2026-09 用户反馈).
    quint64 m_curveCacheEpoch = 0;

    /// Collect the resolved anchor sequence (start + passPoints + end) of a
    /// curve segment. Returns false when any anchor is unresolved.
    [[nodiscard]] bool collectCurveAnchors(const Segment& seg,
                                           std::vector<geo::Vec2>& pts,
                                           std::vector<geo::Vec2>& tIn,
                                           std::vector<geo::Vec2>& tOut,
                                           std::vector<bool>& autoTan) const;

    /// Rebuild m_curveSpans from the CURRENT resolved positions — called from
    /// resolve() only when the local geometry actually changed (epoch delta /
    /// cold cache), never on transform-only frames.
    void rebuildCurveCache();

    /// Resolve a single Interpolated (auxiliary) point from its host segment's
    /// current endpoint positions. Returns true if the point was resolved.
    bool resolveInterpolatedPoint(ParamPoint& pt,
                                  const QHash<QString, double>& params,
                                  const QHash<QString, QList<Condition>>& conditioned,
                                  EvalContext* ctx);

    /// Per-constraint workers of resolveUnresolved's fixpoint switch
    /// (2026-08 拆分, 压平嵌套). Each returns true iff it resolved the point
    /// this call; false = leave unresolved for a later fixpoint pass.
    bool resolvePolarPoint(ParamPoint& pt,
                           const QHash<QString, double>& params,
                           const QHash<QString, QList<Condition>>& conditioned,
                           EvalContext* ctx);
    bool resolveIntersectionPoint(ParamPoint& pt,
                                  const QHash<QString, double>& params,
                                  const QHash<QString, QList<Condition>>& conditioned,
                                  EvalContext* ctx);
    bool resolveCurveAnchorPoint(ParamPoint& pt);
};

} // namespace cad::param
