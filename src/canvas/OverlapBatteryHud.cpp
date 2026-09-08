#include "canvas/OverlapBatteryHud.h"

#include <QGraphicsView>
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasFonts.h"
#include "canvas/CanvasStyle.h"
#include "geometry/Units.h"
#include "geometry/Epsilon.h"

namespace cad::canvas {

using canvas_fonts::monoFont;
using canvas_fonts::uiFont;

OverlapBatteryHud::OverlapBatteryHud(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(250.0);  // 浮在画布最高层
}

void OverlapBatteryHud::setCandidates(std::vector<BatteryCandidate> cands)
{
    prepareGeometryChange();
    m_candidates = std::move(cands);
    if (m_candidates.empty()) {
        m_mode = DisplayMode::Hidden;
    }
    m_hoveredIndex = -1;
    m_selectedIndex = -1;
    update();
}

void OverlapBatteryHud::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode) return;
    prepareGeometryChange();
    m_mode = mode;
    update();
}

void OverlapBatteryHud::setConnectMode(bool connectMode)
{
    if (m_isConnectMode == connectMode) return;
    m_isConnectMode = connectMode;
    update();
}

void OverlapBatteryHud::setHoveredIndex(int index)
{
    if (m_hoveredIndex == index) return;
    m_hoveredIndex = index;
    if (onCandidateHovered) {
        onCandidateHovered(m_hoveredIndex);
    }
    update();
}

void OverlapBatteryHud::setSelectedIndex(int index)
{
    if (m_selectedIndex == index) return;
    m_selectedIndex = index;
    if (onCandidateSelected) {
        onCandidateSelected(m_selectedIndex);
    }
    update();
}

int OverlapBatteryHud::hitCandidateAtWorld(const cad::geo::Vec2& worldPos, double zoom) const
{
    return hitCandidateAtScene(cad::geo::Coord::toScene(worldPos.x, worldPos.y), zoom);
}

void OverlapBatteryHud::updatePosition(const cad::geo::Vec2& worldPos, const QGraphicsView* view)
{
    m_worldPos = worldPos;
    double zoom = (view != nullptr) ? view->transform().m11() : 1.0;
    if (std::abs(zoom) < cad::geo::kGeomEps) zoom = 1.0;
    m_currentZoom = zoom;

    // 1/zoom 补偿: 屏幕尺寸恒定
    setTransform(QTransform().scale(1.0 / zoom, 1.0 / zoom));
    setPos(cad::geo::Coord::toScene(worldPos.x, worldPos.y));
}

QRectF OverlapBatteryHud::badgeRect() const
{
    return QRectF(10.0, -kBadgeH / 2.0, kBadgeW, kBadgeH);
}

QRectF OverlapBatteryHud::chipRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_candidates.size()))
        return QRectF();

    const int n = static_cast<int>(m_candidates.size());
    const qreal totalH = n * kChipHeight + (n - 1) * kChipGap;
    const qreal startY = -totalH / 2.0;
    const qreal y = startY + index * (kChipHeight + kChipGap);
    return QRectF(kOffsetRight, y, kChipWidth, kChipHeight);
}

QPointF OverlapBatteryHud::chipPortLocal(int index) const
{
    const QRectF r = chipRect(index);
    return QPointF(r.left(), r.center().y());
}

QPointF OverlapBatteryHud::candidatePortScenePos(int index, double zoom) const
{
    if (std::abs(zoom) < cad::geo::kGeomEps) zoom = m_currentZoom;
    const QPointF portLocal = chipPortLocal(index);
    // pos() 是场景坐标, portLocal 是屏幕像素坐标 -> ÷ zoom
    return pos() + portLocal / zoom;
}

int OverlapBatteryHud::hitCandidateAtScene(const QPointF& scenePos, double zoom) const
{
    if (m_mode != DisplayMode::Expanded) return -1;
    if (std::abs(zoom) < cad::geo::kGeomEps) zoom = m_currentZoom;

    // 转为本地屏幕像素坐标
    const QPointF deltaScene = scenePos - pos();
    const QPointF localPt = deltaScene * zoom;

    // 连接模式有更大的吸附靶标宽容度
    const qreal tolerance = m_isConnectMode ? 8.0 : 2.0;

    for (int i = 0; i < static_cast<int>(m_candidates.size()); ++i) {
        QRectF r = chipRect(i);
        if (m_isConnectMode) {
            // 向左扩展容差包含引线端口
            r.adjust(-tolerance * 2.0, -tolerance, tolerance, tolerance);
        } else {
            r.adjust(-tolerance, -tolerance, tolerance, tolerance);
        }
        if (r.contains(localPt)) {
            return i;
        }
    }
    return -1;
}

