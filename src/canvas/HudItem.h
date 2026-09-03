#pragma once

#include <QGraphicsItem>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include "geometry/Vec2.h"

class QGraphicsView;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

/// 屏幕常量 HUD 标签（画布内悬浮文字框，随视图缩放做 1/zoom 补偿——
/// 文字/边框在任何缩放级别下都是同一屏幕尺寸；WYSIWYG）。
///
/// 原 ToolSmartPen.h 内的 HudItem（TOOL_SYSTEM_AUDIT P1/M1，2026-08-29
/// 收口）：全仓唯一实现，供
///   · 智能笔预览 HUD / ToolMeasure / ToolIntersection / ToolAngleMeasure
///     （基类 Tool::ensureHud 的浅色跟随标签，Look::ThemeDefault）；
///   · CanvasScene::showToast 与 ToolSelect 重叠提示（深色胶囊，
///     Look::DarkPill）
/// 共用。此前重叠提示与 toast 是各自手搭的 QGraphicsRectItem +
/// QGraphicsSimpleTextItem，量出的"字体像素"直接写进场景单位 rect，缩放下
/// 要么小到看不见、要么大到遮挡画布——本类统一在 transform 层补偿。
/// （全局命名空间 = canvas 模块既有约定，与 CanvasScene/BlockItem 一致。）
class HudItem : public QGraphicsItem
{
public:
    /// 视觉语言：ThemeDefault = 逐帧查询 CanvasScene 主题色板（浅色标签）；
    /// DarkPill = 固定深色胶囊（白字 11px），主题切换不跟随。
    enum class Look { ThemeDefault, DarkPill };

    explicit HudItem(QGraphicsItem* parent = nullptr);

    void setText(const QString& text);
    void setLook(Look look);

    /// 当前文本 —— 调用方据此做"同值短路"或状态断言, 不必自己再存一份。
    [[nodiscard]] const QString& text() const { return m_text; }

    /// 标准光标避让偏移常量（屏幕像素，位于光标右下方）。
    static inline const QPointF kCursorOffset{14.0, 14.0};

    /// 置盒左上角于世界点 userPos + screenOffset(px) 处；px 偏移落地时
    /// 除以 zoom，屏幕上恒为该像素距离（原重叠提示的 "+12.0 场景单位"
    /// 在缩放下忽远忽近，即此参数所治）。
    void moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view,
                     const QPointF& screenOffset = QPointF(0, 0));
    /// 同上，锚点直接给场景坐标（toast 的视口顶中场景点）。
    void placeAtScene(const QPointF& scenePos, const QGraphicsView* view,
                      const QPointF& screenOffset = QPointF(0, 0));

    /// 盒尺寸（px，屏幕常量）——调用方用于预计算居中/锚定偏移。
    [[nodiscard]] QSizeF size() const { return m_rect.size(); }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    void place(const QPointF& scenePos, const QGraphicsView* view,
               const QPointF& screenOffset);

    QString m_text;
    QRectF  m_rect;
    Look    m_look = Look::ThemeDefault;
};
