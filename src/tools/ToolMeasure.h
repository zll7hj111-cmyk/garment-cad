#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"
#include "parametric/MeasureVariable.h"  // MeasureKind

#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

class HudItem;

/// Measure tool — publish a two-point measurement as a MeasureVariable.
///
/// Interaction:
///   1. Click point A (auto-snap).
///   2. Move → live preview line + measurement HUD.
///   3. Click point B → a MeasureVariable is created and published.
///   4. Tool stays active for the next measurement; right-click / Esc cancels
///      the in-progress first point.
///
/// W cycles the measurement mode: 距离 → 水平 → 垂直 (projection on the world
/// axes). Horizontal/Vertical refuse to commit when the two points coincide
/// on the measured axis (dx ≈ 0 / dy ≈ 0) — the second click is ignored and
/// the user is prompted to pick another point or switch mode.
class ToolMeasure : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override
    { return "\xe6\xb5\x8b\xe9\x87\x8f"; }  // "测量"

private:
    enum class State { SelectA, SelectB };

    /// Cycle 距离 → 水平 → 垂直 → 距离 (W key).
    void cycleKind();

    /// Mode name + pending-action hint shown in the HUD before point A.
    [[nodiscard]] QString modeHint() const;

    /// Value (mm) between the two world positions for the current kind.
    [[nodiscard]] double spanValue(const cad::geo::Vec2& a,
                                   const cad::geo::Vec2& b) const;

    /// True when the current kind cannot meaningfully measure @p a and @p b
    /// (horizontal/vertical with the two points coincident on that axis).
    [[nodiscard]] bool axisCoincident(const cad::geo::Vec2& a,
                                      const cad::geo::Vec2& b) const;

    void updateHover(const cad::geo::Vec2& pos, double zoom);
    void updatePreview(const cad::geo::Vec2& cursorPos);
    void clearPreview();
    void resetToSelectA();

    /// Create the MeasureVariable from the two snapped points.
    void commitMeasure();

    State m_state = State::SelectA;
    cad::param::MeasureKind m_kind = cad::param::MeasureKind::Distance;
    cad::geo::Vec2 m_lastCursor;  ///< Last mouse position (W-refresh of preview).

    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_snapA;   ///< First point (committed).
    std::optional<SnapResult> m_hoverSnap;  ///< Current hover point.

    // Preview graphics
    QGraphicsLineItem*    m_previewLine = nullptr;  ///< Dashed A→cursor line.
    QGraphicsEllipseItem* m_markerA     = nullptr;  ///< Circle at point A.
};

} // namespace cad::tools