bool OverlapBatteryHud::hitBadgeAtScene(const QPointF& scenePos, double zoom) const
{
    if (m_mode != DisplayMode::Badge) return false;
    if (std::abs(zoom) < cad::geo::kGeomEps) zoom = m_currentZoom;

    const QPointF deltaScene = scenePos - pos();
    const QPointF localPt = deltaScene * zoom;
    return badgeRect().adjusted(-3.0, -3.0, 3.0, 3.0).contains(localPt);
}

QRectF OverlapBatteryHud::boundingRect() const
{
    if (m_mode == DisplayMode::Hidden || m_candidates.empty())
        return QRectF();

    if (m_mode == DisplayMode::Badge) {
        return badgeRect().adjusted(-4.0, -4.0, 4.0, 4.0);
    }

    const int n = static_cast<int>(m_candidates.size());
    const qreal totalH = n * kChipHeight + (n - 1) * kChipGap;
    const qreal startY = -totalH / 2.0;

    QRectF r(0.0, startY, kOffsetRight + kChipWidth, totalH);
    return r.adjusted(-6.0, -8.0, 8.0, 8.0);
}

void OverlapBatteryHud::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if (m_mode == DisplayMode::Hidden || m_candidates.empty()) return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const CanvasStyle* cs = nullptr;
    if (scene()) {
        if (auto* sc = qobject_cast<CanvasScene*>(scene())) {
            cs = sc->style();
        }
    }
    const CanvasStyle& s = cs ? *cs : CanvasStyle::fallback();

    struct StyleTokens {
        QColor accent;
        QColor accentTint;
        QColor surface;
        QColor surface2;
        QColor surface3;
        QColor border;
        QColor borderStrong;
        QColor text1;
        QColor text2;
        QColor warning;
        QColor teal;
    } tk;

    tk.accent = s.previewLineColor;
    tk.accentTint = s.accentWash();
    tk.surface = s.hudBackground;
    tk.surface2 = s.surfaceColor();
    tk.surface3 = s.borderSoft();
    tk.border = s.borderSoft();
    tk.borderStrong = s.crosshairColor;
    tk.text1 = s.hudText;
    tk.text2 = s.textSecondary();
    tk.warning = s.lockedAttachmentColor;
    tk.teal = s.attachmentNodeColor;

    // ── 模式 1: 徽标态 (Badge) ──
    if (m_mode == DisplayMode::Badge) {
        const QRectF br = badgeRect();

        // 柔和微阴影
        painter->setPen(Qt::NoPen);
        painter->setBrush(s.hudShadowColor);
        painter->drawRoundedRect(br.adjusted(-0.5, 1.0, 0.5, 2.0), 3.0, 3.0);

        // 微标底色与边框
        painter->setBrush(tk.surface3);
        painter->setPen(QPen(tk.borderStrong, 1.0));
        painter->drawRoundedRect(br, 3.0, 3.0);

        // 徽标图标与数量
        const bool isPoint = (m_candidates.front().kind == BatteryCandidate::Kind::Point);
        const QString icon = isPoint ? QStringLiteral("●") : QStringLiteral("━");
        const QString countText = QString::number(m_candidates.size());

        painter->setFont(uiFont(10, true));
        painter->setPen(tk.accent);
        painter->drawText(QRectF(br.left() + 4.0, br.top(), 12.0, br.height()),
                          Qt::AlignCenter, icon);

        painter->setFont(monoFont(10, true));
        painter->setPen(tk.text1);
        painter->drawText(QRectF(br.left() + 16.0, br.top(), br.width() - 18.0, br.height()),
                          Qt::AlignCenter, countText);
        return;
    }

    // ── 模式 2: 展开引线电池组态 (Expanded) ──

    // 1. 原点锚核
    painter->setPen(Qt::NoPen);
    painter->setBrush(tk.accent);
    painter->drawEllipse(QPointF(0.0, 0.0), 2.5, 2.5);

    const int n = static_cast<int>(m_candidates.size());
    for (int i = 0; i < n; ++i) {
        const auto& cand = m_candidates[i];
        const QRectF cr = chipRect(i);
        const QPointF port = chipPortLocal(i);
        const bool isHovered = (i == m_hoveredIndex);
        const bool isSelected = (i == m_selectedIndex);
        const bool isActive = (isSelected || isHovered);

        // 2. 引线 (Leader Line)
        QPainterPath leader;
        leader.moveTo(0.0, 0.0);
        const qreal midX = kOffsetRight - 12.0;
        leader.lineTo(midX, port.y());
        leader.lineTo(port.x(), port.y());

        QPen linePen(isActive ? tk.accent : tk.borderStrong, isSelected ? 2.0 : (isHovered ? 1.6 : 1.0));
        painter->setPen(linePen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(leader);

        // 引线端口圆点
        painter->setPen(Qt::NoPen);
        painter->setBrush(isActive ? tk.accent : tk.borderStrong);
        const qreal dotR = isSelected ? 3.0 : (isHovered ? 2.5 : 1.8);
        painter->drawEllipse(port, dotR, dotR);

        // 连接模式磁吸靶环
        if (m_isConnectMode) {
            QPen ringPen(isActive ? tk.accent : tk.teal, 1.2, Qt::DashLine);
            painter->setPen(ringPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(port, 4.5, 4.5);
        }

        // 3. 电池芯片卡片 (Chip Card)
        // 阴影
        painter->setPen(Qt::NoPen);
        painter->setBrush(s.hudShadowColor);
        painter->drawRoundedRect(cr.adjusted(-0.5, 1.0, 0.5, 2.0), 4.0, 4.0);

        // 芯片底色与外框
        QColor chipBg = isActive ? tk.accentTint : tk.surface;
        QColor chipBorder = isActive ? tk.accent : tk.borderStrong;
        painter->setBrush(chipBg);
        painter->setPen(QPen(chipBorder, isSelected ? 2.0 : (isHovered ? 1.5 : 1.0)));
        painter->drawRoundedRect(cr, 4.0, 4.0);

        // 4. 左侧微图标与主体文字区 (浑然一体胶囊排版，移除独立方块头)
        QString glyph;
        if (cand.kind == BatteryCandidate::Kind::Point) {
            if (cand.isPlaced) glyph = QStringLiteral("◆");
            else if (cand.isCurveAnchor) glyph = QStringLiteral("▲");
            else glyph = QStringLiteral("●");
        } else {
            glyph = QStringLiteral("━");
        }

        const qreal iconX = cr.left() + 8.0;
        painter->setFont(uiFont(9, true));
        painter->setPen(cand.isPlaced ? tk.warning : (isActive ? tk.accent : tk.text2));
        painter->drawText(QRectF(iconX, cr.top() + 2.0, 12.0, 14.0),
                          Qt::AlignCenter, glyph);

        // 主体文字区 (名称 + 所属图块/线段)
        const qreal textLeft = iconX + 15.0;
        const qreal textRight = isSelected ? (cr.right() - 22.0) : (cr.right() - 8.0);
        const qreal textW = std::max<qreal>(10.0, textRight - textLeft);

        QString mainTitle = cand.name.isEmpty() ? cand.title : cand.name;
        if (!cand.blockName.isEmpty() && cand.blockName != mainTitle) {
            mainTitle = QStringLiteral("%1 · %2").arg(mainTitle, cand.blockName);
        }

        painter->setFont(uiFont(10, true));
        painter->setPen(tk.text1);
        const QFontMetricsF fmTitle(painter->font());
        const QString elidedTitle = fmTitle.elidedText(mainTitle, Qt::ElideRight, textW);
        painter->drawText(QRectF(textLeft, cr.top() + 2.0, textW, 14.0),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

        // 副标题 / 角色度量
        QString subText = cand.roleText;
        if (cand.metricValue > 0.0) {
            const QString lenStr = cad::geo::Units::formatLength(cand.metricValue);
            if (subText.isEmpty()) subText = lenStr;
            else subText += QStringLiteral(" · ") + lenStr;
        }
        if (!cand.layerName.isEmpty()) {
            if (subText.isEmpty()) subText = cand.layerName;
            else subText += QStringLiteral(" · ") + cand.layerName;
        }

        painter->setFont(uiFont(8.5, false));
        painter->setPen(tk.text2);
        const QFontMetricsF fmSub(painter->font());
        const QString elidedSub = fmSub.elidedText(subText, Qt::ElideRight, textW);
        painter->drawText(QRectF(textLeft, cr.top() + 15.0, textW, 12.0),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedSub);

        if (isSelected) {
            painter->setFont(uiFont(9, true));
            painter->setPen(tk.accent);
            painter->drawText(QRectF(cr.right() - 16.0, cr.top(), 12.0, cr.height()),
                              Qt::AlignCenter, QStringLiteral("✓"));
        }
    }
}

} // namespace cad::canvas
