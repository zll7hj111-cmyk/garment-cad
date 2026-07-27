#include "SerialDelegate.h"

#include <QPainter>
#include <QApplication>
#include <QFontMetrics>

#include "parametric/Serial.h"

namespace cad::ui {

SerialDelegate::SerialDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void SerialDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const
{
    const QString serial = index.data(SerialRole).toString();
    if (serial.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Let the style draw the background / selection chrome, but no text.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    const QWidget* widget = option.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
    if (!textRect.isValid())
        textRect = opt.rect.adjusted(4, 0, -4, 0);

    const QString pfx = cad::param::Serial::prefix(serial);
    const QString tg  = cad::param::Serial::tag(serial);

    painter->save();
    QFont f = opt.font;

    // Gray random prefix.
    painter->setFont(f);
    painter->setPen(QColor(0x9a, 0x9a, 0x9a));
    const QFontMetrics fm(f);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, pfx);
    const int w = fm.horizontalAdvance(pfx);

    // Bold red type-tag.
    QFont bf = f;
    bf.setBold(true);
    painter->setFont(bf);
    painter->setPen(QColor(0xd4, 0x00, 0x00));
    QRect tagRect = textRect;
    tagRect.setLeft(textRect.left() + w);
    painter->drawText(tagRect, Qt::AlignLeft | Qt::AlignVCenter, tg);
    painter->restore();
}

} // namespace cad::ui
