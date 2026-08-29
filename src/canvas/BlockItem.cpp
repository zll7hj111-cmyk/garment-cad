#include "BlockItem.h"
#include "CanvasScene.h"
#include "CanvasAnimator.h"
#include "CanvasStyle.h"
#include "CurveItem.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>
#include <QSet>
#include <QStyleOptionGraphicsItem>

#include "parametric/ParamDocument.h"
#include "parametric/Serial.h"
#include "parametric/Block.h"
#include "parametric/PerfProbe.h"
#include "geometry/Units.h"   // cad::geo::Coord
#include "geometry/CurveMath.h"

#include <cmath>
#include <limits>

namespace {

/// Shared font instances — creating a QFont per label per frame is expensive
/// (font engine resolution). Pixel-size fonts are device-independent.
const QFont& nameFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(10); return fnt; }();
    return f;
}
const QFont& labelFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(11); return fnt; }();
    return f;
}
/// Length annotations: monospace digits so drag readouts never jitter.
const QFont& lengthFont()
{
    static QFont f = [] {
        QFont fnt;
        fnt.setFamilies({QStringLiteral("Consolas"),
                         QStringLiteral("Courier New"),
                         QStringLiteral("monospace")});
        fnt.setPixelSize(10);
        return fnt;
    }();
    return f;
}

} // namespace

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

QRectF BlockItem::boundingRect() const
{
    // Margin must cover the pick band: shape() strokes lines with
    // tol = hoverRadiusPx ÷ zoom and points with 2.5px ÷ zoom. The scene's
    // spatial index (BSP) rejects items whose boundingRect does not contain
    // the query point, so a margin smaller than the band makes the item
    // unpickable at low zoom (zoom 0.2 → tol ≈ 40 local units; the old ±10
    // margin silently dropped band-only hits).
    constexpr double kPickMargin = 42.0;
    return m_cachedBounds.adjusted(-kPickMargin, -kPickMargin, kPickMargin, kPickMargin);
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
        if (std::abs(m11) > 1e-9)
            pxToLocal = 1.0 / std::abs(m11);
    }
    const double tol = (style ? style->hoverRadiusPx() : 8.0) * pxToLocal;

    // Return the cached path when the tolerance has not changed enough to
    // matter (sub-pixel difference). Rebuilding the stroked path for every
    // hover/collision query is the single largest CPU cost on mouse-move.
    if (m_cachedShapeTol > 0.0 &&
        std::abs(tol - m_cachedShapeTol) < m_cachedShapeTol * 0.02)
        return m_cachedShape;

    QPainterPath path;
    QPainterPathStroker stroker;
    stroker.setWidth(tol * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    GCAD_PERF_SCOPE("shape.rebuild");
    for (const auto& lc : m_lines) {
        QPainterPath seg;
        seg.moveTo(lc.p1);
        seg.lineTo(lc.p2);
        path.addPath(stroker.createStroke(seg));
    }
    // Curves contribute through their OWN child items (CurveItem::shape) —
    // the framework hit-tests them independently, so the block shape only
    // covers lines and points.
    // Points contribute only their visual disc (they are tiny; the segment
    // band already covers their surroundings for block-level picking).
    // PICK radius is unified at 2.5 for ALL point kinds — deliberately larger
    // than the 0.8 visual radius so grabbing stays finger-friendly.
    for (const auto& pc : m_points) {
        const double rPx = 2.5;
        const double r = rPx * pxToLocal;
        path.addEllipse(pc.pos, r, r);
    }

    m_cachedShape = path;
    m_cachedShapeTol = tol;
    return path;
}

