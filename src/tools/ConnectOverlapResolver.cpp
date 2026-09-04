#include "ConnectOverlapResolver.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
#include <QColor>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"           // Coord::toScene
#include "tools/ConnectGesture.h"    // kConnectSnapRadius (吸附环半径常量)
#include "ui/Theme.h"

namespace cad::tools {

namespace {

constexpr double kSourcePortRadiusPx = 5.0;

}

void ConnectOverlapResolver::dispose()
{
    removeConfirmHighlight();
    removeSourcePortMarker();
    removeConnectMarker();
    removeConnectHalo();
}

// ── 候选收集 ──

std::vector<ConfirmCandidate> ConnectOverlapResolver::collectConfirmCandidates(
    const Vec2& connWorldPos, const QUuid& fromBlockId) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == fromBlockId) continue;
        if (block.isShadow) continue;  // 影子不可作为连接目标 (R4, 拆开影子基准)
        const Vec2 local = block.transform.toLocal(connWorldPos);
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            if (sp && sp->resolved
                && sp->resolvedPos.distanceTo(local) < kSnapOverlapEps)
                out.push_back({block.id, seg.id, sp->id});
            if (ep && ep->resolved
                && ep->resolvedPos.distanceTo(local) < kSnapOverlapEps)
                out.push_back({block.id, seg.id, ep->id});
        }
    }
    return out;
}

std::vector<ConfirmCandidate>
ConnectOverlapResolver::collectComponentSwitchCandidates(
    const Vec2& connWorldPos, const QUuid& curBlockId, const QUuid& curSegId,
    const QUuid& fromBlockId, const QUuid& componentId) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc || componentId.isNull()) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == fromBlockId) continue;
        if (const auto* bcomp = m_paramDoc->componentsView().ofBlock(block.id);
            bcomp && bcomp->id == componentId)
            continue;
        const Vec2 local = block.transform.toLocal(connWorldPos);
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const QUuid hit =
                (sp && sp->resolved
                 && sp->resolvedPos.distanceTo(local) < kSnapOverlapEps) ? sp->id
              : (ep && ep->resolved
                 && ep->resolvedPos.distanceTo(local) < kSnapOverlapEps) ? ep->id
              : QUuid();
            if (hit.isNull()) continue;
            if (block.id == curBlockId && seg.id == curSegId) continue;
            out.push_back({block.id, seg.id, hit});
        }
    }
    return out;
}

// ── ConfirmTarget 高亮 ──

void ConnectOverlapResolver::updateHighlightAt(
    const Vec2& pos, const std::vector<ConfirmCandidate>& candidates)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = m_scene->currentZoom();

    QUuid hitBlock, hitSeg;
    if (const auto segSnap = m_snapEngine.findSegmentSnap(
            pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx())) {
        for (const auto& cand : candidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                hitBlock = cand.blockId;
                hitSeg = cand.segId;
                break;
            }
        }
    }

    if (hitSeg.isNull()) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }
    const auto* blk = m_paramDoc->findBlock(hitBlock);
    const auto* seg = blk ? blk->findSegment(hitSeg) : nullptr;
    const auto* sp = seg ? blk->findPoint(seg->startPointId) : nullptr;
    const auto* ep = seg ? blk->findPoint(seg->endPointId) : nullptr;
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    if (!m_confirmHighlight) {
        m_confirmHighlight = new QGraphicsPathItem();
        QPen pen(QColor(0xF39C12), 3.0);
        pen.setCosmetic(true);
        m_confirmHighlight->setPen(pen);
        m_confirmHighlight->setBrush(Qt::NoBrush);
        m_confirmHighlight->setZValue(101.0);
        m_scene->addItem(m_confirmHighlight);
    }
    QPainterPath path;
    path.moveTo(cad::geo::Coord::toScene(blk->worldPos(sp->id)));
    path.lineTo(cad::geo::Coord::toScene(blk->worldPos(ep->id)));
    m_confirmHighlight->setPath(path);
    m_confirmHighlight->setVisible(true);
}

