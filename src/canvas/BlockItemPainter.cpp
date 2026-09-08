#include "BlockItemPainter.h"
#include "CanvasStyle.h"
#include "CanvasAnimator.h"
#include "DirectionMarker.h"
#include "parametric/PerfProbe.h"

#include <QPainter>
#include <QPen>
#include <QFont>
#include <QSet>
#include <QGraphicsItem>
#include <cmath>

namespace {

/// Shared font instances — creating a QFont per label per frame is expensive
/// (font engine resolution). Pixel-size fonts are device-independent.
const QFont& nameFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(10); return fnt; }();
    return f;
}
const QFont& labelFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(11); return fnt; }();
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

namespace BlockItemPainter {

void paint(const BlockPaintContext& ctx)
{
    GCAD_PERF_SCOPE("paint");

    // A block on a hidden layer is not painted at all (setVisible(false) also
    // keeps it out of hit-testing, but guard here too for safety).
    if (ctx.layerMode == LayerMode::Hidden)
        return;

    QPainter* painter = ctx.painter;
    const CanvasStyle* style = ctx.style;
    CanvasAnimator* animator = ctx.animator;
    const QGraphicsItem* targetItem = ctx.animatorTargetItem;

    // Non-active visible layer: render as a gray, semi-transparent reference.
    const bool grayed = (ctx.layerMode == LayerMode::Grayed);
    const QColor kGray = (style && style->dark) ? QColor(176, 171, 160) : QColor(0x9E, 0x9E, 0x9E);
    if (grayed)
        painter->setOpacity((style && style->dark) ? 0.55 : 0.4);

    // Draw segments — the hovered one is drawn LAST so its highlight sits on
    // top of sibling segments instead of being buried under them.
    // Hidden segments (lc.visible == false) are painted only when transiently
    // revealed (hovered or leader-highlighted), in a ghost style.
    constexpr int kGhostAlpha = 110;  ///< Alpha for transiently-revealed hidden lines.
    auto drawSegment = [&](const LineCache& lc) {
        const bool ghost = !lc.visible;
        // Dark-mode adaptation: lift the data color to the role's
        // light-on-dark family so ink lines stay legible on night paper.
        const QColor paintColor = style ? style->displayColor(lc.role, lc.color)
                                        : lc.color;
        EntityPaintParams pp;
        if (animator && targetItem) {
            pp = animator->lineParams(targetItem, lc.id,
                                      paintColor, lc.weight);
        } else {
            // Fallback: resolve state directly without animation.
            pp.lineColor  = paintColor;
            pp.lineWidth  = lc.weight;
            pp.labelColor = style ? style->labelColor(EntityState::Normal, false)
                                  : QColor(100, 100, 100);
        }

        QPen linePen(pp.lineColor, pp.lineWidth);
        linePen.setCosmetic(true);
        linePen.setStyle(lc.penStyle);
        // Leader-candidate override: teal recolor only ("connection" family),
        // same width — consistent with the hover-recolors-only language.
        if (lc.id == ctx.leaderEntity && style)
            linePen.setColor(style->attachmentNodeColor);
        // Grayed layers keep the highlight on hovered/leader segments — the
        // affordance that lets the user aim a connection; everything else
        // falls back to the gray reference tint.
        if (grayed && lc.id != ctx.leaderEntity && lc.id != ctx.hoveredEntity)
            linePen.setColor(kGray);
        if (ghost) {
            QColor c = linePen.color();
            c.setAlpha(kGhostAlpha);
            linePen.setColor(c);
        }
        painter->setPen(linePen);
        painter->drawLine(lc.p1, lc.p2);

        // 正交拐角偏置中心基准轴虚线 (起点至中心基准拐点)
        if (!grayed && lc.isOrtho && lc.showAxis) {
            QPen axisPen(pp.labelColor, 1.0);
            axisPen.setCosmetic(true);
            axisPen.setStyle(Qt::DashLine);
            if (ghost) {
                QColor c = axisPen.color();
                c.setAlpha(kGhostAlpha);
                axisPen.setColor(c);
            }
            painter->setPen(axisPen);
            painter->drawLine(lc.p1, lc.pCenter);
        }

        // 方向指示 (2026-12): 起点 → 终点 小箭头, 换向 (ReverseSegmentCommand)
        // 后 start/end 互换 → 缓存重算 → 箭头自动翻转 —— 换向几何零跳变,
        // 这是画布上唯一的"换向可见反馈"。灰显层不画 (同标签); 全局开关
        // (CanvasScene::directionArrowsEnabled) 关闭时整组隐藏。
        if (!grayed && ctx.dirArrows) {
            QColor dirColor = pp.labelColor;
            if (ghost) dirColor.setAlpha(kGhostAlpha);
            const double ang = std::atan2(lc.p2.y() - lc.p1.y(),
                                          lc.p2.x() - lc.p1.x());
            const QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                              (lc.p1.y() + lc.p2.y()) / 2.0);
            drawDirectionChevron(painter, mid, ang, dirColor);
        }

        // Draw segment name if enabled (suppressed on grayed reference layers).
        if ((lc.showName || ctx.forceName) && !lc.name.isEmpty() && !grayed) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QColor nameColor = pp.labelColor;
            if (ghost) nameColor.setAlpha(kGhostAlpha);
            QPen textPen(nameColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(nameFont());
            painter->drawText(mid + QPointF(4, -4), lc.name);
        }

        // Draw segment length label if enabled (suppressed on grayed layers).
        if ((lc.showLength || ctx.forceLen) && !lc.lengthText.isEmpty() && !grayed) {
            QPointF mid((lc.p1.x() + lc.p2.x()) / 2.0,
                        (lc.p1.y() + lc.p2.y()) / 2.0);
            QColor lenColor = animator ? pp.lengthLabelColor
                : (style ? style->labelColor(EntityState::Normal, true)
                         : QColor(0, 110, 60));
            if (ghost) lenColor.setAlpha(kGhostAlpha);
            QPen textPen(lenColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(lengthFont());
            painter->drawText(mid + QPointF(4, 12), lc.lengthText);
        }
    };

    const LineCache* hoveredLine = nullptr;
    const LineCache* leaderLine  = nullptr;
    for (const auto& lc : ctx.lines) {
        if (lc.id == ctx.hoveredEntity) {
            hoveredLine = &lc;
            continue;
        }
        if (lc.id == ctx.leaderEntity) {
            leaderLine = &lc;
            continue;
        }
        if (!lc.visible) continue;  // hidden and not revealed — skip painting
        drawSegment(lc);
    }
    // Highlighted segments last: leader below, hovered on top. Both are drawn
    // even when hidden (ghosted) so a hover can reveal a hidden segment.
    if (leaderLine)
        drawSegment(*leaderLine);
    if (hoveredLine)
        drawSegment(*hoveredLine);

    // Curves are painted by their OWN child items (CurveItem::paint) —
    // each handles its hover/ghost/grayed/leader states itself.

    // Draw points
    // Dedup key: 0.1 mm-grid position packed into an int64 (two int32 lands)
    // instead of QString::number + concat per point per frame — the old key
    // allocated 2 QStrings + a concat on EVERY labeled point in EVERY paint.
    QSet<qint64> drawnPointLabels;
    for (const auto& pc : ctx.points) {
        if (!pc.visible && pc.id != ctx.hoveredPointId) continue;

        EntityPaintParams pp;
        if (animator && targetItem) {
            pp = animator->pointParams(targetItem, pc.id,
                                       pc.isAuxiliary);
        } else {
            pp.pointFill   = pc.isAuxiliary
                ? (style ? style->pointColor(EntityState::Normal, true) : QColor(67, 160, 71))
                : (style ? style->pointColor(EntityState::Normal, false) : QColor(30, 30, 30));
            pp.pointRadius = 0.8;   // unified marker size (all point kinds)
            pp.labelColor  = style ? style->labelColor(EntityState::Normal, false) : QColor(80, 80, 80);
        }

        // Curve anchors (曲线点) render as a small ETCAD-style pink disc —
        // compact like ETCAD's curve points, distinct from endpoints/aux points.
        // Visual radius is intentionally much smaller than the PICK radius
        // (shape() keeps 2.5 for anchors): the hit area must stay finger-friendly
        // even though the dot is now a subtle marker.
        if (pc.isCurveAnchor) {
            pp.pointFill   = QColor(0xE9, 0x1E, 0x63);  // ETCAD pink
            pp.pointRadius = 0.8;   // 原 2.0 → 缩小一半多；命中范围不变 (shape() 2.5)
        }

        // Placed points (放置点) render as a distinct diamond (菱形) marker
        if (pc.isPlaced) {
            pp.pointFill = QColor(255, 140, 0);  // Amber/orange
            pp.pointRadius = 1.1;
        }

        // Hovered point: enlarged + teal — the "this is a grab/connect point"
        // affordance (same highlight language as hovered lines).
        if (pc.id == ctx.hoveredPointId) {
            pp.pointFill   = QColor(38, 166, 154);
            pp.pointRadius = 1.6;
        }

        // Both point kinds render as solid discs; auxiliary points are
        // distinguished by their green fill (绿色实心小圆) and slightly
        // larger radius. Grayed layers keep the teal on the hovered point.
        if (grayed && pc.id != ctx.hoveredPointId)
            pp.pointFill = kGray;
        painter->setPen(Qt::NoPen);
        painter->setBrush(pp.pointFill);
        if (pc.isPlaced) {
            const double r = pp.pointRadius * 1.3;
            QPolygonF diamond;
            diamond << QPointF(pc.pos.x(), pc.pos.y() - r)
                    << QPointF(pc.pos.x() + r, pc.pos.y())
                    << QPointF(pc.pos.x(), pc.pos.y() + r)
                    << QPointF(pc.pos.x() - r, pc.pos.y());
            painter->drawPolygon(diamond);
        } else {
            painter->drawEllipse(pc.pos, pp.pointRadius, pp.pointRadius);
        }

        // Anchor ring marking a connection point (attachment node). Protected
        // connections use the amber ring (拖动保护视觉区分).
        if (pc.isAttachmentNode && style && style->attachmentRingWidth > 0.0) {
            QPen ringPen(pc.isLockedNode ? style->lockedAttachmentColor
                                         : style->attachmentNodeColor,
                         style->attachmentRingWidth);
            ringPen.setCosmetic(true);
            painter->setPen(ringPen);
            painter->setBrush(Qt::NoBrush);
            const double r = pp.pointRadius + style->attachmentRingGap;
            painter->drawEllipse(pc.pos, r, r);
        }

        // Selected point indicator (accent ring/diamond)
        if (pc.id == ctx.selectedPointId) {
            const QColor selColor = style ? style->pointColor(EntityState::Selected, false) : QColor(204, 120, 92);
            QPen selPen(selColor, 1.8);
            selPen.setCosmetic(true);
            painter->setPen(selPen);
            painter->setBrush(Qt::NoBrush);
            if (pc.isPlaced) {
                const double r = pp.pointRadius * 2.2;
                QPolygonF selDiamond;
                selDiamond << QPointF(pc.pos.x(), pc.pos.y() - r)
                           << QPointF(pc.pos.x() + r, pc.pos.y())
                           << QPointF(pc.pos.x(), pc.pos.y() + r)
                           << QPointF(pc.pos.x() - r, pc.pos.y());
                painter->drawPolygon(selDiamond);
            } else {
                const double r = pp.pointRadius * 2.0;
                painter->drawEllipse(pc.pos, r, r);
            }
        }

        // Draw label (suppressed on grayed reference layers). Overlapping
        // points sharing the same name render ONE label (deduped by a
        // 0.1 mm-grid position key).
        if ((pc.showLabel || ctx.forceName) && !pc.label.isEmpty() && !grayed) {
            const qint64 kx = static_cast<qint64>(qRound(pc.pos.x() * 10.0));
            const qint64 ky = static_cast<qint64>(qRound(pc.pos.y() * 10.0));
            const qint64 posKey = (kx << 32) | (static_cast<quint64>(ky) & 0xFFFFFFFFULL);
            if (drawnPointLabels.contains(posKey))
                continue;
            drawnPointLabels.insert(posKey);
            QPen textPen(pp.labelColor);
            textPen.setCosmetic(true);
            painter->setPen(textPen);
            painter->setFont(labelFont());
            painter->drawText(pc.pos + QPointF(5, -5), pc.label);
        }
    }
}

} // namespace BlockItemPainter
