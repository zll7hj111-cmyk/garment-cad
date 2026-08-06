#include "GroupBadgeItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

GroupBadgeItem::GroupBadgeItem(const QUuid& groupId, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_groupId(groupId)
{
    setAcceptHoverEvents(true);
    setCursor(Qt::PointingHandCursor);
    // Above block items (which lift to 1.5 on hover), below marquee/toast.
    setZValue(900.0);
}

QRectF GroupBadgeItem::boundingRect() const
{
    return m_rect;
}

void GroupBadgeItem::setText(const QString& text)
{
    if (m_text == text) return;
    m_text = text;

    QFont font;
    font.setPixelSize(11);
    QFontMetrics fm(font);
    const double padX = 9.0, padY = 3.5;
    const double w = fm.horizontalAdvance(m_text) + padX * 2.0;
    const double h = fm.height() + padY * 2.0;
    m_rect = QRectF(0.0, 0.0, w, h);
    prepareGeometryChange();
    update();
}

void GroupBadgeItem::setAccent(bool on)
{
    if (m_accent == on) return;
    m_accent = on;
    update();
}

void GroupBadgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                           QWidget* widget)
{
    Q_UNUSED(widget);
    if (m_rect.isEmpty() || m_text.isEmpty()) return;

    // Design tokens match the panel palette: #F4F6F7 background, #2E86C1
    // accent, #34495E primary text, #7F8C8D secondary.
    const QColor border = m_accent ? QColor(0x2E, 0x86, 0xC1) : QColor(0xD5, 0xDB, 0xDB);
    const QColor fill   = m_accent ? QColor(0xEB, 0xF5, 0xFB) : QColor(0xFF, 0xFF, 0xFF);
    const QColor text   = m_accent ? QColor(0x2E, 0x86, 0xC1) : QColor(0x7F, 0x8C, 0x8D);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(fill);
    painter->drawRoundedRect(m_rect, m_rect.height() / 2.0, m_rect.height() / 2.0);

    QFont font;
    font.setPixelSize(11);
    painter->setFont(font);
    painter->setPen(text);
    painter->drawText(m_rect, Qt::AlignCenter, m_text);
}

void GroupBadgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    event->accept();
    emit hoverChanged(true);
}

void GroupBadgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    event->accept();
    emit hoverChanged(false);
}

void GroupBadgeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressedInside = true;
        event->accept();
    } else {
        QGraphicsObject::mousePressEvent(event);
    }
}

void GroupBadgeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedInside) {
        m_pressedInside = false;
        if (m_rect.contains(event->pos()))
            emit clicked(m_groupId);
        event->accept();
    } else {
        QGraphicsObject::mouseReleaseEvent(event);
    }
}
