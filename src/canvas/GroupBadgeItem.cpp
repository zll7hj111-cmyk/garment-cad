#include "GroupBadgeItem.h"

#include "CanvasScene.h"
#include "CanvasStyle.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

GroupBadgeItem::GroupBadgeItem(const QUuid& groupId, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_groupId(groupId)
{
    setAcceptHoverEvents(true);
    // Render behind block items so selection & snapping stay crisp.
    setZValue(10.0);
}

QRectF GroupBadgeItem::boundingRect() const
{
    if (m_showBoundingBox && !m_boxRectLocal.isEmpty())
        return m_boxRectLocal.adjusted(-2.0, -2.0, 2.0, 2.0);
    return QRectF();
}

QPainterPath GroupBadgeItem::shape() const
{
    // Interactive marker: hit-test only the dashed outline, NOT the interior.
    // A filled rect would swallow clicks on member endpoints inside the box and
    // break point-level connection gestures. The stroked outline is wide enough
    // for comfortable mouse/hover hits while leaving interiors to blocks.
    QPainterPath base;
    if (m_showBoundingBox && !m_boxRectLocal.isEmpty())
        base.addRoundedRect(m_boxRectLocal, 4.0, 4.0);
    if (base.isEmpty()) return QPainterPath();
    QPainterPathStroker stroker;
    stroker.setWidth(8.0);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(base).simplified();
}

void GroupBadgeItem::setAccent(bool on)
{
    if (m_accent == on) return;
    m_accent = on;
    update();
}

void GroupBadgeItem::setShowBoundingBox(bool show)
{
    if (m_showBoundingBox == show) return;
    prepareGeometryChange();
    m_showBoundingBox = show;
    update();
}

void GroupBadgeItem::setMemberSceneBounds(const QRectF& sceneBounds)
{
    if (sceneBounds.isEmpty()) {
        if (!m_boxRectLocal.isEmpty()) {
            prepareGeometryChange();
            m_boxRectLocal = QRectF();
            update();
        }
        return;
    }
    const QPointF myPos = pos();
    const double pad = 6.0;
    const QRectF newBoxLocal(sceneBounds.left() - myPos.x() - pad,
                             sceneBounds.top() - myPos.y() - pad,
                             sceneBounds.width() + pad * 2.0,
                             sceneBounds.height() + pad * 2.0);
    if (m_boxRectLocal != newBoxLocal) {
        prepareGeometryChange();
        m_boxRectLocal = newBoxLocal;
        update();
    }
}

void GroupBadgeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_groupId);
        event->accept();
        return;
    }
    QGraphicsObject::mousePressEvent(event);
}

void GroupBadgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    emit hoverChanged(true);
    event->accept();
}

void GroupBadgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    emit hoverChanged(false);
    event->accept();
}

void GroupBadgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                           QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (!m_showBoundingBox || m_boxRectLocal.isEmpty()) return;

    // Design tokens come from the scene's CanvasStyle.
    const auto* style = [&]() -> const CanvasStyle* {
        if (auto* cs = qobject_cast<CanvasScene*>(scene()))
            return cs->style();
        return nullptr;
    }();
    const QColor accent = style ? style->selectColorForBadge()
                                : QColor(0x2F, 0x6F, 0xED);
    const QColor border = m_accent ? accent
                                   : (style ? style->borderSoft()
                                            : QColor(0x9A, 0xA4, 0xB2));

    painter->setRenderHint(QPainter::Antialiasing, true);

    // Dashed bounding box outline
    painter->save();
    QPen dashPen(border);
    dashPen.setWidthF(1.0);
    dashPen.setStyle(Qt::CustomDashLine);
    dashPen.setDashPattern({5.0, 4.0});
    painter->setPen(dashPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(m_boxRectLocal, 4.0, 4.0);
    painter->restore();
}