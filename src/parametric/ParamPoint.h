#pragma once

#include <QUuid>
#include <QString>

#include "geometry/Vec2.h"

namespace cad::param {

/// How a point's position is determined.
enum class PointConstraint {
    Free,         ///< Position given directly in local coordinates.
    Polar,        ///< refPoint + distance + angle.
    Midpoint,     ///< Midpoint of refPointA and refPointB.
    OnSegment,    ///< refPointA + ratio * (refPointB - refPointA).
    Intersection, ///< Intersection of two lines (reserved).
    Interpolated, ///< Interpolation on a host segment: percent + constant + polar offset.
    CurveAnchor,  ///< Curve pass-point (曲线点): positioned on the CHORD of its host
                  ///< segment by interpPercent (fraction along start→end) plus a
                  ///< perpendicular offset interpOffsetDist (positive = left of the
                  ///< start→end direction). The point follows the chord when the
                  ///< segment endpoints move (parametric). Referenced by the host
                  ///< segment's passPointIds to shape the curve.
};

// ────────────────────────────────────────────────────────────────────────────
// 约束类型分派点登记表 (constraint touchpoint registry)
// ────────────────────────────────────────────────────────────────────────────
// ADDING or CHANGING a PointConstraint value requires touching EVERY layer
// below. Exhaustive switches (Block::resolve, pointConstraintStr) fail the
// build when a case is missed, but the if-chains and string maps do NOT — a
// forgotten branch silently degrades (wrong position, no snap, no drag,
// corrupt save file). Walk this list top-to-bottom when extending the enum;
// keep the new value's counter-part in every layer in the SAME change.
//
//   层               分派点                                  位置
//   ───────────────  ─────────────────────────────────────  ──────────────
//   · 数据定义       枚举值 + 本头文件字段                       ParamPoint.h
//   · 求解 (主)      Block::resolve 的 exhaustive switch      Block.cpp:~65
//   · 求解 (辅助点)  resolveInterpolatedPoints (Interpolated) Block.cpp:~484
//   · 求解 (曲线)    rebuildCurveCache 宿主/曲线点判定        Block.cpp:~621/693
//   · 求解 (交点)    Resolver settle 交点 step               Resolver.cpp:~246
//   · 脏传播         跨块引用扫描 (blockReferences)           ParamDocument.cpp:~673
//   · 善后           交点降级 (degradeOrphanedIntersections) ParamDocument.cpp:~488
//   · 渲染           点缓存 + 曲线点标记 (rebuildCache)       BlockItem.cpp:~775
//   · 交互 (选中)    曲线锚点排除 (ToolSelect)                ToolSelect.cpp:~1362
//   · 交互 (曲线)    曲线点编辑 (ToolCurveEdit)               ToolCurveEdit.cpp:~92
//   · 命令           降级恢复 (BlockCommands)                BlockCommands.cpp:~382
//   · 序列化         pointConstraintStr / pointConstraintFrom DocumentSerializer.cpp:~31
//   · 复制           Duplicate 约束检查                       Duplicate.cpp:~133
//
// Test hook: tests/test_commands.cpp 的约束行为测试、tests/test_serializer.cpp
// 的 round-trip 测试会锁定每个约束的求解与序列化语义。

/// A parametric point entity. Position is computed from constraints.
struct ParamPoint {
    QUuid   id   = QUuid::createUuid();
    QString serial;  ///< Human-readable ID, e.g. "a5sdfP1" (assigned by ParamDocument).
    QString name;  ///< Display label (e.g. "A", "B").
    QString annotation;  ///< User annotation / comment.

    PointConstraint constraint = PointConstraint::Free;

    // --- Free mode ---
    geo::Vec2 freePos;  ///< Direct local-coordinate position.

    // --- Polar mode ---
    QUuid  refPointId;    ///< Reference point this point is relative to.
    double distance = 0.0;  ///< Distance from reference (mm).
    double angle    = 0.0;  ///< Angle in degrees (relative to reference direction).
    QUuid  refSegmentId;    ///< Optional: segment whose direction is the angle baseline.

    // --- Formula support (overrides numeric value when non-empty) ---
    /// Formulas operate in the formula domain (cm): variables resolve to cm
    /// values and the result is interpreted as cm, then converted to mm by
    /// Block::resolve(). Example: "hip/4+2" with hip=92cm → 25cm → 250mm.
    QString distanceFormula;  ///< e.g. "hip/4+2" → evaluates to distance (cm, auto-converted to mm).
    QString angleFormula;     ///< e.g. "45" or expression → evaluates to angle (deg).

