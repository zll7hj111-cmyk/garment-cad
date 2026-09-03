#include "CanvasView.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include "ElaMenu.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QScrollBar>
#include <QTransform>
#include <QGraphicsSceneMouseEvent>
#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include <QShowEvent>
#include <QFocusEvent>

#include <cmath>
#include <limits>
#include <chrono>
#include <algorithm>

#include "CanvasScene.h"
#include "BlockItem.h"
#include "InputDispatcher.h"
#include "parametric/ParamDocument.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "document/commands/BlockCommands.h"

CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , m_scene(scene)
{
    // Rendering backend: Qt's native rasterizer by DEFAULT. Software raster
    // renders this scene in tens of microseconds per full viewport — smooth
    // everywhere, including VMs / RDP / driverless machines where OpenGL
    // silently falls back to software implementations that cost tens of
    // milliseconds per repaint (measured: 18.8 ms median vs 0.05 ms raster
    // for the same 10-curve scene). Hardware GL can be opted in with
    // GCAD_ENABLE_GL=1; the probe below still falls back to raster when the
    // driver turns out to be software-backed or slow.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    if (qEnvironmentVariableIsSet("GCAD_ENABLE_GL")) {
        QSurfaceFormat glFormat;
        glFormat.setSamples(4);
        auto* glViewport = new QOpenGLWidget();
        glViewport->setFormat(glFormat);
        setViewport(glViewport);
        // With an OpenGL viewport, full-frame redraws are cheaper than
        // partial updates (no CPU blit); required for correct compositing.
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        m_useGl = true;
    }

    // Rendering quality
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);

    // No scrollbars visible (panning via middle mouse)
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Background (from style) — the pattern-paper ground. CanvasScene::setStyle
    // is the authoritative theme path and refreshes this brush on every theme
    // switch; the constructor seeds it from the scene's current style so a
    // view created after a theme change still paints the right paper.
    if (auto* cs = qobject_cast<CanvasScene*>(this->scene()))
        applyCanvasBackground(cs->style()->canvasBackground);
    else
        setBackgroundBrush(QColor(246, 243, 236));

    // Mouse tracking for coordinate display
    setMouseTracking(true);

    // Initialize scene rect to cover the viewport
    ensureSceneRect();

    // Center on origin
    centerOn(0, 0);
}

CanvasView::~CanvasView() = default;