void BlockItem::paint(QPainter* painter,
                      const QStyleOptionGraphicsItem* /*option*/,
                      QWidget* /*widget*/)
{
    GCAD_PERF_SCOPE("paint");
    // Obtain animator from scene (may be null in edge cases).
    CanvasAnimator* animator = nullptr;
    const CanvasStyle* style = nullptr;
    bool forceName = false, forceLen = false;  // Hold-to-show (N/M keys).
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        animator  = cs->animator();
        style     = cs->style();
        forceName = cs->forceShowName();
        forceLen  = cs->forceShowLength();
    }

    // A block on a hidden layer is not painted at all (setVisible(false) also
    // keeps it out of hit-testing, but guard here too for safety).
    if (m_layerMode == LayerMode::Hidden)
        return;

    // Non-active visible layer: render as a gray, semi-transparent reference.
    const bool grayed = (m_layerMode == LayerMode::Grayed);
    const QColor kGray(0x9E, 0x9E, 0x9E);
    if (grayed)
        painter->setOpacity(0.4);

    // Draw segments — the hovered one is drawn LAST so its highlight sits on
    // top of sibling segments instead of being buried under them.
    // Hidden segments (lc.visible == false) are painted only when transiently
    // revealed (hovered or leader-highlighted), in a ghost style.
    constexpr int kGhostAlpha = 110;  ///< Alpha for transiently-revealed hidden lines.
    auto drawSegment = [&](const LineCache& lc) {
        const bool ghost = !lc.visible;
        // Dark-mode adaptation: lift the data color to the role's
        // light-on-dark family so ink lines stay legible on night paper.
        const QColor paintColor = style ? style->displayColor(lc.role, lc.color)
                                        : lc.color;
        EntityPaintParams pp;
        if (animator) {
            pp = animator->lineParams(this, lc.id,
                                      paintColor, lc.weight);
        } else {
            // Fallback: resolve state directly without animation.
            pp.lineColor  = paintColor;
            pp.lineWidth  = lc.weight;
            pp.labelColor = QColor(100, 100, 100);
        }

        QPen linePen(pp.lineColor, pp.lineWidth);
        linePen.setCosmetic(true);
        linePen.setStyle(lc.penStyle);
        // Leader-candidate override: teal recolor only ("connection" family),
        // same width — consistent with the hover-recolors-only language.
        if (lc.id == m_leaderEntity && style)
            linePen.setColor(style->attachmentNodeColor);
        // Grayed layers keep the highlight on hovered/leader segments — the
        // affordance that lets the user aim a connection; everything else
        // falls back to the gray reference tint.
        if (grayed && lc.id != m_leaderEntity && lc.id != m_hoveredEntity)
            linePen.setColor(kGray);
        if (ghost) {
            QColor c = linePen.color();
            c.setAlpha(kGhostAlpha);
            linePen.setColor(c);
        }
        painter->setPen(linePen);
        painter->drawLine(lc.p1, lc.p2);

        // Draw segment name if enabled (suppressed on grayed reference layers).
        if ((lc.showName || forceName) && !lc.name.isEmpty() && !grayed) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QColor nameColor = pp.labelColor;
            if (ghost) nameColor.setAlpha(kGhostAlpha);
            QPen textPen(nameColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(nameFont());
            painter->drawText(mid + QPointF(4, -4), lc.name);
        }

        // Draw segment length label if enabled (suppressed on grayed layers).
        if ((lc.showLength || forceLen) && !lc.lengthText.isEmpty() && !grayed) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QColor lenColor = animator ? pp.lengthLabelColor
                : (style ? style->labelColor(EntityState::Normal, true)
                         : QColor(0, 110, 60));
            if (ghost) lenColor.setAlpha(kGhostAlpha);
            QPen textPen(lenColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(lengthFont());
            painter->drawText(mid + QPointF(4, 12), lc.lengthText);
        }
    };

    const LineCache* hoveredLine = nullptr;
    const LineCache* leaderLine  = nullptr;
    for (const auto& lc : m_lines) {
        if (lc.id == m_hoveredEntity) {
            hoveredLine = &lc;
            continue;
        }
        if (lc.id == m_leaderEntity) {
            leaderLine = &lc;
            continue;
        }
        if (!lc.visible) continue;  // hidden and not revealed — skip painting
        drawSegment(lc);
    }
    // Highlighted segments last: leader below, hovered on top. Both are drawn
    // even when hidden (ghosted) so a hover can reveal a hidden segment.
    if (leaderLine)
        drawSegment(*leaderLine);
    if (hoveredLine)
        drawSegment(*hoveredLine);

    // Curves are painted by their OWN child items (CurveItem::paint) —
    // each handles its hover/ghost/grayed/leader states itself.

    // Draw points
    // Dedup key: 0.1 mm-grid position packed into an int64 (two int32 lands)
    // instead of QString::number + concat per point per frame — the old key
    // allocated 2 QStrings + a concat on EVERY labeled point in EVERY paint.
    QSet<qint64> drawnPointLabels;
    for (const auto& pc : m_points) {
        EntityPaintParams pp;
        if (animator) {
            pp = animator->pointParams(this, pc.id,
                                       pc.isAuxiliary);
        } else {
            pp.pointFill   = pc.isAuxiliary ? QColor(67, 160, 71) : QColor(30, 30, 30);
            pp.pointRadius = 0.8;   // unified marker size (all point kinds)
            pp.labelColor  = QColor(80, 80, 80);
        }

        // Curve anchors (曲线点) render as a small ETCAD-style pink disc —
        // compact like ETCAD's curve points, distinct from endpoints/aux points.
        // Visual radius is intentionally much smaller than the PICK radius
        // (shape() keeps 2.5 for anchors): the hit area must stay finger-friendly
        // even though the dot is now a subtle marker.
        if (pc.isCurveAnchor) {
            pp.pointFill   = QColor(0xE9, 0x1E, 0x63);  // ETCAD pink
            pp.pointRadius = 0.8;   // 原 2.0 → 缩小一半多；命中范围不变 (shape() 2.5)
        }

        // Hovered point: enlarged + teal — the "this is a grab/connect point"
        // affordance (same highlight language as hovered lines).
        if (pc.id == m_hoveredPointId) {
            pp.pointFill   = QColor(38, 166, 154);
            pp.pointRadius = 1.6;
        }

        // Both point kinds render as solid discs; auxiliary points are
        // distinguished by their green fill (绿色实心小圆) and slightly
        // larger radius. Grayed layers keep the teal on the hovered point.
        if (grayed && pc.id != m_hoveredPointId)
            pp.pointFill = kGray;
        painter->setPen(Qt::NoPen);
        painter->setBrush(pp.pointFill);
        painter->drawEllipse(pc.pos, pp.pointRadius, pp.pointRadius);

        // Anchor ring marking a connection point (attachment node). Protected
        // connections use the amber ring (拖动保护视觉区分).
        if (pc.isAttachmentNode && style && style->attachmentRingWidth > 0.0) {
            QPen ringPen(pc.isLockedNode ? style->lockedAttachmentColor
                                         : style->attachmentNodeColor,
                         style->attachmentRingWidth);
            ringPen.setCosmetic(true);
            painter->setPen(ringPen);
            painter->setBrush(Qt::NoBrush);
            const double r = pp.pointRadius + style->attachmentRingGap;
            painter->drawEllipse(pc.pos, r, r);
        }

        // Draw label (suppressed on grayed reference layers). Overlapping
        // points sharing the same name render ONE label (deduped by a
        // 0.1 mm-grid position key).
        if ((pc.showLabel || forceName) && !pc.label.isEmpty() && !grayed) {
            const qint64 kx = static_cast<qint64>(qRound(pc.pos.x() * 10.0));
            const qint64 ky = static_cast<qint64>(qRound(pc.pos.y() * 10.0));
            const qint64 posKey = (kx << 32) | (static_cast<quint64>(ky) & 0xFFFFFFFFULL);
            if (drawnPointLabels.contains(posKey))
                continue;
            drawnPointLabels.insert(posKey);
            QPen textPen(pp.labelColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(labelFont());
            painter->drawText(pc.pos + QPointF(5, -5), pc.label);
        }
    }
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
    if (std::abs(block->transform.rotation - m_lastRotation) > 1e-9 ||
        block->geometryEpoch() != m_lastGeometryEpoch ||
        block->points.size() != m_lastPointCount) {
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
    for (auto* ci : m_curveItems)
        ci->setLeader(segmentId == ci->curveId());
    update();
}

void BlockItem::setToolSelected(bool selected)
{
    if (m_toolSelected == selected) return;
    m_toolSelected = selected;

    // Push the new state to the animator so the red highlight animates in/out.
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        CanvasAnimator* anim = cs->animator();
        for (const auto& lc : m_lines)
            anim->setState(this, lc.id,
                           static_cast<EntityState>(resolveState(lc.id)));
        for (auto* ci : m_curveItems)
            anim->setState(this, ci->curveId(),
                           static_cast<EntityState>(resolveState(ci->curveId())));
        for (const auto& pc : m_points)
            anim->setState(this, pc.id,
                           static_cast<EntityState>(resolveState(pc.id)));
    }
    update();
}

