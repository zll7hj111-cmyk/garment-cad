#pragma once

#include <QGraphicsObject>
#include <QPainterPath>
#include <QUuid>

#include "parametric/Segment.h"  // cad::param::SegmentRole (Data default)

class QGraphicsSceneHoverEvent;

namespace cad { class CanvasScene; }

class BlockItem;

/// Renders ONE curve segment of a Block as its own scene item (曲线拆子item).
/// Lives as a child of its owning BlockItem (local coordinates — the parent
/// applies the rigid-body transform), so the QGraphicsScene framework
/// provides hit-testing, hover and repaint per curve instead of the parent's
/// manual per-frame hitTest scan.
///
/// Animation state (hover/selected) is driven through the owning BlockItem
/// (the CanvasAnimator owner), so the parent keeps full arbitration over
/// which entity is highlighted; this item only reports hover entry/leave.
class CurveItem : public QGraphicsObject
{
    Q_OBJECT

public:
    /// Curve render data (built once per resolve by BlockItem::rebuildCache).
    struct Data {
        QUuid id;
        QPainterPath path;       ///< Dense flattened polyline (for painting).
        QPainterPath shapePath;  ///< Coarse control-polygon polyline (for hit shape).
        QPointF labelPos;        ///< Name/length label anchor.
        double labelAngle = 0;   ///< Label tangent angle (radians).
        QColor color;
        cad::param::SegmentRole role = cad::param::SegmentRole::Outline;
        double weight;
        Qt::PenStyle penStyle;
        QString name;
        bool showName = false;
        bool showLength = false;
        QString lengthText;      ///< Pre-formatted arc-length label (cm).
        bool visible = true;     ///< Hidden curves stay pickable/hoverable (ghost reveal).
    };

    explicit CurveItem(BlockItem* owner, const Data& data);

    [[nodiscard]] const QUuid& curveId() const { return m_data.id; }
    [[nodiscard]] BlockItem* ownerItem() const { return m_owner; }

    /// Replace the render data (called by BlockItem::rebuildCache).
    void setData(const Data& data);
    /// Hidden-curve semantics: visible=false keeps the item pickable, but
    /// paint() only draws it while hovered (ghost style).
    void setCurveVisible(bool visible);
    /// Layer display mode: grayed layers render at reduced opacity in gray.
    void setGrayed(bool grayed);
    void setLeader(bool leader);
    /// Parent-driven hover flag (hover arbitration lives in BlockItem).
    void setHoveredByParent(bool hovered);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    BlockItem* m_owner;
    Data m_data;
    bool m_hovered = false;  ///< Under the cursor (also set by parent arbitration).
    bool m_leader = false;   ///< Pen-tool leader candidate (teal).
    bool m_grayed = false;   ///< Non-active layer reference rendering.

    /// Cached stroked hit shape (shapePath widened to the pick tolerance);
    /// rebuilt lazily when the tolerance moves >2% (same policy as the old
    /// BlockItem-level caches). Qt calls shape() at high frequency.
    mutable QPainterPath m_strokedShape;
    mutable double m_strokedTol = -1.0;
};