void CanvasView::applyCanvasBackground(const QColor& c)
{
    setBackgroundBrush(c);
}

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
        && m_inputDispatcher) {
        // Convert scene coords (+Y down) to user coords (+Y up) before dispatch
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMousePress);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y));
        sceneEvent.setButton(event->button());
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_inputDispatcher->dispatchMousePress(&sceneEvent);
        event->accept();
        return;
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

    if (m_inputDispatcher) {
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseMove);
        auto up = cad::geo::Coord::toUser(scenePos);
        sceneEvent.setScenePos(QPointF(up.x, up.y)); // user coords
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_inputDispatcher->dispatchMouseMove(&sceneEvent);
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

    if (event->button() == Qt::LeftButton && m_inputDispatcher) {
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseRelease);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y)); // user coords
        sceneEvent.setButton(Qt::LeftButton);
        sceneEvent.setModifiers(event->modifiers());
        m_inputDispatcher->dispatchMouseRelease(&sceneEvent);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_inputDispatcher) {
        // Convert scene coords (+Y down) to user coords (+Y up) before dispatch
        QPointF sp = mapToScene(event->pos());
        QGraphicsSceneMouseEvent sceneEvent(QEvent::GraphicsSceneMouseDoubleClick);
        auto up = cad::geo::Coord::toUser(sp);
        sceneEvent.setScenePos(QPointF(up.x, up.y));
        sceneEvent.setButton(event->button());
        sceneEvent.setButtons(event->buttons());
        sceneEvent.setModifiers(event->modifiers());
        m_inputDispatcher->dispatchMouseDoubleClick(&sceneEvent);
        event->accept();
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    // Hold-to-show keys: N = all names, M = all lengths (M freed from the
    // measure action shortcut). Swallowed BEFORE tool dispatch; auto-repeat
    // presses keep the state already set.
    if (event->key() == Qt::Key_N || event->key() == Qt::Key_M) {
        if (!event->isAutoRepeat() && m_scene) {
            if (event->key() == Qt::Key_N)
                m_scene->setForceShowName(true);
            else
                m_scene->setForceShowLength(true);
        }
        event->accept();
        return;
    }
    if (m_inputDispatcher) {
        m_inputDispatcher->dispatchKeyPress(event);
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event)
{
    // Release the matching override (each key releases only its own state so
    // N held + L tapped then released keeps N's names shown).
    if (m_scene) {
        if (event->key() == Qt::Key_N) {
            m_scene->setForceShowName(false);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_M) {
            m_scene->setForceShowLength(false);
            event->accept();
            return;
        }
    }
    if (m_inputDispatcher) {
        m_inputDispatcher->dispatchKeyRelease(event);
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent* event)
{
    // Never leave hold-to-show stuck when focus leaves the canvas.
    if (m_scene) {
        m_scene->setForceShowName(false);
        m_scene->setForceShowLength(false);
    }
    QGraphicsView::focusOutEvent(event);
}

void CanvasView::showEvent(QShowEvent* event)
{
    QGraphicsView::showEvent(event);
    // Deferred GL probe: the QOpenGLWidget context only exists once the
    // widget is exposed. Probe on the next event-loop turn so the window
    // paints its first frame immediately instead of waiting for the probe.
    if (!m_glProbed) {
        m_glProbed = true;
        if (m_useGl)
            QTimer::singleShot(0, this, [this] { probeGlAndMaybeFallback(); });
    }
}

void CanvasView::probeGlAndMaybeFallback()
{
    if (qEnvironmentVariableIsSet("GCAD_NO_GL")) {
        fallbackToSoftware();
        return;
    }

    auto* gl = qobject_cast<QOpenGLWidget*>(viewport());
    if (!gl) return;  // already software (or a custom viewport)

    // A missing/invalid context means no usable GL driver at all.
    if (!gl->isValid() || !gl->context()) {
        qWarning("[canvas] OpenGL context unavailable — falling back to software rendering");
        fallbackToSoftware();
        return;
    }

    // Software GL implementations (Microsoft WARP/GDI Generic, Mesa llvmpipe,
    // SwiftShader, remote-desktop indirect contexts) report a software
    // renderer. Their per-frame cost is tens of milliseconds — far slower
    // than Qt's native rasterizer on the same scene, so prefer raster.
    gl->makeCurrent();
    QOpenGLFunctions* f = gl->context()->functions();
    const char* raw = f ? reinterpret_cast<const char*>(f->glGetString(GL_RENDERER))
                        : nullptr;
    gl->doneCurrent();
    const QByteArray renderer = raw ? QByteArray(raw) : QByteArray();
    qInfo("[canvas] OpenGL renderer: %s", renderer.isEmpty()
          ? "<unknown>" : renderer.constData());

    static const char* kSoftwareTokens[] = {
        "llvmpipe", "softpipe", "swiftshader", "gdi generic",
        "basic render", "basic display", "indirect", "warp",
    };
    const QByteArray lower = renderer.toLower();
    for (const char* token : kSoftwareTokens) {
        if (lower.contains(token)) {
            qWarning("[canvas] software GL renderer detected (%s) — falling back to "
                     "software rendering (GCAD_NO_GL=1 forces this)", token);
            fallbackToSoftware();
            return;
        }
    }

    // Vendor-name blacklists miss software-backed contexts that advertise a
    // real GPU (VMs commonly expose a virtual "Intel(R) Graphics"). Measure
    // the actual per-paint overhead instead: makeCurrent + clear + finish on
    // a healthy driver returns in well under a millisecond; software GL takes
    // many — and every viewport repaint pays that tax.
    gl->makeCurrent();
    f = gl->context()->functions();
    double worstMs = 0.0;
    if (f) {
        for (int i = 0; i < 3; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            gl->makeCurrent();
            f->glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            f->glClear(GL_COLOR_BUFFER_BIT);
            f->glFinish();
            gl->doneCurrent();
            const auto t1 = std::chrono::steady_clock::now();
            worstMs = std::max(worstMs,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
    }
    gl->doneCurrent();
    if (worstMs > 2.0) {
        qWarning("[canvas] GL submission cost %.1f ms/frame — falling back to "
                 "software rendering", worstMs);
        fallbackToSoftware();
    }
}

void CanvasView::fallbackToSoftware()
{
    if (!qobject_cast<QOpenGLWidget*>(viewport())) return;  // already software

    // Swap the viewport to Qt's native rasterizer. QAbstractScrollArea does
    // NOT own the viewport widget, so the old GL widget must be deleted after
    // the swap (the scene re-renders through the new viewport automatically).
    auto* oldGl = qobject_cast<QOpenGLWidget*>(viewport());
    auto* soft = new QWidget();
    setViewport(soft);
    // Partial updates are correct and cheap with the raster backend — the
    // FullViewportUpdate mode was only needed for GL compositing.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    if (oldGl) oldGl->deleteLater();
    viewport()->update();
}

bool CanvasView::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    return false;  // Tab stays with the canvas (tool shortcut, not focus move).
}

void CanvasView::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_paramDoc) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    // App-layer suppression hook (P1-6): whether a context menu should appear
    // at all is a TOOL policy (e.g. the rotate tool owns a pending target,
    // where right-click means "confirm/back out"). The canvas does not know
    // about tools, so the app installs the predicate.
    if (m_contextMenuGuard && m_contextMenuGuard())
        return;

    // Hit-test: find the closest segment under the cursor.
    const QPointF sp = mapToScene(event->pos());
    const auto userPos = cad::geo::Coord::toUser(sp);
    const double zoom = zoomFactor();
    const double tolerance = 8.0 / zoom;

    const QList<QGraphicsItem*> hits = scene()->items(sp);
    QUuid bestBlockId, bestSegId;
    double bestDist = std::numeric_limits<double>::max();

    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        auto* bi = BlockItem::containingItem(item);
        if (!bi) continue;
        const auto* blk = m_paramDoc->findBlock(bi->blockId());
        if (!blk) continue;
        for (const auto& seg : blk->segments) {
            const auto* pSp = blk->findPoint(seg.startPointId);
            const auto* pEp = blk->findPoint(seg.endPointId);
            if (!pSp || !pEp || !pSp->resolved || !pEp->resolved) continue;
            const auto w1 = blk->transform.toWorld(pSp->resolvedPos);
            const auto w2 = blk->transform.toWorld(pEp->resolvedPos);
            const double d = cad::geo::Vec2::distanceToSegment(userPos, w1, w2);
            if (d < bestDist) {
                bestDist = d;
                bestBlockId = blk->id;
                bestSegId = seg.id;
            }
        }
    }

    if (bestSegId.isNull() || bestDist > tolerance) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    // Cursor projection along the hit segment (the app layer needs it to place
    // an auxiliary point at the clicked position).
    double paramT = 0.5;
    const auto* blk = m_paramDoc->findBlock(bestBlockId);
    const auto* seg = blk ? blk->findSegment(bestSegId) : nullptr;
    if (blk && seg) {
        const auto* pSp = blk->findPoint(seg->startPointId);
        const auto* pEp = blk->findPoint(seg->endPointId);
        if (pSp && pEp && pSp->resolved && pEp->resolved) {
            const auto w1 = blk->transform.toWorld(pSp->resolvedPos);
            const auto w2 = blk->transform.toWorld(pEp->resolvedPos);
            const auto ab = w2 - w1;
            const double lenSq = ab.lengthSquared();
            paramT = (lenSq < 1e-12) ? 0.5
                   : std::clamp((userPos - w1).dot(ab) / lenSq, 0.0, 1.0);
        }
    }

    // P1-6: the menu itself (发布长度参数 / 添加辅助点 / 烘焙到操作层), the
    // dialogs, the commands and the tool/UI follow-ups live in the APP layer.
    emit segmentContextMenuRequested(cad::canvas::SegmentHit{
        bestBlockId, bestSegId, sp, paramT, event->globalPos()});
    event->accept();
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
