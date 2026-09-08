#include "ToolSelect.h"
#include "ToolManager.h"

#include "SelectDragController.h"
#include "CurveAnchorDragSession.h"
#include "OverlapDisambiguationController.h"
#include "SelectHoverFeedback.h"

#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

#include "canvas/CanvasScene.h"
#include "HitTester.h"
#include "canvas/BlockItem.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ConnectGesture.h"
#include "CopyDragController.h"
#include "MarqueeGesture.h"
#include "document/commands/EndpointCommands.h"

namespace cad::tools {

namespace {

constexpr double kOverlapSelectThresholdPx = 5.0;  ///< 重叠集群激活阈值.

}

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

ToolDescriptor ToolSelect::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Select;
    d.displayName = QString::fromUtf8("选择(&V)");
    d.iconName = QStringLiteral("cursor-click");
    d.shortcut = QKeySequence(Qt::Key_V);
    d.hintText = modeIndicatorFor(-1, 0)
                     .hint(reinterpret_cast<const char*>(u8"选择"));
    d.factory = [] { return std::make_unique<ToolSelect>(); };
    return d;
}

ToolSelect::ToolSelect() = default;
ToolSelect::~ToolSelect() = default;

void ToolSelect::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_state = SelectState::Idle;
    m_selectionConfirmed = false;

    QUndoStack* const undo = m_paramDoc ? m_paramDoc->undoStack() : nullptr;

    // ── Extracted gesture controllers (阶段 3 拆分) ──
    m_dragCtl = std::make_unique<SelectDragController>(
        m_paramDoc, undo,
        [this](SelectState s) { setState(s); });
    m_anchorDrag = std::make_unique<CurveAnchorDragSession>(m_paramDoc, undo);
    m_overlapCtl = std::make_unique<OverlapDisambiguationController>(
        m_scene, m_paramDoc,
        [this](const QUuid& bid, const QUuid& sid) {
            // 在常驻多选模式下，消歧选择仅替换重叠候选簇内的对象，保留簇外已选对象
            if (m_overlapCtl) {
                for (const auto& c : m_overlapCtl->candidates()) {
                    if (c.blockId != bid)
                        m_selection.remove(c.blockId);
                }
            }
            m_selection.insert(bid);
            m_lastHitSegmentId = sid;
            syncSelectionVisual();
            setState(SelectState::Selecting);
            notifyEditTarget();
        },
        [this]() { refreshModeIndicator(); },
        [this](const QUuid& bid, const QUuid& pid) {
            // 点候选命中: 若为放置点则进入放置点属性状态
            if (m_paramDoc) {
                if (auto* blk = m_paramDoc->findBlock(bid)) {
                    if (auto* pt = blk->findPoint(pid)) {
                        if (pt->isPlaced) {
                            selectPlacedPoint(bid, pid);
                            return;
                        }
                    }
                }
            }
            if (m_overlapCtl) {
                for (const auto& c : m_overlapCtl->candidates()) {
                    if (c.blockId != bid)
                        m_selection.remove(c.blockId);
                }
            }
            m_selection.insert(bid);
            syncSelectionVisual();
            setState(SelectState::Selecting);
            notifyEditTarget();
        });

    // (Re)create the extracted gestures with the current context. The tools
    // forward their state transitions / selection queries through callbacks.
    // (The document may be null during ToolManager construction; the gestures
    // no-op on missing doc until setParamDocument wires the real one in.)
    m_connectGesture = std::make_unique<ConnectGesture>(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this](const QString& t) { showToast(t); },
        [this]() { clearSelectionAndIdle(); },
        [this]() { return m_selection.isEmpty(); },
        [this](const QUuid& bid, const QUuid& sid, const QUuid& attId, double initial) {
            reportConnectAngleSession(bid, sid, attId, initial);
        },
        [this](bool valid) { reportConnectAngleValidity(valid); });
    m_copyDrag = std::make_unique<CopyDragController>(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this]() {
            setState(m_selection.isEmpty() ? SelectState::Idle
                                           : SelectState::Selecting);
        },
        [this]() { clearSelectionAndIdle(); });
    m_marqueeGesture = std::make_unique<MarqueeGesture>(m_scene, m_paramDoc);
    refreshModeIndicator();
}