void BlockItem::setToolLocked(bool locked)
{
    if (m_toolLocked == locked) return;
    m_toolLocked = locked;

    // Push the new state to the animator so the bold weight animates in/out.
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        CanvasAnimator* anim = cs->animator();
        for (const auto& lc : m_lines)
            anim->setState(this, lc.id,
                           static_cast<EntityState>(resolveState(lc.id)));
        for (auto* ci : m_curveItems)
            anim->setState(this, ci->curveId(),
                           static_cast<EntityState>(resolveState(ci->curveId())));
        for (const auto& pc : m_points)
            anim->setState(this, pc.id,
                           static_cast<EntityState>(resolveState(pc.id)));
    }
    update();
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
        if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
            CanvasAnimator* anim = cs->animator();
            for (const auto& lc : m_lines) {
                const EntityState st = static_cast<EntityState>(resolveState(lc.id));
                anim->setState(this, lc.id, st);
            }
            for (auto* ci : m_curveItems) {
                const EntityState st = static_cast<EntityState>(resolveState(ci->curveId()));
                anim->setState(this, ci->curveId(), st);
            }
            for (const auto& pc : m_points) {
                const EntityState st = static_cast<EntityState>(resolveState(pc.id));
                anim->setState(this, pc.id, st);
            }
        }
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
    if (m_layerMode == LayerMode::Hidden) {
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
    double threshold = 8.0;  // default
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        threshold = cs->style()->hoverRadiusPx();
        const qreal m11 = cs->currentZoom();
        if (std::abs(m11) > 1e-9)
            threshold /= std::abs(m11);
    }
    return threshold;
}

