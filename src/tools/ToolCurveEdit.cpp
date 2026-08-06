#include "ToolCurveEdit.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPen>
#include <QBrush>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "document/commands/BlockCommands.h"
#include "GroupGuard.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ToolCurveEdit::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = State::Idle;
}

void ToolCurveEdit::deactivate()
{
    clearGraphics();
    m_scene = nullptr;
    m_paramDoc = nullptr;
}

void ToolCurveEdit::clearGraphics()
{
    if (m_scene) {
        if (m_curvePtPreview) { m_scene->removeItem(m_curvePtPreview); delete m_curvePtPreview; }
        if (m_snapIndicator) { m_scene->removeItem(m_snapIndicator); delete m_snapIndicator; }
        if (m_hLineIn)  { m_scene->removeItem(m_hLineIn);  delete m_hLineIn; }
        if (m_hLineOut) { m_scene->removeItem(m_hLineOut); delete m_hLineOut; }
        if (m_hDotIn)   { m_scene->removeItem(m_hDotIn);   delete m_hDotIn; }
        if (m_hDotOut)  { m_scene->removeItem(m_hDotOut);  delete m_hDotOut; }
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

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // 1) Grab a tangent handle of the active curve anchor (highest priority).
    if (!m_handleBlockId.isNull()) {
        const int h = handleHitTest(clickPos, zoom);
        if (h != 0) { beginHandleDrag(h); return; }
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

// ---------------------------------------------------------------------------
// Curve point placement preview
// ---------------------------------------------------------------------------

void ToolCurveEdit::updateCurvePointPreview(const cad::geo::Vec2& worldPos,
                                            Qt::KeyboardModifiers mods)
{
    m_segSnap.reset();
    if (!m_scene || !m_paramDoc) { hideCurvePointPreview(); return; }

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Curve-point snap wins: no placement dot while the cursor is over a
    // curve-relevant point (a curve anchor/endpoint under the cursor is grabbed
    // instead — see mousePress). Non-curve points do NOT suppress the preview.
    if (m_snapEngine.findCurvePointSnap(worldPos, m_paramDoc, zoom)) {
        hideCurvePointPreview();
        return;
    }

    m_segSnap = m_snapEngine.findSegmentSnap(
        worldPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (!m_segSnap) { hideCurvePointPreview(); return; }

    const auto* blk = m_paramDoc->findBlock(m_segSnap->blockId);
    const auto* seg = blk ? blk->findSegment(m_segSnap->segmentId) : nullptr;
    if (!seg) { hideCurvePointPreview(); return; }

    // Bridge lines (桥接线) must stay straight — no curve points on them.
    if (blk->isBridge) { hideCurvePointPreview(); return; }

    // Ctrl is REQUIRED to place a curve point — unified for both straight and
    // curve segments (the old "first point needs no shortcut" exception caused
    // accidental placement / 误触).
    if (!(mods & Qt::ControlModifier)) {
        hideCurvePointPreview();
        return;
    }

    if (!m_curvePtPreview) {
        constexpr double r = 2.2;
        m_curvePtPreview = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        m_curvePtPreview->setPen(Qt::NoPen);
        m_curvePtPreview->setBrush(QColor(0xE9, 0x1E, 0x63));  // ETCAD pink cue
        m_curvePtPreview->setZValue(103.0);
        m_scene->addItem(m_curvePtPreview);
    }
    m_curvePtPreview->setPos(cad::geo::Coord::toScene(m_segSnap->worldPos));
    m_curvePtPreview->setVisible(true);
}

void ToolCurveEdit::hideCurvePointPreview()
{
    if (m_curvePtPreview)
        m_curvePtPreview->setVisible(false);
}

// ---------------------------------------------------------------------------
// Curve point editing (place / drag / delete)
// ---------------------------------------------------------------------------

QUuid ToolCurveEdit::placeCurvePoint(const SegmentSnapResult& segSnap)
{
    if (!m_paramDoc) return {};
    auto* block = m_paramDoc->findBlock(segSnap.blockId);
    auto* seg = block ? block->findSegment(segSnap.segmentId) : nullptr;
    if (!block || !seg) return {};
    if (block->isBridge) return {};  // 桥接线必须保持直线
    // Group guard: adding a curve point is a structural change (组内线拦截).
    if (guardGroupedBlock(m_scene, m_paramDoc, segSnap.blockId,
                          QString::fromUtf8("\xe6\xb7\xbb\xe5\x8a\xa0\xe6\x9b\xb2\xe7\xba\xbf\xe7\x82\xb9")))  // 添加曲线点
        return {};

    const cad::geo::Vec2 localPos = block->transform.toLocal(segSnap.worldPos);
    double percent = 0.5, offset = 0.0;
    if (!chordParams(*block, *seg, localPos, &percent, &offset)) return {};

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::CurveAnchor;
    pt.hostSegmentId = seg->id;
    pt.interpPercent = percent;
    pt.interpOffsetDist = offset;
    pt.isAuxiliary = false;
    pt.visible = true;
    pt.selectable = true;   // snappable by other tools (端点吸附)
    pt.showName = false;
    pt.autoTangent = true;  // C2 auto tangent
    pt.serial = m_paramDoc->newPointSerial();
    const QUuid newId = pt.id;

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddCurvePointCommand(
            m_paramDoc, block->id, seg->id, std::move(pt)));
    } else {
        block->addPoint(pt);
        seg->passPointIds.push_back(newId);
        seg->type = cad::param::SegmentType::Bezier;
        m_paramDoc->resolveAll();
    }
    return newId;
}

void ToolCurveEdit::startAnchorDrag(const QUuid& blockId, const QUuid& pointId)
{
    auto* block = m_paramDoc->findBlock(blockId);
    auto* pt = block ? block->findPoint(pointId) : nullptr;
    if (!pt || pt->constraint != cad::param::PointConstraint::CurveAnchor) return;

    m_state = State::DraggingCurvePoint;
    m_dragBlockId = blockId;
    m_dragPointId = pointId;
    m_dragOldPercent = pt->interpPercent;
    m_dragOldOffset  = pt->interpOffsetDist;
    m_dragOldFollowBlockId = pt->followBlockId;
    m_dragOldFollowPointId = pt->followPointId;
    m_dragOldFollowOffset  = pt->followOffset;
}

void ToolCurveEdit::beginCurveAnchorDrag(const SnapResult& snap)
{
    startAnchorDrag(snap.blockId, snap.pointId);
}

void ToolCurveEdit::dragCurveAnchorTo(const cad::geo::Vec2& worldPos)
{
    auto* block = m_paramDoc->findBlock(m_dragBlockId);
    auto* pt = block ? block->findPoint(m_dragPointId) : nullptr;
    if (!block || !pt) { m_state = State::Idle; return; }

    // A follow connection pins this anchor back onto its target on every
    // resolve pass — detach it as soon as the user starts moving the point so
    // the anchor (and its handles) follow the cursor; otherwise only the curve
    // shape moves while the point stays glued to the old target. The old
    // connection was snapshotted in startAnchorDrag for cancel/undo.
    if (!pt->followPointId.isNull()) {
        pt->followBlockId = QUuid();
        pt->followPointId = QUuid();
        pt->followOffset = cad::geo::Vec2::zero();
    }

    const auto* seg = block->findSegment(pt->hostSegmentId);
    if (!seg) { m_state = State::Idle; return; }

    const cad::geo::Vec2 localPos = block->transform.toLocal(worldPos);
    double percent = 0.5, offset = 0.0;
    if (!chordParams(*block, *seg, localPos, &percent, &offset)) return;

    pt->interpPercent = percent;
    pt->interpOffsetDist = offset;
    m_dragLastCursor = worldPos;  // remember for snap on release
    m_paramDoc->invalidateLayer(block->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveAll();

    // --- Snap indicator: show green circle when cursor is near another point ---
    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    auto snap = m_snapEngine.findSnap(worldPos, m_paramDoc, zoom, -1.0, m_dragPointId);
    if (snap && snap->pointId != m_dragPointId) {
        if (!m_snapIndicator) {
            constexpr double r = 7.0;
            m_snapIndicator = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            QPen pen(QColor(0x4C, 0xAF, 0x50), 2.0);  // green
            pen.setCosmetic(true);
            m_snapIndicator->setPen(pen);
            m_snapIndicator->setBrush(Qt::NoBrush);
            m_snapIndicator->setZValue(106.0);
            m_scene->addItem(m_snapIndicator);
        }
        m_snapIndicator->setPos(cad::geo::Coord::toScene(snap->worldPos));
        m_snapIndicator->setVisible(true);
    } else {
        if (m_snapIndicator) m_snapIndicator->setVisible(false);
    }
}

void ToolCurveEdit::endCurveAnchorDrag()
{
    if (auto* block = m_paramDoc->findBlock(m_dragBlockId)) {
        if (auto* pt = block->findPoint(m_dragPointId)) {
            const bool moved =
                std::abs(pt->interpPercent - m_dragOldPercent) > 1e-9 ||
                std::abs(pt->interpOffsetDist - m_dragOldOffset) > 1e-9;

            // --- Snap-connect: check if the cursor is near another point ---
            // If so, establish a parametric follow connection so this curve
            // point tracks the target point when it moves.
            if (moved && pt->resolved) {
                double zoom = 1.0;
                if (m_scene && !m_scene->views().isEmpty())
                    zoom = m_scene->views().first()->transform().m11();
                // Use the last cursor position (not the resolved point pos)
                // so snap feels responsive to where the user actually pointed.
                auto snap = m_snapEngine.findSnap(m_dragLastCursor, m_paramDoc, zoom,
                                                  -1.0, m_dragPointId);
                if (snap && snap->pointId != m_dragPointId) {
                    // Verify target is resolved and not the same point.
                    const auto* tBlk = m_paramDoc->findBlock(snap->blockId);
                    const auto* tPt = tBlk ? tBlk->findPoint(snap->pointId) : nullptr;
                    if (tPt && tPt->resolved) {
                        // Snap the curve point EXACTLY onto the target (zero
                        // offset) so the two coincide — no visible misalignment.
                        const cad::geo::Vec2 targetWorld =
                            tBlk->transform.toWorld(tPt->resolvedPos);
                        const cad::geo::Vec2 targetLocal =
                            block->transform.toLocal(targetWorld);
                        // Re-project the target position onto the chord.
                        const auto* seg2 = block->findSegment(pt->hostSegmentId);
                        const auto* sp2 = seg2 ? block->findPoint(seg2->startPointId) : nullptr;
                        const auto* ep2 = seg2 ? block->findPoint(seg2->endPointId) : nullptr;
                        if (seg2 && sp2 && ep2 && sp2->resolved && ep2->resolved) {
                            const cad::geo::Vec2 chord = ep2->resolvedPos - sp2->resolvedPos;
                            const double clen = chord.length();
                            if (clen > 1e-9) {
                                const cad::geo::Vec2 u = chord / clen;
                                const cad::geo::Vec2 n{-u.y, u.x};
                                const cad::geo::Vec2 rel = targetLocal - sp2->resolvedPos;
                                pt->interpPercent = rel.dot(u) / clen;
                                pt->interpOffsetDist = rel.dot(n);
                            }
                        }
                        pt->followBlockId = snap->blockId;
                        pt->followPointId = snap->pointId;
                        pt->followOffset = cad::geo::Vec2::zero();
                    }
                } else if (!snap) {
                    // Dragged away from any point: release existing follow.
                    pt->followBlockId = QUuid();
                    pt->followPointId = QUuid();
                    pt->followOffset = cad::geo::Vec2::zero();
                }
            }

            if (moved && m_undoStack) {
                m_undoStack->push(new cad::cmd::MoveCurveAnchorCommand(
                    m_paramDoc, m_dragBlockId, m_dragPointId,
                    m_dragOldPercent, m_dragOldOffset,
                    pt->interpPercent, pt->interpOffsetDist,
                    m_dragOldFollowBlockId, m_dragOldFollowPointId,
                    m_dragOldFollowOffset,
                    pt->followBlockId, pt->followPointId, pt->followOffset));
            }
        }
    }
    // Full document resolve on release to propagate to followers/panels.
    m_paramDoc->resolveAll();
    m_state = State::Idle;
    m_dragBlockId = QUuid();
    m_dragPointId = QUuid();
    if (m_snapIndicator) m_snapIndicator->setVisible(false);
}

void ToolCurveEdit::cancelCurveAnchorDrag()
{
    if (auto* block = m_paramDoc->findBlock(m_dragBlockId)) {
        if (auto* pt = block->findPoint(m_dragPointId)) {
            pt->interpPercent = m_dragOldPercent;
            pt->interpOffsetDist = m_dragOldOffset;
            // Restore the follow connection detached by the drag (if any).
            pt->followBlockId = m_dragOldFollowBlockId;
            pt->followPointId = m_dragOldFollowPointId;
            pt->followOffset = m_dragOldFollowOffset;
            m_paramDoc->resolveAll();
        }
    }
    m_state = State::Idle;
    m_dragBlockId = QUuid();
    m_dragPointId = QUuid();
}

void ToolCurveEdit::deleteCurvePoint(const SnapResult& snap)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(snap.blockId);
    auto* pt = block ? block->findPoint(snap.pointId) : nullptr;
    if (!block || !pt) return;
    // Group guard: removing a curve point is a structural change (组内线拦截).
    if (guardGroupedBlock(m_scene, m_paramDoc, snap.blockId,
                          QString::fromUtf8("\xe5\x88\xa0\xe9\x99\xa4\xe6\x9b\xb2\xe7\xba\xbf\xe7\x82\xb9")))  // 删除曲线点
        return;

    const QUuid segId = pt->hostSegmentId;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::RemoveCurvePointCommand(
            m_paramDoc, block->id, segId, snap.pointId));
    } else if (auto* seg = block->findSegment(segId)) {
        auto& ids = seg->passPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), snap.pointId), ids.end());
        if (ids.empty()) seg->type = cad::param::SegmentType::Line;
        auto& pts = block->points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [&](const cad::param::ParamPoint& p) { return p.id == snap.pointId; }),
            pts.end());
        block->rebuildPointIndex();
        m_paramDoc->resolveAll();
    }
}

