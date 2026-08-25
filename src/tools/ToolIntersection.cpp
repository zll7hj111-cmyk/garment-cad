#include "ToolIntersection.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QPainterPath>
#include <QPen>
#include <QUndoStack>
#include <QKeyEvent>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "document/commands/BlockCommands.h"
#include "ToolSmartPen.h"  // HudItem

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ToolIntersection::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    Tool::activate(scene, paramDoc);
    m_state = State::SelectLine;
}

void ToolIntersection::deactivate()
{
    clearPreview();
    clearHoverMarkers();
    if (m_previewRay)   { m_scene->removeItem(m_previewRay);   delete m_previewRay;   m_previewRay   = nullptr; }
    if (m_intersectDot) { m_scene->removeItem(m_intersectDot); delete m_intersectDot; m_intersectDot = nullptr; }
    if (m_noHitMarker)  { m_scene->removeItem(m_noHitMarker);  delete m_noHitMarker;  m_noHitMarker  = nullptr; }
    if (m_originMarker) { m_scene->removeItem(m_originMarker); delete m_originMarker; m_originMarker = nullptr; }
    if (m_segHighlight) { m_scene->removeItem(m_segHighlight); delete m_segHighlight; m_segHighlight = nullptr; }
    if (m_aimMarker)    { m_scene->removeItem(m_aimMarker);    delete m_aimMarker;    m_aimMarker    = nullptr; }
    Tool::deactivate();
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void ToolIntersection::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        // Backtrack one state.
        if (m_state == State::BorrowAim) {
            clearAim();
            m_state = State::AimAngle;
        } else if (m_state == State::AimAngle) {
            clearPreview();
            m_state = State::SelectPoint;
        } else if (m_state == State::SelectPoint) {
            clearHoverMarkers();
            if (m_segHighlight) m_segHighlight->setVisible(false);
            m_state = State::SelectLine;
        } else {
            resetState();
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    switch (m_state) {
    case State::SelectLine:  handleSelectLinePress(clickPos, zoom);  break;
    case State::SelectPoint: handleSelectPointPress(clickPos, zoom); break;
    case State::AimAngle:    handleAimAnglePress(clickPos, zoom);    break;
    case State::BorrowAim:   handleBorrowAimPress(clickPos, zoom);   break;
    }
}

void ToolIntersection::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    m_lastZoom = zoom;

    switch (m_state) {
    case State::SelectLine:  updateLineHover(cursorPos, zoom);  break;
    case State::SelectPoint: updatePointHover(cursorPos, zoom); break;
    case State::AimAngle:    updateAimPreview(cursorPos, zoom); break;
    case State::BorrowAim:   updateAimPreview(cursorPos, zoom); break;
    }
}

void ToolIntersection::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolIntersection::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_state == State::BorrowAim) {
            clearAim();
            m_state = State::AimAngle;
        } else if (m_state == State::AimAngle) {
            clearPreview();
            m_state = State::SelectPoint;
        } else if (m_state == State::SelectPoint) {
            clearHoverMarkers();
            if (m_segHighlight) m_segHighlight->setVisible(false);
            m_state = State::SelectLine;
        } else {
            resetState();
        }
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }

    // W toggles the aiming reference between follower angle (relative to
    // the target segment) and absolute angle (通过绝对角度反算). Only while aiming.
    // (Not Tab: Tab is a focus-navigation key and would move focus away.)
    if (event->key() == Qt::Key_W
        && (m_state == State::AimAngle || m_state == State::BorrowAim)) {
        m_worldAngleMode = !m_worldAngleMode;
        updateAimPreview(m_lastCursorPos, m_lastZoom);  // refresh HUD/preview immediately
    }
}

void ToolIntersection::keyRelease(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = false;
    }
}

// ---------------------------------------------------------------------------
// State: SelectLine
// ---------------------------------------------------------------------------