    // --- Midpoint / OnSegment mode ---
    QUuid  refPointA;
    QUuid  refPointB;
    double ratio = 0.5;  ///< 0.0~1.0 for OnSegment; ignored for Midpoint.

    // --- Intersection mode (射线-线段交点) ---
    /// Ray origin: refPointA (reused). Target segment: hostSegmentId (reused).
    /// The ray is cast from refPointA at interAngle relative to the target
    /// segment's start→end direction; the intersection with the target segment
    /// determines this point's position.
    double  interAngle = 90.0;         ///< Ray angle in degrees relative to the
                                       ///< target segment's start→end direction (CCW+).
    QString interAngleFormula;         ///< Formula overriding interAngle (degrees).
    bool    interBidirectional = false; ///< false = ray (one direction only);
                                       ///< true = full line (both directions).
    /// Aim point (指向点): when non-null the ray direction directly points at
    /// this point (world atan2 from refPointA), ignoring interAngle/formula.
    /// Parametric: the direction follows the aim point when it moves.
    /// interAngle/interAngleFormula are kept as the fallback when the aim
    /// point is deleted (degradeOrphanedIntersections clears this reference).
    QUuid   interAimPointId;           ///< Point the ray points at (any block).

    // --- Interpolated mode (auxiliary point on a host segment) ---
    QUuid  hostSegmentId;              ///< The segment this auxiliary point belongs to.
                                       ///< Also used by Intersection mode as the target segment.
    QUuid  interpRefPointId;           ///< Measurement reference point (null = use segment
                                       ///< endpoint per interpFromEnd). When set, percent and
                                       ///< constant are measured from this point's position
                                       ///< along the host segment direction. The referenced
                                       ///< point must lie on the same host segment.
    double interpPercent = 0.5;        ///< Fraction along host segment (can exceed [0,1] for extrapolation).
    QString interpPercentFormula;      ///< Formula for percent (e.g. "0.5" or variable expression).
    double interpConstant = 0.0;       ///< Constant offset along host direction (mm internal).
    QString interpConstantFormula;     ///< Formula for constant (cm domain, auto-converted to mm).
    double interpOffsetAngle = 0.0;    ///< Deflection angle relative to host direction (degrees, CCW+).
    QString interpOffsetAngleFormula;  ///< Formula for offset angle (degrees).
    double interpOffsetDist = 0.0;     ///< Offset distance along deflection angle (mm internal).
    QString interpOffsetDistFormula;   ///< Formula for offset distance (cm domain, auto-converted to mm).
    bool interpFromEnd = false;        ///< Direction reference: false = measure from the host
                                       ///< segment's START toward its END (default); true = measure
                                       ///< from the END toward the START. Flips the base direction
                                       ///< used by percent, constant and the offset angle alike.

    // --- Curve anchor tangent handles (used when referenced by Segment::passPointIds
    //     or as a curve endpoint) ---
    geo::Vec2 tangentIn;        ///< Incoming tangent vector (local coords, direction+magnitude).
    geo::Vec2 tangentOut;       ///< Outgoing tangent vector.
    bool tangentLocked = true;  ///< true = in/out collinear (smooth); false = corner allowed.
    bool autoTangent   = true;  ///< true = Catmull-Rom auto; false = user-overridden handles.

    // --- Curve anchor follow connection (曲线点跟随连接) ---
    /// When set, this curve anchor parametrically follows another point: its
    /// chord-relative position (interpPercent/interpOffsetDist) is recomputed
    /// each resolve pass so that its world position stays at the target point's
    /// world position + followOffset. Established by snap-connecting during a
    /// curve point drag (ToolCurveEdit) or via the anchor panel.
    QUuid     followBlockId;    ///< Block containing the followed point (null = none).
    QUuid     followPointId;    ///< The followed point.
    geo::Vec2 followOffset;     ///< World-space offset from target to this point (mm).

    // --- Resolved result (filled by Resolver) ---
    geo::Vec2 resolvedPos;  ///< Final position in local coordinates.
    bool      resolved = false;

    // --- Metadata ---
    bool isAuxiliary = false;  ///< Auxiliary point (positioning only, no line drawn).
    bool visible     = true;
    bool selectable  = true;   ///< false for anchor points (invisible pivot).
    bool showName    = false;  ///< Whether to display the point name on canvas.
};

} // namespace cad::param
