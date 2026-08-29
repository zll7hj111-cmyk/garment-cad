#include "canvas/HudItem.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsView>
#include <QPainter>

#include "canvas/CanvasScene.h"
#include "geometry/Units.h"   // Coord::toScene (user→scene)

namespace {

/// DarkPill 固定色板（toast / 重叠提示的历史视觉，白字 11px）。
constexpr QColor kPillBg(38, 50, 56, 225);
constexpr QColor kPillBorder(0, 0, 0, 40);
constexpr QColor kPillFg(255, 255, 255);

QFont pillFont()
{
    QFont f;
    f.setPixelSize(11);
    return f;
}

} // namespace

HudItem::HudItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    // 历史默认层级（跟随标签浮在预览图元之上）；toast/重叠提示创建后自行抬高。
    setZValue(200.0);
}

void HudItem::setText(const QString& text)
{
    prepareGeometryChange();
    m_text = text;
    // 盒 = 字形外扩：ThemeDefault 沿用原轻量 pad；DarkPill = 原 toast/重叠
    // 提示的 8/4 px 内边距（两处历史值一致，顺带统一）。
    const QFontMetricsF fm(m_look == Look::DarkPill ? pillFont()
                                                    : QFont(QStringLiteral("Segoe UI"), 9));
    const qreal padX = m_look == Look::DarkPill ? 8.0 : 4.0;
    const qreal padY = m_look == Look::DarkPill ? 4.0 : 2.0;
    m_rect = fm.boundingRect(m_text).adjusted(-padX, -padY, padX, padY);
}

void HudItem::setLook(Look look)
{
    if (m_look == look) return;
    m_look = look;
    if (!m_text.isEmpty())
        setText(m_text);   // 重排版
}

void HudItem::moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view,
                          const QPointF& screenOffset)
{
    place(cad::geo::Coord::toScene(userPos.x, userPos.y), view, screenOffset);
}

void HudItem::placeAtScene(const QPointF& scenePos, const QGraphicsView* view,
                           const QPointF& screenOffset)
{
    place(scenePos, view, screenOffset);
}

void HudItem::place(const QPointF& scenePos, const QGraphicsView* view,
                    const QPointF& screenOffset)
{
    double zoom = (view != nullptr) ? view->transform().m11() : 1.0;
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    // 1/zoom 补偿：盒与文字在任何视图变换下保持恒定屏幕尺寸。
    setTransform(QTransform().scale(1.0 / zoom, 1.0 / zoom));
    // 屏幕像素偏移 → 场景单位（÷zoom），WYSIWYG 恒距。
    setPos(scenePos + screenOffset / zoom);
}

QRectF HudItem::boundingRect() const
{
    return m_rect.adjusted(-1.0, -1.0, 1.0, 1.0);
}

void HudItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if (m_text.isEmpty()) return;

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    if (m_look == Look::DarkPill) {
        painter->setPen(QPen(kPillBorder, 0.5));
        painter->setBrush(kPillBg);
        painter->drawRect(m_rect);
        painter->setPen(kPillFg);
        painter->setFont(pillFont());
        painter->drawText(m_rect, Qt::AlignCenter, m_text);
        return;
    }

    // ThemeDefault: 逐帧取所属场景的 CanvasStyle（跟随亮/暗主题）。
    QColor bg(255, 255, 255, 215);
    QColor fg(30, 30, 30);
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        bg = cs->style()->hudBackground;
        fg = cs->style()->hudText;
    }

    painter->setPen(QPen(QColor(180, 180, 180), 0.5));
    painter->setBrush(bg);
    painter->drawRect(m_rect);

    painter->setPen(fg);
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter->drawText(m_rect, Qt::AlignCenter, m_text);
}