void ToolIntersection::handleSelectLinePress(const cad::geo::Vec2& pos, double zoom)
{
    // ignoreLayerFilter: the intersection tool only references the target
    // segment (no attachment is created), so grayed aux-layer geometry is a
    // legitimate pick target. Radius = snapRadius (12px), the same as point
    // picking — a segment body is a thin target, an 8px hover radius made
    // clicks miss silently.
    auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_snapEngine.snapRadius, nullptr,
        /*ignoreLayerFilter=*/true);
    if (!segSnap) return;


    m_targetBlockId   = segSnap->blockId;
    m_targetSegmentId = segSnap->segmentId;

    // Confirmed selection highlight (thicker than the hover ghost).
    ensureSegHighlight(/*hover=*/false);
    const auto* block = m_paramDoc->findBlock(m_targetBlockId);
    const auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    m_segHighlight->setLine(QLineF(cad::geo::Coord::toScene(w1),
                                   cad::geo::Coord::toScene(w2)));
    m_segHighlight->setVisible(true);

    clearHoverMarkers();
    m_state = State::SelectPoint;
    updateStepHud(pos, QString::fromUtf8("\u2461 \u70b9\u51fb\u5c04\u7ebf\u8d77\u70b9"));  // ② 点击射线起点
}

void ToolIntersection::updateLineHover(const cad::geo::Vec2& pos, double zoom)
{
    m_hoverSeg.reset();
    auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_snapEngine.snapRadius, nullptr,
        /*ignoreLayerFilter=*/true);
    if (segSnap) {
        m_hoverSeg = segSnap;
        // Live ghost highlight: the user sees exactly what a click selects.
        const auto* block = m_paramDoc->findBlock(segSnap->blockId);
        const auto* seg = block ? block->findSegment(segSnap->segmentId) : nullptr;
        if (block && seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                ensureSegHighlight(/*hover=*/true);
                cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                m_segHighlight->setLine(QLineF(cad::geo::Coord::toScene(w1),
                                               cad::geo::Coord::toScene(w2)));
                m_segHighlight->setVisible(true);
            }
        }
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->setCursor(Qt::CrossCursor);
    } else {
        if (m_segHighlight) m_segHighlight->setVisible(false);
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->unsetCursor();
    }

    // Step guidance: always visible, so a failed click is self-explanatory.
    updateStepHud(pos, QString::fromUtf8("\u2460 \u70b9\u51fb\u76ee\u6807\u7ebf\u6bb5"));  // ① 点击目标线段
}

// ---------------------------------------------------------------------------
// State: SelectPoint
// ---------------------------------------------------------------------------

void ToolIntersection::handleSelectPointPress(const cad::geo::Vec2& pos, double zoom)
{
    // ignoreLayerFilter: ray origin is referenced, not attached (see above).
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, -1.0, {},
                                      nullptr, /*ignoreLayerFilter=*/true);
    if (!snap) return;

    m_originBlockId = snap->blockId;
    m_originPointId = snap->pointId;
    m_originPos     = snap->worldPos;

    // Show origin marker.
    if (!m_originMarker) {
        constexpr double r = 5.0;
        m_originMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen pen(m_scene->style()->snapPointColor, 2.0);
        pen.setCosmetic(true);
        m_originMarker->setPen(pen);
        m_originMarker->setBrush(Qt::NoBrush);
        m_originMarker->setZValue(102.0);
        m_scene->addItem(m_originMarker);
    }
    m_originMarker->setPos(cad::geo::Coord::toScene(m_originPos));
    m_originMarker->setVisible(true);

    clearHoverMarkers();
    m_state = State::AimAngle;

    // Show the aim preview immediately (no need to wait for the next mouse
    // move) — the HUD switches from the step hint to the live angle readout.
    updateAimPreview(pos, zoom);
}

void ToolIntersection::updatePointHover(const cad::geo::Vec2& pos, double zoom)
{
    m_hoverPoint.reset();
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, -1.0, {},
                                      nullptr, /*ignoreLayerFilter=*/true);
    if (snap) {
        m_hoverPoint = snap;
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->setCursor(Qt::CrossCursor);
    } else {
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->unsetCursor();
    }

    // Step guidance: keeps telling the user what to pick next.
    updateStepHud(pos, QString::fromUtf8("\u2461 \u70b9\u51fb\u5c04\u7ebf\u8d77\u70b9"));  // ② 点击射线起点
}

// ---------------------------------------------------------------------------
// State: AimAngle
// ---------------------------------------------------------------------------

