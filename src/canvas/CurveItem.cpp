#include "CurveItem.h"

#include <QPainter>
#include <QPen>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>

#include <cmath>

#include "BlockItem.h"
#include "CanvasScene.h"
#include "CanvasAnimator.h"
#include "DirectionMarker.h"
#include "BlockItemPick.h"
#include "CanvasFonts.h"
#include "geometry/Angle.h"
#include "geometry/Epsilon.h"

using canvas_fonts::lengthFont;
using canvas_fonts::nameFont;

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
    // The margin must cover the pick band (shape() strokes the drawn path with
    // tol = hoverRadiusPx ÷ zoom). QGraphicsSceneIndex (BSP) rejects items
    // whose boundingRect does not contain the query point, so a margin smaller
    // than the band makes the item unpickable at low zoom (e.g. zoom 0.2 →
    // tol ≈ 40 local units; the old ±10 margin silently dropped hits in the
    // band outside the path bbox — 选择工具对曲线判定不准的另一半成因).
    // Worst case: hoverRadiusPx(8) / ZOOM_MIN(0.2) = 40 + cap safety.
    QRectF r = m_data.path.boundingRect();
    if (m_data.guide.valid) {
        // ① 半径基准虚线从**圆心**出发, 而窄弧 (如包角 10°) 的折线包围盒
        // 根本不含圆心 —— 不并进来就等于「画到包围盒外」, 会留下未重绘像素。
        r = r.united(QRectF(m_data.guide.center - QPointF(1.0, 1.0),
                            QSizeF(2.0, 2.0)));
    }
    return r.adjusted(-BlockItemPick::kPickMarginLocal,
                      -BlockItemPick::kPickMarginLocal,
                      BlockItemPick::kPickMarginLocal,
                      BlockItemPick::kPickMarginLocal);
}

QPainterPath CurveItem::shape() const
{
    // Pick tolerance in scene units: screen px ÷ view zoom (same conversion
    // as the parent's hover threshold, so picking and hover agree).
    double pxToLocal = 1.0;
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        const qreal m11 = cs->currentZoom();
        if (std::abs(m11) > cad::geo::kGeomEps)
            pxToLocal = 1.0 / std::abs(m11);
    }
    double tol = CanvasStyle::fallback().hoverRadiusPx();
    if (auto* cs = qobject_cast<CanvasScene*>(scene()))
        tol = cs->style()->hoverRadiusPx();
    tol *= pxToLocal;

    // Cached stroked shape: rebuild only when the tolerance changed enough
    // to matter (sub-pixel). Stroking the DENSE flattened path (the actual
    // drawn geometry) on every hit query was the historical per-frame cost —
    // the old "coarse control-polygon" hit region deviates from the drawn
    // curve by many millimetres on strong curves, so clicks ON the curve
    // missed (选择工具对曲线判定失灵, 用户报告 2026-10). The stroke is cached
    // the same way; only zoom sweeps / geometry changes pay the rebuild.
    if (m_strokedTol > 0.0 &&
        std::abs(tol - m_strokedTol) < m_strokedTol * 0.02)
        return m_strokedShape;

    QPainterPathStroker stroker;
    stroker.setWidth(tol * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    m_strokedShape = stroker.createStroke(m_data.path);
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
    const CanvasStyle* sceneStyle = nullptr;
    bool forceName = false, forceLen = false;  // Hold-to-show (N/M keys).
    bool dirArrows = true;   // 方向基准箭头全局开关 (2026-12); 无场景时保持画.
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        animator   = cs->animator();
        sceneStyle = cs->style();
        forceName  = cs->forceShowName();
        forceLen   = cs->forceShowLength();
        dirArrows  = cs->directionArrowsEnabled();
    }
    // Style is always present (audit P0-1): no inline QColor fallbacks.
    const CanvasStyle& st = sceneStyle ? *sceneStyle : CanvasStyle::fallback();

    const bool ghost = !m_data.visible;  // hovered hidden curve → ghost style
    const QColor kGray = st.grayedLineColor;
    if (m_grayed)
        painter->setOpacity(st.grayedOpacity);

    // Dark-mode adaptation: lift the data color to the role's light-on-dark
    // family so ink curves stay legible on night paper.
    const QColor paintColor = st.displayColor(m_data.role, m_data.color);
    EntityPaintParams pp;
    if (animator) {
        pp = animator->lineParams(m_owner, m_data.id, paintColor, m_data.weight);
    } else {
        pp.lineColor  = paintColor;
        pp.lineWidth  = m_data.weight;
        pp.labelColor = st.labelColor(EntityState::Normal, false);
    }

    QPen curvePen(pp.lineColor, pp.lineWidth);
    curvePen.setCosmetic(true);
    curvePen.setStyle(m_data.penStyle);
    if (m_leader)
        curvePen.setColor(st.attachmentNodeColor);
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
    painter->drawPath(m_data.paintPath.isEmpty() ? m_data.path : m_data.paintPath);

    // 方向指示 (2026-12): 弧长中点处的起点→终点箭头, 换向后缓存重算自动翻转
    // (曲线保形换向, 几何零跳变 —— 这是画布上唯一的换向可见反馈)。灰显层不画;
    // 全局开关 (CanvasScene::directionArrowsEnabled) 关闭时隐藏。
    if (!m_grayed && dirArrows) {
        QColor dirColor = pp.labelColor;
        if (ghost) dirColor.setAlpha(kGhostAlpha);
        drawDirectionChevron(painter, m_data.labelPos, m_data.labelAngle, dirColor);
    }

    // ① 圆心→接缝半径基准 (2026-12): 悬停/选中时画出「0° 在哪」。灰显层
    // 与隐藏曲线不画 —— 基准是编辑参照, 不是几何本身。
    if (m_data.guide.valid && !m_grayed && !ghost
        && (m_hovered || m_owner->toolSelected() || m_owner->toolLocked())) {
        drawCircleGuide(painter, st);
    }

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
                                   : st.labelColor(EntityState::Normal, true);
        if (ghost) lenColor.setAlpha(kGhostAlpha);
        QPen textPen(lenColor);
        textPen.setCosmetic(true);
        painter->setPen(textPen);
        painter->setFont(lengthFont());
        painter->drawText(m_data.labelPos + QPointF(4, 12), m_data.lengthText);
    }
}

