#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <QGraphicsItem>
#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;

namespace cad::param { class ParamDocument; class Block; struct ParamPoint; struct Segment; }

namespace cad::tools {

/// Curve Edit tool (曲线工具) — dedicated editing of curve points and tangent
/// handles, split off from the SmartPen so line drawing and curve shaping never
/// interfere (no accidental handle grabs while drawing).
///
/// Interaction (Idle):
///   - Click a straight segment body  → place a curve point (line → curve) and
///     immediately start dragging it (click-and-drag bends in one gesture).
///   - Ctrl + click a curve body      → add another curve point.
///   - Click / drag a curve point     → select it (reveal tangent handles) / move it.
///   - Drag a tangent handle          → adjust that point's tangent (manual mode).
///   - Shift + click a curve point    → delete it (reverts to a line when it was
///     the only curve point).
///   - Esc                            → cancel the in-progress drag / handle edit.
class ToolCurveEdit : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override { return "\xe6\x9b\xb2\xe7\xba\xbf"; }  // "曲线"

private:
    enum class State { Idle, DraggingCurvePoint, DraggingHandle };

    // --- Curve point placement preview ---
    /// Show the placement dot while hovering a segment body with Ctrl held
    /// (Ctrl is required for all curve-point placement, straight or curve).
    void updateCurvePointPreview(const cad::geo::Vec2& worldPos,
                                 Qt::KeyboardModifiers mods);
    void hideCurvePointPreview();

    // --- Curve point editing (place / drag / delete) ---
    /// Place a CurveAnchor pass-point on the snapped segment (promotes a
    /// straight segment to a Bézier curve). Pushes AddCurvePointCommand.
    /// Returns the new anchor's point ID (null QUuid on failure).
    [[nodiscard]] QUuid placeCurvePoint(const SegmentSnapResult& segSnap);
    void startAnchorDrag(const QUuid& blockId, const QUuid& pointId);
    void beginCurveAnchorDrag(const SnapResult& snap);
    void dragCurveAnchorTo(const cad::geo::Vec2& worldPos);
    void endCurveAnchorDrag();
    void cancelCurveAnchorDrag();
    /// Shift+click deletion (reverts to a line when it was the only pass-point).
    void deleteCurvePoint(const SnapResult& snap);

    // --- Tangent handles (切线手柄, manual Bézier handles) ---
    /// Effective tangents (in/out) for a curve anchor: stored manual tangents,
    /// or the C2 auto-solved tangents when autoTangent is set.
    void anchorTangents(const cad::param::Block& block,
                        const cad::param::ParamPoint& pt,
                        cad::geo::Vec2* tanIn, cad::geo::Vec2* tanOut) const;
    void showHandles(const QUuid& blockId, const QUuid& pointId);
    void updateHandleGraphics();
    void hideHandles();
    /// Which handle (if any) is under worldPos: 0=none, 1=in, 2=out.
    [[nodiscard]] int handleHitTest(const cad::geo::Vec2& worldPos, double zoom) const;
    void beginHandleDrag(int which);
    void dragHandleTo(const cad::geo::Vec2& worldPos);
    void endHandleDrag();
    void cancelHandleDrag();

    /// Decompose a local position into (percent, offset) relative to a
    /// segment's chord (start→end). Returns false on degenerate geometry.
    [[nodiscard]] bool chordParams(const cad::param::Block& block,
                                   const cad::param::Segment& seg,
                                   const cad::geo::Vec2& localPos,
                                   double* percent, double* offset) const;

    /// Release all transient graphics (preview dot + handles).
    void clearGraphics();

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    State m_state = State::Idle;

    SnapEngine m_snapEngine;
    std::optional<SegmentSnapResult> m_segSnap;  ///< Current segment-body hover.

    // Curve-anchor drag state (valid while DraggingCurvePoint)
    QUuid  m_dragBlockId;
    QUuid  m_dragPointId;
    double m_dragOldPercent = 0.0;
    double m_dragOldOffset  = 0.0;
    cad::geo::Vec2 m_dragLastCursor;  ///< Last cursor world pos during drag (for snap).
    /// Follow connection before the drag stroke (restored on cancel; recorded
    /// in the undo command). Dragging detaches an existing follow on the first
    /// move so the anchor can leave its target point.
    QUuid m_dragOldFollowBlockId;
    QUuid m_dragOldFollowPointId;
    cad::geo::Vec2 m_dragOldFollowOffset;

    // Tangent-handle editing state (active anchor + optional handle drag)
    QUuid  m_handleBlockId;          ///< Anchor whose handles are shown (null=none).
    QUuid  m_handlePointId;
    int    m_dragHandle = 0;         ///< 0=none, 1=dragging tangentIn, 2=dragging tangentOut.
    cad::geo::Vec2 m_handleOldTanIn;
    cad::geo::Vec2 m_handleOldTanOut;
    bool   m_handleOldAuto = true;

    // Transient graphics
    QGraphicsEllipseItem* m_curvePtPreview = nullptr;  ///< Placement dot.
    QGraphicsEllipseItem* m_snapIndicator  = nullptr;  ///< Green circle snap feedback.
    QGraphicsLineItem*    m_hLineIn  = nullptr;
    QGraphicsLineItem*    m_hLineOut = nullptr;
    QGraphicsEllipseItem* m_hDotIn   = nullptr;
    QGraphicsEllipseItem* m_hDotOut  = nullptr;
};

} // namespace cad::tools
