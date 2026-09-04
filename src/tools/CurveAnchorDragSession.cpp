#include "CurveAnchorDragSession.h"

#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"

#include <QUndoStack>
#include <cmath>

namespace cad::tools {

std::optional<std::pair<QUuid, QUuid>> CurveAnchorDragSession::hitAt(
    const cad::geo::Vec2& worldPos, double zoom,
    const QSet<QUuid>& selection) const
{
    if (!m_paramDoc || selection.isEmpty()) return std::nullopt;

    const double radius = 10.0 / std::max(zoom, 1e-9);  // 10px screen radius
    const double rSq = radius * radius;

    for (const QUuid& blockId : selection) {
        const auto* block = m_paramDoc->blocksView().byId(blockId);
        if (!block) continue;

        for (const auto& seg : block->segments) {
            if (!seg.isCurve()) continue;
            for (const auto& ppId : seg.passPointIds) {
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                // CurveAnchor points are dragged with the SmartPen (parametric
                // percent/offset); dragging them here would convert them to Free
                // points and break the chord link. Skip them.
                if (pp->constraint == cad::param::PointConstraint::CurveAnchor) continue;
                cad::geo::Vec2 w = block->transform.toWorld(pp->resolvedPos);
                if (worldPos.distanceSquaredTo(w) < rSq)
                    return std::make_pair(blockId, ppId);
            }
        }
    }
    return std::nullopt;
}

void CurveAnchorDragSession::begin(const QUuid& blockId, const QUuid& pointId)
{
    auto* block = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    if (!block) return;
    auto* pt = block->findPoint(pointId);
    if (!pt) return;

    m_dragging = true;
    m_blockId = blockId;
    m_pointId = pointId;
    m_origPos = pt->freePos;
}

void CurveAnchorDragSession::update(const cad::geo::Vec2& worldPos)
{
    if (!m_dragging || !m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    auto* pt = block->findPoint(m_pointId);
    if (!pt) return;

    // Convert world cursor to local coords and set as new freePos
    cad::geo::Vec2 local = block->transform.toLocal(worldPos);
    pt->freePos = local;
    pt->constraint = cad::param::PointConstraint::Free;

    m_paramDoc->invalidateLayer(block->layer);
    m_paramDoc->resolveForDrag({m_blockId});
    // resolveForDrag() emits resolved() → CanvasScene::syncBlockPositions(), which
    // rebuilds ONLY the blocks whose geometryEpoch changed. The old
    // refreshAllBlockItems() here rebuilt EVERY block item (all curves' C2
    // solve + cache) on every mouse-move — a full-document duplicate of the
    // sync that the signal already performs.
}

void CurveAnchorDragSession::end()
{
    if (!m_dragging) return;
    m_dragging = false;

    // Push undo command (old position → new position)
    if (m_undoStack && m_paramDoc) {
        auto* block = m_paramDoc->findBlock(m_blockId);
        auto* pt = block ? block->findPoint(m_pointId) : nullptr;
        if (pt) {
            m_undoStack->push(new cad::cmd::MovePointCommand(
                m_paramDoc, m_blockId, m_pointId, m_origPos, pt->freePos));
        }
    }

    m_blockId = QUuid();
    m_pointId = QUuid();
}

} // namespace cad::tools
