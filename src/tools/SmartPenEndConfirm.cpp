#include "ToolSmartPen.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QFontMetricsF>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QtMath>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/BlockItem.h"
#include "HitTester.h"
#include "canvas/CanvasScene.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "ui/QuickAuxDialog.h"
#include "LeaderCandidatePicker.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// 落点确认 (stacked-point disambiguation)
// ---------------------------------------------------------------------------

std::vector<SnapResult> ToolSmartPen::overlapPool(
    const cad::geo::Vec2& spot, const SnapResult& snap) const
{
    std::vector<SnapResult> out;
    if (!m_paramDoc) return out;
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    // findSnapCandidates applies the same layer policy as findSnap
    // (layerSnappable) — only LEGAL attachment targets ever enter the pool,
    // so a switch can never propose a rejected cross-layer attachment.
    const auto cands = m_snapEngine.findSnapCandidates(spot, m_paramDoc, zoom);
    for (const auto& c : cands)
        if (c.worldPos.distanceTo(snap.worldPos) <= kSnapOverlapEps)
            out.push_back(c);
    return out;
}

std::optional<SnapResult> ToolSmartPen::preferActiveLayer(
    const std::vector<SnapResult>& pool,
    const std::optional<SnapResult>& fallback) const
{
    if (!m_paramDoc) return fallback;
    const QUuid active = m_paramDoc->activeLayer();
    for (const auto& c : pool) {   // nearest first
        const auto* blk = m_paramDoc->findBlock(c.blockId);
        if (blk && blk->layer == active) return c;
    }
    return fallback;
}

void ToolSmartPen::enterEndConfirm(const SnapResult& snap,
                                   const std::vector<SnapResult>& pool)
{
    // 先选活动层: the default end target is the active layer's stacked
    // point (fallback = the raw nearest pick; findSnap already resolves
    // exact coincidences to the active layer too).
    m_endAutoPick = preferActiveLayer(pool, snap);

    // Confirmable segments: every segment incident (endpoint or interpolated
    // host) to any pool point — same collection rule as the leader picker.
    m_endCands.clear();
    QSet<QUuid> seen;
    for (const auto& p : pool) {
        const auto* blk = m_paramDoc ? m_paramDoc->findBlock(p.blockId) : nullptr;
        if (!blk) continue;
        for (const auto& seg : blk->segments) {
            const bool isEndpoint = (seg.startPointId == p.pointId
                                     || seg.endPointId == p.pointId);
            bool isHost = false;
            if (const auto* pt = blk->findPoint(p.pointId))
                isHost = (pt->constraint == cad::param::PointConstraint::Interpolated
                          && pt->hostSegmentId == seg.id);
            if (!isEndpoint && !isHost) continue;
            if (seen.contains(seg.id)) continue;
            seen.insert(seg.id);
            m_endCands.push_back({p.blockId, seg.id, p.pointId, p});
        }
    }

    setState(State::ConfirmEnd);
    if (m_scene)
        m_scene->showToast(QString::fromUtf8(
            "落点存在多个重叠点：点选线段切换落点，空白点击接受默认，Esc 取消"));
    if (m_endAutoPick)
        updatePreview(m_endAutoPick->worldPos);   // lock the rubber band
}

void ToolSmartPen::handleConfirmEndPress(const cad::geo::Vec2& clickPos)
{
    if (!m_endAutoPick) { cancelLine(); return; }

    std::optional<SnapResult> confirmed;
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        clickPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_endCands) {
            if (cand.blockId == segSnap->blockId
                && cand.segId == segSnap->segmentId) {
                confirmed = cand.snap;
                break;
            }
        }
    }
    // Candidate segment → that point; blank click = accept the default
    // (active-layer) pick. Esc cancels the whole stroke (see keyPress).
    commitEndSnap(confirmed.value_or(*m_endAutoPick));
}

void ToolSmartPen::commitEndSnap(const SnapResult& endSnap)
{
    if (!m_startSnap) {
        // 用户拍板: 起点自由 + 终点吸附 = 翻转新线 —— 吸附点成为新线
        // 起点, 原起点位置成为自由终点 (终点线变起点, 解开后语义不变:
        // 线段几何/长度/角度完全一致, 仅端点身份互换). 这样终点吸附
        // 也能走统一的“起点连接”路径创建连接, 而非被忽略成自由线.
        const cad::geo::Vec2 origStart = m_startPoint;
        m_startPoint = endSnap.worldPos;
        m_startSnap  = endSnap;
        m_leaderPicker->setRefDirDeg(0.0);
        commitLine(origStart, std::nullopt);
        return;
    }
    commitLine(endSnap.worldPos, endSnap);
}

bool ToolSmartPen::trySwitchStartPoint(const LeaderCandidate& cand, int candIndex)
{
    if (!m_startSnap) return false;
    if (cand.pointId == m_startSnap->pointId) return false;
    // Only pool members (legal, snappable targets) may become the start
    // point — a grayed-layer segment in the leader list stays a pure angle
    // reference and cannot hijack the attachment.
    for (const auto& p : m_startPool) {
        if (p.blockId == cand.blockId && p.pointId == cand.pointId) {
            m_startPoint = p.worldPos;
            m_startSnap  = p;
            // Keep the clicked segment as the construction-angle reference.
            m_leaderPicker->setIndex(candIndex);
            return true;
        }
    }
    return false;
}

void ToolSmartPen::updateEndConfirmHighlight(const cad::geo::Vec2& worldPos)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = m_scene->currentZoom();

    QUuid hitBlock, hitSeg;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        worldPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_endCands) {
            if (cand.blockId == segSnap->blockId
                && cand.segId == segSnap->segmentId) {
                hitBlock = cand.blockId;
                hitSeg = cand.segId;
                break;
            }
        }
    }
    if (hitBlock == m_endHighlightBlockId && hitSeg == m_endHighlightSegId)
        return;
    clearEndConfirmHighlight();
    m_endHighlightBlockId = hitBlock;
    m_endHighlightSegId = hitSeg;
    if (!hitBlock.isNull())
        if (auto* item = m_scene->findBlockItem(hitBlock))
            item->setLeaderHighlight(hitSeg);
}

void ToolSmartPen::clearEndConfirmHighlight()
{
    if (m_scene && !m_endHighlightBlockId.isNull())
        if (auto* item = m_scene->findBlockItem(m_endHighlightBlockId))
            item->setLeaderHighlight(QUuid());
    m_endHighlightBlockId = QUuid();
    m_endHighlightSegId = QUuid();
}

void ToolSmartPen::resetStrokeTargets()
{
    m_startPool.clear();
    m_endCands.clear();
    m_endAutoPick.reset();
    clearEndConfirmHighlight();
}

} // namespace cad::tools