void ToolSelect::onDeactivate()
{
    hideHoverEndpointRing();

    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_overlapCtl) m_overlapCtl->dispose();
    m_connectGesture.reset();
    m_copyDrag.reset();
    m_marqueeGesture.reset();
    m_dragCtl.reset();
    m_anchorDrag.reset();
    m_overlapCtl.reset();

    clearPlacedPointSelection();
    m_selection.clear();
    m_selectionConfirmed = false;
    if (m_scene && m_paramDoc)
        syncSelectionVisual();
    if (m_scene)
        m_scene->clearSelection();
    m_hoverCtl.resetCursor();
    if (m_scene && !m_scene->views().isEmpty())
        m_scene->views().first()->viewport()->setCursor(Qt::ArrowCursor);

    m_state = SelectState::Idle;
}

// ═══════════════════════════════════════════════════════════════════════════════
// State management
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::setState(SelectState s)
{
    if (s == SelectState::Confirmed)
        s = SelectState::Selecting;
    m_state = s;
}

void ToolSelect::connectAngleTextChanged(const QString& text)
{
    if (m_connectGesture) m_connectGesture->onAngleTextChanged(text);
}

void ToolSelect::connectAngleModeChanged(cad::param::RotationMode mode)
{
    if (m_connectGesture) m_connectGesture->onAngleModeChanged(mode);
}

void ToolSelect::connectAngleCommitted()
{
    if (m_connectGesture) m_connectGesture->commitAngle();
}

void ToolSelect::connectAngleCancelled()
{
    if (m_connectGesture) m_connectGesture->cancelAngle();
}

void ToolSelect::selectPlacedPoint(const QUuid& blockId, const QUuid& pointId)
{
    m_selection.clear();
    m_selectionConfirmed = false;
    syncSelectionVisual();
    setState(SelectState::Selecting);
    notifyEditTarget();

    m_selectedPlacedBlockId = blockId;
    m_selectedPlacedPointId = pointId;

    if (m_scene && m_paramDoc) {
        for (const auto& blk : m_paramDoc->blocks()) {
            if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
                bi->setSelectedPoint(blk.id == blockId ? pointId : QUuid());
            }
        }
    }

    reportPlacedPointTarget(blockId, pointId);
}

void ToolSelect::clearPlacedPointSelection()
{
    if (m_selectedPlacedPointId.isNull()) return;
    m_selectedPlacedBlockId = QUuid();
    m_selectedPlacedPointId = QUuid();

    if (m_scene && m_paramDoc) {
        for (const auto& blk : m_paramDoc->blocks()) {
            if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
                bi->setSelectedPoint(QUuid());
            }
        }
    }
    reportClearPlacedPoint();
}

void ToolSelect::clearSelectionAndIdle()
{
    hideHoverEndpointRing();
    m_selectionConfirmed = false;
    clearPlacedPointSelection();
    if (m_overlapCtl) m_overlapCtl->deactivate();
    m_selection.clear();
    syncSelectionVisual();
    setState(SelectState::Idle);
    notifyEditTarget();
}

void ToolSelect::clearSelectionOnLayerChange()
{
    hideHoverEndpointRing();
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_marqueeGesture) m_marqueeGesture->cancel();
    clearSelectionAndIdle();
}

void ToolSelect::selectBlocksExternally(const QList<QUuid>& blockIds)
{
    if (!m_paramDoc || blockIds.isEmpty()) return;
    if (m_overlapCtl) m_overlapCtl->deactivate();
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_marqueeGesture) m_marqueeGesture->cancel();

    m_selection = QSet<QUuid>(blockIds.begin(), blockIds.end());
    m_selectionConfirmed = false;
    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    notifyEditTarget();
}

ModeIndicator ToolSelect::modeIndicator() const
{
    const int idx = m_overlapCtl ? m_overlapCtl->index() : -1;
    const int cnt = m_overlapCtl ? m_overlapCtl->candidates().size() : 0;
    return modeIndicatorFor(idx, cnt);
}