// ---------------------------------------------------------------------------
// Tangent handles (切线手柄)
// ---------------------------------------------------------------------------

void ToolCurveEdit::anchorTangents(const cad::param::Block& block,
                                   const cad::param::ParamPoint& pt,
                                   cad::geo::Vec2* tanIn, cad::geo::Vec2* tanOut) const
{
    *tanIn = pt.tangentIn;
    *tanOut = pt.tangentOut;
    if (!pt.autoTangent) return;  // manual mode: use the stored tangents

    // Auto mode: read the C2 tangents back out of the frame-level Bézier
    // cache (built once per resolve pass) so the displayed handles match the
    // rendered curve exactly — without a second tridiagonal solve per frame.
    // Find the curve segment: for CurveAnchor use hostSegmentId; for endpoints
    // scan for a segment that references this point.
    const cad::param::Segment* seg = nullptr;
    if (!pt.hostSegmentId.isNull()) {
        seg = block.findSegment(pt.hostSegmentId);
    }
    if (!seg) {
        for (const auto& s : block.segments) {
            if (!s.isCurve()) continue;
            if (s.startPointId == pt.id || s.endPointId == pt.id) {
                seg = &s;
                break;
            }
        }
    }
    if (!seg) return;

    const cad::param::CurveSpanEntry* entry = block.curveSpanEntry(seg->id);
    if (!entry || entry->spans.empty()) return;
    const auto& anchors = entry->anchors;
    const auto& spans   = entry->spans;

    // Locate this point's index in the anchor sequence (start + passPoints + end).
    int myIndex = -1;
    int idx = 0;
    if (seg->startPointId == pt.id) myIndex = idx;
    ++idx;
    for (const auto& ppId : seg->passPointIds) {
        if (ppId == pt.id) myIndex = idx;
        ++idx;
    }
    if (seg->endPointId == pt.id) myIndex = idx;

    const int n = static_cast<int>(anchors.size());
    if (myIndex < 0 || myIndex >= n || n < 2) return;

    // Hermite-to-Bézier form: ctrl1 = P + T/3 and ctrl2 = P' − T'/3, so the
    // C2 tangent at an anchor is recovered as 3·(ctrl − P). For auto points
    // tanIn == tanOut (one per-point tangent); curve endpoints only have one
    // adjacent span, so mirror the solved value (matches the old solveC2
    // behaviour where both received c2[myIndex]).
    if (myIndex < n - 1) {
        *tanOut = (spans[myIndex].ctrl1 - anchors[myIndex]) * 3.0;
        if (myIndex == 0) *tanIn = *tanOut;
    }
    if (myIndex > 0) {
        *tanIn = (anchors[myIndex] - spans[myIndex - 1].ctrl2) * 3.0;
        if (myIndex == n - 1) *tanOut = *tanIn;
    }
}

