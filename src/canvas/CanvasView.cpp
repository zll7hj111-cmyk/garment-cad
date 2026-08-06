#include "CanvasView.h"

#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
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

#include <cmath>
#include <limits>
#include <chrono>
#include <algorithm>

#include "CanvasScene.h"
#include "BlockItem.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "tools/QuickAuxDialog.h"
#include "parametric/ParamDocument.h"
#include "parametric/LinkedVariable.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/GroupCommands.h"
#include "document/commands/DocumentCommands.h"

CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
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

    // Background (from style)
    setBackgroundBrush(QColor(250, 250, 250));

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

    // ── Group section (成组): right-clicking a GROUPED line offers group
    // operations directly — no need to select the whole group first.
    QUuid hitGroupId;
    if (!bestBlockId.isNull())
        hitGroupId = m_paramDoc->groupOfBlock(bestBlockId);

    QMenu menu(this);
    QAction* groupSelectAct = nullptr;
    QAction* groupDissolveAct = nullptr;
    QAction* groupRenameAct = nullptr;
    if (!hitGroupId.isNull()) {
        groupSelectAct = menu.addAction(QString::fromUtf8("\xe9\x80\x89\xe4\xb8\xad\xe6\x95\xb4\xe7\xbb\x84"));  // 选中整组
        groupDissolveAct = menu.addAction(QString::fromUtf8("\xe8\xa7\xa3\xe6\x95\xa3\xe7\xbb\x84"));  // 解散组
        groupRenameAct = menu.addAction(QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"));  // 重命名组
        menu.addSeparator();
    }

    // Check if already published.
    const bool alreadyPublished =
        m_paramDoc->findLinkedBySource(bestBlockId, bestSegId) != nullptr;

    QAction* publishAction = menu.addAction(QStringLiteral("发布长度参数"));
    publishAction->setEnabled(!alreadyPublished);
    if (alreadyPublished)
        publishAction->setText(QStringLiteral("已发布长度参数"));

    // --- 添加辅助点 ---
    QAction* auxPointAction = menu.addAction(QStringLiteral("添加辅助点"));
    if (!hitGroupId.isNull()) {
        // Structural change on a group member — blocked (保护守卫).
        auxPointAction->setEnabled(false);
        auxPointAction->setText(
            QString::fromUtf8("\xe6\xb7\xbb\xe5\x8a\xa0\xe8\xbe\x85\xe5\x8a\xa9\xe7\x82\xb9"
                              "\xef\xbc\x88\xe7\xbb\x84\xe5\x86\x85\xe7\xba\xbf\xe6\xae\xb5\xe4\xb8\x8d\xe5\x8f\xaf\xef\xbc\x89"));  // 添加辅助点（组内线段不可）
    }

    // --- 烘焙到操作层 (measure line on the aux layer only) ---
    // The hit segment owns a MeasureVariable AND sits on the auxiliary layer
    // → offer a COPY onto any working layer (烘焙语义=复制而非移动: the source
    // line stays as the measurement owner).
    if (m_paramDoc->findMeasureByOwner(bestBlockId)) {
        const auto* hitBlock = m_paramDoc->findBlock(bestBlockId);
        if (hitBlock && m_paramDoc->isAuxLayer(hitBlock->layer)) {
            QMenu* bakeMenu = menu.addMenu(QStringLiteral("烘焙到操作层"));
            const auto& layerList = m_paramDoc->layers();
            for (int i = 0; i < static_cast<int>(layerList.size()); ++i) {
                if (m_paramDoc->isAuxLayer(i)) continue;   // skip the aux layer
                QAction* act = bakeMenu->addAction(layerList[static_cast<size_t>(i)].name);
                act->setProperty("bakeTargetLayer", i);
            }
        }
    }

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == groupSelectAct) {
        // 选中整组: activate the selection tool with the whole group confirmed.
        const QList<QUuid> members = m_paramDoc->blocksInGroup(hitGroupId);
        if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                m_toolManager->activeTool()))
            ts->selectBlocksExternally(members);
    } else if (chosen == groupDissolveAct) {
        if (auto* stack = m_paramDoc->undoStack())
            stack->push(new cad::cmd::UngroupCommand(m_paramDoc, hitGroupId));
    } else if (chosen == groupRenameAct) {
        QString current;
        if (const auto* g = m_paramDoc->findGroup(hitGroupId))
            current = g->name;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this,
            QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"),   // 重命名组
            QString::fromUtf8("\xe7\xbb\x84\xe5\x90\x8d\xe7\xa7\xb0"),               // 组名称
            QLineEdit::Normal, current, &ok);
        if (ok)
            m_paramDoc->setGroupName(hitGroupId, name.trimmed());
    } else if (chosen == publishAction && !alreadyPublished) {
        publishLengthAt(sp);
    } else if (chosen == auxPointAction) {
        // Compute projection parameter t on the segment.
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
                const double t = (lenSq < 1e-12) ? 0.5
                    : std::clamp((userPos - w1).dot(ab) / lenSq, 0.0, 1.0);

                // Prepare the auxiliary point (same as ToolSmartPen::openAuxDialog).
                cad::param::ParamPoint pt;
                pt.constraint = cad::param::PointConstraint::Interpolated;
                pt.hostSegmentId = seg->id;
                pt.isAuxiliary = true;
                pt.visible = true;
                pt.showName = false;
                pt.interpPercent = t;
                pt.serial = m_paramDoc->newPointSerial();

                cad::tools::QuickAuxDialog dlg(pt, pSp, pEp, this);
                if (dlg.exec() == QDialog::Accepted) {
                    auto* stack = m_paramDoc->undoStack();
                    if (stack)
                        stack->push(new cad::cmd::AddAuxPointCommand(
                            m_paramDoc, bestBlockId, bestSegId, dlg.point()));
                    else {
                        auto* b = m_paramDoc->findBlock(bestBlockId);
                        auto* s = b ? b->findSegment(bestSegId) : nullptr;
                        if (b && s) {
                            b->addPoint(dlg.point());
                            s->auxPointIds.push_back(dlg.point().id);
                            m_paramDoc->resolveAll();
                        }
                    }
                }
            }
        }
    } else if (chosen && chosen->property("bakeTargetLayer").isValid()) {
        // 烘焙到操作层: bake a COPY of the measure line onto the chosen
        // working layer, then switch to that layer and select the new line.
        const int targetLayer = chosen->property("bakeTargetLayer").toInt();
        auto* bakeCmd = new cad::cmd::BakeMeasureCopyCommand(
            m_paramDoc, bestBlockId, targetLayer);
        if (!bakeCmd->isValid()) {
            delete bakeCmd;
        } else {
            const QUuid bakedId = bakeCmd->newBlockId();
            if (auto* stack = m_paramDoc->undoStack()) {
                stack->push(bakeCmd);
            } else {
                bakeCmd->redo();
                delete bakeCmd;
            }
            m_paramDoc->setActiveLayer(targetLayer);
            if (m_toolManager) {
                m_toolManager->switchTool(cad::tools::ToolType::Select);
                if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                        m_toolManager->activeTool()))
                    ts->selectBlocksExternally(QList<QUuid>{bakedId});
            }
            if (auto* cs = qobject_cast<CanvasScene*>(scene()))
                cs->showToast(QStringLiteral("已烘焙到操作层「%1」").arg(chosen->text()));
        }
    }

    event->accept();
}

void CanvasView::publishLengthAt(const QPointF& scenePos)
{
    if (!m_paramDoc) return;

    const auto userPos = cad::geo::Coord::toUser(scenePos);
    const double zoom = zoomFactor();
    const double tolerance = 8.0 / zoom;

    const QList<QGraphicsItem*> hits = scene()->items(scenePos);
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

    if (bestSegId.isNull() || bestDist > tolerance) return;

    // Build the LinkedVariable via the shared factory.
    const auto* blk = m_paramDoc->findBlock(bestBlockId);
    const auto* seg = blk ? blk->findSegment(bestSegId) : nullptr;
    if (!blk || !seg) return;
    cad::param::LinkedVariable lv = cad::param::LinkedVariable::fromSegment(*blk, *seg);

    // Push via undo stack.
    auto* stack = m_paramDoc->undoStack();
    if (stack)
        stack->push(new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    else
        m_paramDoc->addLinked(lv);
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
