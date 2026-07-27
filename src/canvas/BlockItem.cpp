#include "BlockItem.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QStyleOptionGraphicsItem>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"   // cad::geo::Coord

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
    setZValue(1.0);

    rebuildCache();
}

QRectF BlockItem::boundingRect() const
{
    return m_cachedBounds.adjusted(-10, -10, 10, 10);
}

void BlockItem::paint(QPainter* painter,
                      const QStyleOptionGraphicsItem* /*option*/,
                      QWidget* /*widget*/)
{
    const bool selected = isSelected();

    // Draw segments
    for (const auto& lc : m_lines) {
        QPen linePen;
        if (selected) {
            linePen = QPen(QColor(220, 40, 40), lc.weight + 0.6);
        } else if (m_groupHighlight) {
            linePen = QPen(QColor(0, 120, 215, 130), lc.weight + 0.4);
        } else {
            linePen = QPen(lc.color, lc.weight);
        }
        linePen.setCosmetic(true);
        linePen.setStyle(lc.penStyle);
        painter->setPen(linePen);
        painter->drawLine(lc.p1, lc.p2);

        // Draw segment name if enabled
        if (lc.showName && !lc.name.isEmpty()) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QPen textPen = selected ? QPen(QColor(220, 40, 40))
                                    : QPen(QColor(100, 100, 100));
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            QFont font;
            font.setPixelSize(10);
            painter->setFont(font);
            painter->drawText(mid + QPointF(4, -4), lc.name);
        }

        // Draw segment length label if enabled
        if (lc.showLength && !lc.lengthText.isEmpty()) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QPen textPen = selected ? QPen(QColor(220, 40, 40))
                                    : QPen(QColor(0, 110, 60));
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            QFont font;
            font.setPixelSize(10);
            painter->setFont(font);
            // Offset below the name label to avoid overlap
            painter->drawText(mid + QPointF(4, 12), lc.lengthText);
        }
    }

    // Draw points
    for (const auto& pc : m_points) {
        if (pc.isAuxiliary) {
            QPen auxPen = selected ? QPen(QColor(220, 40, 40), 1.0)
                                   : QPen(QColor(150, 80, 0), 1.0);
            auxPen.setCosmetic(true);
            painter->setPen(auxPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(pc.pos, 3.0, 3.0);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? QColor(220, 40, 40)
                              : m_groupHighlight ? QColor(0, 120, 215, 130)
                                                 : QColor(30, 30, 30));
            painter->drawEllipse(pc.pos, 2.5, 2.5);
        }

        // Draw label
        if (pc.showLabel && !pc.label.isEmpty()) {
            QPen textPen = selected ? QPen(QColor(220, 40, 40))
                                    : QPen(QColor(80, 80, 80));
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            QFont font;
            font.setPixelSize(11);
            painter->setFont(font);
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

void BlockItem::setGroupHighlight(bool on)
{
    if (m_groupHighlight == on)
        return;
    m_groupHighlight = on;
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
        // No cache rebuild needed — geometry is in local coords, item pos handles offset.
        update();
    }
    return QGraphicsObject::itemChange(change, value);
}

void BlockItem::rebuildCache()
{
    m_lines.clear();
    m_points.clear();
    m_cachedBounds = QRectF();

    if (!m_doc) return;

    const cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // Item position = block origin in scene coords.
    // All cached geometry is LOCAL (relative to block origin).
    cad::geo::Vec2 origin = block->transform.origin;
    setPos(cad::geo::Coord::toScene(origin));

    // Build line cache from segments
    for (const auto& seg : block->segments) {
        if (!seg.visible) continue;

        cad::geo::Vec2 w1 = block->worldPos(seg.startPointId);
        cad::geo::Vec2 w2 = block->worldPos(seg.endPointId);

        // Convert to local scene coords: subtract origin, then Y-flip
        QPointF p1 = cad::geo::Coord::toScene(w1.x - origin.x, w1.y - origin.y);
        QPointF p2 = cad::geo::Coord::toScene(w2.x - origin.x, w2.y - origin.y);

        Qt::PenStyle ps = Qt::SolidLine;
        if (seg.lineStyle == cad::param::LineStyle::Dashed) ps = Qt::DashLine;
        else if (seg.lineStyle == cad::param::LineStyle::Dotted) ps = Qt::DotLine;

        // Pre-format the length label (internal mm → display cm).
        QString lenText;
        if (seg.showLength) {
            const cad::param::ParamPoint* sp = block->findPoint(seg.startPointId);
            const cad::param::ParamPoint* ep = block->findPoint(seg.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
                lenText = cad::geo::Units::formatLength(lenMm);
            }
        }

        m_lines.push_back({p1, p2, seg.color, seg.weight, ps, seg.name,
                           seg.showName, seg.showLength, lenText});

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

    // Build point cache
    for (const auto& pt : block->points) {
        if (!pt.visible || !pt.resolved) continue;

        cad::geo::Vec2 w = block->transform.toWorld(pt.resolvedPos);
        QPointF pos = cad::geo::Coord::toScene(w.x - origin.x, w.y - origin.y);  // local scene coords
        m_points.push_back({pos, pt.isAuxiliary, pt.name, pt.showName});

        // Include label area in bounds to prevent ghosting during drag
        QRectF ptBounds(pos - QPointF(6, 6), pos + QPointF(6, 6));
        if (pt.showName && !pt.name.isEmpty()) {
            ptBounds |= QRectF(pos + QPointF(5, -16), pos + QPointF(5 + pt.name.length() * 8, 4));
        }
        m_cachedBounds |= ptBounds;
    }
}
