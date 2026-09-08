#include "ToolCurveEdit.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ToolDescriptor ToolCurveEdit::describe()
{
    ToolDescriptor d;
    d.id = ToolType::CurveEdit;
    d.displayName = QString::fromUtf8("曲线(&C)");
    // M4 (TOOL_SYSTEM_AUDIT): 原用 "pen" 与智能笔同图, 菜单里两个一模一样
    // 的笔分不清。换成专用贝塞尔曲线图标 (resources/icons/bezier-curve.svg)。
    d.iconName = QStringLiteral("bezier-curve");
    d.shortcut = QKeySequence(Qt::Key_C);
    d.hintText = QString::fromUtf8("曲线：点线身加曲线点 | 拖曲线点弯曲 | 拖手柄调切线 | Ctrl加点 Shift删点 | Esc取消");
    d.factory = [] { return std::make_unique<ToolCurveEdit>(); };
    return d;
}

void ToolCurveEdit::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_state = State::Idle;
}

void ToolCurveEdit::onDeactivate()
{
    clearGraphics();
}

void ToolCurveEdit::clearGraphics()
{
    if (m_scene) {
        m_managed.clear();   // 统一释放 + 影子指针置空 (P1/L1; 原实现竟不置空指针)
    }
    m_curvePtPreview = nullptr;
    m_snapIndicator = nullptr;
    m_hLineIn = m_hLineOut = nullptr;
    m_hDotIn = m_hDotOut = nullptr;
    m_handleBlockId = QUuid();
    m_handlePointId = QUuid();
    m_dragHandle = 0;
    m_segSnap.reset();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ToolCurveEdit::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || event->button() != Qt::LeftButton) return;
    if (m_state != State::Idle) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());

    double zoom = m_scene->currentZoom();

    // 1) Grab a tangent handle of the active curve anchor (highest priority).
    if (!m_handleBlockId.isNull()) {
        const int h = handleHitTest(clickPos, zoom);
        if (h != 0) { beginHandleDrag(h, event->modifiers()); return; }
    }

    // 2) Click on a curve-relevant point (a CurveAnchor pass-point, or a curve
    //    endpoint) → drag the anchor (Shift+click deletes) or show handles.
    //    Use the curve-specific snap so an attached (overlapping) non-curve
    //    point does not shadow the curve point/endpoint the user wants to grab.
    auto snap = m_snapEngine.findCurvePointSnap(clickPos, m_paramDoc, zoom);
    if (snap) {
        const auto* blk = m_paramDoc->findBlock(snap->blockId);
        const auto* pt  = blk ? blk->findPoint(snap->pointId) : nullptr;
        if (pt && pt->constraint == cad::param::PointConstraint::CurveAnchor) {
            hideCurvePointPreview();
            if (event->modifiers() & Qt::ShiftModifier) {
                hideHandles();
                deleteCurvePoint(*snap);       // Shift+click → delete
            } else {
                beginCurveAnchorDrag(*snap);   // press+move → reshape curve
                showHandles(snap->blockId, snap->pointId);
            }
            return;
        }
        // Otherwise it is a curve endpoint (findCurvePointSnap only returns
        // curve-relevant points) → show its handles.
        hideCurvePointPreview();
        showHandles(snap->blockId, snap->pointId);
        return;
    }

    // 3) Ctrl+click on a segment body → place a curve point (straight → curve,
    //    or add another pass-point to an existing curve). Ctrl is REQUIRED for
    //    all placement (unified) to avoid accidental triggers (误触).
    auto segSnap = m_snapEngine.findSegmentSnap(
        clickPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        const auto* blk = m_paramDoc->findBlock(segSnap->blockId);
        const auto* seg = blk ? blk->findSegment(segSnap->segmentId) : nullptr;
        const bool ctrl = event->modifiers() & Qt::ControlModifier;
        if (seg && ctrl) {
            hideCurvePointPreview();
            const QUuid newPt = placeCurvePoint(*segSnap);
            if (!newPt.isNull()) {
                startAnchorDrag(segSnap->blockId, newPt);
                showHandles(segSnap->blockId, newPt);
            }
        }
        return;
    }

    // 4) Empty space → deselect (hide the active anchor's handles).
    hideHandles();
}

void ToolCurveEdit::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;
    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());

    if (m_state == State::DraggingCurvePoint) {
        dragCurveAnchorTo(cursorPos);
        updateHandleGraphics();  // keep the handles attached while the point moves
        return;
    }
    if (m_state == State::DraggingHandle) {
        dragHandleTo(cursorPos);
        return;
    }

    // Idle: curve-point placement preview.
    updateCurvePointPreview(cursorPos, event->modifiers());
}

void ToolCurveEdit::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_state == State::DraggingCurvePoint)
        endCurveAnchorDrag();
    else if (m_state == State::DraggingHandle)
        endHandleDrag();
}

void ToolCurveEdit::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_state == State::DraggingCurvePoint) cancelCurveAnchorDrag();
        else if (m_state == State::DraggingHandle) cancelHandleDrag();
    }
}

} // namespace cad::tools
