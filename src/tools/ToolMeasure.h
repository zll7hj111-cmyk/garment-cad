#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

class HudItem;

/// Measure tool — quickly publish a two-point distance as a MeasureVariable.
///
/// Interaction:
///   1. Click point A (auto-snap).
///   2. Move → live preview line + distance HUD.
///   3. Click point B → a MeasureVariable (|A-B|) is created and published.
///   4. Tool stays active for the next measurement; right-click / Esc cancels
///      the in-progress first point.
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

    void updateHover(const cad::geo::Vec2& pos, double zoom);
    void updatePreview(const cad::geo::Vec2& cursorPos);
    void clearPreview();
    void resetToSelectA();

    /// Create the MeasureVariable from the two snapped points.
    void commitMeasure();

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    State m_state = State::SelectA;

    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_snapA;   ///< First point (committed).
    std::optional<SnapResult> m_hoverSnap;  ///< Current hover point.

    // Preview graphics
    QGraphicsLineItem*    m_previewLine = nullptr;  ///< Dashed A→cursor line.
    QGraphicsEllipseItem* m_markerA     = nullptr;  ///< Circle at point A.
    HudItem*              m_hud         = nullptr;  ///< Live distance readout.
};

} // namespace cad::tools
