#pragma once

#include <QGraphicsObject>
#include <QUuid>

namespace cad::param { class ParamDocument; class Block; }

/// Renders a parametric Block as a single QGraphicsItem.
/// Supports selection highlighting and drag-to-move (modifies Block Transform).
class BlockItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit BlockItem(const QUuid& blockId, cad::param::ParamDocument* doc,
                       QGraphicsItem* parent = nullptr);

    [[nodiscard]] const QUuid& blockId() const { return m_blockId; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    /// Refresh geometry cache from the document (call after resolve).
    void updateFromBlock();

    /// Toggle the soft "same group" highlight (used when another member of the
    /// attachment group is selected). Selected items keep the strong color.
    void setGroupHighlight(bool on);
    [[nodiscard]] bool groupHighlight() const { return m_groupHighlight; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    QUuid m_blockId;
    cad::param::ParamDocument* m_doc = nullptr;

    // Cached geometry in world coordinates for fast painting
    struct LineCache {
        QPointF p1; QPointF p2;
        QColor color;
        double weight;
        Qt::PenStyle penStyle;
        QString name;
        bool showName;
        bool showLength;
        QString lengthText;  ///< Pre-formatted length label (cm).
    };
    struct PointCache { QPointF pos; bool isAuxiliary; QString label; bool showLabel; };

    std::vector<LineCache>  m_lines;
    std::vector<PointCache> m_points;
    QRectF m_cachedBounds;
    bool m_groupHighlight = false;

    void rebuildCache();
};
