#pragma once

#include <QPoint>
#include <QPointF>
#include <QUuid>

class QGraphicsSceneMouseEvent;
class QKeyEvent;

namespace cad::canvas {

/// What the canvas hit under the cursor for a segment context menu. Reported
/// as a signal so the MENU / DIALOG / policy lives in the app layer (P1-6) —
/// the canvas only reports the hit, it does not decide what to do with it.
struct SegmentHit {
    QUuid  blockId;    ///< Hit block (null when nothing was hit).
    QUuid  segmentId;  ///< Closest segment of that block.
    QPointF scenePos;  ///< Cursor position in scene coordinates (+Y down).
    double paramT = 0.5;  ///< Cursor projection along the segment [0,1].
    QPoint globalPos;  ///< Cursor in global coordinates (menu placement).
};

/// Abstract input sink implemented by the tools layer (cad::tools::ToolManager).
///
/// P1-6 (2026-12): the canvas forwards pointer/keyboard input to the active
/// tool, but it must NOT include tools/ headers — the layering is one-way
/// (`gcad_geometry` → `gcad_parametric` → `gcad_canvas`/`gcad_document` →
/// `gcad_ui` → `gcad_tools` → `gcad_app`), and an include from canvas to tools
/// is an upward edge no compiler flag catches. Declaring the sink HERE inverts
/// the dependency: the canvas knows only this interface, the tools layer
/// implements it (dependency inversion instead of a layering violation).
///
/// Coordinates: the view converts to user space (+Y up) before dispatching, so
/// implementations never see scene coordinates.
class InputDispatcher
{
public:
    virtual ~InputDispatcher() = default;

    virtual void dispatchMousePress(QGraphicsSceneMouseEvent* event) = 0;
    virtual void dispatchMouseMove(QGraphicsSceneMouseEvent* event) = 0;
    virtual void dispatchMouseRelease(QGraphicsSceneMouseEvent* event) = 0;
    virtual void dispatchMouseDoubleClick(QGraphicsSceneMouseEvent* event) = 0;
    virtual void dispatchKeyPress(QKeyEvent* event) = 0;
    virtual void dispatchKeyRelease(QKeyEvent* event) = 0;
};

} // namespace cad::canvas
