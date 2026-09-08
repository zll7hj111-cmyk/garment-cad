#include "ToolIntersection.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "document/commands/BlockCommands.h"
#include "geometry/RayCast.h"
#include "tools/IntersectionAngleAim.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ToolDescriptor ToolIntersection::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Intersection;
    d.displayName = QString::fromUtf8("交点(&I)");
    d.iconName = QStringLiteral("crosshair");
    d.shortcut = QKeySequence(Qt::Key_I);
    d.hintText = modeIndicatorFor(false, State::SelectLine)
                     .hint(reinterpret_cast<const char*>(u8"交点"));
    d.factory = [] { return std::make_unique<ToolIntersection>(); };
    return d;
}

void ToolIntersection::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_visuals.setScene(m_scene);
    resetState();
}

void ToolIntersection::onDeactivate()
{
    clearPreview();
    clearHoverMarkers();
    m_visuals.clearAll();
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void ToolIntersection::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        if (m_state == State::BorrowAim) {
            clearAim();
            setState(State::AimAngle);
        } else if (m_state == State::AimAngle) {
            clearPreview();
            setState(State::SelectPoint);
        } else if (m_state == State::SelectPoint) {
            clearHoverMarkers();
            m_visuals.hideSegHighlight();
            setState(State::SelectLine);
        } else {
            resetState();
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());
    double zoom = m_scene->currentZoom();

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
    double zoom = m_scene->currentZoom();
    m_lastZoom = zoom;

    switch (m_state) {
    case State::SelectLine:  updateLineHover(cursorPos, zoom);  break;
    case State::SelectPoint: updatePointHover(cursorPos, zoom); break;
    case State::AimAngle:    updateAimPreview(cursorPos, zoom); reportHoverTarget(QUuid(), QUuid()); break;
    case State::BorrowAim:   updateAimPreview(cursorPos, zoom); reportHoverTarget(QUuid(), QUuid()); break;
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
            setState(State::AimAngle);
        } else if (m_state == State::AimAngle) {
            clearPreview();
            setState(State::SelectPoint);
        } else if (m_state == State::SelectPoint) {
            clearHoverMarkers();
            m_visuals.hideSegHighlight();
            setState(State::SelectLine);
        } else {
            resetState();
        }
        return;
    }

    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }

    if (event->key() == Qt::Key_W
        && (m_state == State::AimAngle || m_state == State::BorrowAim)) {
        m_worldAngleMode = !m_worldAngleMode;
        announceModeChange();
        updateAimPreview(m_lastCursorPos, m_lastZoom);
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
    auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_snapEngine.snapRadius, nullptr,
        /*ignoreLayerFilter=*/true);
    if (!segSnap) return;

    m_targetBlockId   = segSnap->blockId;
    m_targetSegmentId = segSnap->segmentId;

    const auto* block = m_paramDoc->findBlock(m_targetBlockId);
    const auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    m_visuals.showSegHighlight(w1, w2, /*hover=*/false);

    clearHoverMarkers();
    setState(State::SelectPoint);
    m_visuals.updateStepHud(ensureHud(), pos, QString::fromUtf8("② 点击射线起点"));
}

void ToolIntersection::updateLineHover(const cad::geo::Vec2& pos, double zoom)
{
    m_hoverSeg.reset();
    auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_snapEngine.snapRadius, nullptr,
        /*ignoreLayerFilter=*/true);
    if (segSnap) {
        m_hoverSeg = segSnap;
        const auto* block = m_paramDoc->findBlock(segSnap->blockId);
        const auto* seg = block ? block->findSegment(segSnap->segmentId) : nullptr;
        if (block && seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                m_visuals.showSegHighlight(w1, w2, /*hover=*/true);
            }
        }
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->setCursor(Qt::CrossCursor);
    } else {
        m_visuals.hideSegHighlight();
        if (!m_scene->views().isEmpty())
            m_scene->views().first()->unsetCursor();
    }

    reportHoverTarget(m_hoverSeg ? m_hoverSeg->blockId : QUuid(),
                      m_hoverSeg ? m_hoverSeg->segmentId : QUuid());

    m_visuals.updateStepHud(ensureHud(), pos, QString::fromUtf8("① 点击目标线段"));
}

// ---------------------------------------------------------------------------
// State: SelectPoint
// ---------------------------------------------------------------------------

