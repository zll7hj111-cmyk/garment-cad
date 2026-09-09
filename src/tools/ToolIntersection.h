#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"
#include "tools/IntersectionToolVisuals.h"

#include <QPointer>
#include <optional>

namespace cad::param {
class ParamDocument;
struct ParamPoint;
class Block;
struct Segment;
}

namespace cad::tools {

/// Intersection tool — creates a parametric Intersection point on a target
/// segment by casting a ray from an existing point at a specified angle.
class ToolIntersection : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;
    void keyRelease(QKeyEvent* event) override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"交点"); }

private:
    enum class State { SelectLine, SelectPoint, AimAngle, BorrowAim };

    void setState(State s);

    [[nodiscard]] ModeIndicator modeIndicator() const override;
    [[nodiscard]] static ModeIndicator modeIndicatorFor(bool worldAngleMode, State s);

    // --- State handlers ---
    void handleSelectLinePress(const cad::geo::Vec2& pos, double zoom);
    void handleSelectPointPress(const cad::geo::Vec2& pos, double zoom);
    void handleAimAnglePress(const cad::geo::Vec2& pos, double zoom);
    void handleBorrowAimPress(const cad::geo::Vec2& pos, double zoom);

    void clearAim();
    [[nodiscard]] std::optional<cad::geo::Vec2> aimPointWorldPos() const;
    [[nodiscard]] QString aimPointLabel() const;

    // --- Preview / feedback ---
    void updateLineHover(const cad::geo::Vec2& pos, double zoom);
    void updatePointHover(const cad::geo::Vec2& pos, double zoom);
    void updateAimPreview(const cad::geo::Vec2& cursorPos, double zoom);
    void clearPreview();
    void clearHoverMarkers();

    struct TargetGeometry {
        const cad::param::Block* block = nullptr;
        const cad::param::Segment* seg = nullptr;
        cad::geo::Vec2 w1;
        cad::geo::Vec2 w2;
        cad::geo::Vec2 segDir;
        double baseAngle = 0.0;
    };

    /// Resolve the target segment's ray base direction (world space).
    /// Returns nullopt for a degenerate line. A fitted circle has a degenerate
    /// chord by construction, so its base direction is the CCW tangent at the
    /// start point (CIRCLE_TOOL_DESIGN.md §16) — the host is otherwise consumed
    /// through its curve spans.
    [[nodiscard]] static std::optional<TargetGeometry> targetGeometry(
        const cad::param::Block& block, const cad::param::Segment& seg);

    /// Highlight the target segment: curve spans (circle / Bézier) as a path,
    /// straight segments as their chord.
    void showTargetHighlight(const cad::param::Block& block,
                             const cad::param::Segment& seg, bool hover);

    [[nodiscard]] std::optional<cad::geo::Vec2> computeIntersection(
        double angleDeg, double* outT = nullptr,
        const TargetGeometry* cachedGeom = nullptr) const;

    void commitIntersection();
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
    bool   m_worldAngleMode = false;  ///< W toggles: aim by world angle vs follower angle.
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

    // --- Visuals helper ---
    IntersectionToolVisuals m_visuals;
};

} // namespace cad::tools