QUuid BlockItem::hitTestPoint(const QPointF& localPos, double radius) const
{
    QUuid best;
    double bestDistSq = radius * radius;
    for (const auto& pc : m_points) {
        const double dx = pc.pos.x() - localPos.x();
        const double dy = pc.pos.y() - localPos.y();
        const double d = dx * dx + dy * dy;
        if (d < bestDistSq) {
            bestDistSq = d;
            best = pc.id;
        }
    }
    return best;
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
    for (auto* ci : m_curveItems)
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
    // Points are deliberately NOT hover targets: they are tiny snap anchors,
    // and every point interaction (double-click edit, pen-tool connection via
    // SnapEngine) goes through segments or the document directly. Hovering
    // only ever highlights segments — no contention at endpoints.
    // Curves are hit-tested by their OWN child items (CurveItem::shape).
    double bestDist = threshold;
    QUuid bestId;

    // Test line segments.
    for (const auto& lc : m_lines) {
        // Distance from point to line segment.
        const double ax = lc.p1.x(), ay = lc.p1.y();
        const double bx = lc.p2.x(), by = lc.p2.y();
        const double px = localPos.x(), py = localPos.y();

        const double abx = bx - ax, aby = by - ay;
        const double apx = px - ax, apy = py - ay;
        const double lenSq = abx * abx + aby * aby;

        double t = 0.0;
        if (lenSq > 1e-12)
            t = std::clamp((apx * abx + apy * aby) / lenSq, 0.0, 1.0);

        const double cx = ax + t * abx - px;
        const double cy = ay + t * aby - py;
        const double dist = std::sqrt(cx * cx + cy * cy);

        if (dist < bestDist) {
            bestDist = dist;
            bestId = lc.id;
        }
    }

    if (bestDistOut)
        *bestDistOut = bestDist;
    return bestId;
}