void ToolIntersection::handleSelectPointPress(const cad::geo::Vec2& pos, double zoom)
{
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, -1.0, {},
                                      nullptr, /*ignoreLayerFilter=*/true);
    if (!snap) return;

    m_originBlockId = snap->blockId;
    m_originPointId = snap->pointId;
    m_originPos     = snap->worldPos;

    m_visuals.showOriginMarker(m_originPos);

    clearHoverMarkers();
    setState(State::AimAngle);

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

    QUuid hoverSeg;
    if (m_hoverPoint) {
        if (const auto* blk = m_paramDoc->findBlock(m_hoverPoint->blockId))
            hoverSeg = blk->exitSegmentAtPoint(m_hoverPoint->pointId);
    }
    reportHoverTarget(m_hoverPoint && !hoverSeg.isNull()
                          ? m_hoverPoint->blockId : QUuid(), hoverSeg);

    m_visuals.updateStepHud(ensureHud(), pos, QString::fromUtf8("② 点击射线起点"));
}

// ---------------------------------------------------------------------------
// State: AimAngle / BorrowAim
// ---------------------------------------------------------------------------

void ToolIntersection::handleAimAnglePress(const cad::geo::Vec2& pos, double zoom)
{
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
            setState(State::BorrowAim);
            updateAimPreview(pos, zoom);
            auto hit = computeIntersection(m_currentAngleDeg);
            if (hit)
                commitIntersection();
            return;
        }
    }

    auto hit = computeIntersection(m_currentAngleDeg);
    if (!hit) return;

    commitIntersection();
}

void ToolIntersection::handleBorrowAimPress(const cad::geo::Vec2& pos, double zoom)
{
    (void)pos;
    (void)zoom;

    auto hit = computeIntersection(m_currentAngleDeg);
    if (!hit) return;

    commitIntersection();
}

void ToolIntersection::updateAimPreview(const cad::geo::Vec2& cursorPos, double zoom)
{
    if (!m_paramDoc) return;

    const auto* block = m_paramDoc->findBlock(m_targetBlockId);
    const auto* seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
    cad::geo::Vec2 segDir = w2 - w1;
    if (segDir.length() < 1e-9) return;
    double segAngleRad = std::atan2(segDir.y, segDir.x);
    TargetGeometry targetGeom{block, seg, w1, w2, segDir, segAngleRad};

    m_lastCursorPos = cursorPos;

    std::optional<SnapResult> aimSnap;
    std::optional<cad::geo::Vec2> aimPos;
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

    auto angles = computeAimAngles(m_originPos, cursorPos, aimPos,
                                   segAngleRad, m_worldAngleMode, m_angleSnap);
    m_displayAngleDeg = angles.displayDeg;
    m_currentAngleDeg = angles.storageDeg;

    double t = 0.0;
    auto hit = computeIntersection(m_currentAngleDeg, &t, &targetGeom);

    double theta = segAngleRad + m_currentAngleDeg * M_PI / 180.0;
    m_visuals.showRayAndHit(m_originPos, hit, theta);

    if (aimPos) {
        m_visuals.showAimMarker(*aimPos);
    } else {
        m_visuals.hideAimMarker();
    }

    const QString aimLabel = (m_state == State::BorrowAim)
        ? aimPointLabel()
        : (aimSnap ? aimSnap->pointName : QString());
    m_visuals.updateAimHud(ensureHud(), cursorPos, aimPos.has_value(),
                           m_state == State::BorrowAim, aimLabel,
                           m_displayAngleDeg, m_worldAngleMode,
                           hit.has_value(), t);
}

// ---------------------------------------------------------------------------
// Intersection computation
// ---------------------------------------------------------------------------