void ToolCurveEdit::showHandles(const QUuid& blockId, const QUuid& pointId)
{
    m_handleBlockId = blockId;
    m_handlePointId = pointId;
    updateHandleGraphics();
}

void ToolCurveEdit::updateHandleGraphics()
{
    if (!m_scene || !m_paramDoc || m_handleBlockId.isNull()) return;
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (!block || !pt || !pt->resolved) { hideHandles(); return; }

    cad::geo::Vec2 tanIn, tanOut;
    anchorTangents(*block, *pt, &tanIn, &tanOut);

    // Handle tips are the Bézier control points: P - tanIn/3 and P + tanOut/3.
    const cad::geo::Vec2 pLocal = pt->resolvedPos;
    const cad::geo::Vec2 inWorld  = block->transform.toWorld(pLocal - tanIn / 3.0);
    const cad::geo::Vec2 outWorld = block->transform.toWorld(pLocal + tanOut / 3.0);
    const cad::geo::Vec2 pWorld   = block->transform.toWorld(pLocal);

    const QColor kHandle(0x00, 0xA8, 0xE1);  // cyan handles

    if (!m_hLineIn) {
        m_hLineIn = new QGraphicsLineItem();
        QPen pen(kHandle, 1.0); pen.setCosmetic(true);
        m_hLineIn->setPen(pen); m_hLineIn->setZValue(104.0);
        m_scene->addItem(m_hLineIn);
    }
    if (!m_hLineOut) {
        m_hLineOut = new QGraphicsLineItem();
        QPen pen(kHandle, 1.0); pen.setCosmetic(true);
        m_hLineOut->setPen(pen); m_hLineOut->setZValue(104.0);
        m_scene->addItem(m_hLineOut);
    }
    if (!m_hDotIn) {
        constexpr double r = 3.0;
        m_hDotIn = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        m_hDotIn->setPen(Qt::NoPen); m_hDotIn->setBrush(kHandle);
        m_hDotIn->setZValue(105.0);
        m_scene->addItem(m_hDotIn);
    }
    if (!m_hDotOut) {
        constexpr double r = 3.0;
        m_hDotOut = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        m_hDotOut->setPen(Qt::NoPen); m_hDotOut->setBrush(kHandle);
        m_hDotOut->setZValue(105.0);
        m_scene->addItem(m_hDotOut);
    }

    const QPointF pS   = cad::geo::Coord::toScene(pWorld);
    const QPointF inS  = cad::geo::Coord::toScene(inWorld);
    const QPointF outS = cad::geo::Coord::toScene(outWorld);
    m_hLineIn->setLine(QLineF(pS, inS));
    m_hLineOut->setLine(QLineF(pS, outS));
    m_hDotIn->setPos(inS);
    m_hDotOut->setPos(outS);
    m_hLineIn->setVisible(true);  m_hLineOut->setVisible(true);
    m_hDotIn->setVisible(true);   m_hDotOut->setVisible(true);
}

