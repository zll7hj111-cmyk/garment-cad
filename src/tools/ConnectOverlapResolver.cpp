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
#include "tools/ConnectGesture.h"
#include "tools/HitTester.h"              // isConnectTargetBlock (2026-12 审计 P0-5)
#include "tools/InteractionTolerances.h"  // kConnectSnapRadiusPx / kSourcePortRadiusPx / kOverlapEpsMm
#include "canvas/OverlapBatteryHud.h"
#include <QGraphicsView>
#include "ui/Theme.h"
#include "geometry/Epsilon.h"

namespace cad::tools {

void ConnectOverlapResolver::dispose()
{
    m_managed.clear();
}

// ── 候选收集 ──

std::vector<ConfirmCandidate> ConnectOverlapResolver::collectConfirmCandidates(
    const Vec2& connWorldPos, const QUuid& fromBlockId) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == fromBlockId) continue;
        if (!isConnectTargetBlock(block)) continue;  // 影子不可作为连接目标 (R4)
        const Vec2 local = block.transform.toLocal(connWorldPos);
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            if (sp && sp->resolved
                && sp->resolvedPos.distanceTo(local) < kOverlapEpsMm)
                out.push_back({block.id, seg.id, sp->id});
            if (ep && ep->resolved
                && ep->resolvedPos.distanceTo(local) < kOverlapEpsMm)
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
        if (!isConnectTargetBlock(block)) continue;  // 影子不可作为连接目标 (R4)
        if (const auto* bcomp = m_paramDoc->componentsView().ofBlock(block.id);
            bcomp && bcomp->id == componentId)
            continue;
        const Vec2 local = block.transform.toLocal(connWorldPos);
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const QUuid hit =
                (sp && sp->resolved
                 && sp->resolvedPos.distanceTo(local) < kOverlapEpsMm) ? sp->id
              : (ep && ep->resolved
                 && ep->resolvedPos.distanceTo(local) < kOverlapEpsMm) ? ep->id
              : QUuid();
            if (hit.isNull()) continue;
            if (block.id == curBlockId && seg.id == curSegId) continue;
            out.push_back({block.id, seg.id, hit});
        }
    }
    return out;
}

// ── ConfirmTarget 高亮 ──

void ConnectOverlapResolver::highlightCandidate(const QUuid& blockId, const QUuid& segId)
{
    if (!m_paramDoc || !m_scene || blockId.isNull() || segId.isNull()) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }
    const auto* blk = m_paramDoc->findBlock(blockId);
    const auto* seg = blk ? blk->findSegment(segId) : nullptr;
    if (!seg) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }
    const auto* sp = blk->findPoint(seg->startPointId);
    const auto* ep = blk->findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }

    if (!m_confirmHighlight) {
        m_confirmHighlight = new QGraphicsPathItem();
        m_managed.own(m_confirmHighlight, &m_confirmHighlight);
        const CanvasStyle& st = *m_scene->style();
        QPen pen(st.confirmHighlightColor, 3.0);
        pen.setCosmetic(true);
        m_confirmHighlight->setPen(pen);
        m_confirmHighlight->setBrush(Qt::NoBrush);
        m_confirmHighlight->setZValue(101.0);
        m_scene->addItem(m_confirmHighlight);
    }
    QPainterPath path;
    const auto* curveSpan = blk->curveSpanEntry(seg->id);
    if (curveSpan && !curveSpan->flatLocal.empty()) {
        path.moveTo(cad::geo::Coord::toScene(blk->transform.toWorld(curveSpan->flatLocal.front())));
        for (size_t i = 1; i < curveSpan->flatLocal.size(); ++i) {
            path.lineTo(cad::geo::Coord::toScene(blk->transform.toWorld(curveSpan->flatLocal[i])));
        }
    } else {
        path.moveTo(cad::geo::Coord::toScene(blk->worldPos(sp->id)));
        path.lineTo(cad::geo::Coord::toScene(blk->worldPos(ep->id)));
    }
    m_confirmHighlight->setPath(path);
    m_confirmHighlight->setVisible(true);
}

void ConnectOverlapResolver::updateHighlightAt(
    const Vec2& pos, const std::vector<ConfirmCandidate>& candidates)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = m_scene->safeZoom();

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

    highlightCandidate(hitBlock, hitSeg);
}

void ConnectOverlapResolver::removeConfirmHighlight()
{
    if (m_confirmHighlight) {
        m_managed.release(m_confirmHighlight);
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
        m_managed.own(m_sourcePortMarker, &m_sourcePortMarker);
        m_sourcePortMarker->setPen(QPen(cad::ui::Theme::tokens().accent, 2.0));
        QColor portWash = cad::ui::Theme::tokens().accent;   // 与描边同源 (审计 P0-1)
        portWash.setAlpha(120);
        m_sourcePortMarker->setBrush(portWash);
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
        m_managed.release(m_sourcePortMarker);
    }
}

// ── 吸附环 + 源光晕 ──