ModeIndicator ToolSelect::modeIndicatorFor(int overlapIndex, int overlapCount)
{
    if (overlapIndex >= 0) {
        ModeIndicator mi;
        mi.modeName = QString::fromUtf8("重叠候选");
        mi.detail   = QString::fromUtf8("第 %1/%2 项 | 点击确认 | Esc/空白取消")
                          .arg(overlapIndex + 1).arg(overlapCount);
        mi.wAction  = QString::fromUtf8("W 循环候选");
        mi.isDefault = true;
        return mi;
    }

    ModeIndicator mi;
    mi.modeName = QString::fromUtf8("选择");
    mi.detail   = QString::fromUtf8("长按单线拖动 | 悬停端点拖动连接 | 空白框选 | 右键确定移动");
    mi.wAction  = QString();
    mi.toast    = QString();
    mi.isDefault = true;
    return mi;
}

void ToolSelect::showToast(const QString& text)
{
    if (m_scene)
        m_scene->showToast(text);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection management (toggle + red highlight)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleBlock(const QUuid& blockId)
{
    const bool anySelected = m_selection.contains(blockId);
    if (anySelected) m_selection.remove(blockId);
    else             m_selection.insert(blockId);

    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    notifyEditTarget();
}

void ToolSelect::syncSelectionVisual()
{
    if (!m_scene || !m_paramDoc) return;
    for (const auto& blk : m_paramDoc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            const bool inSel = m_selection.contains(blk.id);
            bi->setToolSelected(inSel);
            bi->setToolLocked(inSel);
        }
    }
}

