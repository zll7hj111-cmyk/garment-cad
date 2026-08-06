#pragma once

#include <QGraphicsObject>
#include <QString>
#include <QUuid>

/// Canvas pill badge for a user group (组徽标): renders the group's label
/// (user name, falling back to the serial, plus the member count) anchored
/// at the top-left of the group's bounding box.
///
/// States:
///   - Normal: subtle white pill with a light border.
///   - Accent (hovered or selected): blue-tinted pill + accent text — the
///     group reads as one unit at a glance.
///   - Click: selects the whole group on canvas (CanvasScene re-emits the
///     click; the host window drives the selection tool).
class GroupBadgeItem : public QGraphicsObject
{
    Q_OBJECT

public:
    /// Custom item type so scene hit-testing can identify badges.
    enum { Type = UserType + 2 };
    [[nodiscard]] int type() const override { return Type; }

    explicit GroupBadgeItem(const QUuid& groupId, QGraphicsItem* parent = nullptr);

    [[nodiscard]] const QUuid& groupId() const { return m_groupId; }
    [[nodiscard]] QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    /// Label text (already formatted, e.g. "前片 · 3条"); re-sizes the pill.
    void setText(const QString& text);
    /// Accent state: the group is hovered or selected.
    void setAccent(bool on);

signals:
    void clicked(const QUuid& groupId);
    void hoverChanged(bool hovered);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QUuid m_groupId;
    QString m_text;
    QRectF m_rect;
    bool m_accent = false;
    bool m_pressedInside = false;
};
