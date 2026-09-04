#include "ToolSelect.h"
#include "ToolManager.h"

#include "SelectDragController.h"
#include "CurveAnchorDragSession.h"
#include "OverlapDisambiguationController.h"
#include "SelectHoverFeedback.h"

#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMenu>
#include <QUndoStack>
#include <QPen>
#include <QFontMetrics>
#include <QLineEdit>
#include <QInputDialog>
#include <QWidget>
#include <QEvent>

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
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "parametric/FollowerAngle.h"
#include "ui/LinePropertyDialog.h"
#include "ConnectGesture.h"
#include "CopyDragController.h"
#include "MarqueeGesture.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/LayerCommands.h"
#include "ui/DeleteImpactConfirm.h"

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
    d.hintText = modeIndicatorFor(SelectionMode::Single, -1, 0)
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
    m_selectionMode = SelectionMode::Single;

    QUndoStack* const undo = m_paramDoc ? m_paramDoc->undoStack() : nullptr;

    // ── Extracted gesture controllers (阶段 3 拆分) ──
    m_dragCtl = std::make_unique<SelectDragController>(
        m_paramDoc, undo,
        [this](SelectState s) { setState(s); });
    m_anchorDrag = std::make_unique<CurveAnchorDragSession>(m_paramDoc, undo);
    m_overlapCtl = std::make_unique<OverlapDisambiguationController>(
        m_scene, m_paramDoc,
        [this](const QUuid& bid, const QUuid& sid) {
            // 把候选写回选择集 + 编辑目标 (与单击选中语义一致).
            m_selection = {bid};
            m_lastHitSegmentId = sid;
            syncSelectionVisual();
            setState(SelectState::Selecting);
            notifyEditTarget();
        },
        [this]() { refreshModeIndicator(); });

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
            // 2026-09 取消确认基准: 选中即就绪 (Confirmed 由 setState 归一化).
            setState(m_selection.isEmpty() ? SelectState::Idle
                                           : SelectState::Selecting);
        },
        [this]() { clearSelectionAndIdle(); });
    m_marqueeGesture = std::make_unique<MarqueeGesture>(m_scene, m_paramDoc);
    refreshModeIndicator();
}

