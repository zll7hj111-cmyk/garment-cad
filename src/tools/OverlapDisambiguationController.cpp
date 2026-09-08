#include "OverlapDisambiguationController.h"

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "canvas/OverlapBatteryHud.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "tools/HitTester.h"             // isInteractiveBlock (2026-12 审计 P0-5)
#include "tools/InteractionTolerances.h"  // kOverlapEpsMm (2026-12 审计 P1-3)

#include <QGraphicsView>
#include <algorithm>
#include "geometry/Epsilon.h"

namespace cad::tools {

namespace {

/// 线段角色 → 界面文案 (与 LinePropertyDialog 的角色下拉一致).
QString segmentRoleText(cad::param::SegmentRole role)
{
    using cad::param::SegmentRole;
    switch (role) {
    case SegmentRole::Outline:   return QString::fromUtf8("轮廓线");
    case SegmentRole::Internal:  return QString::fromUtf8("内部线");
    case SegmentRole::Auxiliary: return QString::fromUtf8("辅助线");
    }
    return QString::fromUtf8("轮廓线");
}

} // namespace

// ── 收集 / 构造 ──

QList<OverlapDisambiguationController::Candidate>
OverlapDisambiguationController::collect(const cad::geo::Vec2& worldPos) const
{
    QList<Candidate> out;
    if (!m_scene || !m_paramDoc) return out;

    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    // 统一命中 (P1/M7+L2): 堆叠降序 + 去重 + 活动层过滤都在 HitTester 一处。
    for (const auto& h : blockHitsAtScene(*m_scene, *m_paramDoc, scenePt)) {
        const auto* blk = m_paramDoc->blocksView().byId(h.blockId);
        if (!blk) continue;
        out.append(makeCandidate(*blk, h.segmentId));
    }
    return out;
}

QList<OverlapDisambiguationController::Candidate>
OverlapDisambiguationController::collectPoints(const cad::geo::Vec2& worldPos, double zoom) const
{
    QList<Candidate> out;
    if (!m_scene || !m_paramDoc) return out;
    if (zoom < cad::geo::kGeomEps) zoom = 1.0;

    const double reach = kConnectGrabRadiusPx / zoom;  // 端点抓取半径 → 用户单位

    struct PtEntry {
        const cad::param::Block* block = nullptr;
        cad::param::ParamPoint pt;
        cad::geo::Vec2 wpos;
    };
    std::vector<PtEntry> nearby;

    for (const auto& blk : m_paramDoc->blocks()) {
        if (!isInteractiveBlock(blk, *m_paramDoc)) continue;  // 2026-12 审计 P0-5
        for (const auto& pt : blk.points) {
            if (!pt.resolved) continue;
            const cad::geo::Vec2 wp = blk.worldPos(pt.id);
            if (wp.distanceTo(worldPos) <= reach) {
                nearby.push_back({&blk, pt, wp});
            }
        }
    }

    if (nearby.size() < 2) return out;

    // 按到光标的距离排序
    std::sort(nearby.begin(), nearby.end(), [&](const PtEntry& a, const PtEntry& b) {
        return a.wpos.distanceTo(worldPos) < b.wpos.distanceTo(worldPos);
    });

    const cad::geo::Vec2 refPos = nearby.front().wpos;

    for (const auto& entry : nearby) {
        if (entry.wpos.distanceTo(refPos) <= kOverlapEpsMm) {
            out.append(makePointCandidate(*entry.block, entry.pt));
        }
    }

    if (out.size() < 2) out.clear();
    return out;
}

OverlapDisambiguationController::Candidate
OverlapDisambiguationController::makeCandidate(const cad::param::Block& blk,
                                              const QUuid& segmentId) const
{
    Candidate c;
    c.kind = Candidate::Kind::Segment;
    c.blockId = blk.id;
    c.segmentId = segmentId;
    const cad::param::Segment* seg = nullptr;
    if (!segmentId.isNull())
        seg = blk.findSegment(segmentId);
    if (!seg && !blk.segments.empty())
        seg = &blk.segments.front();

    c.serial = seg ? seg->serial.toInt() : 0;
    c.title = seg ? (seg->serial.startsWith(QLatin1Char('L')) ? seg->serial : QStringLiteral("L%1").arg(seg->serial)) : QStringLiteral("L");

    // 显示名: 线段名 → 块名 → 线段 serial
    if (seg && !seg->name.isEmpty())        c.name = seg->name;
    else if (!blk.name.isEmpty())           c.name = blk.name;
    else if (seg && !seg->serial.isEmpty()) c.name = seg->serial;
    else                                    c.name = QString::fromUtf8("(未命名)");

    c.blockName = blk.name;

    bool isOrtho = false;
    if (seg) {
        if (const auto* ep = blk.findPoint(seg->endPointId)) {
            isOrtho = (ep->constraint == cad::param::PointConstraint::OrthoOffset &&
                       (std::abs(ep->orthoOffsetDist) > cad::geo::kGeomEpsLoose || !ep->orthoOffsetDistFormula.isEmpty()));
        }
    }
    c.roleText = (isOrtho ? QString::fromUtf8("偏置") : QString())
        + segmentRoleText(seg ? seg->role : cad::param::SegmentRole::Outline);
    c.layerName.clear();
    for (const auto& l : m_paramDoc->layers())
        if (l.id == blk.layer) { c.layerName = l.name; break; }

    if (seg) {
        c.lengthMm = blk.segmentEffectiveLength(seg->id);
    }
    return c;
}

OverlapDisambiguationController::Candidate
OverlapDisambiguationController::makePointCandidate(const cad::param::Block& blk,
                                                    const cad::param::ParamPoint& pt) const
{
    Candidate c;
    c.kind = Candidate::Kind::Point;
    c.blockId = blk.id;
    c.pointId = pt.id;
    c.segmentId = blk.exitSegmentAtPoint(pt.id);
    c.serial = 0;
    c.title = pt.serial.isEmpty() ? QStringLiteral("P") : pt.serial;
    c.name = !pt.name.isEmpty() ? pt.name : (!blk.name.isEmpty() ? blk.name : c.title);
    c.blockName = blk.name;
    c.isPlaced = pt.isPlaced;
    c.isAuxiliary = pt.isAuxiliary;
    c.isCurveAnchor = false;

    if (pt.isPlaced) {
        c.roleText = QString::fromUtf8("放置点");
    } else if (pt.isAuxiliary) {
        c.roleText = QString::fromUtf8("辅助点");
    } else {
        c.roleText = QString::fromUtf8("端点");
    }

    c.layerName.clear();
    for (const auto& l : m_paramDoc->layers())
        if (l.id == blk.layer) { c.layerName = l.name; break; }

    return c;
}

// ── 循环上下文 ──

void OverlapDisambiguationController::activate(const QList<Candidate>& cands,
                                                const QUuid& hitBlockId,
                                                const cad::geo::Vec2& anchor)
{
    if (cands.size() < 2) { deactivate(); return; }
    m_candidates = cands;
    int idx = 0;
    for (int i = 0; i < m_candidates.size(); ++i)
        if (m_candidates[i].blockId == hitBlockId) { idx = i; break; }
    m_index = idx;
    m_anchor = anchor;
    applyPick(m_index);

    // 联动展开电池组 HUD
    showBattery(anchor, m_candidates, cad::canvas::OverlapBatteryHud::DisplayMode::Expanded);
    if (m_batteryHud) {
        m_batteryHud->setHoveredIndex(m_index);
        m_batteryHud->setSelectedIndex(m_index);
    }

    m_modeFn();
}

void OverlapDisambiguationController::deactivate()
{
    m_index = -1;
    m_candidates.clear();
    hideBattery();
    m_modeFn();
}

void OverlapDisambiguationController::cycle()
{
    if (m_index < 0 || m_candidates.isEmpty()) return;

    // 剔除已消失的块, 实时重取身份信息
    QList<Candidate> live;
    for (const auto& c : m_candidates) {
        const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
        if (!blk) continue;
        if (c.kind == Candidate::Kind::Point) {
            if (const auto* pt = blk->findPoint(c.pointId))
                live.append(makePointCandidate(*blk, *pt));
        } else {
            live.append(makeCandidate(*blk, c.segmentId));
        }
    }
    if (live.isEmpty()) { deactivate(); return; }
    if (live.size() != m_candidates.size())
        m_index = qBound(0, m_index, live.size() - 1);
    m_candidates = live;

    m_index = (m_index + 1) % m_candidates.size();
    applyPick(m_index);

    if (m_batteryHud) {
        m_batteryHud->setHoveredIndex(m_index);
        m_batteryHud->setSelectedIndex(m_index);
    }

    m_modeFn();
}

void OverlapDisambiguationController::applyPick(int index)
{
    if (index < 0 || index >= m_candidates.size()) return;
    const auto& c = m_candidates[index];
    if (c.kind == Candidate::Kind::Point) {
        if (m_pointSelFn) m_pointSelFn(c.blockId, c.pointId);
    } else {
        if (m_selFn) m_selFn(c.blockId, c.segmentId);
    }
}

void OverlapDisambiguationController::pick(int index)
{
    if (index < 0 || index >= m_candidates.size()) return;
    m_index = index;
    applyPick(index);

    if (m_batteryHud) {
        m_batteryHud->setHoveredIndex(m_index);
        m_batteryHud->setSelectedIndex(m_index);
    }
}

// ── 电池组 HUD (引线 + 胶囊卡片, 任务 4) ──

void OverlapDisambiguationController::showBattery(
    const cad::geo::Vec2& worldPos, const QList<Candidate>& cands,
    cad::canvas::OverlapBatteryHud::DisplayMode mode)
{
    if (!m_scene || !m_paramDoc || cands.isEmpty()) {
        hideBattery();
        return;
    }

    m_batteryAnchor = worldPos;
    m_batteryCandidates = cands;

    std::vector<cad::canvas::BatteryCandidate> bCands;
    bCands.reserve(cands.size());
    for (const auto& c : cands) {
        cad::canvas::BatteryCandidate bc;
        bc.kind = (c.kind == Candidate::Kind::Point)
            ? cad::canvas::BatteryCandidate::Kind::Point
            : cad::canvas::BatteryCandidate::Kind::Segment;
        bc.blockId = c.blockId;
        bc.pointId = c.pointId;
        bc.segmentId = c.segmentId;
        bc.serial = c.serial;
        bc.title = c.title;
        bc.name = c.name;
        bc.blockName = c.blockName;
        bc.layerName = c.layerName;
        bc.roleText = c.roleText;
        bc.metricValue = c.lengthMm;
        bc.isPlaced = c.isPlaced;
        bc.isAuxiliary = c.isAuxiliary;
        bc.isCurveAnchor = c.isCurveAnchor;
        bCands.push_back(bc);
    }

    if (!m_batteryHud) {
        m_batteryHud = new cad::canvas::OverlapBatteryHud();
        m_scene->addItem(m_batteryHud);
        m_managed.own(m_batteryHud, &m_batteryHud);
    }

    m_batteryHud->setCandidates(std::move(bCands));
    m_batteryHud->setConnectMode(false);
    m_batteryHud->setDisplayMode(mode);

    const QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_batteryHud->updatePosition(worldPos, view);
    m_batteryHud->setVisible(true);
}

void OverlapDisambiguationController::hideBattery()
{
    m_batteryCandidates.clear();
    if (m_batteryHud) {
        m_managed.release(m_batteryHud);
    }
}

bool OverlapDisambiguationController::hasBattery() const
{
    return m_batteryHud != nullptr && m_batteryHud->isVisible() &&
           m_batteryHud->displayMode() != cad::canvas::OverlapBatteryHud::DisplayMode::Hidden;
}

cad::canvas::OverlapBatteryHud::DisplayMode OverlapDisambiguationController::batteryMode() const
{
    return m_batteryHud ? m_batteryHud->displayMode() : cad::canvas::OverlapBatteryHud::DisplayMode::Hidden;
}

void OverlapDisambiguationController::setBatteryMode(cad::canvas::OverlapBatteryHud::DisplayMode mode)
{
    if (m_batteryHud) {
        m_batteryHud->setDisplayMode(mode);
    }
}

int OverlapDisambiguationController::hitBatteryCandidateAt(const QPointF& scenePos, double zoom) const
{
    if (!hasBattery()) return -1;
    return m_batteryHud->hitCandidateAtScene(scenePos, zoom);
}

int OverlapDisambiguationController::hitBatteryCandidateAtWorld(const cad::geo::Vec2& worldPos, double zoom) const
{
    if (!hasBattery()) return -1;
    return m_batteryHud->hitCandidateAtWorld(worldPos, zoom);
}

bool OverlapDisambiguationController::hitBatteryBadgeAt(const QPointF& scenePos, double zoom) const
{
    if (!hasBattery()) return false;
    return m_batteryHud->hitBadgeAtScene(scenePos, zoom);
}

void OverlapDisambiguationController::setBatteryHoveredIndex(int index)
{
    if (m_batteryHud) {
        m_batteryHud->setHoveredIndex(index);
    }
}

int OverlapDisambiguationController::batteryHoveredIndex() const
{
    return m_batteryHud ? m_batteryHud->hoveredIndex() : -1;
}

void OverlapDisambiguationController::setBatterySelectedIndex(int index)
{
    if (m_batteryHud) {
        m_batteryHud->setSelectedIndex(index);
    }
}

int OverlapDisambiguationController::batterySelectedIndex() const
{
    return m_batteryHud ? m_batteryHud->selectedIndex() : -1;
}

int OverlapDisambiguationController::batteryCandidateCount() const
{
    return m_batteryCandidates.size();
}

OverlapDisambiguationController::Candidate
OverlapDisambiguationController::batteryCandidateAt(int index) const
{
    if (index >= 0 && index < m_batteryCandidates.size())
        return m_batteryCandidates[index];
    return Candidate{};
}

void OverlapDisambiguationController::recordClickedOverlap(
    const cad::geo::Vec2& pos, double zoom,
    const std::function<void(const QString&)>& toastFn)
{
    const auto ptCands = collectPoints(pos, zoom);
    if (ptCands.size() >= 2) {
        m_clickedPos = pos;
        m_clickedCands = ptCands;
        if (toastFn) {
            toastFn(QStringLiteral("此处有 %1 个重叠点，按 W 键打开电池组切换").arg(ptCands.size()));
        }
    } else {
        m_clickedCands.clear();
    }
}

bool OverlapDisambiguationController::handleSpaceOrAltKey()
{
    if (m_clickedCands.isEmpty()) return false;
    if (hasBattery()) {
        hideBattery();
    } else {
        showBattery(m_clickedPos, m_clickedCands,
                    cad::canvas::OverlapBatteryHud::DisplayMode::Expanded);
        setBatterySelectedIndex(0);
    }
    return true;
}

bool OverlapDisambiguationController::handleWOrBKey(const cad::geo::Vec2& cursorPos, double zoom)
{
    if (index() >= 0) {
        cycle();
        return true;
    }
    if (hasBattery()) {
        hideBattery();
        return true;
    }
    if (!m_clickedCands.isEmpty()) {
        showBattery(m_clickedPos, m_clickedCands,
                    cad::canvas::OverlapBatteryHud::DisplayMode::Expanded);
        setBatterySelectedIndex(0);
        return true;
    }
    const auto hoverPts = collectPoints(cursorPos, zoom);
    if (hoverPts.size() >= 2) {
        m_clickedPos = cursorPos;
        m_clickedCands = hoverPts;
        showBattery(m_clickedPos, m_clickedCands,
                    cad::canvas::OverlapBatteryHud::DisplayMode::Expanded);
        setBatterySelectedIndex(0);
        return true;
    }
    return false;
}

void OverlapDisambiguationController::dispose()
{
    m_index = -1;
    m_candidates.clear();
    m_clickedCands.clear();
    hideBattery();
}

} // namespace cad::tools
