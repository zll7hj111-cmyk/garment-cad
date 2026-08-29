#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <QPointer>
#include <optional>

class QGraphicsPathItem;
class QGraphicsEllipseItem;

#include "canvas/ManagedItems.h"

namespace cad::param { class ParamDocument; struct ParamPoint; }

// P2-4: these are QWidget components living in src/ui/ (cad::ui) now —
// tools/ keeps only gestures and state machines.
namespace cad::ui { class QuickAuxDialog; }

namespace cad::tools {
/// Break tool — splits a segment at an auxiliary point into two independent
/// blocks connected by an Attachment (follower angle 0°).
///
/// Interaction:
///   - Hover a breakable aux point → green circle highlight + scissors cursor.
///   - Click a breakable aux point → execute break immediately.
///   - Hover a segment body → X marker (same as smart pen).
///   - Click a segment body → open QuickAuxDialog to create a point, then
///     auto-break at the new point.
///   - Click an aux point WITH offset → ignored (not on the segment line).
class ToolBreak : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"打断"); }

private:
    /// Update hover markers: breakable-point circle or segment X.
    void updateHover(const cad::geo::Vec2& worldPos);
    void hideMarkers();

    /// Execute the break at the given aux point.
    void executeBreak(const QUuid& blockId, const QUuid& segmentId,
                      const QUuid& auxPointId);

    /// Open the quick-aux dialog for creating a point on a segment body,
    /// then auto-break at the created point.
    void openAuxDialogForBreak(const SegmentSnapResult& segSnap);
    void onAuxDialogAccepted(const cad::param::ParamPoint& pt);

    /// Check if a point is breakable (Interpolated, no offset, on a Line seg).
    [[nodiscard]] bool isBreakable(const QUuid& blockId,
                                   const QUuid& pointId) const;

    SnapEngine m_snapEngine;

    // Hover state
    std::optional<SnapResult> m_hoverPoint;
    std::optional<SegmentSnapResult> m_hoverSeg;
    bool m_hoverBreakable = false;

    // Graphics markers
    QGraphicsEllipseItem* m_breakCircle = nullptr;  ///< Green circle on breakable point.
    /// 临时图元统一登记 (deactivate 统一释放, TOOL_SYSTEM_AUDIT P1/L1)。
    ManagedItems m_managed;
    QGraphicsPathItem*    m_segMarker   = nullptr;  ///< X marker on segment body.

    // Non-modal quick-aux dialog
    QPointer<cad::ui::QuickAuxDialog> m_auxDialog;
    SegmentSnapResult m_auxDialogSegSnap;
};

} // namespace cad::tools