void ToolCurveEdit::hideHandles()
{
    m_handleBlockId = QUuid();
    m_handlePointId = QUuid();
    m_dragHandle = 0;
    if (m_hLineIn)  m_hLineIn->setVisible(false);
    if (m_hLineOut) m_hLineOut->setVisible(false);
    if (m_hDotIn)   m_hDotIn->setVisible(false);
    if (m_hDotOut)  m_hDotOut->setVisible(false);
}

int ToolCurveEdit::handleHitTest(const cad::geo::Vec2& worldPos, double zoom) const
{
    if (m_handleBlockId.isNull() || !m_paramDoc) return 0;
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (!block || !pt || !pt->resolved) return 0;

    cad::geo::Vec2 tanIn, tanOut;
    anchorTangents(*block, *pt, &tanIn, &tanOut);
    const cad::geo::Vec2 pLocal = pt->resolvedPos;
    const cad::geo::Vec2 inWorld  = block->transform.toWorld(pLocal - tanIn / 3.0);
    const cad::geo::Vec2 outWorld = block->transform.toWorld(pLocal + tanOut / 3.0);

    const double radius = 8.0 / std::max(zoom, 1e-9);  // screen-space hit radius
    const double rSq = radius * radius;
    if (worldPos.distanceSquaredTo(inWorld)  < rSq) return 1;
    if (worldPos.distanceSquaredTo(outWorld) < rSq) return 2;
    return 0;
}

