#include "BlockItem.h"
#include "BlockItemPainter.h"
#include "BlockItemPick.h"
#include "CanvasScene.h"
#include "CanvasAnimator.h"
#include "CanvasStyle.h"
#include "CurveItem.h"

#include <QGraphicsSceneHoverEvent>
#include <QStyleOptionGraphicsItem>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"

#include <cmath>
#include "geometry/Epsilon.h"

BlockItem::BlockItem(const QUuid& blockId, cad::param::ParamDocument* doc,
                     QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_blockId(blockId)
    , m_doc(doc)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    // Dragging is driven by ToolSelect (whole attachment group moves as a rigid
    // body), so the item itself is not individually movable.
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(true);
    setZValue(1.0);

    rebuildCache();
}

QUuid BlockItem::hitSegmentAtScene(const QPointF& scenePos) const
{
    return hitTest(mapFromScene(scenePos), hoverThreshold());
}

QRectF BlockItem::boundingRect() const
{
    // Margin must cover the pick band: shape() strokes lines with
    // tol = hoverRadiusPx ÷ zoom and points with 2.5px ÷ zoom. The scene's
    // spatial index (BSP) rejects items whose boundingRect does not contain
    // the query point, so a margin smaller than the band makes the item
    // unpickable at low zoom (zoom 0.2 → tol ≈ 40 local units; the old ±10
    // margin silently dropped band-only hits).
    return m_cache.cachedBounds().adjusted(-BlockItemPick::kPickMarginLocal,
                                           -BlockItemPick::kPickMarginLocal,
                                           BlockItemPick::kPickMarginLocal,
                                           BlockItemPick::kPickMarginLocal);
}

QPainterPath BlockItem::shape() const
{
    // Pick tolerance in scene units: screen px ÷ view zoom (same conversion
    // as the hover threshold, so picking and hover agree).
    const CanvasStyle* style = nullptr;
    if (auto* cs = qobject_cast<CanvasScene*>(scene()))
        style = cs->style();

    double pxToLocal = 1.0;
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        const qreal m11 = cs->currentZoom();
        if (std::abs(m11) > cad::geo::kGeomEps)
            pxToLocal = 1.0 / std::abs(m11);
    }
    const double tol = (style ? style->hoverRadiusPx()
                              : CanvasStyle::fallback().hoverRadiusPx()) * pxToLocal;

    // Return the cached path when the tolerance has not changed enough to
    // matter (sub-pixel difference). Rebuilding the stroked path for every
    // hover/collision query is the single largest CPU cost on mouse-move.
    if (m_cachedShapeTol > 0.0 &&
        std::abs(tol - m_cachedShapeTol) < m_cachedShapeTol * 0.02)
        return m_cachedShape;

    m_cachedShape = BlockItemPick::buildShape(m_cache.lines(), m_cache.points(), tol, pxToLocal);
    m_cachedShapeTol = tol;
    return m_cachedShape;
}

void BlockItem::paint(QPainter* painter,
                      const QStyleOptionGraphicsItem* /*option*/,
                      QWidget* /*widget*/)
{
    CanvasAnimator* animator = nullptr;
    const CanvasStyle* style = nullptr;
    bool forceName = false, forceLen = false;  // Hold-to-show (N/M keys).
    bool dirArrows = true;   // 方向基准箭头全局开关 (2026-12); 无场景时保持画.
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        animator  = cs->animator();
        style     = cs->style();
        forceName = cs->forceShowName();
        forceLen  = cs->forceShowLength();
        dirArrows = cs->directionArrowsEnabled();
    }

    BlockPaintContext ctx{
        .painter = painter,
        .style = style,
        .animator = animator,
        .animatorTargetItem = this,
        .lines = m_cache.lines(),
        .points = m_cache.points(),
        .layerMode = m_cache.layerMode(),
        .hoveredEntity = m_hoveredEntity,
        .hoveredPointId = m_hoveredPointId,
        .selectedPointId = m_selectedPointId,
        .leaderEntity = m_leaderEntity,
        .forceName = forceName,
        .forceLen = forceLen,
        .dirArrows = dirArrows,
    };
    BlockItemPainter::paint(ctx);
}

