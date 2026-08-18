#pragma once

#include <QGraphicsObject>
#include <QString>
#include <QUuid>

/// Canvas visual marker for a user group (组包围框): dashed bounding box
/// around the member union, anchored at the top-left of the union bounds.
/// It is interactive: shape() returns a stroked outline of the dashed box so
/// mouse/hover hits land on the border (the interior stays transparent to
/// clicks, preserving point-level endpoint gestures on members). A left click
/// emits clicked(groupId); hover enters/leaves emit hoverChanged(bool).
///
/// States:
///   - Normal: dashed outline in the soft border color.
///   - Accent (group selected or hovered): dashed outline in the accent color.
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
    [[nodiscard]] QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    /// Accent state: the group is hovered or selected.
    void setAccent(bool on);
    /// Show or hide the dashed bounding box outline.
    void setShowBoundingBox(bool show);
    [[nodiscard]] bool showBoundingBox() const { return m_showBoundingBox; }
    /// Update member bounds in scene coordinates to adjust the dashed outline.
    void setMemberSceneBounds(const QRectF& sceneBounds);

    /// Legacy compatibility no-op.
    void setText(const QString&) {}

signals:
    void clicked(const QUuid& groupId);
    void hoverChanged(bool hovered);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QUuid m_groupId;
    QRectF m_boxRectLocal;  ///< Dashed bounding box rect (local).
    bool m_accent = false;
    bool m_showBoundingBox = true;
};