std::optional<cad::geo::Vec2> ToolIntersection::computeIntersection(
    double angleDeg, double* outT, const TargetGeometry* cachedGeom) const
{
    if (!m_paramDoc) return std::nullopt;

    const cad::param::Block* block = nullptr;
    const cad::param::Segment* seg = nullptr;
    cad::geo::Vec2 w1, segDir;
    double baseAngle = 0.0;

    if (cachedGeom) {
        block = cachedGeom->block;
        seg = cachedGeom->seg;
        w1 = cachedGeom->w1;
        segDir = cachedGeom->segDir;
        baseAngle = cachedGeom->baseAngle;
    } else {
        block = m_paramDoc->findBlock(m_targetBlockId);
        seg = block ? block->findSegment(m_targetSegmentId) : nullptr;
        if (!block || !seg) return std::nullopt;

        const auto* sp = block->findPoint(seg->startPointId);
        const auto* ep = block->findPoint(seg->endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) return std::nullopt;

        w1 = block->transform.toWorld(sp->resolvedPos);
        cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        segDir = w2 - w1;
        if (segDir.length() < 1e-9) return std::nullopt;

        baseAngle = std::atan2(segDir.y, segDir.x);
    }
    if (!block || !seg) return std::nullopt;

    double theta = baseAngle + angleDeg * M_PI / 180.0;
    cad::geo::Vec2 d{std::cos(theta), std::sin(theta)};

    // --- Curve target: use rayVsCurveSpans ---
    if (seg->isCurve()) {
        const cad::param::CurveSpanEntry* entry = block->curveSpanEntry(seg->id);
        if (!entry || entry->spans.empty()) return std::nullopt;
        auto hit = cad::geo::rayVsCurveSpans(m_originPos, d, entry->spans,
                                             block->transform.origin,
                                             block->transform.rotation,
                                             m_bidirectional);
        if (!hit) return std::nullopt;
        if (outT) *outT = hit->t;
        return hit->point;
    }

    // --- Straight-line target ---
    auto hit = cad::geo::raySegmentIntersect(m_originPos, d, w1, segDir, m_bidirectional);
    if (!hit) return std::nullopt;
    if (outT) *outT = hit->t;
    return hit->point;
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

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Intersection;
    pt.serial = m_paramDoc->newPointSerial();
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;

    pt.refPointA = m_originPointId;
    pt.hostSegmentId = m_targetSegmentId;

    pt.interUseWorldAngle = m_worldAngleMode;
    pt.interAngle = m_worldAngleMode ? m_displayAngleDeg : m_currentAngleDeg;
    pt.interBidirectional = m_bidirectional;
    pt.interAimPointId = m_aimPointId;

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, m_targetBlockId, m_targetSegmentId, pt));
    } else {
        block->addPoint(pt);
        seg->auxPointIds.push_back(pt.id);
        m_paramDoc->resolveAll();
    }

    resetState();

    m_visuals.flashSuccessHud(ensureHud(), m_lastCursorPos, QString::fromUtf8("✓ 交点已创建"));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ToolIntersection::clearAim()
{
    m_aimPointId = QUuid();
    m_aimBlockId = QUuid();
    m_visuals.hideAimMarker();
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
    m_visuals.clearPreview();
    if (m_hud) m_hud->setVisible(false);
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
    m_visuals.hideSegHighlight();
    m_targetBlockId   = QUuid();
    m_targetSegmentId = QUuid();
    m_originBlockId   = QUuid();
    m_originPointId   = QUuid();
    m_aimPointId      = QUuid();
    m_aimBlockId      = QUuid();
    m_currentAngleDeg = 90.0;
    m_displayAngleDeg = 90.0;
    m_worldAngleMode  = false;
    m_bidirectional   = false;
    setState(State::SelectLine);
}

void ToolIntersection::setState(State s)
{
    m_state = s;
    refreshModeIndicator();
}

ModeIndicator ToolIntersection::modeIndicator() const
{
    return modeIndicatorFor(m_worldAngleMode, m_state);
}

ModeIndicator ToolIntersection::modeIndicatorFor(bool worldAngleMode, State s)
{
    const bool aiming = (s == State::AimAngle || s == State::BorrowAim);

    ModeIndicator mi;
    mi.modeName = worldAngleMode ? QString::fromUtf8("绝对角度")
                                 : QString::fromUtf8("跟随角度");
    mi.wAction = aiming
        ? QString::fromUtf8("W 切%1").arg(worldAngleMode ? QString::fromUtf8("跟随角度")
                                                         : QString::fromUtf8("绝对角度"))
        : QString::fromUtf8("瞄准中 W 切角度基准");
    mi.isDefault = !worldAngleMode;

    switch (s) {
        case State::SelectLine:
            mi.detail = QString::fromUtf8("点选目标线段 | 右键/Esc取消");
            break;
        case State::SelectPoint:
            mi.detail = QString::fromUtf8("点选射线起点 | 右键/Esc取消");
            break;
        case State::AimAngle:
            mi.detail = QString::fromUtf8("悬停点预览指向 | 点击借用点直接创建 | 空白点击确认方向");
            break;
        case State::BorrowAim:
            mi.detail = QString::fromUtf8("⚠ 射线未穿过线段 | 点击锁定方向创建 | 右键/Esc解锁");
            break;
    }
    mi.toast = worldAngleMode
        ? QString::fromUtf8("绝对角度模式：角度相对世界坐标系")
        : QString::fromUtf8("跟随角度模式：角度相对目标线段");
    return mi;
}

} // namespace cad::tools
