#include "ToolSelect.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>

#include <algorithm>
#include <limits>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/GroupModel.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "LinePropertyDialog.h"

namespace cad::tools {

namespace {

/// Distance from point p to the segment [a, b] (all in the same coordinate space).
double distancePointToSegment(const cad::geo::Vec2& p,
                              const cad::geo::Vec2& a,
                              const cad::geo::Vec2& b)
{
    const cad::geo::Vec2 ab = b - a;
    const double lenSq = ab.lengthSquared();
    if (lenSq < 1e-12)
        return p.distanceTo(a);

    const double t = std::clamp((p - a).dot(ab) / lenSq, 0.0, 1.0);
    return p.distanceTo(a + ab * t);
}

} // namespace

void ToolSelect::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
}

void ToolSelect::deactivate()
{
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_scene = nullptr;
    m_paramDoc = nullptr;
}

void ToolSelect::mousePress(QGraphicsSceneMouseEvent* event)
{
    // Selection itself is handled by the scene's built-in item selection
    // (ItemIsSelectable). Here we prepare a possible group drag: grab the
    // attachment group of the block under the cursor.
    m_dragGroup.clear();
    m_dragOrigins.clear();

    if (event->button() != Qt::LeftButton) return;
    if (!m_scene || !m_paramDoc) return;

    const QPointF up = event->scenePos();  // user coords (+Y up)
    const QList<QGraphicsItem*> hits = m_scene->items(cad::geo::Coord::toScene(up.x(), up.y()));
    BlockItem* blockItem = nullptr;
    for (QGraphicsItem* item : hits) {
        if (auto* bi = qgraphicsitem_cast<BlockItem*>(item)) {
            blockItem = bi;
            break;
        }
    }
    if (!blockItem) return;

    m_dragStartPos = cad::geo::Vec2(up.x(), up.y());
    m_dragGroup = collectGroup(blockItem->blockId());
    for (const QUuid& id : m_dragGroup) {
        if (const auto* b = m_paramDoc->findBlock(id))
            m_dragOrigins.insert(id, b->transform.origin);
    }
}

void ToolSelect::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (m_dragGroup.isEmpty()) return;
    if (!(event->buttons() & Qt::LeftButton)) return;
    if (!m_scene || !m_paramDoc) return;

    // Rigid-body translation of the whole attachment group by the cursor delta.
    // Attachments stay satisfied because every member moves by the same delta.
    const QPointF up = event->scenePos();
    const cad::geo::Vec2 delta = cad::geo::Vec2(up.x(), up.y()) - m_dragStartPos;

    for (const QUuid& id : m_dragGroup) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_dragOrigins.value(id) + delta;
    }
    m_scene->refreshAllBlockItems();
}

void ToolSelect::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
    m_dragGroup.clear();
    m_dragOrigins.clear();
}

QList<QUuid> ToolSelect::collectGroup(const QUuid& seedBlockId) const
{
    if (!m_paramDoc)
        return {};
    // Connected component of the attachment graph (both directions).
    return cad::param::collectGroupBlockIds(*m_paramDoc, seedBlockId);
}

void ToolSelect::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    // Event position is in user coords (+Y up); scene queries need scene coords (+Y down).
    const QPointF up = event->scenePos();
    const QList<QGraphicsItem*> hits = m_scene->items(cad::geo::Coord::toScene(up.x(), up.y()));
    BlockItem* blockItem = nullptr;
    for (QGraphicsItem* item : hits) {
        if (auto* bi = qgraphicsitem_cast<BlockItem*>(item)) {
            blockItem = bi;
            break;
        }
    }
    if (!blockItem) return;

    cad::param::Block* block = m_paramDoc->findBlock(blockItem->blockId());
    if (!block || block->segments.empty()) return;

    // Pick the segment closest to the click position (user coords).
    const cad::geo::Vec2 clickPos(up.x(), up.y());

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    constexpr double kTolerancePx = 8.0;  // screen pixels
    const double tolerance = kTolerancePx / zoom;

    QUuid bestSegId;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& seg : block->segments) {
        const cad::param::ParamPoint* sp = block->findPoint(seg.startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        const double d = distancePointToSegment(clickPos, w1, w2);
        if (d < bestDist) {
            bestDist = d;
            bestSegId = seg.id;
        }
    }

    if (bestSegId.isNull() || bestDist > tolerance) return;

    // Open the property dialog for the hit segment.
    QWidget* parentWidget = m_scene->views().isEmpty()
        ? nullptr : m_scene->views().first();
    LinePropertyDialog dlg(blockItem->blockId(), bestSegId, m_paramDoc, m_scene,
                           parentWidget);
    dlg.exec();

    // Refresh after dialog (user may have changed length/formulas).
    m_scene->refreshAllBlockItems();
}

void ToolSelect::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedBlocks();
        event->accept();
    }
}

void ToolSelect::deleteSelectedBlocks()
{
    if (!m_scene || !m_paramDoc) return;

    // Collect block IDs first: removeBlock() destroys the BlockItems,
    // which invalidates the selectedItems() list.
    QList<QUuid> toRemove;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* item : selected) {
        if (auto* bi = qgraphicsitem_cast<BlockItem*>(item))
            toRemove.append(bi->blockId());
    }
    if (toRemove.isEmpty()) return;

    for (const QUuid& id : toRemove)
        m_paramDoc->removeBlock(id);

    m_scene->refreshAllBlockItems();
}

} // namespace cad::tools
