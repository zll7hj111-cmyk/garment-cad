#include "ToolCurveEdit.h"

#include <QGraphicsEllipseItem>
#include <QPen>
#include <QBrush>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"
#include "document/commands/BlockCommands.h"
#include "geometry/Epsilon.h"
#include "ui/UiStrings.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// D8: 圆段置灰
// ---------------------------------------------------------------------------

bool ToolCurveEdit::belongsToCircleSegment(const cad::param::Block& block,
                                           const cad::param::ParamPoint& pt)
{
    for (const auto& seg : block.segments) {
        if (seg.fitKind != cad::param::FitKind::Circle) continue;
        if (seg.startPointId == pt.id || seg.endPointId == pt.id) return true;
        if (std::find(seg.passPointIds.begin(), seg.passPointIds.end(), pt.id)
            != seg.passPointIds.end()) {
            return true;
        }
    }
    return false;
}

void ToolCurveEdit::notifyCircleLocked()
{
    if (m_scene) m_scene->showToast(cad::ui::str::kCircleCurveLockedHint);
}

// ---------------------------------------------------------------------------
// Curve point placement preview
// ---------------------------------------------------------------------------

void ToolCurveEdit::updateCurvePointPreview(const cad::geo::Vec2& worldPos,
                                            Qt::KeyboardModifiers mods)
{
    m_segSnap.reset();
    if (!m_scene || !m_paramDoc) { hideCurvePointPreview(); return; }

    double zoom = m_scene->safeZoom();

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

    // D8: 圆段置灰 —— 不加曲线点 (也不显示预览点)。
    if (seg->fitKind == cad::param::FitKind::Circle) { hideCurvePointPreview(); return; }

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
        m_curvePtPreview->setBrush(m_scene->style()->curveAnchorColor);  // ETCAD pink cue
        m_curvePtPreview->setZValue(103.0);
        m_scene->addItem(m_curvePtPreview);
        m_managed.own(m_curvePtPreview, &m_curvePtPreview);
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
    if (seg->fitKind == cad::param::FitKind::Circle) return {};  // D8: 圆段不加锚

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
        block->touchGeometry();
        m_paramDoc->resolveAll();
    }
    return newId;
}

void ToolCurveEdit::startAnchorDrag(const QUuid& blockId, const QUuid& pointId)
{
    auto* block = m_paramDoc->findBlock(blockId);
    auto* pt = block ? block->findPoint(pointId) : nullptr;
    if (!pt || pt->constraint != cad::param::PointConstraint::CurveAnchor) return;
    if (belongsToCircleSegment(*block, *pt)) return;  // D8: 圆段置灰

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
    // Per-frame hot path (锚点拖拽每帧): resolve ONLY the host block's dirty
    // subgraph (followers/referencers cascade via collectAffected) — the old
    // resolveAll() re-resolved the whole document every frame.
    m_paramDoc->invalidateLayer(block->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveForDrag(QList<QUuid>{block->id});

    // --- Snap indicator: show green circle when cursor is near another point ---
    double zoom = m_scene->safeZoom();
    auto snap = m_snapEngine.findSnap(worldPos, m_paramDoc, zoom, -1.0, m_dragPointId);
    if (snap && snap->pointId != m_dragPointId) {
        if (!m_snapIndicator) {
            constexpr double r = 7.0;
            m_snapIndicator = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            QPen pen(m_scene->style()->snapIndicatorColor, 2.0);  // 吸附绿 (审计 P0-1 统一)
            pen.setCosmetic(true);
            m_snapIndicator->setPen(pen);
            m_snapIndicator->setBrush(Qt::NoBrush);
            m_snapIndicator->setZValue(106.0);
            m_scene->addItem(m_snapIndicator);
            m_managed.own(m_snapIndicator, &m_snapIndicator);
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
                std::abs(pt->interpPercent - m_dragOldPercent) > cad::geo::kGeomEps ||
                std::abs(pt->interpOffsetDist - m_dragOldOffset) > cad::geo::kGeomEps;

            // --- Snap-connect: check if the cursor is near another point ---
            // If so, establish a parametric follow connection so this curve
            // point tracks the target point when it moves.
            if (moved && pt->resolved) {
                double zoom = m_scene->safeZoom();
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
                        // Re-project the target position onto the chord (复用 chordParams 收口).
                        const auto* seg2 = block->findSegment(pt->hostSegmentId);
                        if (seg2) {
                            double percent = 0.0, offset = 0.0;
                            if (chordParams(*block, *seg2, targetLocal, &percent, &offset)) {
                                pt->interpPercent = percent;
                                pt->interpOffsetDist = offset;
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
    if (belongsToCircleSegment(*block, *pt)) return;  // D8: 圆段置灰

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
    if (len < cad::geo::kGeomEps) return false;

    const cad::geo::Vec2 unitDir = chord / len;
    const cad::geo::Vec2 normal{-unitDir.y, unitDir.x};  // left of start→end
    const cad::geo::Vec2 rel = localPos - sp->resolvedPos;

    *percent = rel.dot(unitDir) / len;
    *offset  = rel.dot(normal);
    return true;
}

} // namespace cad::tools
