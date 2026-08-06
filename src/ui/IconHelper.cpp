#include "IconHelper.h"

#include <QFile>
#include <QByteArray>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>

namespace cad::ui {

QIcon IconHelper::icon(const QString& resourcePath, const QColor& color)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon();

    QByteArray svgData = file.readAll();

    // Tint: replace currentColor with the requested color.
    if (color.isValid()) {
        svgData.replace("currentColor", color.name().toUtf8());
    }

    // Render at multiple sizes for crisp display on HiDPI.
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid())
        return QIcon();

    QIcon result;
    for (int size : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        renderer.render(&painter);
        painter.end();
        result.addPixmap(pm);
    }
    return result;
}

QIcon IconHelper::iconByName(const QString& name, const QColor& color)
{
    return icon(QStringLiteral(":/icons/%1.svg").arg(name), color);
}

QIcon IconHelper::icon2State(const QString& name, const QColor& normal, const QColor& active)
{
    QIcon base = iconByName(name, normal);
    QIcon result;
    for (int size : {16, 20, 24, 32, 48, 64}) {
        result.addPixmap(base.pixmap(size, size), QIcon::Normal, QIcon::Off);
        result.addPixmap(iconByName(name, active).pixmap(size, size), QIcon::Active, QIcon::Off);
        result.addPixmap(iconByName(name, active).pixmap(size, size), QIcon::Selected, QIcon::Off);
    }
    return result;
}

QIcon IconHelper::appIcon()
{
    return iconByName(QStringLiteral("t-shirt"), QColor(0x2E, 0x86, 0xC1));
}

} // namespace cad::ui