void ToolSelect::setBlockHighlight(const QUuid& blockId, bool on)
{
    if (!m_scene) return;
    if (BlockItem* bi = m_scene->findBlockItem(blockId))
        bi->setToolSelected(on);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse events — main dispatch
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    hideHoverEndpointRing();

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());
    m_lastCursorPos = pos;

    if (m_connectGesture && m_connectGesture->active()) {
        if (m_state == SelectState::AngleInput) {
            if (event->button() == Qt::LeftButton)
                m_connectGesture->pressAngleTarget(pos);
            return;
        }
        if (event->button() == Qt::RightButton) {
            m_connectGesture->cancel();
            return;
        }
        if (event->button() == Qt::LeftButton
            && m_state == SelectState::ConfirmTarget)
            m_connectGesture->pressConfirmTarget(pos);
        else if (event->button() == Qt::LeftButton
                 && m_state == SelectState::ConfirmSource)
            m_connectGesture->pressConfirmSource(pos);
        return;
    }

    // ── Right button: 上下文菜单 / 空白切智能笔 ──
    if (event->button() == Qt::RightButton) {
        showContextMenu(event);
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // ── 检查是否点击了电池 HUD (展开态点击切换选中) ──
    if (m_overlapCtl && m_overlapCtl->hasBattery()
        && m_overlapCtl->batteryMode() == cad::canvas::OverlapBatteryHud::DisplayMode::Expanded) {
        const double zoom = m_scene->currentZoom();
        int hitIdx = m_overlapCtl->hitBatteryCandidateAtWorld(pos, zoom);
        if (hitIdx >= 0 && hitIdx < m_overlapCtl->batteryCandidateCount()) {
            const auto cand = m_overlapCtl->batteryCandidateAt(hitIdx);
            m_overlapCtl->setBatterySelectedIndex(hitIdx);
            if (cand.kind == OverlapDisambiguationController::Candidate::Kind::Point) {
                if (cand.isPlaced) {
                    m_selection.clear();
                    syncSelectionVisual();
                    selectPlacedPoint(cand.blockId, cand.pointId);
                } else {
                    clearPlacedPointSelection();
                    m_selection = {cand.blockId};
                    m_lastHitSegmentId = cand.segmentId;
                    syncSelectionVisual();
                    setState(SelectState::Selecting);
                    notifyEditTarget();
                }
            } else {
                clearPlacedPointSelection();
                m_selection = {cand.blockId};
                m_lastHitSegmentId = cand.segmentId;
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
            }
            event->accept();
            return;
        }
    }

    // 点击其他区域，若电池组当前处于显示状态，则收起电池组
    if (m_overlapCtl && m_overlapCtl->hasBattery()) {
        m_overlapCtl->hideBattery();
    }

    // ── 记录点击位置处的重叠点，供快捷键 (W/B) 随时打开电池组 ──
    if (m_overlapCtl) {
        double zoom = m_scene ? m_scene->currentZoom() : 1.0;
        if (zoom < 1e-9) zoom = 1.0;
        m_overlapCtl->recordClickedOverlap(pos, zoom, [this](const QString& msg) { showToast(msg); });
    }

    // ── Placed point press: 单击选中放置点 ──
    SnapEngine snapEngine;
    double zoom = m_scene->currentZoom();
    if (zoom < 1e-9) zoom = 1.0;
    auto snap = snapEngine.findSnap(pos, m_paramDoc, zoom, 12.0, {}, nullptr, true);
    if (snap) {
        if (auto* blk = m_paramDoc->findBlock(snap->blockId)) {
            if (auto* pt = blk->findPoint(snap->pointId)) {
                if (pt->isPlaced) {
                    deactivateOverlapContext();
                    selectPlacedPoint(snap->blockId, snap->pointId);
                    return;
                }
            }
        }
    }

    // 点击其他元素或空白，清除放置点选择状态
    clearPlacedPointSelection();

    // ── Ctrl+press → 快捷复制 ──
    if ((event->modifiers() & Qt::ControlModifier) && m_copyDrag) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull()) {
            deactivateOverlapContext();
            if (!m_selection.contains(blockHit)) {
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = {blockHit};
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
            }
            const QSet<QUuid> copySet =
                m_paramDoc->componentsView().closure(
                    m_paramDoc->attachmentsView().lockedClosure(m_selection));
            m_copyDrag->begin(copySet, blockHit, pos);
            return;
        }
    }

    // ── Point-level press: 曲线锚点 / 端点连接 ──
    if (tryPointOperation(pos)) {
        if (m_overlapCtl) m_overlapCtl->deactivate();
        return;
    }

    // ── 线身 press: 统一多选模式 + 待定拖动 ──
    switch (m_state) {
    case SelectState::ConfirmedReady: {
        // 已通过右键菜单确认选中移动：在画布任意位置按住拖动即搬运整组
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull() || !m_selection.isEmpty()) {
            m_hoverCtl.beginPending(pos, blockHit, true);
        } else {
            setSelectionConfirmed(false);
            clearSelectionAndIdle();
        }
        return;
    }
    case SelectState::Idle:
    case SelectState::Selecting: {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull()) {
            deactivateOverlapContext();
            const bool wasSelected = m_selection.contains(blockHit);
            m_lastHitSegmentId = hitSegmentAt(pos);
            if (!wasSelected) {
                // 点击未选线段：加选到选择集
                toggleBlock(blockHit);
            }
            m_hoverCtl.beginPending(pos, blockHit, wasSelected);
            const auto cands = m_overlapCtl ? m_overlapCtl->collect(pos) : QList<OverlapCandidate>();
            if (cands.size() >= 2)
                m_overlapCtl->activate(cands, blockHit, pos);
            else if (m_overlapCtl)
                m_overlapCtl->deactivate();
            return;
        }
        deactivateOverlapContext();
        if (m_marqueeGesture) {
            m_marqueeGesture->begin(pos, m_selection);
            setState(SelectState::Marquee);
        }
        break;
    }

    default:
        break;
    }
}