void ToolSelect::onDeactivate()
{
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

    m_selection.clear();
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
    if (s == SelectState::Confirmed) s = SelectState::Selecting;
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

void ToolSelect::clearSelectionAndIdle()
{
    if (m_overlapCtl) m_overlapCtl->deactivate();
    m_selection.clear();
    syncSelectionVisual();
    setState(SelectState::Idle);
    notifyEditTarget();
}

void ToolSelect::clearSelectionOnLayerChange()
{
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
    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    notifyEditTarget();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection mode (W toggle: 多选 ↔ 单选)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleSelectionMode()
{
    const bool switchingToMulti = (m_selectionMode == SelectionMode::Single);
    m_selectionMode = switchingToMulti ? SelectionMode::Multi
                                       : SelectionMode::Single;
    if (!switchingToMulti)
        clearSelectionAndIdle();
    announceModeChange();
}

ModeIndicator ToolSelect::modeIndicator() const
{
    return modeIndicatorFor(m_selectionMode, m_overlapCtl->index(),
                            m_overlapCtl->candidates().size());
}

ModeIndicator ToolSelect::modeIndicatorFor(SelectionMode mode, int overlapIndex,
                                           int overlapCount)
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
    if (mode == SelectionMode::Multi) {
        mi.modeName = QString::fromUtf8("多选");
        mi.detail   = QString::fromUtf8("点击加减选 | 框选 | 已选按住拖动 | 双击编辑 | Del删除 | 空白右键→智能笔");
        mi.wAction  = QString::fromUtf8("W 切单选");
        mi.toast    = QString::fromUtf8("多选模式：点击加减选 | 框选 | 已选按住拖动");
    } else {
        mi.modeName = QString::fromUtf8("单选");
        mi.detail   = QString::fromUtf8("点击选中 | 按住拖动 | 端点按住连接 | 双击编辑 | Del删除 | 空白右键→智能笔");
        mi.wAction  = QString::fromUtf8("W 切多选");
        mi.toast    = QString::fromUtf8("单选模式：点击选中 | 按住拖动 | 端点按住连接");
        mi.isDefault = true;
    }
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

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

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
        const auto cands = m_overlapCtl->collect(pos);
        QMenu menu;
        QAction* actCancel = nullptr;
        QAction* actComponent = nullptr;
        QMenu* layerMenu = nullptr;
        QList<std::pair<QAction*, QUuid>> layerActions;
        bool layerMoved = false;
        QMenu* overlapMenu = nullptr;

        if (!m_selection.isEmpty()) {
            if (m_selection.size() >= 2)
                actComponent = menu.addAction(QString::fromUtf8("创建构件"));

            layerMenu = menu.addMenu(QString::fromUtf8("移动到图层"));
            const QUuid activeLayerId = m_paramDoc ? m_paramDoc->activeLayer() : QUuid();
            if (m_paramDoc) {
                for (const auto& layer : m_paramDoc->layersView().all()) {
                    if (m_paramDoc->layersView().isAuxLayer(layer.id))
                        continue;
                    if (layer.id == activeLayerId)
                        continue;
                    auto* act = layerMenu->addAction(layer.name);
                    const QUuid targetId = layer.id;
                    QObject::connect(act, &QAction::triggered, [this, targetId, &layerMoved]() {
                        if (!layerMoved) {
                            layerMoved = true;
                            deactivateOverlapContext();
                            moveSelectionToLayer(targetId);
                        }
                    });
                    layerActions.append({act, layer.id});
                }

                const QUuid auxId = m_paramDoc->layersView().auxLayerId();
                if (!auxId.isNull() && auxId != activeLayerId) {
                    if (!layerActions.isEmpty())
                        layerMenu->addSeparator();
                    auto* act = layerMenu->addAction(QString::fromUtf8("辅助层"));
                    QObject::connect(act, &QAction::triggered, [this, auxId, &layerMoved]() {
                        if (!layerMoved) {
                            layerMoved = true;
                            deactivateOverlapContext();
                            moveSelectionToLayer(auxId);
                        }
                    });
                    layerActions.append({act, auxId});
                }
            }
            if (layerActions.isEmpty()) {
                layerMenu->setEnabled(false);
            }

            actCancel = menu.addAction(QString::fromUtf8("取消选择"));
        }
        if (cands.size() >= 2) {
            overlapMenu = menu.addMenu(QString::fromUtf8("重叠候选 (%1 条)").arg(cands.size()));
            for (int i = 0; i < cands.size(); ++i) {
                const auto& c = cands[i];
                QString label = QString::fromUtf8("%1 %2").arg(c.roleText, c.name);
                if (!c.layerName.isEmpty())
                    label += QString::fromUtf8(" · %1").arg(c.layerName);
                if (c.lengthMm > 0.0)
                    label += QString::fromUtf8(" · %1").arg(
                        cad::geo::Units::formatLength(c.lengthMm));
                QAction* act = overlapMenu->addAction(label);
                act->setProperty("overlapPick", i);
            }
        }
        if (overlapMenu == nullptr && actCancel == nullptr && actComponent == nullptr && layerMenu == nullptr) {
            if (m_selection.isEmpty() && hitBlock(pos).isNull())
                requestToolSwitch(ToolType::SmartPen);
            return;
        }
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == actCancel) {
            deactivateOverlapContext();
            clearSelectionAndIdle();
        } else if (chosen && chosen == actComponent && actComponent) {
            deactivateOverlapContext();
            createComponentFromSelection();
        } else if (chosen && chosen->property("overlapPick").isValid()) {
            pickOverlapCandidate(chosen->property("overlapPick").toInt());
            deactivateOverlapContext();
        } else if (chosen && !layerMoved) {
            for (const auto& [act, targetLayerId] : layerActions) {
                if (chosen == act) {
                    deactivateOverlapContext();
                    moveSelectionToLayer(targetLayerId);
                    break;
                }
            }
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

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

    // ── 线身 press: 单选/多选分派 + 待定拖动 ──
    switch (m_state) {
    case SelectState::Idle:
    case SelectState::Selecting: {
        const QUuid blockHit = hitBlock(pos);
        if (m_selectionMode == SelectionMode::Single) {
            if (!blockHit.isNull()) {
                const bool wasSelected = m_selection.contains(blockHit);
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = {blockHit};
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
                m_hoverCtl.beginPending(pos, blockHit, wasSelected);
                const auto cands = m_overlapCtl->collect(pos);
                if (cands.size() >= 2)
                    m_overlapCtl->activate(cands, blockHit, pos);
                else
                    m_overlapCtl->deactivate();
            } else if (!m_selection.isEmpty()) {
                deactivateOverlapContext();
                clearSelectionAndIdle();
            }
            return;
        }
        if (!blockHit.isNull()) {
            deactivateOverlapContext();
            const bool wasSelected = m_selection.contains(blockHit);
            if (wasSelected) {
                m_hoverCtl.beginPending(pos, blockHit, true);
            } else {
                toggleBlock(blockHit);
                m_hoverCtl.beginPending(pos, blockHit, false);
            }
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
            m_hoverCtl.cancelPending();
            if (m_overlapCtl) m_overlapCtl->hideHint();  // 拖动期间隐藏重叠 HUD
            m_dragCtl->setZoom(zoom);
            m_dragCtl->begin(startPos, m_selection);
        }
        return;
    }

    // 无按钮悬停: 光标 + hover 目标 + 重叠提示
    if (event->buttons() == Qt::NoButton
        && !m_anchorDrag->active()
        && m_state != SelectState::Dragging
        && m_state != SelectState::Marquee) {
        const QPointF scenePt = cad::geo::Coord::toScene(pos.x, pos.y);
        const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
        const QUuid blockHit = hits.empty() ? QUuid() : hits.front().blockId;
        const bool ctrlHeld = (QGuiApplication::keyboardModifiers() & Qt::ControlModifier);
        const bool isSel = m_selection.contains(blockHit);

        const double zoom = m_scene->currentZoom();
        // 悬停十字只对已选块生效 (提示可连接); 未选块即使端点悬停也只用抓手.
        const Qt::CursorShape cur = m_hoverCtl.cursorShapeFor(
            m_paramDoc, isSel ? blockHit : QUuid(), pos, zoom, ctrlHeld);
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

        // 重叠提示: 无按钮悬停时跟随 (集群 ≥2 条才显示; 同值短路).
        QList<OverlapDisambiguationController::Candidate> hoverCands;
        for (const auto& h : hits) {
            if (const auto* blk = m_paramDoc->blocksView().byId(h.blockId))
                hoverCands.append(m_overlapCtl->makeCandidate(*blk, h.segmentId));
        }
        m_overlapCtl->refreshHint(pos, &hoverCands);
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
        if (m_selectionMode == SelectionMode::Multi
            && wasSelected && !pendingBlock.isNull())
            toggleBlock(pendingBlock);
        return;
    }

    switch (m_state) {
    case SelectState::Marquee: {
        if (m_marqueeGesture)
            m_selection = m_marqueeGesture->end(pos);
        syncSelectionVisual();
        setState(m_selection.isEmpty() ? SelectState::Idle
                                       : SelectState::Selecting);
        break;
    }
    case SelectState::Dragging: {
        // 纯单击 (位移≈0): end() 返回 true 表示已收尾为单击, 选择集保留.
        const double zoom = m_scene->currentZoom();
        if (m_dragCtl->end(pos, zoom))
            return;
        m_scene->refreshAllBlockItems();
        clearSelectionAndIdle();
        break;
    }
    default: break;
    }
}

void ToolSelect::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 clickPos(up.x(), up.y());

    const QUuid blockId = hitBlock(clickPos);
    if (blockId.isNull()) return;
    cad::param::Block* block = m_paramDoc->findBlock(blockId);
    if (!block || block->segments.empty()) return;
    double zoom = m_scene->currentZoom();
    if (zoom < 1e-9) zoom = 1.0;
    constexpr double kTolerancePx = 8.0;
    const double tolerance = kTolerancePx / zoom;

    QUuid bestSegId;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& seg : block->segments) {
        const auto* sp = block->findPoint(seg.startPointId);
        const auto* ep = block->findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

        double d;
        if (seg.isCurve()) {
            const auto spans = block->spansForSegment(seg, /*skipUnresolvedPassPoints=*/true);
            if (spans.empty()) continue;
            auto proj = cad::geo::projectPointOnCurve(
                block->transform.toLocal(clickPos), spans);
            d = proj.valid ? proj.distance : std::numeric_limits<double>::max();
        } else {
            const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            d = cad::geo::Vec2::distanceToSegment(clickPos, w1, w2);
        }
        if (d < bestDist) { bestDist = d; bestSegId = seg.id; }
    }
    if (bestSegId.isNull() || bestDist > tolerance) return;

    QWidget* parentWidget = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new cad::ui::LinePropertyDialog(blockId, bestSegId, m_paramDoc,
                                       m_scene, parentWidget);
    dlg->show();
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

    if (event->key() == Qt::Key_Escape && m_overlapCtl->index() >= 0) {
        m_overlapCtl->deactivate();
        event->accept();
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedBlocks();
        event->accept();
    } else if (event->key() == Qt::Key_W) {
        if (m_overlapCtl->index() >= 0)
            m_overlapCtl->cycle();
        else
            toggleSelectionMode();
        event->accept();
    } else if (event->key() == Qt::Key_D
               && !(m_connectGesture && m_connectGesture->active())) {
        quickDetachSelection();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
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

// ═══════════════════════════════════════════════════════════════════════════════
// Overlap disambiguation (tool-facing API, delegates to OverlapDisambiguationController)
// ═══════════════════════════════════════════════════════════════════════════════

const QList<ToolSelect::OverlapCandidate>& ToolSelect::overlapCandidates() const
{
    static const QList<ToolSelect::OverlapCandidate> empty;
    if (!m_overlapCtl) return empty;
    return m_overlapCtl->candidates();
}

int ToolSelect::overlapIndex() const
{
    return m_overlapCtl ? m_overlapCtl->index() : -1;
}

QString ToolSelect::overlapHintText() const
{
    return m_overlapCtl ? m_overlapCtl->hintText() : QString();
}

void ToolSelect::pickOverlapCandidate(int index)
{
    if (m_overlapCtl) m_overlapCtl->pick(index);
}

void ToolSelect::deactivateOverlapContext()
{
    if (m_overlapCtl) m_overlapCtl->deactivate();
}

} // namespace cad::tools