void ToolIntersection::handleAimAnglePress(const cad::geo::Vec2& pos, double zoom)
{
    // Clicking a point (other than the ray origin) borrows its direction: the
    // ray points straight at it. When the ray hits the target segment the
    // intersection is created IMMEDIATELY (one-step flow); when it misses the
    // segment the direction locks (BorrowAim) and the HUD explains why, so
    // the user can adjust or cancel with right-click/Esc.
    //
    // The click position is checked with the wider snap radius (12px, same as
    // origin picking); a slightly-off click falls back to the hover position
    // so it still borrows what the preview showed.
    if (m_scene) {
        auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom,
                                          m_snapEngine.snapRadius, m_originPointId,
                                          nullptr, /*ignoreLayerFilter=*/true);
        if (!snap)
            snap = m_snapEngine.findSnap(m_lastCursorPos, m_paramDoc, m_lastZoom,
                                         m_snapEngine.snapRadius, m_originPointId,
                                         nullptr, /*ignoreLayerFilter=*/true);
        if (snap) {
            m_aimPointId = snap->pointId;
            m_aimBlockId = snap->blockId;
            // Lock the direction first (the BorrowAim preview branch keeps
            // m_aimPointId instead of re-snapping with the hover radius).
            m_state = State::BorrowAim;
            updateAimPreview(pos, zoom);
            auto hit = computeIntersection(m_currentAngleDeg);
            if (hit)
                commitIntersection();  // one-step create
            // else: stay locked, HUD shows the missing-intersection reason.
            return;
        }
    }

    // Free-aim commit (existing behavior).
    auto hit = computeIntersection(m_currentAngleDeg);
    if (!hit) return;  // No valid intersection — ignore click.

    commitIntersection();
}

void ToolIntersection::handleBorrowAimPress(const cad::geo::Vec2& pos, double zoom)
{
    (void)pos;
    (void)zoom;

    // Commit along the LOCKED aim direction (m_currentAngleDeg was refreshed
    // by the last updateAimPreview pass).
    auto hit = computeIntersection(m_currentAngleDeg);
    if (!hit) return;  // No valid intersection — ignore click.

    commitIntersection();
}

