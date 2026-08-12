#include "CurveItem.h"

#include <QPainter>
#include <QPen>
#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>

#include <cmath>

#include "BlockItem.h"
#include "CanvasScene.h"
#include "CanvasAnimator.h"

namespace {

/// Shared font instance (creating a QFont per label per frame is expensive).
const QFont& nameFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(10); return fnt; }();
    return f;
}
/// Length annotations: monospace digits so drag readouts never jitter.
const QFont& lengthFont()
{
    static QFont f = [] {
        QFont fnt;
        fnt.setFamilies({QStringLiteral("Consolas"),
                         QStringLiteral("Courier New"),
                         QStringLiteral("monospace")});
        fnt.setPixelSize(10);
        return fnt;
    }();
    return f;
}

} // namespace

CurveItem::CurveItem(BlockItem* owner, const Data& data)
    : QGraphicsObject(owner)
    , m_owner(owner)
    , m_data(data)
{
    setAcceptHoverEvents(true);
}

void CurveItem::setData(const Data& data)
{
    prepareGeometryChange();
    m_data = data;
    m_strokedTol = -1.0;  // geometry changed — rebuild stroked hit shape lazily
    update();
}

void CurveItem::setCurveVisible(bool visible)
{
    if (m_data.visible == visible) return;
    m_data.visible = visible;
    update();  // paint() decides: hidden + not hovered → nothing
}

void CurveItem::setGrayed(bool grayed)
{
    if (m_grayed == grayed) return;
    m_grayed = grayed;
    update();
}

void CurveItem::setLeader(bool leader)
{
    if (m_leader == leader) return;
    m_leader = leader;
    update();
}

void CurveItem::setHoveredByParent(bool hovered)
{
    if (m_hovered == hovered) return;
    m_hovered = hovered;
    update();  // hidden-curve ghost reveal follows the flag
}

QRectF CurveItem::boundingRect() const
{
    return m_data.path.boundingRect().adjusted(-10, -10, 10, 10);
}

QPainterPath CurveItem::shape() const
{
    // Pick tolerance in scene units: screen px ÷ view zoom (same conversion
    // as the parent's hover threshold, so picking and hover agree).
    double pxToLocal = 1.0;
    if (scene() && !scene()->views().isEmpty()) {
        const qreal m11 = scene()->views().first()->transform().m11();
        if (std::abs(m11) > 1e-9)
            pxToLocal = 1.0 / std::abs(m11);
    }
    double tol = 8.0;
    if (auto* cs = qobject_cast<CanvasScene*>(scene()))
        tol = cs->style()->hoverRadiusPx();
    tol *= pxToLocal;

    // Cached stroked shape: rebuild only when the tolerance changed enough
    // to matter (sub-pixel). Stroking the coarse control-polygon polyline on
    // every hit query was the dominant per-frame cost (Seamly2D technique).
    if (m_strokedTol > 0.0 &&
        std::abs(tol - m_strokedTol) < m_strokedTol * 0.02)
        return m_strokedShape;

    QPainterPathStroker stroker;
    stroker.setWidth(tol * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    m_strokedShape = stroker.createStroke(m_data.shapePath);
    m_strokedTol = tol;
    return m_strokedShape;
}

void CurveItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
                      QWidget* /*widget*/)
{
    // Hidden curve: painted only while hovered (ghost reveal) — but ALWAYS
    // pickable, exactly like the parent's hidden-segment behaviour.
    if (!m_data.visible && !m_hovered)
        return;

    CanvasAnimator* animator = nullptr;
    const CanvasStyle* style = nullptr;
    bool forceName = false, forceLen = false;  // Hold-to-show (N/M keys).
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        animator  = cs->animator();
        style     = cs->style();
        forceName = cs->forceShowName();
        forceLen  = cs->forceShowLength();
    }

    const bool ghost = !m_data.visible;  // hovered hidden curve → ghost style
    const QColor kGray(0x9E, 0x9E, 0x9E);
    if (m_grayed)
        painter->setOpacity(0.4);

    // Dark-mode adaptation: lift the data color to the role's light-on-dark
    // family so ink curves stay legible on night paper.
    const QColor paintColor = style ? style->displayColor(m_data.role, m_data.color)
                                    : m_data.color;
    EntityPaintParams pp;
    if (animator) {
        pp = animator->lineParams(m_owner, m_data.id, paintColor, m_data.weight);
    } else {
        pp.lineColor  = paintColor;
        pp.lineWidth  = m_data.weight;
        pp.labelColor = QColor(100, 100, 100);
    }

    QPen curvePen(pp.lineColor, pp.lineWidth);
    curvePen.setCosmetic(true);
    curvePen.setStyle(m_data.penStyle);
    if (m_leader && style)
        curvePen.setColor(style->attachmentNodeColor);
    // Grayed reference layers keep the highlight on hovered/leader curves so
    // connections can be aimed (matches the parent's hover policy).
    if (m_grayed && !m_hovered && !m_leader)
        curvePen.setColor(kGray);
    constexpr int kGhostAlpha = 110;
    if (ghost) {
        QColor c = curvePen.color();
        c.setAlpha(kGhostAlpha);
        curvePen.setColor(c);
    }
    painter->setPen(curvePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_data.path);

    // Labels at the cached arc-length midpoint (suppressed on grayed layers).
    if ((m_data.showName || forceName) && !m_data.name.isEmpty() && !m_grayed) {
        QColor nameColor = pp.labelColor;
        if (ghost) nameColor.setAlpha(kGhostAlpha);
        QPen textPen(nameColor);
        textPen.setCosmetic(true);
        painter->setPen(textPen);
        painter->setFont(nameFont());
        painter->drawText(m_data.labelPos + QPointF(4, -4), m_data.name);
    }
    if ((m_data.showLength || forceLen) && !m_data.lengthText.isEmpty() && !m_grayed) {
        QColor lenColor = animator ? pp.lengthLabelColor
            : (style ? style->labelColor(EntityState::Normal, true)
                     : QColor(0, 110, 60));
        if (ghost) lenColor.setAlpha(kGhostAlpha);
        QPen textPen(lenColor);
        textPen.setCosmetic(true);
        painter->setPen(textPen);
        painter->setFont(lengthFont());
        painter->drawText(m_data.labelPos + QPointF(4, 12), m_data.lengthText);
    }
}

void CurveItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hovered = true;
    m_owner->onCurveHover(this, event->pos());
    event->accept();
    QGraphicsObject::hoverEnterEvent(event);
}

void CurveItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hovered = false;
    m_owner->onCurveHoverLeave(this);
    event->accept();
    QGraphicsObject::hoverLeaveEvent(event);
}