void CurveItem::drawCircleGuide(QPainter* painter, const CanvasStyle& st) const
{
    const Data::CircleGuide& g = m_data.guide;
    if (!g.valid || g.radius <= cad::geo::kGeomEps)
        return;

    painter->save();
    QPen pen(st.gizmoAccentColor, 1.0, Qt::DashLine);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(g.center, g.seam);

    // 圆心小十字: 基准线的起点 (半径方向由它出发)。
    constexpr double kCross = 2.0;
    painter->drawLine(QPointF(g.center.x() - kCross, g.center.y()),
                      QPointF(g.center.x() + kCross, g.center.y()));
    painter->drawLine(QPointF(g.center.x(), g.center.y() - kCross),
                      QPointF(g.center.x(), g.center.y() + kCross));

    // 世界角标注: 直接挂在虚线上 (m01094 ②「不要一个大方块」) —— 文字沿半径
    // 方向排布, 用画布底色描边保证可读, 不再画深色圆角块。
    QPointF dir = g.seam - g.center;
    const double len = std::hypot(dir.x(), dir.y());
    if (len <= cad::geo::kGeomEps) {
        painter->restore();
        return;
    }
    dir /= len;
    const double deg = cad::geo::toDisplayDeg(
        g.worldAngleDeg, cad::geo::AngleDisplayRole::WorldDirection);
    const QString text = QStringLiteral("%1°").arg(deg, 0, 'f', 1);  // units-allow: 角度标注(度), 非长度单位
    painter->setFont(canvas_fonts::uiFontPt(9, true));
    const QFontMetricsF fm(painter->font());
    // 贴在虚线旁: 半径 62% 处, 沿法线抬半个字高 + 2 单位 (不再包底框)。
    const QPointF perp(-dir.y(), dir.x());
    const QPointF anchor = g.center + dir * (len * 0.62)
        + perp * (fm.height() * 0.5 + 2.0);
    // 文字随半径方向排布; 超过 ±90° 翻正, 避免倒着读。
    double angDeg = cad::geo::radToDeg(std::atan2(dir.y(), dir.x()));
    if (angDeg > 90.0) angDeg -= 180.0;
    else if (angDeg < -90.0) angDeg += 180.0;

    painter->translate(anchor);
    painter->rotate(angDeg);
    QPainterPath textPath;
    textPath.addText(QPointF(-fm.horizontalAdvance(text) * 0.5, 0.0),
                     painter->font(), text);
    QPen halo(st.dark ? st.gizmoBadgeBg : st.canvasBackground);
    halo.setCosmetic(true);
    halo.setWidthF(3.0);
    halo.setJoinStyle(Qt::RoundJoin);
    painter->setPen(halo);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(textPath);
    painter->setPen(Qt::NoPen);
    painter->setBrush(st.gizmoAccentColor);
    painter->drawPath(textPath);
    painter->restore();
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
