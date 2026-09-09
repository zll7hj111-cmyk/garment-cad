#pragma once

#include <QGraphicsObject>
#include <QPainterPath>
#include <QUuid>

#include "parametric/Segment.h"  // cad::param::SegmentRole (Data default)

class QGraphicsSceneHoverEvent;

class CanvasScene;

class CanvasStyle;

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
        QPainterPath path;       ///< Dense flattened polyline (for painting AND hit shape).
        /// Analytic paint path (D16 圆拟合段): 非空时 paint() 用它、shape()/
        /// boundingRect() 仍用上面的折线 —— 折线是按固定 0.1 mm 绝对容差预扁平
        /// 的, 放大 ~300% 就能看出棱角; 命中带宽远大于 0.1 mm, 无需跟着换。
        QPainterPath paintPath;
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

        /// 圆心→接缝半径基准 (① 一期补充 2026-12): 圆段作角度基准 (D9) 的
        /// 基准方向就是这条半径 —— 整圆上「0° 在哪」不可见, 所以悬停/选中
        /// 时从圆心到接缝 (起点) 画一条虚线并标注该方向的世界角。
        /// valid=false 时完全不画 (非圆段 / 退化半径)。
        struct CircleGuide {
            bool valid = false;
            QPointF center;              ///< 场景局部坐标 (y 向下)。
            QPointF seam;                ///< 半径外端点 = 起点接缝 (同一坐标系)。
            double radius = 0.0;         ///< 场景单位 (mm)。
            /// 该半径的**世界**方向角 (度, 逆时针为正)。含块旋转 —— 缓存坐标
            /// 已经把块旋转烘进去 (BlockGeometryCache::toLocal), 所以这里必须
            /// 是 a₀ + 块旋转, 否则整块转起来后标注纹丝不动 (用户报告 m01094:
            /// 「圆的角度是恒定的世界角度」)。
            double worldAngleDeg = 0.0;
        };
        CircleGuide guide;
    };

    explicit CurveItem(BlockItem* owner, const Data& data);

    [[nodiscard]] const QUuid& curveId() const { return m_data.id; }
    [[nodiscard]] BlockItem* ownerItem() const { return m_owner; }
    /// 解析绘制路径 (D16 圆拟合段)。空 = 绘制也用折线 `path`。
    [[nodiscard]] const QPainterPath& paintPath() const { return m_data.paintPath; }

    /// ① 圆心→接缝半径基准 (悬停/选中时画虚线 + 世界角徽标)。
    /// valid=false = 非解析圆 (普通曲线 / 变形圆)。
    [[nodiscard]] const Data::CircleGuide& circleGuide() const { return m_data.guide; }

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

    /// ① 圆心→接缝基准虚线 + 世界角徽标 (只在悬停/选中且非灰显时画)。
    void drawCircleGuide(QPainter* painter, const CanvasStyle& st) const;

    /// Cached stroked hit shape (path widened to the pick tolerance);
    /// rebuilt lazily when the tolerance moves >2% (same policy as the old
    /// BlockItem-level caches). Qt calls shape() at high frequency.
    mutable QPainterPath m_strokedShape;
    mutable double m_strokedTol = -1.0;
};