void BlockItem::updateFromBlock()
{
    prepareGeometryChange();
    rebuildCache();
    update();
}

void BlockItem::syncFromBlock()
{
    if (!m_doc) return;
    const cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // Full rebuild required when:
    //  - rotation changed (local-scene mapping of every cached point shifts);
    //  - internal geometry changed (resolve moved points inside the block,
    //    e.g. a variable edit re-positioned an auxiliary point);
    //  - point count changed (a point was added or removed while the rest
    //    stayed put — without this, deleted points linger in the cache).
    // Pure translation keeps all local coordinates identical: just slide the
    // item (O(1), no alloc).
    if (std::abs(block->transform.rotation - m_cache.lastRotation()) > cad::geo::kGeomEps ||
        block->geometryEpoch() != m_cache.lastGeometryEpoch() ||
        block->points.size() != m_cache.lastPointCount()) {
        updateFromBlock();
        return;
    }

    const QPointF newPos = cad::geo::Coord::toScene(block->transform.origin);
    if (pos() != newPos)
        setPos(newPos);  // triggers scene re-index + repaint of old/new area
}

void BlockItem::setLeaderHighlight(const QUuid& segmentId)
{
    if (m_leaderEntity == segmentId) return;
    m_leaderEntity = segmentId;
    // Curves may also be the leader candidate (teal recolor, see CurveItem).
    for (auto* ci : m_cache.curveItems())
        ci->setLeader(segmentId == ci->curveId());
    update();
}

void BlockItem::setToolSelected(bool selected)
{
    if (m_toolSelected == selected) return;
    m_toolSelected = selected;

    // Push the new state to the animator so the red highlight animates in/out.
    forEachEntityPushState();
    update();
}

void BlockItem::setToolLocked(bool locked)
{
    if (m_toolLocked == locked) return;
    m_toolLocked = locked;

    // Push the new state to the animator so the bold weight animates in/out.
    forEachEntityPushState();
    update();
}

void BlockItem::setSelectedPoint(const QUuid& pointId)
{
    if (m_selectedPointId != pointId) {
        m_selectedPointId = pointId;
        update();
    }
}

QVariant BlockItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        // Keep the Block's Transform origin in sync when the item position is
        // set programmatically (e.g. group drag via ToolSelect, or resolve).
        // Scene pos (+Y down) → user coords (+Y up).
        QPointF newPos = value.toPointF();
        if (m_doc) {
            cad::param::Block* block = m_doc->findBlock(m_blockId);
            if (block) {
                block->transform.origin = cad::geo::Coord::toUser(newPos);
            }
        }
    }
    if (change == ItemPositionHasChanged) {
        update();
    }
    if (change == ItemSelectedHasChanged) {
        // Selection state changed — push new states to animator.
        forEachEntityPushState();
        update();
    }
    return QGraphicsObject::itemChange(change, value);
}

// ---------------------------------------------------------------------------
// Hover handling
// ---------------------------------------------------------------------------

void BlockItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    // Only snap-eligible layers are hoverable (grayed WORKING layers stay
    // hoverable so connections can be aimed; a grayed auxiliary layer is
    // reference-only — same policy as layerSnappable()).
    if (m_cache.layerMode() == LayerMode::Hidden) {
        event->accept();
        return;
    }
    const auto* block = m_doc ? m_doc->findBlock(m_blockId) : nullptr;
    if (!block || !m_doc->layersView().layerSnappable(block->layer)) {
        event->accept();
        return;
    }

    // Convert hover threshold from screen px to scene units.
    const double threshold = hoverThreshold();

    const QPointF localPos = event->pos();
    // Point hover has TOP priority: near a point → highlight it (and clear
    // any line hover). Points are tiny, so without this the cursor gives no
    // "this is a grab/connect point" affordance — users had to rely on
    // intuition to aim at endpoints.
    const QUuid pointHit = hitTestPoint(localPos, threshold);
    if (!pointHit.isNull()) {
        if (m_hoveredPointId != pointHit) { m_hoveredPointId = pointHit; update(); }
        if (!m_hoveredEntity.isNull())
            updateHoverState(QUuid());
        event->accept();
        return;
    }
    if (!m_hoveredPointId.isNull()) { m_hoveredPointId = QUuid(); update(); }

    // A curve child under the cursor keeps its own highlight (the child
    // already notified us via onCurveHover); line hover must not override it.
    if (!m_curvesUnderCursor.isEmpty()) {
        // Safety: if a line highlight somehow survived the arbitration, drop it.
        if (!m_hoveredEntity.isNull() && !isCurveId(m_hoveredEntity))
            updateHoverState(QUuid());
        event->accept();
        return;
    }
    const QUuid hit = hitTest(localPos, threshold);
    updateHoverState(hit);
    event->accept();
}

void BlockItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    if (!m_hoveredPointId.isNull()) { m_hoveredPointId = QUuid(); update(); }
    updateHoverState(QUuid());  // Clear hover
    event->accept();
}

/// Pick tolerance in scene units: screen px ÷ view zoom.
double BlockItem::hoverThreshold() const
{
    return BlockItemPick::computeHoverThreshold(qobject_cast<CanvasScene*>(scene()));
}

QUuid BlockItem::hitTestPoint(const QPointF& localPos, double radius) const
{
    return BlockItemPick::hitTestPoints(m_cache.points(), localPos, radius);
}

void BlockItem::updateHoverState(const QUuid& newHover)
{
    if (newHover == m_hoveredEntity)
        return;

    auto* cs = qobject_cast<CanvasScene*>(scene());
    if (!cs) {
        m_hoveredEntity = newHover;
        update();
        return;
    }
    CanvasAnimator* anim = cs->animator();

    const QUuid oldHover = m_hoveredEntity;
    // Update FIRST so resolveState() sees the new hover target.
    m_hoveredEntity = newHover;

    // Lift the whole block above siblings while something in it is hovered,
    // so the recolored entity is never buried under an overlapping block.
    setZValue(newHover.isNull() ? 1.0 : 1.5);

    // Old hovered entity reverts to its non-hover state.
    if (!oldHover.isNull()) {
        const EntityState st = static_cast<EntityState>(resolveState(oldHover));
        anim->setState(this, oldHover, st);
    }

    // New hovered entity gets hover state (unless overridden by selection).
    if (!newHover.isNull()) {
        const EntityState st = static_cast<EntityState>(resolveState(newHover));
        anim->setState(this, newHover, st);
    }

    update();
}

// ---------------------------------------------------------------------------
// Curve-child hover arbitration (曲线拆子item: 悬停仲裁)
// ---------------------------------------------------------------------------

void BlockItem::onCurveHover(CurveItem* item, const QPointF& localPos)
{
    m_curvesUnderCursor.insert(item);

    // A line closer than HALF the threshold still wins over the curve
    // (matches the legacy hitTest ordering, where curves scored a constant
    // approxDist = threshold * 0.5).
    const double threshold = hoverThreshold();
    double lineDist = 0.0;
    const QUuid lineHit = hitTest(localPos, threshold, &lineDist);
    if (!lineHit.isNull() && lineDist < threshold * 0.5) {
        item->setHoveredByParent(false);
        updateHoverState(lineHit);
        return;
    }

    // Curve wins: restore the previous entity, then highlight the curve.
    const QUuid oldHover = m_hoveredEntity;
    m_hoveredEntity = item->curveId();
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        CanvasAnimator* anim = cs->animator();
        if (!oldHover.isNull())
            anim->setState(this, oldHover,
                           static_cast<EntityState>(resolveState(oldHover)));
        anim->setState(this, item->curveId(),
                       static_cast<EntityState>(EntityState::Hover));
    }
    setZValue(1.5);  // lift the block above siblings (same as updateHoverState)
    update();
}

