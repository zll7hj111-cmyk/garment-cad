#include "canvas/HudItem.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsView>
#include <QPainter>

#include "canvas/CanvasScene.h"
#include "geometry/Units.h"   // Coord::toScene (user→scene)

namespace {

/// 统一 Endfield 2.0 纸黄色工程图纸面规范
constexpr QColor kPillBg(255, 250, 209, 245);
constexpr QColor kPillBorder(216, 204, 128, 220);
constexpr QColor kPillFg(26, 32, 44);

QFont hudFont()
{
    QFont f(QStringLiteral("Segoe UI"));
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
    // 盒 = 字形外扩：统一呼吸感内边距 (padX = 7.0, padY = 3.5)
    const QFontMetricsF fm(hudFont());
    const qreal padX = 7.0;
    const qreal padY = 3.5;
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
    // 预留抗锯齿描边 (1px) 与微阴影扩展 (2px) 空间
    return m_rect.adjusted(-2.0, -1.0, 2.0, 3.0);
}

void HudItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if (m_text.isEmpty()) return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // 1. 微柔暖阴影 (向下 1px，微弱透明暖阴影)
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(60, 50, 20, 28));
    painter->drawRoundedRect(m_rect.adjusted(-0.5, 1.0, 0.5, 2.0), 4.0, 4.0);

    // 2. 底色与边框：优先从场景取主题（或默认纸黄色）
    QColor bg = kPillBg;
    QColor fg = kPillFg;
    QColor border = kPillBorder;

    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        if (cs->style()) {
            bg = cs->style()->hudBackground;
            fg = cs->style()->hudText;
            border = cs->style()->dark
                ? QColor(107, 94, 56, 220)
                : QColor(216, 204, 128, 220);
        }
    }

    if (m_look == Look::DarkPill) {
        bg = kPillBg;
        fg = kPillFg;
        border = kPillBorder;
    }

    // 3. 绘制反转技术墨面圆角矩形 (3.5px 功能微圆角)
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(bg);
    painter->drawRoundedRect(m_rect, 3.5, 3.5);

    // 4. 文字绘制
    painter->setPen(fg);
    painter->setFont(hudFont());
    painter->drawText(m_rect, Qt::AlignCenter, m_text);
}