void ToolIntersection::updateAimPreview(const cad::geo::Vec2& cursorPos, double zoom)
{
    if (!m_paramDoc) return;

    // Compute angle from origin to cursor, relative to the target segment direction.
    const auto* block = m_paramDoc->findBlock(m_targetBlockId);
    const auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    double segAngleRad = std::atan2(w2.y - w1.y, w2.x - w1.x);

    m_lastCursorPos = cursorPos;

    // Aim-point state (指向点):
    //   BorrowAim — the borrowed point is LOCKED; resolve its LIVE position
    //               (the point may have moved since the borrow click).
    //   AimAngle  — hovering a point (other than the origin) previews aiming
    //               at it; clicking it locks the borrow (handleAimAnglePress).
    std::optional<SnapResult> aimSnap;    // hover candidate (marker/HUD name)
    std::optional<cad::geo::Vec2> aimPos; // effective aim target position
    if (m_state == State::BorrowAim) {
        aimPos = aimPointWorldPos();
    } else {
        if (m_scene) {
            aimSnap = m_snapEngine.findSnap(cursorPos, m_paramDoc, zoom,
                                            m_scene->style()->hoverRadiusPx(),
                                            m_originPointId, nullptr,
                                            /*ignoreLayerFilter=*/true);
        }
        if (aimSnap) {
            m_aimPointId = aimSnap->pointId;
            m_aimBlockId = aimSnap->blockId;
            aimPos = aimSnap->worldPos;
        } else {
            m_aimPointId = QUuid();
            m_aimBlockId = QUuid();
        }
    }

    // Aim direction (world, degrees): borrowed point or free cursor aiming.
    double worldDeg;
    if (aimPos) {
        cad::geo::Vec2 toAim = *aimPos - m_originPos;
        worldDeg = std::atan2(toAim.y, toAim.x) * 180.0 / M_PI;
    } else {
        cad::geo::Vec2 toCursor = cursorPos - m_originPos;
        worldDeg = std::atan2(toCursor.y, toCursor.x) * 180.0 / M_PI;
    }
    double segAngleDeg = segAngleRad * 180.0 / M_PI;

    // Display angle depends on the aiming mode; Shift snaps in that mode
    // (never while borrowing a point — the direction must stay exact).
    double displayDeg;
    if (m_worldAngleMode) {
        // Aim by WORLD angle; Shift snaps to world 45° (easy vertical/horizontal).
        displayDeg = worldDeg;
        if (m_angleSnap && !aimPos) displayDeg = std::round(displayDeg / 45.0) * 45.0;
    } else {
        // Aim by FOLLOWER angle (relative to the target segment).
        displayDeg = worldDeg - segAngleDeg;
        if (m_angleSnap && !aimPos) displayDeg = std::round(displayDeg / 45.0) * 45.0;
    }
    while (displayDeg < 0.0) displayDeg += 360.0;
    while (displayDeg >= 360.0) displayDeg -= 360.0;

    // Storage is ALWAYS the segment-relative angle (back-calculate from world).
    double relDeg = m_worldAngleMode ? (displayDeg - segAngleDeg) : displayDeg;
    while (relDeg < 0.0) relDeg += 360.0;
    while (relDeg >= 360.0) relDeg -= 360.0;

    m_displayAngleDeg = displayDeg;
    m_currentAngleDeg = relDeg;

    // Compute intersection with the finalized angle.
    double t = 0.0;
    auto hit = computeIntersection(m_currentAngleDeg, &t);

    // --- Draw preview ray ---
    if (!m_previewRay) {
        m_previewRay = new QGraphicsLineItem();
        QPen pen(m_scene->style()->previewLineColor, 1.2);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_previewRay->setPen(pen);
        m_previewRay->setZValue(101.0);
        m_scene->addItem(m_previewRay);
    }

    if (hit) {
        m_previewRay->setLine(QLineF(cad::geo::Coord::toScene(m_originPos),
                                     cad::geo::Coord::toScene(*hit)));
        m_previewRay->setVisible(true);

        // Intersection dot.
        if (!m_intersectDot) {
            constexpr double r = 4.0;
            m_intersectDot = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            m_intersectDot->setPen(Qt::NoPen);
            m_intersectDot->setBrush(m_scene->style()->snapIndicatorColor);
            m_intersectDot->setZValue(103.0);
            m_scene->addItem(m_intersectDot);
        }
        m_intersectDot->setPos(cad::geo::Coord::toScene(*hit));
        m_intersectDot->setVisible(true);
        if (m_noHitMarker) m_noHitMarker->setVisible(false);
    } else {
        // No intersection: draw ray in the direction anyway (fixed length).
        double theta = segAngleRad + m_currentAngleDeg * M_PI / 180.0;
        cad::geo::Vec2 dir{std::cos(theta), std::sin(theta)};
        cad::geo::Vec2 rayEnd = m_originPos + dir * 200.0;  // 200mm visual length.
        m_previewRay->setLine(QLineF(cad::geo::Coord::toScene(m_originPos),
                                     cad::geo::Coord::toScene(rayEnd)));
        m_previewRay->setVisible(true);
        if (m_intersectDot) m_intersectDot->setVisible(false);

        // X marker at ray end.
        if (!m_noHitMarker) {
            constexpr double s = 5.0;
            QPainterPath cross;
            cross.moveTo(-s, -s); cross.lineTo(s, s);
            cross.moveTo(-s, s);  cross.lineTo(s, -s);
            m_noHitMarker = new QGraphicsPathItem(cross);
            QPen pen(m_scene->style()->snapIndicatorColor, 1.8);
            pen.setCosmetic(true);
            m_noHitMarker->setPen(pen);
            m_noHitMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            m_noHitMarker->setZValue(103.0);
            m_scene->addItem(m_noHitMarker);
        }
        m_noHitMarker->setPos(cad::geo::Coord::toScene(rayEnd));
        m_noHitMarker->setVisible(true);
    }

    // --- Aim-point marker (指向点标记) ---
    if (aimPos) {
        if (!m_aimMarker) {
            constexpr double r = 5.0;
            m_aimMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            QPen pen(m_scene->style()->snapPointColor, 1.6);
            pen.setCosmetic(true);
            m_aimMarker->setPen(pen);
            m_aimMarker->setBrush(Qt::NoBrush);
            m_aimMarker->setZValue(102.0);
            m_scene->addItem(m_aimMarker);
        }
        m_aimMarker->setPos(cad::geo::Coord::toScene(*aimPos));
        m_aimMarker->setVisible(true);
    } else if (m_aimMarker) {
        m_aimMarker->setVisible(false);
    }

    // --- HUD ---
    if (!m_hud) {
        m_hud = ensureHud();
    }
    if (m_hud) {
        QString text;
        if (aimPos) {
            // Aim mode: show the borrowed point (指向点).
            const QString pn = (m_state == State::BorrowAim)
                ? aimPointLabel() : aimSnap->pointName;
            text = QString::fromUtf8("\u6307\u5411\u70b9 %1 = %2\u00b0")  // 指向点 X = 90°
                .arg(pn.isEmpty() ? QString::fromUtf8("(\u70b9)") : pn,  // (点)
                     QString::number(m_displayAngleDeg, 'f', 1));
            if (m_state == State::BorrowAim)
                text += QString::fromUtf8(" | \u26a0 \u65e0\u4ea4\u70b9\uff0c\u5c04\u7ebf\u672a\u7a7f\u8fc7\u7ebf\u6bb5");  // | ⚠ 无交点，射线未穿过线段
            else
                text += QString::fromUtf8(" | \u70b9\u51fb\u521b\u5efa");  // | 点击创建
        } else {
            const QString modeLabel = m_worldAngleMode
                ? QString::fromUtf8("绝对角度")     // 绝对角度
                : QString::fromUtf8("跟随角度");   // 跟随角度
            text = QString::fromUtf8("%1 = %2\u00b0")
                .arg(modeLabel, QString::number(m_displayAngleDeg, 'f', 1));
        }
        if (hit) {
            text += QString::fromUtf8(" | t = %1").arg(QString::number(t, 'f', 3));
        } else {
            text += QString::fromUtf8(" | \u26a0 \u65e0\u4ea4\u70b9\uff08\u70b9\u51fb\u65e0\u6548\uff09");  // | ⚠ 无交点（点击无效）
        }
        m_hud->setText(text);
        QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
        m_hud->moveToPoint(cursorPos, view);
        m_hud->setVisible(true);
    }
}

