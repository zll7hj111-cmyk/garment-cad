#include "OriginCrosshair.h"

#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

OriginCrosshair::OriginCrosshair()
{
    // Not selectable, not movable - purely a visual reference
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setZValue(-1000.0); // Draw behind everything
}

QRectF OriginCrosshair::boundingRect() const
{
    return QRectF(-EXTENT, -EXTENT, EXTENT * 2.0, EXTENT * 2.0);
}

void OriginCrosshair::paint(QPainter* painter,
                            const QStyleOptionGraphicsItem* option,
                            QWidget* /*widget*/)
{
    QPen pen(QColor(200, 200, 200), 1.0);
    pen.setCosmetic(true); // Line width stays 1px regardless of zoom
    painter->setPen(pen);

    // Use exposed rect to limit drawing to visible area for performance
    QRectF exposed = option->exposedRect;

    // X axis (horizontal line through origin)
    if (exposed.top() <= 0.0 && exposed.bottom() >= 0.0) {
        painter->drawLine(QLineF(exposed.left(), 0.0, exposed.right(), 0.0));
    }

    // Y axis (vertical line through origin)
    if (exposed.left() <= 0.0 && exposed.right() >= 0.0) {
        painter->drawLine(QLineF(0.0, exposed.top(), 0.0, exposed.bottom()));
    }
}