void BlockItem::onCurveHoverLeave(CurveItem* item)
{
    m_curvesUnderCursor.remove(item);
    item->setHoveredByParent(false);
    if (m_hoveredEntity == item->curveId())
        updateHoverState(QUuid());  // clear — hoverMoveEvent re-arbitrates
}

bool BlockItem::isCurveId(const QUuid& entityId) const
{
    for (auto* ci : m_cache.curveItems())
        if (ci->curveId() == entityId) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Scene hit-test helper (曲线拆子item: 场景命中上溯)
// ---------------------------------------------------------------------------

BlockItem* BlockItem::containingItem(QGraphicsItem* item)
{
    for (QGraphicsItem* cur = item; cur; cur = cur->parentItem())
        if (auto* bi = qgraphicsitem_cast<BlockItem*>(cur))
            return bi;
    return nullptr;
}

// ---------------------------------------------------------------------------
// State resolution
// ---------------------------------------------------------------------------

int BlockItem::resolveState(const QUuid& entityId) const
{
    // Priority: Locked > Selected > Hover > Normal.
    // Locked = selected (bold), driven by m_toolLocked (2026-09 取消确认
    // 基准: 选中即加粗, 原 confirmed 语义并入).
    // Selection is driven by the tool-managed flag (m_toolSelected) rather
    // than Qt's isSelected(), giving the selection tool full manual control.
    if (m_toolLocked)
        return static_cast<int>(EntityState::Locked);
    if (m_toolSelected)
        return static_cast<int>(EntityState::Selected);
    if (entityId == m_hoveredEntity && !m_hoveredEntity.isNull())
        return static_cast<int>(EntityState::Hover);
    return static_cast<int>(EntityState::Normal);
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

QUuid BlockItem::hitTest(const QPointF& localPos, double threshold,
                         double* bestDistOut) const
{
    return BlockItemPick::hitTestLines(m_cache.lines(), localPos, threshold, bestDistOut);
}

void BlockItem::forEachEntityPushState()
{
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        CanvasAnimator* anim = cs->animator();
        if (!anim) return;
        for (const auto& lc : m_cache.lines()) {
            const EntityState st = static_cast<EntityState>(resolveState(lc.id));
            anim->setState(this, lc.id, st);
        }
        for (auto* ci : m_cache.curveItems()) {
            const EntityState st = static_cast<EntityState>(resolveState(ci->curveId()));
            anim->setState(this, ci->curveId(), st);
        }
        for (const auto& pc : m_cache.points()) {
            const EntityState st = static_cast<EntityState>(resolveState(pc.id));
            anim->setState(this, pc.id, st);
        }
    }
}

void BlockItem::rebuildCache()
{
    m_hoveredPointId = QUuid();  // cache rebuild drops transient hover state
    // Curve children are rebuilt from scratch (their geometry may be stale).
    // Deleting them also drops any in-flight hover report — a mid-rebuild
    // cursor position will simply re-trigger hover after the rebuild.
    m_curvesUnderCursor.clear();
    m_cachedShapeTol = -1.0;  // invalidate shape cache (geometry changed)

    if (m_cache.rebuild(m_blockId, m_doc, this)) {
        setPos(m_cache.originScene());
        setVisible(m_cache.layerMode() != LayerMode::Hidden);
        setAcceptHoverEvents(m_cache.snapEligible());
    }

    // A layer-mode flip may leave a stale hover highlight behind — drop it.
    if (!m_hoveredEntity.isNull() || !m_hoveredPointId.isNull()) {
        m_hoveredEntity = QUuid();
        m_hoveredPointId = QUuid();
    }
}