void BlockItem::rebuildCache()
{
    GCAD_PERF_EVENT("cache.rebuild");
    m_lines.clear();
    m_hoveredPointId = QUuid();  // cache rebuild drops transient hover state
    // Curve children are rebuilt from scratch (their geometry may be stale).
    // Deleting them also drops any in-flight hover report — a mid-rebuild
    // cursor position will simply re-trigger hover after the rebuild.
    m_curvesUnderCursor.clear();
    for (auto* ci : m_curveItems) delete ci;  // child items leave the scene
    m_curveItems.clear();
    m_points.clear();
    m_cachedBounds = QRectF();
    m_cachedShapeTol = -1.0;  // invalidate shape cache (geometry changed)

    if (!m_doc) return;

    const cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // Track rotation so syncFromBlock() can detect rigid-body rotation.
    m_lastRotation = block->transform.rotation;
    m_lastGeometryEpoch = block->geometryEpoch();
    m_lastPointCount = block->points.size();

    // Item position = block origin in scene coords.
    // All cached geometry is LOCAL (relative to block origin).
    cad::geo::Vec2 origin = block->transform.origin;
    setPos(cad::geo::Coord::toScene(origin));

    // Build line cache from segments.
    // Hidden segments (seg.visible == false) are deliberately KEPT in the cache
    // so they still contribute to shape()/hitTest() — the user must be able to
    // hover (transient reveal) and double-click them to re-open properties and
    // turn visibility back on. They are simply not painted unless hovered.
    for (const auto& seg : block->segments) {
        // --- Curve segment ---
        if (seg.isCurve()) {
            // Frame-level Bézier cache: spans, flattened polyline, label
            // midpoint/tangent and exact arc length are built ONCE per resolve
            // pass (Block::rebuildCurveCache) and shared with the snap engine /
            // tangent handles. This rebuild only applies rotation + Y-flip — no
            // re-solve, no re-flatten, no re-integration.
            const cad::param::CurveSpanEntry* entry = block->curveSpanEntry(seg.id);
            if (!entry || entry->spans.empty()) continue;

            // Convert to local scene coords: apply block transform (rotation),
            // subtract world origin, then Y-flip — same as point cache below.
            // cos/sin hoisted: the flatten polyline has dozens of points and
            // each would otherwise recompute the rotation trig.
            const double rot = block->transform.rotation;
            const double cosR = std::cos(rot), sinR = std::sin(rot);
            auto toLocal = [&](const cad::geo::Vec2& localPos) -> QPointF {
                const double rx = localPos.x * cosR - localPos.y * sinR;
                const double ry = localPos.x * sinR + localPos.y * cosR;
                return cad::geo::Coord::toScene(rx, ry);
            };

            // Render the curve as a dense POLYLINE (Seamly2D technique): the
            // Bézier spans were flattened ONCE per resolve into discrete
            // points (0.1 mm tolerance — below visual resolution at any zoom).
            // Painting line segments is far cheaper than a cubic QPainterPath
            // — the rasterizer / GL backend draws lines directly, while cubic
            // segments are recursively subdivided and triangulated on EVERY
            // repaint.
            const auto& flat = entry->flatLocal;
            if (flat.empty()) continue;
            QPainterPath curvePath;
            curvePath.moveTo(toLocal(flat.front()));
            for (size_t fi = 1; fi < flat.size(); ++fi)
                curvePath.lineTo(toLocal(flat[fi]));

            // Label position: parametric midpoint (t = 0.5), cached at resolve
            // time. For smooth garment curves this is visually close to the
            // arc-length midpoint but avoids the expensive arc-length bisection
            // on every rebuild (the length text below uses the exact cached
            // length).
            const QPointF labelPos = toLocal(entry->labelLocal);
            const cad::geo::Vec2& midTan = entry->labelLocalDir;
            // Rotate tangent by block rotation for correct label orientation.
            const cad::geo::Vec2 worldTan(midTan.x * cosR - midTan.y * sinR,
                                           midTan.x * sinR + midTan.y * cosR);
            const double labelAngle = std::atan2(-worldTan.y, worldTan.x);  // scene Y-flip

            // Pre-format arc-length label (exact arc length, cached at resolve).
            // ALWAYS formatted — the hold-to-show force (L key) reveals the
            // length even when seg.showLength is off.
            const QString lenText = cad::geo::Units::formatLength(entry->arcLengthMm);

            Qt::PenStyle ps = Qt::SolidLine;
            if (seg.lineStyle == cad::param::LineStyle::Dashed) ps = Qt::DashLine;
            else if (seg.lineStyle == cad::param::LineStyle::Dotted) ps = Qt::DotLine;

            // Hit-test shape: the SAME dense flattened path that is painted
            // (CurveItem::shape strokes it). The old "coarse control-polygon"
            // hit region deviates from the drawn curve by many millimetres on
            // strong curves — clicks ON the curve body missed the pick band
            // (选择工具对曲线判定失灵, 用户报告 2026-10). The item-level stroke
            // cache (CurveItem::shape) keeps the per-frame cost the same.
            auto* curveItem = new CurveItem(this, CurveItem::Data{
                seg.id, curvePath, labelPos, labelAngle,
                seg.color, seg.role, seg.weight, ps, seg.name,
                seg.showName, seg.showLength, lenText, seg.visible});
            m_curveItems.push_back(curveItem);

            m_cachedBounds |= curveItem->boundingRect();
            continue;
        }

        // --- Straight-line segment (existing logic) ---
        cad::geo::Vec2 w1 = block->worldPos(seg.startPointId);
        cad::geo::Vec2 w2 = block->worldPos(seg.endPointId);

        // Convert to local scene coords: subtract origin, then Y-flip
        QPointF p1 = cad::geo::Coord::toScene(w1.x - origin.x, w1.y - origin.y);
        QPointF p2 = cad::geo::Coord::toScene(w2.x - origin.x, w2.y - origin.y);

        Qt::PenStyle ps = Qt::SolidLine;
        if (seg.lineStyle == cad::param::LineStyle::Dashed) ps = Qt::DashLine;
        else if (seg.lineStyle == cad::param::LineStyle::Dotted) ps = Qt::DotLine;

        // Pre-format the length label (internal mm → display cm). ALWAYS
        // formatted — the hold-to-show force (L key) reveals the length even
        // when seg.showLength is off.
        QString lenText;
        {
            // 端点延长线：长度标注按"实际画出的长度"（本体+尾巴, D6）。
            const double lenMm = block->worldPos(seg.startPointId)
                                     .distanceTo(block->worldPos(seg.endPointId));
            lenText = cad::geo::Units::formatLength(lenMm);
        }

        m_lines.push_back({seg.id, p1, p2, seg.color, seg.role, seg.weight, ps,
                           seg.name, seg.showName, seg.showLength, lenText,
                           seg.visible});

        QRectF lineBounds = QRectF(p1, p2).normalized();
        QPointF mid((p1.x() + p2.x()) / 2.0, (p1.y() + p2.y()) / 2.0);
        if (seg.showName && !seg.name.isEmpty()) {
            lineBounds |= QRectF(mid + QPointF(4, -14), mid + QPointF(4 + seg.name.length() * 7, 4));
        }
        if (seg.showLength && !lenText.isEmpty()) {
            lineBounds |= QRectF(mid + QPointF(4, 4), mid + QPointF(4 + lenText.length() * 7, 20));
        }
        m_cachedBounds |= lineBounds;
    }

    // Collect the points of this block that participate in a connection
    // (either side — leader or follower) so they get the anchor-ring marker.
    // PROTECTED connections get the amber ring (拖动保护视觉区分).
    QSet<QUuid> attachmentPoints;
    QSet<QUuid> lockedPoints;
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            attachmentPoints.insert(att.fromPointId);
            if (att.isLocked) lockedPoints.insert(att.fromPointId);
        }
        if (att.toBlockId == m_blockId) {
            attachmentPoints.insert(att.toPointId);
            if (att.isLocked) lockedPoints.insert(att.toPointId);
        }
    }

    // Build point cache
    for (const auto& pt : block->points) {
        if (!pt.visible || !pt.resolved) continue;

        cad::geo::Vec2 w = block->transform.toWorld(block->effectiveLocalPos(pt.id));
        QPointF pos = cad::geo::Coord::toScene(w.x - origin.x, w.y - origin.y);  // local scene coords
        // Fall back to serial for unnamed aux/intersection points, otherwise the
        // "show name" checkbox has no visible effect.
        const QString pointLabel = pt.name.isEmpty()
            ? cad::param::Serial::tag(pt.serial) : pt.name;
        m_points.push_back({pt.id, pos, pt.isAuxiliary, pointLabel, pt.showName,
                            attachmentPoints.contains(pt.id),
                            pt.constraint == cad::param::PointConstraint::CurveAnchor,
                            lockedPoints.contains(pt.id)});

        // Include label area in bounds to prevent ghosting during drag
        QRectF ptBounds(pos - QPointF(6, 6), pos + QPointF(6, 6));
        if (pt.showName && !pointLabel.isEmpty()) {
            ptBounds |= QRectF(pos + QPointF(5, -16), pos + QPointF(5 + pointLabel.length() * 8, 4));
        }
        m_cachedBounds |= ptBounds;
    }

    // Layer display mode: a manually hidden layer is not painted nor pickable;
    // any non-active layer renders GRAYED — including the auxiliary layer,
    // whose construction geometry stays visible as a reference draft (only
    // the active layer is full color). Hover feedback follows SNAP eligibility
    // (layerSnappable): grayed WORKING layers stay hoverable so connections
    // can be aimed from the auxiliary layer; a grayed auxiliary layer is
    // reference-only (never a hover/snap target).
    if (!m_doc->layersView().layerVisible(block->layer)) {
        m_layerMode = LayerMode::Hidden;
    } else if (block->layer != m_doc->layersView().activeLayer()) {
        m_layerMode = LayerMode::Grayed;
    } else {
        m_layerMode = LayerMode::Normal;
    }
    const bool snapEligible = m_doc->layersView().layerSnappable(block->layer);
    setVisible(m_layerMode != LayerMode::Hidden);
    setAcceptHoverEvents(snapEligible);
    // A layer-mode flip may leave a stale hover highlight behind — drop it.
    if (!m_hoveredEntity.isNull() || !m_hoveredPointId.isNull()) {
        m_hoveredEntity = QUuid();
        m_hoveredPointId = QUuid();
    }
    // Curve children mirror the layer display mode (grayed reference layers
    // render at reduced opacity; hidden layers suppress everything via the
    // parent's visibility) and the hover eligibility.
    for (auto* ci : m_curveItems) {
        ci->setGrayed(m_layerMode == LayerMode::Grayed);
        ci->setAcceptHoverEvents(snapEligible);
    }
}