void ConnectOverlapResolver::removeConfirmHighlight()
{
    if (m_confirmHighlight) {
        m_confirmHighlight->setVisible(false);
        delete m_confirmHighlight;
        m_confirmHighlight = nullptr;
    }
}

// ── ConfirmSource 源端口标记 ──

void ConnectOverlapResolver::setSourcePortMarker(const ConfirmCandidate& cand)
{
    if (!m_scene || !m_paramDoc) return;
    const auto* blk = m_paramDoc->findBlock(cand.blockId);
    if (!blk) return;

    const QPointF c = cad::geo::Coord::toScene(
        blk->worldPos(cand.pointId).x, blk->worldPos(cand.pointId).y);
    if (!m_sourcePortMarker) {
        m_sourcePortMarker = new QGraphicsEllipseItem();
        m_sourcePortMarker->setPen(QPen(cad::ui::Theme::tokens().accent, 2.0));
        m_sourcePortMarker->setBrush(QColor(47, 111, 237, 120));
        m_sourcePortMarker->setZValue(100.0);
        m_scene->addItem(m_sourcePortMarker);
    }
    m_sourcePortMarker->setRect(c.x() - kSourcePortRadiusPx,
                                c.y() - kSourcePortRadiusPx,
                                2.0 * kSourcePortRadiusPx,
                                2.0 * kSourcePortRadiusPx);
    m_sourcePortMarker->show();
}

void ConnectOverlapResolver::removeSourcePortMarker()
{
    if (m_sourcePortMarker) {
        if (m_scene) m_scene->removeItem(m_sourcePortMarker);
        delete m_sourcePortMarker;
        m_sourcePortMarker = nullptr;
    }
}

// ── 吸附环 + 源光晕 ──

void ConnectOverlapResolver::showConnectMarker(const Vec2& worldPos)
{
    if (!m_scene) return;
    double zoom = m_scene->currentZoom();
    if (zoom < 1e-9) zoom = 1.0;
    const double r = kConnectSnapRadius / zoom;
    const QPointF c = cad::geo::Coord::toScene(worldPos.x, worldPos.y);

    if (!m_connectMarker) {
        m_connectMarker = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));          // teal: "release = connect"
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_connectMarker->setPen(pen);
        m_connectMarker->setBrush(QColor(38, 166, 154, 50));
        m_connectMarker->setZValue(9999);
        m_scene->addItem(m_connectMarker);
    }
    m_connectMarker->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectMarker->show();
}

void ConnectOverlapResolver::removeConnectMarker()
{
    if (m_connectMarker) {
        if (m_scene) m_scene->removeItem(m_connectMarker);
        delete m_connectMarker;
        m_connectMarker = nullptr;
    }
}

void ConnectOverlapResolver::updateConnectHalo(const Vec2& fromPointWorld)
{
    if (!m_scene) return;
    double zoom = m_scene->currentZoom();
    if (zoom < 1e-9) zoom = 1.0;
    const double r = kConnectSnapRadius / zoom;  // halo == connect reach
    const QPointF c = cad::geo::Coord::toScene(fromPointWorld.x,
                                               fromPointWorld.y);

    if (!m_connectHalo) {
        m_connectHalo = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_connectHalo->setPen(pen);
        m_connectHalo->setBrush(QColor(38, 166, 154, 20));
        m_connectHalo->setZValue(9998);           // under the snap ring
        m_scene->addItem(m_connectHalo);
    }
    m_connectHalo->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectHalo->show();
}

void ConnectOverlapResolver::removeConnectHalo()
{
    if (m_connectHalo) {
        if (m_scene) m_scene->removeItem(m_connectHalo);
        delete m_connectHalo;
        m_connectHalo = nullptr;
    }
}

} // namespace cad::tools
