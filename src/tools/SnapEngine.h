#pragma once

#include <QUuid>
#include <QString>
#include <QSet>
#include <optional>

#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// Two snap candidates "stack on the same spot" when their world positions
/// are within this distance (mm). Drives the overlap disambiguation in the
/// connect gesture (ConfirmTarget) and the smart pen's start/end point
/// confirm flow (点选线段确认落点). Single shared source of truth.
inline constexpr double kSnapOverlapEps = 0.5;  // mm

/// Result of a successful snap operation.
struct SnapResult {
    cad::geo::Vec2 worldPos;  ///< World-coordinate position of the snapped point.
    QUuid blockId;            ///< Block containing the point.
    QUuid pointId;            ///< The ParamPoint that was snapped to.
    QString pointName;        ///< Display name (may be empty).
};

/// Result of a successful segment-body snap (线身吸附): the cursor's
/// perpendicular projection onto the nearest segment. Used by the
/// smart pen's X marker / quick auxiliary-point creation.
struct SegmentSnapResult {
    cad::geo::Vec2 worldPos;  ///< Projection point on the segment (world coords).
    QUuid blockId;            ///< Block containing the segment.
    QUuid segmentId;          ///< The segment that was snapped to.
    double t = 0.0;           ///< Projection parameter: line=linear ratio; curve=arc-length ratio.
    cad::geo::Vec2 tangent;   ///< Unit tangent at projection (curve only; zero for lines).
    cad::geo::Vec2 normal;    ///< Unit normal (tangent ⊥ CCW; curve only).
};

/// Provides point-snapping capability for drawing tools.
/// Searches all resolved points in the ParamDocument within a screen-space radius.
class SnapEngine
{
public:
    /// Snap radius in scene pixels (default 12px).
    double snapRadius = 12.0;

    /// Find the nearest snappable point within the radius of the given world position.
    /// @param worldPos   Cursor position in user/world coordinates (+Y up).
    /// @param paramDoc   The parametric document to search.
    /// @param zoom       Current view zoom factor (for screen-space radius conversion).
    /// @param radiusPx   Screen-space radius override in pixels; <= 0 uses snapRadius.
    /// @param excludePointId  Optional point to skip (e.g. the point being dragged).
    /// @param excludeBlockId  Optional block whose points are ALL skipped (e.g. the
    ///                    dragged block during a connect drag — its own nearby
    ///                    points must not shadow the real target).
    /// @param ignoreLayerFilter  When true, only hidden layers are skipped and the
    ///                    aux-layer active-only restriction is lifted. For tools
    ///                    that reference points WITHOUT creating attachments
    ///                    (e.g. the intersection tool's aim point) — snapping to
    ///                    a grayed aux layer is safe there.
    /// @return           SnapResult if a point was found, std::nullopt otherwise.
    [[nodiscard]] std::optional<SnapResult> findSnap(
        const cad::geo::Vec2& worldPos,
        const cad::param::ParamDocument* paramDoc,
        double zoom,
        double radiusPx = -1.0,
        const QUuid& excludePointId = {},
        const QUuid* excludeBlockId = nullptr,
        bool ignoreLayerFilter = false) const;

    /// Return ALL snappable points within the radius, nearest first. Used by
    /// the connect gesture to detect overlapping candidates (several blocks'
    /// endpoints stacked at one spot) so the user can confirm the intended
    /// leader segment instead of silently picking the traversal order.
    /// Same parameters as findSnap.
    [[nodiscard]] std::vector<SnapResult> findSnapCandidates(
        const cad::geo::Vec2& worldPos,
        const cad::param::ParamDocument* paramDoc,
        double zoom,
        double radiusPx = -1.0,
        const QUuid& excludePointId = {},
        const QUuid* excludeBlockId = nullptr,
        bool ignoreLayerFilter = false) const;

    /// Find the nearest curve-relevant point within the radius: a CurveAnchor
    /// pass-point, or an endpoint of a curve segment. Used by the curve edit
    /// tool so that an attached (overlapping) non-curve point does not shadow
    /// the curve point/endpoint the user wants to grab a handle on.
    /// @param worldPos   Cursor position in user/world coordinates (+Y up).
    /// @param paramDoc   The parametric document to search.
    /// @param zoom       Current view zoom factor (screen-space radius conversion).
    /// @param radiusPx   Screen-space radius in pixels; <= 0 uses snapRadius.
    /// @return           SnapResult if a curve-relevant point was found.
    [[nodiscard]] std::optional<SnapResult> findCurvePointSnap(
        const cad::geo::Vec2& worldPos,
        const cad::param::ParamDocument* paramDoc,
        double zoom,
        double radiusPx = -1.0) const;

    /// Find the nearest segment whose body is within the radius of the given
    /// world position. Hidden (隐藏) segments are included — hiding is
    /// visual-only; hidden geometry is typically kept as a positioning
    /// reference and must stay snappable.
    /// @param worldPos         Cursor position in user/world coordinates (+Y up).
    /// @param paramDoc         The parametric document to search.
    /// @param zoom             Current view zoom factor (screen-space radius conversion).
    /// @param radiusPx         Screen-space radius in pixels; <= 0 uses snapRadius.
    /// @param excludeSegments  Optional segment IDs to skip (e.g. the smart pen's
    ///                         leader candidates, whose body click switches the
    ///                         reference instead of creating an aux point).
    /// @param ignoreLayerFilter  Same semantics as findSnap (no-attachment tools).
    /// @return                 SegmentSnapResult if a segment was found.
    [[nodiscard]] std::optional<SegmentSnapResult> findSegmentSnap(
        const cad::geo::Vec2& worldPos,
        const cad::param::ParamDocument* paramDoc,
        double zoom,
        double radiusPx = -1.0,
        const QSet<QUuid>* excludeSegments = nullptr,
        bool ignoreLayerFilter = false) const;
};

} // namespace cad::tools