void ToolSelect::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());
    m_lastCursorPos = pos;

    if (m_connectGesture && m_connectGesture->active()) {
        m_connectGesture->move(pos);
        return;
    }
    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->move(pos);
        return;
    }

    // 长按拖动判定
    if (m_hoverCtl.pending()) {
        double zoom = m_scene->currentZoom();
        if (pos.distanceTo(m_hoverCtl.pos()) > m_hoverCtl.thresholdUserUnits(zoom)) {
            const cad::geo::Vec2 startPos = m_hoverCtl.pos();
            const QUuid pendingBlock = m_hoverCtl.blockId();
            m_hoverCtl.cancelPending();
            hideHoverEndpointRing();
            if (m_overlapCtl) {
                m_overlapCtl->hideBattery();
            }
            m_dragCtl->setZoom(zoom);
            if (m_selectionConfirmed) {
                // 已右键确认：拖动整组选择集
                m_dragCtl->begin(startPos, m_selection);
            } else {
                // 未确认：单线长按拖动 (仅拖动当前按住的线段)
                QSet<QUuid> singleSet;
                if (!pendingBlock.isNull()) singleSet.insert(pendingBlock);
                else singleSet = m_selection;
                m_dragCtl->begin(startPos, singleSet);
            }
            setState(SelectState::Dragging);
        }
        return;
    }

    // 无按钮悬停: 端点圆环 + 光标 + hover 目标
    if (event->buttons() != Qt::NoButton) {
        hideHoverEndpointRing();
    } else if (!m_anchorDrag->active()
        && m_state != SelectState::Dragging
        && m_state != SelectState::Marquee) {
        updateHoverEndpointRing(pos);

        const QPointF scenePt = cad::geo::Coord::toScene(pos.x, pos.y);
        const double zoom = m_scene->currentZoom();

        // 1. 悬停在展开的电池 HUD 上时的交互
        if (m_overlapCtl && m_overlapCtl->hasBattery()
            && m_overlapCtl->batteryMode() == cad::canvas::OverlapBatteryHud::DisplayMode::Expanded) {
            int hitIdx = m_overlapCtl->hitBatteryCandidateAtWorld(pos, zoom);
            m_overlapCtl->setBatteryHoveredIndex(hitIdx);
            if (hitIdx >= 0 && hitIdx < m_overlapCtl->batteryCandidateCount()) {
                const auto cand = m_overlapCtl->batteryCandidateAt(hitIdx);
                reportHoverTarget(cand.blockId, cand.segmentId);
                return;
            }
            const double dist = pos.distanceTo(m_overlapCtl->batteryAnchor());
            if (dist > (250.0 / (zoom > 1e-9 ? zoom : 1.0))) {
                m_overlapCtl->hideBattery();
            }
        }

        const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
        const QUuid blockHit = hits.empty() ? QUuid() : hits.front().blockId;
        const bool ctrlHeld = (QGuiApplication::keyboardModifiers() & Qt::ControlModifier);

        // 无论是否已选，端点悬停均显现连接十字；线身显示抓手
        const Qt::CursorShape cur = m_hoverCtl.cursorShapeFor(
            m_paramDoc, blockHit, pos, zoom, ctrlHeld);
        // 同值短路只设 viewport: 光标状态存 ToolSelect 内部, 避免控制器持有 view
        if (blockHit.isNull())
            reportHoverTarget(QUuid(), QUuid());
        else
            reportHoverTarget(hits.front().blockId, hits.front().segmentId);

        if (cur != m_hoverCtl.cursor()) {
            m_hoverCtl.setCursor(cur);
            if (m_scene && !m_scene->views().isEmpty())
                m_scene->views().first()->viewport()->setCursor(cur);
        }
        return;
    }

    if (m_anchorDrag->active()) {
        m_anchorDrag->update(pos);
        return;
    }

    switch (m_state) {
    case SelectState::Dragging:
        m_dragCtl->update(pos);
        break;
    case SelectState::Marquee:
        if (m_marqueeGesture) m_marqueeGesture->update(pos);
        break;
    default: break;
    }
}