void ToolCurveEdit::beginHandleDrag(int which)
{
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (!pt || which == 0) { m_dragHandle = 0; return; }

    m_handleOldTanIn  = pt->tangentIn;
    m_handleOldTanOut = pt->tangentOut;
    m_handleOldAuto   = pt->autoTangent;

    // First manual edit: materialize the current auto tangents so both handles
    // hold valid values once autoTangent flips to false.
    if (pt->autoTangent) {
        cad::geo::Vec2 ti, to;
        anchorTangents(*block, *pt, &ti, &to);
        pt->tangentIn = ti;
        pt->tangentOut = to;
        pt->autoTangent = false;
    }

    m_dragHandle = which;
    m_state = State::DraggingHandle;
}

void ToolCurveEdit::dragHandleTo(const cad::geo::Vec2& worldPos)
{
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (!block || !pt || m_dragHandle == 0) { m_state = State::Idle; return; }

    const cad::geo::Vec2 localPos = block->transform.toLocal(worldPos);
    const cad::geo::Vec2 pLocal = pt->resolvedPos;
    if (m_dragHandle == 2) {
        pt->tangentOut = (localPos - pLocal) * 3.0;  // ctrl = P + tanOut/3
        // Collinear-but-independent-length handles (平滑不等长): the opposite
        // handle rotates to stay on the same line (so the curve stays smooth,
        // no cusp) but KEEPS its own length — so the two sides of the curve can
        // bend differently (one sharp, one gentle). For smoothness the two
        // tangents must share a direction (tangentOut = k*tangentIn, k>0);
        // their magnitudes are free.
        if (pt->tangentLocked) {
            const double inLen  = pt->tangentIn.length();
            const double outLen = pt->tangentOut.length();
            if (inLen > 1e-9 && outLen > 1e-9)
                pt->tangentIn = pt->tangentOut * (inLen / outLen);  // same dir, in's length
        }
    } else {
        pt->tangentIn  = (pLocal - localPos) * 3.0;  // ctrl = P - tanIn/3
        if (pt->tangentLocked) {
            const double inLen  = pt->tangentIn.length();
            const double outLen = pt->tangentOut.length();
            if (inLen > 1e-9 && outLen > 1e-9)
                pt->tangentOut = pt->tangentIn * (outLen / inLen);  // same dir, out's length
        }
    }
    pt->autoTangent = false;
    // A tangent change reshapes the curve without moving any point, so the
    // resolve pass won't bump geometryEpoch — bump it here to force the
    // BlockItem cache rebuild (otherwise the curve wouldn't refresh live).
    ++block->geometryEpoch;
    m_paramDoc->invalidateLayer(block->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveAll();
    updateHandleGraphics();
}

void ToolCurveEdit::endHandleDrag()
{
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (block && pt && m_undoStack) {
        m_undoStack->push(new cad::cmd::SetCurveTangentCommand(
            m_paramDoc, m_handleBlockId, m_handlePointId,
            m_handleOldTanIn, m_handleOldTanOut, m_handleOldAuto,
            pt->tangentIn, pt->tangentOut, pt->autoTangent));
    }
    // Full document resolve on release to propagate to followers/panels.
    m_paramDoc->resolveAll();
    m_dragHandle = 0;
    m_state = State::Idle;
}

void ToolCurveEdit::cancelHandleDrag()
{
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (pt) {
        pt->tangentIn = m_handleOldTanIn;
        pt->tangentOut = m_handleOldTanOut;
        pt->autoTangent = m_handleOldAuto;
        m_paramDoc->resolveAll();
        updateHandleGraphics();
    }
    m_dragHandle = 0;
    m_state = State::Idle;
}

// ---------------------------------------------------------------------------
// Chord decomposition
// ---------------------------------------------------------------------------

bool ToolCurveEdit::chordParams(const cad::param::Block& block,
                                const cad::param::Segment& seg,
                                const cad::geo::Vec2& localPos,
                                double* percent, double* offset) const
{
    const auto* sp = block.findPoint(seg.startPointId);
    const auto* ep = block.findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return false;

    const cad::geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
    const double len = chord.length();
    if (len < 1e-9) return false;

    const cad::geo::Vec2 unitDir = chord / len;
    const cad::geo::Vec2 normal{-unitDir.y, unitDir.x};  // left of start→end
    const cad::geo::Vec2 rel = localPos - sp->resolvedPos;

    *percent = rel.dot(unitDir) / len;
    *offset  = rel.dot(normal);
    return true;
}

} // namespace cad::tools
