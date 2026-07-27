#include "CanvasView.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTransform>
#include <QGraphicsSceneMouseEvent>

#include "CanvasScene.h"
#include "tools/ToolManager.h"
#include "geometry/Units.h"

CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    // Rendering quality
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);

    // No scrollbars visible (panning via middle mouse)
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Background
    setBackgroundBrush(QColor(255, 255, 255));

    // Mouse tracking for coordinate display
    setMouseTracking(true);

    // Initialize scene rect to cover the viewport
    ensureSceneRect();

    // Center on origin
    centerOn(0, 0);
}

CanvasView::~CanvasView() = default;

double CanvasView::zoomFactor() const
{
    // m11 is the X-axis scale factor (always positive; no Y-flip is applied).
    return transform().m11();
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    // Zoom centered on mouse cursor position
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    double currentZoom = zoomFactor();
    double factor = (event->angleDelta().y() > 0) ? ZOOM_FACTOR_STEP
                                                   : (1.0 / ZOOM_FACTOR_STEP);

    // Clamp zoom level
    double newZoom = currentZoom * factor;
    if (newZoom < ZOOM_MIN || newZoom > ZOOM_MAX) {
        event->accept();
        return;
    }

    scale(factor, factor);
    emitZoomChanged();
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        // Start panning
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if ((event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
        && m_toolManager) {
        // Convert scene coords (+Y down) to user coords (+Y up) before dispatch
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMousePress);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y));
        sceneEvent.setButton(event->button());
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_toolManager->dispatchMousePress(&sceneEvent);
    }

    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    // Always emit user coordinates (+Y up) for status bar
    QPointF scenePos = mapToScene(event->pos());
    auto userPos = cad::geo::Coord::toUser(scenePos);
    emit mouseScenePosChanged(userPos.x, userPos.y);

    if (m_panning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        // Adjust scrollbars to pan the view
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() - delta.y());

        // Expand scene rect if panning near boundaries
        ensureSceneRect();

        event->accept();
        return;
    }

    if (m_toolManager) {
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseMove);
        auto up = cad::geo::Coord::toUser(scenePos);
        sceneEvent.setScenePos(QPointF(up.x, up.y)); // user coords
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_toolManager->dispatchMouseMove(&sceneEvent);
    }

    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_toolManager) {
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseRelease);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y)); // user coords
        sceneEvent.setButton(Qt::LeftButton);
        sceneEvent.setModifiers(event->modifiers());
        m_toolManager->dispatchMouseRelease(&sceneEvent);
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_toolManager) {
        // Convert scene coords (+Y down) to user coords (+Y up) before dispatch
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseDoubleClick);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y));
        sceneEvent.setButton(event->button());
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_toolManager->dispatchMouseDoubleClick(&sceneEvent);
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    if (m_toolManager) {
        m_toolManager->dispatchKeyPress(event);
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event)
{
    if (m_toolManager) {
        m_toolManager->dispatchKeyRelease(event);
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::emitZoomChanged()
{
    emit zoomFactorChanged(zoomFactor());
}

void CanvasView::ensureSceneRect()
{
    // Get the visible area in scene coordinates with a generous margin
    QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    double margin = qMax(visible.width(), visible.height()) * 2.0;
    QRectF needed = visible.adjusted(-margin, -margin, margin, margin);

    // Clamp to hard boundary (±SCENE_BOUND from origin)
    QRectF bound(-SCENE_BOUND, -SCENE_BOUND,
                 SCENE_BOUND * 2.0, SCENE_BOUND * 2.0);
    needed = needed.intersected(bound);

    // Only expand, never shrink the scene rect
    QRectF current = scene()->sceneRect();
    if (!current.contains(needed)) {
        scene()->setSceneRect(current.united(needed).intersected(bound));
    }
}