void ConnectOverlapResolver::showConnectMarker(const Vec2& worldPos)
{
    if (!m_scene) return;
    double zoom = m_scene->safeZoom();
    if (zoom < cad::geo::kGeomEps) zoom = 1.0;
    const double r = kConnectSnapRadiusPx / zoom;
    const QPointF c = cad::geo::Coord::toScene(worldPos.x, worldPos.y);

    if (!m_connectMarker) {
        m_connectMarker = new QGraphicsEllipseItem();
        m_managed.own(m_connectMarker, &m_connectMarker);
        const QColor teal = m_scene->style()->snapNodeColor;   // "release = connect"
        QPen pen(teal);
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_connectMarker->setPen(pen);
        m_connectMarker->setBrush(QColor(teal.red(), teal.green(), teal.blue(), 50));
        m_connectMarker->setZValue(9999);
        m_scene->addItem(m_connectMarker);
    }
    m_connectMarker->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectMarker->show();
}

void ConnectOverlapResolver::removeConnectMarker()
{
    if (m_connectMarker) {
        m_managed.release(m_connectMarker);
    }
}

void ConnectOverlapResolver::updateConnectHalo(const Vec2& fromPointWorld)
{
    if (!m_scene) return;
    double zoom = m_scene->safeZoom();
    if (zoom < cad::geo::kGeomEps) zoom = 1.0;
    const double r = kConnectSnapRadiusPx / zoom;  // halo == connect reach
    const QPointF c = cad::geo::Coord::toScene(fromPointWorld.x,
                                               fromPointWorld.y);

    if (!m_connectHalo) {
        m_connectHalo = new QGraphicsEllipseItem();
        m_managed.own(m_connectHalo, &m_connectHalo);
        const QColor teal = m_scene->style()->snapNodeColor;
        QPen pen(teal);
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_connectHalo->setPen(pen);
        m_connectHalo->setBrush(QColor(teal.red(), teal.green(), teal.blue(), 20));
        m_connectHalo->setZValue(9998);           // under the snap ring
        m_scene->addItem(m_connectHalo);
    }
    m_connectHalo->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectHalo->show();
}

void ConnectOverlapResolver::removeConnectHalo()
{
    if (m_connectHalo) {
        m_managed.release(m_connectHalo);
    }
}

// ── 电池组靶标 (Battery Landing Pad, 任务 4) ──

void ConnectOverlapResolver::showBatteryLandingPads(
    const Vec2& worldPos, const std::vector<ConfirmCandidate>& candidates)
{
    if (!m_scene || !m_paramDoc || candidates.empty()) {
        hideBatteryLandingPads();
        return;
    }

    std::vector<cad::canvas::BatteryCandidate> bCands;
    bCands.reserve(candidates.size());

    for (const auto& c : candidates) {
        const auto* blk = m_paramDoc->findBlock(c.blockId);
        if (!blk) continue;
        const auto* pt = blk->findPoint(c.pointId);
        const auto* seg = blk->findSegment(c.segId);

        cad::canvas::BatteryCandidate bc;
        bc.kind = cad::canvas::BatteryCandidate::Kind::Point;
        bc.blockId = c.blockId;
        bc.pointId = c.pointId;
        bc.segmentId = c.segId;
        bc.serial = 0;
        bc.title = pt ? (pt->serial.isEmpty() ? QStringLiteral("P") : pt->serial) : QString();
        bc.name = (pt && !pt->name.isEmpty()) ? pt->name : (seg && !seg->name.isEmpty() ? seg->name : blk->name);
        bc.blockName = blk->name;
        bc.roleText = (seg && !seg->name.isEmpty()) ? seg->name : QString::fromUtf8("基准目标");
        bc.isPlaced = pt ? pt->isPlaced : false;
        bc.isAuxiliary = pt ? pt->isAuxiliary : false;
        bc.isCurveAnchor = false;
        bCands.push_back(bc);
    }

    if (!m_batteryHud) {
        m_batteryHud = new cad::canvas::OverlapBatteryHud();
        m_scene->addItem(m_batteryHud);
        m_managed.own(m_batteryHud, &m_batteryHud);
    }

    m_batteryHud->setCandidates(std::move(bCands));
    m_batteryHud->setConnectMode(true);
    m_batteryHud->setDisplayMode(cad::canvas::OverlapBatteryHud::DisplayMode::Expanded);

    const QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_batteryHud->updatePosition(worldPos, view);
    m_batteryHud->setVisible(true);
}

void ConnectOverlapResolver::hideBatteryLandingPads()
{
    if (m_batteryHud) {
        m_managed.release(m_batteryHud);
    }
}

bool ConnectOverlapResolver::hasBatteryLandingPads() const
{
    return m_batteryHud != nullptr && m_batteryHud->isVisible() &&
           m_batteryHud->displayMode() == cad::canvas::OverlapBatteryHud::DisplayMode::Expanded;
}

int ConnectOverlapResolver::hitBatteryCandidateAt(const QPointF& scenePos, double zoom) const
{
    if (!hasBatteryLandingPads()) return -1;
    return m_batteryHud->hitCandidateAtScene(scenePos, zoom);
}

void ConnectOverlapResolver::setBatteryHoveredIndex(int index)
{
    if (m_batteryHud) {
        m_batteryHud->setHoveredIndex(index);
    }
}

QPointF ConnectOverlapResolver::batteryPortScenePos(int index, double zoom) const
{
    if (!m_batteryHud) return QPointF();
    return m_batteryHud->candidatePortScenePos(index, zoom);
}

} // namespace cad::tools
