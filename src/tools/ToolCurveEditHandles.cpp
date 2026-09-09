#include "ToolCurveEdit.h"

#include <QGraphicsLineItem>
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

namespace cad::tools {

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
    // D8: 圆段的点没有可拖的切向手柄 —— 圆段由拟合重写，直接不显示。
    if (const auto* block = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr) {
        const auto* pt = block->findPoint(pointId);
        if (pt && belongsToCircleSegment(*block, *pt)) { hideHandles(); return; }
    }
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
    if (belongsToCircleSegment(*block, *pt)) { hideHandles(); return; }  // D8

    cad::geo::Vec2 tanIn, tanOut;
    anchorTangents(*block, *pt, &tanIn, &tanOut);

    // Handle tips are the Bézier control points: P - tanIn/3 and P + tanOut/3.
    const cad::geo::Vec2 pLocal = pt->resolvedPos;
    const cad::geo::Vec2 inWorld  = block->transform.toWorld(pLocal - tanIn / 3.0);
    const cad::geo::Vec2 outWorld = block->transform.toWorld(pLocal + tanOut / 3.0);
    const cad::geo::Vec2 pWorld   = block->transform.toWorld(pLocal);

    // P3-1 (D4): an unlocked point (尖角模式, tangentLocked == false) tints its
    // handles orange so the user sees the corner state right on the canvas
    // (locked = cyan; only the colors change — no extra scene items).
    const bool unlocked = !pt->tangentLocked;
    const QColor handleBase = unlocked ? m_scene->style()->handleUnlockedColor
                                       : m_scene->style()->handleLockedColor;
    QColor kHandleLine = handleBase;
    kHandleLine.setAlpha(120);
    QColor kHandleTipPen = handleBase;
    kHandleTipPen.setAlpha(unlocked ? 160 : 150);
    // 控制点填充 = 完全透明 (NoBrush) — 圈内不遮挡任何几何, 只留淡描边;
    // 圆点比原 r2.0 再缩小 (2026-09 用户两轮反馈: 实心大点不透明/太大 →
    // 纸色填充仍是"不透明补丁"盖住圈下线条 → 真透明 + 更小).
    const QBrush kHandleTipFill = Qt::NoBrush;

    if (!m_hLineIn) {
        m_hLineIn = new QGraphicsLineItem();
        QPen pen(kHandleLine, 1.0); pen.setCosmetic(true);
        m_hLineIn->setPen(pen); m_hLineIn->setZValue(104.0);
        m_scene->addItem(m_hLineIn);
        m_managed.own(m_hLineIn, &m_hLineIn);
    }
    if (!m_hLineOut) {
        m_hLineOut = new QGraphicsLineItem();
        QPen pen(kHandleLine, 1.0); pen.setCosmetic(true);
        m_hLineOut->setPen(pen); m_hLineOut->setZValue(104.0);
        m_scene->addItem(m_hLineOut);
        m_managed.own(m_hLineOut, &m_hLineOut);
    }
    if (!m_hDotIn) {
        constexpr double r = 1.6;
        m_hDotIn = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen tipPen(kHandleTipPen, 1.0); tipPen.setCosmetic(true);
        m_hDotIn->setPen(tipPen); m_hDotIn->setBrush(kHandleTipFill);
        m_hDotIn->setZValue(105.0);
        m_scene->addItem(m_hDotIn);
        m_managed.own(m_hDotIn, &m_hDotIn);
    }
    if (!m_hDotOut) {
        constexpr double r = 1.6;
        m_hDotOut = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen tipPen(kHandleTipPen, 1.0); tipPen.setCosmetic(true);
        m_hDotOut->setPen(tipPen); m_hDotOut->setBrush(kHandleTipFill);
        m_hDotOut->setZValue(105.0);
        m_scene->addItem(m_hDotOut);
        m_managed.own(m_hDotOut, &m_hDotOut);
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

    // 2026-12 审计 TOOL-P0-10: 手柄拾取半径 = canvas 悬停 token (与 CurveAnchorDragSession 同源)。
    const double radius = (m_scene ? m_scene->style()->hoverRadiusPx()
                                   : CanvasStyle::fallback().hoverRadiusPx()) /
                          std::max(zoom, cad::geo::kGeomEps);
    const double rSq = radius * radius;
    if (worldPos.distanceSquaredTo(inWorld)  < rSq) return 1;
    if (worldPos.distanceSquaredTo(outWorld) < rSq) return 2;
    return 0;
}

void ToolCurveEdit::beginHandleDrag(int which, Qt::KeyboardModifiers mods)
{
    auto* block = m_paramDoc->findBlock(m_handleBlockId);
    auto* pt = block ? block->findPoint(m_handlePointId) : nullptr;
    if (!pt || which == 0) { m_dragHandle = 0; return; }

    m_handleOldTanIn  = pt->tangentIn;
    m_handleOldTanOut = pt->tangentOut;
    m_handleOldAuto   = pt->autoTangent;
    m_handleOldLocked = pt->tangentLocked;

    // First manual edit: materialize the current auto tangents so both handles
    // hold valid values once autoTangent flips to false.
    if (pt->autoTangent) {
        cad::geo::Vec2 ti, to;
        anchorTangents(*block, *pt, &ti, &to);
        pt->tangentIn = ti;
        pt->tangentOut = to;
        pt->autoTangent = false;
    }

    // P3-1 (D1): Alt+drag = corner mode — break the tangent lock PERSISTENTLY
    // for this stroke (the pre-drag state is snapshotted for the undo command
    // and for cancelHandleDrag). Once unlocked, dragHandleTo()'s two
    // [pt->tangentLocked] branches skip the collinear mirroring, so the
    // opposite handle keeps its exact pre-drag direction/length — that is the
    // corner the user asked for. The mods are captured HERE (drag start), not
    // live during the stroke (D3).
    if (mods & Qt::AltModifier)
        pt->tangentLocked = false;

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
            if (inLen > cad::geo::kGeomEps && outLen > cad::geo::kGeomEps)
                pt->tangentIn = pt->tangentOut * (inLen / outLen);  // same dir, in's length
        }
    } else {
        pt->tangentIn  = (pLocal - localPos) * 3.0;  // ctrl = P - tanIn/3
        if (pt->tangentLocked) {
            const double inLen  = pt->tangentIn.length();
            const double outLen = pt->tangentOut.length();
            if (inLen > cad::geo::kGeomEps && outLen > cad::geo::kGeomEps)
                pt->tangentOut = pt->tangentIn * (outLen / inLen);  // same dir, out's length
        }
    }
    pt->autoTangent = false;
    // A tangent change reshapes the curve without moving any point, so the
    // resolve pass won't bump geometryEpoch — bump it here to force the
    // BlockItem cache rebuild (otherwise the curve wouldn't refresh live).
    block->touchGeometry();
    // Per-frame hot path (切线拖拽每帧): resolve ONLY the host block's dirty
    // subgraph; syncFromBlock rebuilds just the blocks whose epoch changed
    // (the old resolveAll() re-resolved the whole document every frame).
    m_paramDoc->invalidateLayer(block->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveForDrag(QList<QUuid>{block->id});
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
            pt->tangentIn, pt->tangentOut, pt->autoTangent,
            m_handleOldLocked, pt->tangentLocked));
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
        pt->tangentLocked = m_handleOldLocked;  // Alt may have broken it — restore
        m_paramDoc->resolveAll();
        updateHandleGraphics();
    }
    m_dragHandle = 0;
    m_state = State::Idle;
}

} // namespace cad::tools
