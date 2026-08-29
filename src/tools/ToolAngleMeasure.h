#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <optional>

class QGraphicsLineItem;
#include "canvas/ManagedItems.h"

namespace cad::param { class ParamDocument; }

class HudItem;

namespace cad::tools {

/// Angle-measure tool — publish the relative (directed) angle between two
/// segments as an AngleMeasureVariable.
///
/// The angle semantics match the follower angle (跟随角度): the directed
/// angle from the FIRST picked line's world direction (start→end) to the
/// SECOND picked line's, normalized to (-180, 180].
///
/// Interaction:
///   1. Click line A (auto-snap to segment body) — it becomes the reference.
///   2. Move → live highlight of the hovered line + angle HUD.
///   3. Click line B → an AngleMeasureVariable (A→B) is created and published.
///   4. Tool stays active for the next measurement; right-click / Esc cancels
///      the in-progress first line.
class ToolAngleMeasure : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return "\xe8\xa7\x92\xe5\xba\xa6\xe6\xb5\x8b\xe9\x87\x8f"; }  // "角度测量"

private:
    enum class State { SelectA, SelectB };

    void updateHover(const cad::geo::Vec2& pos, double zoom);
    void updatePreview(const cad::geo::Vec2& cursorPos);
    void clearPreview();
    void resetToSelectA();

    /// World direction (radians, start→end) of a segment.
    [[nodiscard]] double segmentWorldDir(const QUuid& blockId,
                                         const QUuid& segmentId) const;
    /// World endpoints of a segment (false if unresolved/missing).
    [[nodiscard]] bool segmentWorldEndpoints(const QUuid& blockId,
                                             const QUuid& segmentId,
                                             cad::geo::Vec2& outA,
                                             cad::geo::Vec2& outB) const;
    /// Directed angle (degrees, (-180, 180]) from segment A to segment B.
    [[nodiscard]] double angleBetween(const SegmentSnapResult& a,
                                      const SegmentSnapResult& b) const;

    /// Create the AngleMeasureVariable from the two snapped segments.
    void commitAngleMeasure();

    State m_state = State::SelectA;

    SnapEngine m_snapEngine;
    std::optional<SegmentSnapResult> m_snapA;      ///< First line (committed).
    std::optional<SegmentSnapResult> m_hoverSnap;  ///< Current hovered line.

    // Preview graphics
    QGraphicsLineItem* m_highlightA = nullptr;  ///< Overlay on line A (amber).
    QGraphicsLineItem* m_highlightB = nullptr;  ///< Overlay on hovered line B (blue).
    /// 临时图元统一登记 (deactivate 统一释放 + 影子置空, TOOL_SYSTEM_AUDIT P1/L1)。
    ManagedItems m_managed;
};

} // namespace cad::tools
