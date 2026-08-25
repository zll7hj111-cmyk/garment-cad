#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <QPointer>
#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsView;

namespace cad::param { class ParamDocument; struct ParamPoint; }

namespace cad::tools {

class HudItem;

/// Intersection tool — creates a parametric Intersection point on a target
/// segment by casting a ray from an existing point at a specified angle.
///
/// Interaction flow:
///   1. SelectLine:   hover highlights segments; click selects target L.
///   2. SelectPoint:  hover highlights points; click selects ray origin A.
///   3. AimAngle:     hover a point → preview aims the ray at it; clicking
///                    that point creates the intersection IMMEDIATELY
///                    (one-step 指向点 borrow); clicking blank commits the
///                    free-aim direction.
///   4. BorrowAim:    entered only when the borrowed ray misses the target
///                    segment (no intersection) — the direction stays locked
///                    and the HUD explains why; right-click / Esc unlocks.
///
/// Right-click / Esc cancels current step (backtracks one state).
class ToolIntersection : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;
    void keyRelease(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override
    { return "\xe4\xba\xa4\xe7\x82\xb9"; }  // "交点"

private:
    enum class State { SelectLine, SelectPoint, AimAngle, BorrowAim };

    // --- State handlers ---
    void handleSelectLinePress(const cad::geo::Vec2& pos, double zoom);
    void handleSelectPointPress(const cad::geo::Vec2& pos, double zoom);
    /// AimAngle click: hitting a point borrows it (→ BorrowAim); blank commits.
    void handleAimAnglePress(const cad::geo::Vec2& pos, double zoom);
    /// BorrowAim click: commit the intersection along the locked aim direction.
    void handleBorrowAimPress(const cad::geo::Vec2& pos, double zoom);

    /// Release the borrowed aim point (BorrowAim → AimAngle).
    void clearAim();
    /// Live world position of the borrowed aim point (nullopt if gone).
    [[nodiscard]] std::optional<cad::geo::Vec2> aimPointWorldPos() const;
    /// Display label of the borrowed aim point (name or serial tag, may be empty).
    [[nodiscard]] QString aimPointLabel() const;

    // --- Preview / feedback ---
    void updateLineHover(const cad::geo::Vec2& pos, double zoom);
    void updatePointHover(const cad::geo::Vec2& pos, double zoom);
    void updateAimPreview(const cad::geo::Vec2& cursorPos, double zoom);
    void clearPreview();
    void clearHoverMarkers();

    /// Ensure the step-hint HUD exists and shows @p text near the cursor
    /// (SelectLine/SelectPoint step guidance).
    void updateStepHud(const cad::geo::Vec2& cursorPos, const QString& text);
    /// Ensure the segment-highlight overlay exists; @p hover picks the
    /// thinner live-hover style vs the thicker confirmed-selection style.
    void ensureSegHighlight(bool hover);

    /// Compute the intersection of the ray (from m_originPos at m_currentAngleDeg
    /// relative to the target segment direction) with the target segment.
    /// Returns the intersection position and the segment parameter t, or nullopt.
    [[nodiscard]] std::optional<cad::geo::Vec2> computeIntersection(
        double angleDeg, double* outT = nullptr) const;

    /// Create the intersection ParamPoint and push to undo stack.
    void commitIntersection();

    /// Reset to initial state (SelectLine).
    void resetState();
    State m_state = State::SelectLine;

    // --- Selection state ---
    QUuid m_targetBlockId;       ///< Block containing the target segment.
    QUuid m_targetSegmentId;     ///< Target segment L.
    QUuid m_originBlockId;       ///< Block containing the ray origin point.
    QUuid m_originPointId;       ///< Ray origin point A.
    cad::geo::Vec2 m_originPos;  ///< Cached world position of A.

    // --- Angle state ---
    double m_currentAngleDeg = 90.0;  ///< Current ray angle (ALWAYS relative to L direction).
    double m_displayAngleDeg = 90.0;  ///< Angle shown in HUD (mode-dependent: world or construction).
    bool   m_angleSnap = false;       ///< Shift held → 45° snap.
    bool   m_worldAngleMode = false;  ///< W toggles: aim by world angle (back-calculated
                                      ///< to relative for storage) vs follower angle.
    cad::geo::Vec2 m_lastCursorPos;   ///< Last cursor pos (to refresh preview on mode toggle).
    double m_lastZoom = 1.0;          ///< Last view zoom (point-snap radius conversion).
    bool   m_bidirectional = false;   ///< Bidirectional mode toggle.

    // --- Aim-point state (指向点) ---
    QUuid m_aimPointId;               ///< Point the ray currently points at (null = angle mode).
    QUuid m_aimBlockId;               ///< Block containing the aim point.

    // --- Snap engine ---
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_hoverPoint;
    std::optional<SegmentSnapResult> m_hoverSeg;

    // --- Preview graphics ---
    QGraphicsLineItem*    m_previewRay   = nullptr;  ///< Dashed ray from A to intersection.
    QGraphicsEllipseItem* m_intersectDot = nullptr;  ///< Intersection marker (filled circle).
    QGraphicsPathItem*    m_noHitMarker  = nullptr;  ///< X marker when no intersection.
    QGraphicsEllipseItem* m_originMarker = nullptr;  ///< Circle at origin A.
    QGraphicsEllipseItem* m_aimMarker    = nullptr;  ///< Circle at the aim point (指向点).
    QGraphicsLineItem*    m_segHighlight = nullptr;  ///< Highlighted target segment overlay.
};

} // namespace cad::tools
