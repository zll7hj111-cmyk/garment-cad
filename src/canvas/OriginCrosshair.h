#pragma once

#include <QGraphicsItem>

/// Draws a light-gray crosshair at the scene origin (0,0),
/// representing the X and Y axes in DXF coordinate system.
class OriginCrosshair : public QGraphicsItem
{
public:
    OriginCrosshair();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    static constexpr double EXTENT = 100000.0;
};