void ToolSelect::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    hideHoverEndpointRing();
    if (event->button() != Qt::LeftButton) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->release(pos);
        return;
    }
    if (m_connectGesture && m_connectGesture->active()
        && m_state == SelectState::Connecting) {
        m_connectGesture->release(pos);
        return;
    }

    if (m_anchorDrag->active()) {
        m_anchorDrag->end();
        return;
    }

    // press 未触发拖动 = 单击
    if (m_hoverCtl.pending()) {
        const QUuid pendingBlock = m_hoverCtl.blockId();
        const bool wasSelected = m_hoverCtl.wasSelected();
        m_hoverCtl.cancelPending();
        if (wasSelected && !pendingBlock.isNull())
            toggleBlock(pendingBlock);
        return;
    }

    switch (m_state) {
    case SelectState::Marquee: {
        hideHoverEndpointRing();
        if (m_marqueeGesture) {
            const QSet<QUuid> boxed = m_marqueeGesture->end(pos);
            const double moveDist = pos.distanceTo(m_marqueeGesture->startPos());
            double zoom = m_scene->currentZoom();
            if (zoom < 1e-9) zoom = 1.0;
            // 若位移极小且没有框到任何东西，视为单纯单击空白处 -> 取消全部选中
            if (moveDist < 4.0 / zoom && boxed.isEmpty()) {
                clearSelectionAndIdle();
            } else {
                // 框选加选与反向减选 (XOR)
                for (const QUuid& bid : boxed) {
                    if (m_selection.contains(bid))
                        m_selection.remove(bid);
                    else
                        m_selection.insert(bid);
                }
                syncSelectionVisual();
                setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
                notifyEditTarget();
            }
        }
        break;
    }
    case SelectState::Dragging: {
        hideHoverEndpointRing();
        const double zoom = m_scene->currentZoom();
        if (m_dragCtl->end(pos, zoom))
            return;
        m_scene->refreshAllBlockItems();
        // 拖动提交后结束状态 (清空选择集回到 Idle)
        m_selectionConfirmed = false;
        clearSelectionAndIdle();
        break;
    }
    default: break;
    }
}

void ToolSelect::keyPress(QKeyEvent* event)
{
    if (m_connectGesture && m_connectGesture->active()
        && m_connectGesture->keyPress(event)) {
        event->accept();
        return;
    }
    if (m_state == SelectState::CopyDragging) {
        if (event->key() == Qt::Key_Escape) {
            if (m_copyDrag) m_copyDrag->cancel();
            event->accept();
        }
        return;
    }

    if (event->key() == Qt::Key_Escape && m_overlapCtl && m_overlapCtl->hasBattery()) {
        m_overlapCtl->hideBattery();
        event->accept();
        return;
    } else if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Alt) {
        if (m_overlapCtl && m_overlapCtl->handleSpaceOrAltKey()) {
            event->accept();
            return;
        }
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!m_selectedPlacedPointId.isNull() && !m_selectedPlacedBlockId.isNull()) {
            const QUuid blkId = m_selectedPlacedBlockId;
            const QUuid ptId = m_selectedPlacedPointId;
            clearPlacedPointSelection();
            QUndoStack* stack = m_undoStack ? m_undoStack : (m_paramDoc ? m_paramDoc->undoStack() : nullptr);
            if (stack) {
                stack->push(new cad::cmd::RemovePlacedPointCommand(m_paramDoc, blkId, ptId));
            }
            if (m_scene) m_scene->refreshAllBlockItems();
            event->accept();
            return;
        }
        deleteSelectedBlocks();
    } else if (event->key() == Qt::Key_W || event->key() == Qt::Key_B) {
        if (m_overlapCtl) {
            double zoom = m_scene ? m_scene->currentZoom() : 1.0;
            if (zoom < 1e-9) zoom = 1.0;
            m_overlapCtl->handleWOrBKey(m_lastCursorPos, zoom);
        }
        event->accept();
        return;
    } else if (event->key() == Qt::Key_D
               && !(m_connectGesture && m_connectGesture->active())) {
        quickDetachSelection();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        if (!m_selectedPlacedPointId.isNull()) {
            clearPlacedPointSelection();
            event->accept();
            return;
        }
        if (m_state == SelectState::Dragging) {
            m_dragCtl->cancelDrag();
            m_scene->refreshAllBlockItems();
            setState(SelectState::Selecting);
        } else {
            if (m_marqueeGesture) m_marqueeGesture->cancel();
            clearSelectionAndIdle();
        }
        event->accept();
    }
}

} // namespace cad::tools