// ---------------------------------------------------------------------------
// Intersection computation
// ---------------------------------------------------------------------------

std::optional<cad::geo::Vec2> ToolIntersection::computeIntersection(
    double angleDeg, double* outT) const
{
    if (!m_paramDoc) return std::nullopt;

    const auto* block = m_paramDoc->findBlock(m_targetBlockId);
    const auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return std::nullopt;

    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return std::nullopt;

    cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    cad::geo::Vec2 segDir = w2 - w1;
    double segLen = segDir.length();
    if (segLen < 1e-9) return std::nullopt;

    double baseAngle = std::atan2(segDir.y, segDir.x);
    double theta = baseAngle + angleDeg * M_PI / 180.0;
    cad::geo::Vec2 d{std::cos(theta), std::sin(theta)};

    // --- Curve target: use rayCurveIntersect ---
    if (seg->isCurve()) {
        // Frame-level Bézier cache (built once per resolve pass) — no C2
        // re-solve per aim-preview frame.
        const cad::param::CurveSpanEntry* entry = block->curveSpanEntry(seg->id);
        if (!entry || entry->spans.empty()) return std::nullopt;
        const auto& spans = entry->spans;

        // Transform ray to local coords
        cad::geo::Vec2 localOrigin = block->transform.toLocal(m_originPos);
        cad::geo::Vec2 localDir = d.rotated(-block->transform.rotation);

        auto hits = cad::geo::rayCurveIntersect(localOrigin, localDir, spans, m_bidirectional);
        if (hits.empty()) return std::nullopt;

        // Take the first hit (closest along curve parameter)
        cad::geo::Vec2 worldHit = block->transform.toWorld(hits[0].point);
        if (outT) {
            int spanCount = static_cast<int>(spans.size());
            *outT = (spanCount > 0) ? hits[0].t / spanCount : 0.0;
        }
        return worldHit;
    }

    // --- Straight-line target (existing logic) ---
    double denom = d.cross(segDir);
    if (std::abs(denom) < 1e-9) return std::nullopt;  // Parallel.

    cad::geo::Vec2 w = w1 - m_originPos;
    double s = w.cross(segDir) / denom;
    double t = w.cross(d) / denom;

    constexpr double eps = 1e-6;
    bool validT = (t >= -eps && t <= 1.0 + eps);
    bool validS = m_bidirectional ? true : (s >= -eps);
    if (!validT || !validS) return std::nullopt;

    if (outT) *outT = t;
    return m_originPos + d * s;
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

void ToolIntersection::commitIntersection()
{
    if (!m_paramDoc) return;

    auto* block = m_paramDoc->findBlock(m_targetBlockId);
    auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return;

    // Build the Intersection ParamPoint.
    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Intersection;
    pt.serial = m_paramDoc->newPointSerial();
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;

    // Ray origin.
    pt.refPointA = m_originPointId;

    // Target segment.
    pt.hostSegmentId = m_targetSegmentId;

    // Angle.
    pt.interUseWorldAngle = m_worldAngleMode;
    pt.interAngle = m_worldAngleMode ? m_displayAngleDeg : m_currentAngleDeg;
    pt.interBidirectional = m_bidirectional;
    // Aim point (指向点): the ray direction follows this point parametrically;
    // the stored angle stays as the fallback if the point is deleted.
    pt.interAimPointId = m_aimPointId;

    // Add to the block via undo command (same pattern as AddAuxPointCommand).
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, m_targetBlockId, m_targetSegmentId, pt));
    } else {
        block->addPoint(pt);
        seg->auxPointIds.push_back(pt.id);
        m_paramDoc->resolveAll();
    }

    // Reset for next intersection (stay in tool).
    resetState();

    // Success feedback: the created point is a small green dot (0.8mm), easy
    // to miss — flash a confirmation in the HUD. The next mouse-move step
    // hint (① 点击目标线段) naturally replaces it.
    if (m_hud && m_scene) {
        m_hud->setText(QString::fromUtf8("\u2713 \u4ea4\u70b9\u5df2\u521b\u5efa"));  // ✓ 交点已创建
        QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
        m_hud->moveToPoint(m_lastCursorPos, view);
        m_hud->setVisible(true);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ToolIntersection::updateStepHud(const cad::geo::Vec2& cursorPos, const QString& text)
{
    if (!m_scene) return;
    if (!m_hud) {
        m_hud = ensureHud();
    }
    m_hud->setText(text);
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_hud->moveToPoint(cursorPos, view);
    m_hud->setVisible(true);
}

void ToolIntersection::ensureSegHighlight(bool hover)
{
    if (m_segHighlight) {
        QPen pen(m_scene->style()->previewLineColor, hover ? 2.0 : 2.5);
        pen.setCosmetic(true);
        m_segHighlight->setPen(pen);
        return;
    }
    m_segHighlight = new QGraphicsLineItem();
    QPen pen(m_scene->style()->previewLineColor, hover ? 2.0 : 2.5);
    pen.setCosmetic(true);
    m_segHighlight->setPen(pen);
    m_segHighlight->setZValue(100.0);
    m_scene->addItem(m_segHighlight);
}

void ToolIntersection::clearAim()
{
    m_aimPointId = QUuid();
    m_aimBlockId = QUuid();
    if (m_aimMarker) m_aimMarker->setVisible(false);
}

std::optional<cad::geo::Vec2> ToolIntersection::aimPointWorldPos() const
{
    if (m_aimPointId.isNull() || !m_paramDoc) return std::nullopt;
    for (const auto& b : m_paramDoc->blocks()) {
        const auto* p = b.findPoint(m_aimPointId);
        if (p && p->resolved) return b.transform.toWorld(p->resolvedPos);
    }
    return std::nullopt;
}

QString ToolIntersection::aimPointLabel() const
{
    if (m_aimPointId.isNull() || !m_paramDoc) return QString();
    for (const auto& b : m_paramDoc->blocks()) {
        const auto* p = b.findPoint(m_aimPointId);
        if (p) return p->name;
    }
    return QString();
}

void ToolIntersection::clearPreview()
{
    if (m_previewRay)   m_previewRay->setVisible(false);
    if (m_intersectDot) m_intersectDot->setVisible(false);
    if (m_noHitMarker)  m_noHitMarker->setVisible(false);
    if (m_originMarker) m_originMarker->setVisible(false);
    if (m_aimMarker)    m_aimMarker->setVisible(false);
    if (m_hud)          m_hud->setVisible(false);
}

void ToolIntersection::clearHoverMarkers()
{
    m_hoverPoint.reset();
    m_hoverSeg.reset();
    if (!m_scene) return;
    if (!m_scene->views().isEmpty())
        m_scene->views().first()->unsetCursor();
}

void ToolIntersection::resetState()
{
    clearPreview();
    clearHoverMarkers();
    if (m_segHighlight) m_segHighlight->setVisible(false);
    m_targetBlockId   = QUuid();
    m_targetSegmentId = QUuid();
    m_originBlockId   = QUuid();
    m_originPointId   = QUuid();
    m_aimPointId      = QUuid();
    m_aimBlockId      = QUuid();
    m_currentAngleDeg = 90.0;
    m_displayAngleDeg = 90.0;
    m_worldAngleMode  = false;
    m_state = State::SelectLine;
}

} // namespace cad::tools
