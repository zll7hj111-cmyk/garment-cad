#include "ToolSelect.h"

#include <QList>
#include <QUuid>
#include <QString>
#include <QUndoStack>
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "parametric/LayerRegistry.h"
#include "document/commands/ComponentCommands.h"
#include "document/commands/LayerCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/BlockLifecycleCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/DeleteImpactConfirm.h"
#include "ui/LinePropertyDialog.h"
#include "ui/PlacedPointDialog.h"
#include "ConnectGesture.h"
#include "OverlapDisambiguationController.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "parametric/Serial.h"
#include "tools/InteractionTolerances.h"  // kConnectGrabRadiusPx

#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QAction>
#include <QCursor>
#include <limits>
#include "geometry/Epsilon.h"
#include "document/CommandTexts.h"
#include "ui/UiStrings.h"

namespace cad::tools {
// ═══════════════════════════════════════════════════════════════════════════════
// Component / layer / delete / quick-detach
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::createComponentFromSelection()
{
    if (!m_paramDoc || m_selection.size() < 2) return;

    QList<QUuid> members;
    for (const auto& b : m_paramDoc->blocks())
        if (m_selection.contains(b.id))
            members.append(b.id);
    if (members.size() < 2) return;

    const QString name = cad::ui::str::kComponentFmt
        .arg(m_paramDoc->components().size() + 1);
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::MakeComponentCommand(m_paramDoc, members, name));
    else {
        cad::param::Component c;
        c.name = name;
        c.memberBlockIds.assign(members.begin(), members.end());
        m_paramDoc->addComponent(c);
    }

    showToast(QStringLiteral("已创建组件（%1 条线段）——拖任一成员整组移动").arg(members.size()));
    clearSelectionAndIdle();
}

void ToolSelect::moveSelectionToLayer(const QUuid& targetLayerId)
{
    if (!m_paramDoc || m_selection.isEmpty() || targetLayerId.isNull()) return;

    QString targetName;
    for (const auto& l : m_paramDoc->layersView().all()) {
        if (l.id == targetLayerId) {
            targetName = l.name;
            break;
        }
    }
    if (targetName.isEmpty())
        targetName = cad::cmd::texts::kOtherLayers;

    const QList<QUuid> blockIds = m_selection.values();
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::MoveBlocksToLayerCommand(
            m_paramDoc, blockIds, targetLayerId));
    } else {
        for (const auto& id : blockIds) {
            if (auto* b = m_paramDoc->findBlock(id)) {
                b->layer = targetLayerId;
            }
        }
        emit m_paramDoc->layersChanged();
    }

    showToast(QStringLiteral("已将 %1 条线段移动到「%2」")
                  .arg(blockIds.size())
                  .arg(targetName));
    clearSelectionAndIdle();
}

void ToolSelect::deleteSelectedBlocks()
{
    if (!m_scene || !m_paramDoc || m_selection.isEmpty()) return;

    const QList<QUuid> toRemove = m_selection.values();

    QWidget* const parent = m_scene->views().isEmpty()
        ? nullptr : m_scene->views().first();
    if (!cad::doc::confirmDeleteImpact(parent, m_paramDoc, toRemove))
        return;

    if (m_undoStack) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe5\x88\xa0\xe9\x99\xa4 %1 \xe6\x9d\xa1\xe7\xba\xbf\xe6\xae\xb5").arg(toRemove.size()));
        for (const QUuid& id : toRemove)
            m_undoStack->push(new cad::cmd::RemoveBlockCommand(m_paramDoc, id));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& id : toRemove)
            m_paramDoc->removeBlock(id);
    }

    m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}



void ToolSelect::showContextMenu(QGraphicsSceneMouseEvent* event)
{
    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());
    const auto cands = m_overlapCtl->collect(pos);
    QMenu menu;
    QAction* actCancel = nullptr;
    QAction* actComponent = nullptr;
    QAction* actDetachAux = nullptr;
    QMenu* layerMenu = nullptr;
    QList<std::pair<QAction*, QUuid>> layerActions;
    bool layerMoved = false;
    QMenu* overlapMenu = nullptr;

    // 检查右键点击位置是否有挂载了连接的辅助点
    QList<QUuid> auxAttIds;
    QString auxMountDesc;
    if (m_connectGesture && m_paramDoc) {
        const auto ptCands = m_connectGesture->hitPointCandidates(pos);
        for (const auto& c : ptCands) {
            const auto* blk = m_paramDoc->findBlock(c.blockId);
            if (!blk) continue;
            const auto* pt = blk->findPoint(c.pointId);
            if (!pt || !pt->isAuxiliary) continue;

            for (const auto& att : m_paramDoc->attachments()) {
                if (att.isPin || att.angleOnly) continue;
                if (att.toBlockId == c.blockId && att.toPointId == c.pointId) {
                    auxAttIds.append(att.id);
                    if (const auto* fb = m_paramDoc->findBlock(att.fromBlockId)) {
                        const QUuid fs = fb->exitSegmentAtPoint(att.fromPointId);
                        if (const auto* fsg = fb->findSegment(fs)) {
                            QString t = cad::param::Serial::tag(fsg->serial);
                            if (!fsg->name.isEmpty()) t += QStringLiteral("·") + fsg->name;
                            const QString desc = QString::fromUtf8("挂载 %1").arg(t);
                            if (auxMountDesc.isEmpty())
                                auxMountDesc = desc;
                            else
                                auxMountDesc += QStringLiteral(", ") + desc;
                        }
                    }
                } else if (att.fromBlockId == c.blockId && att.fromPointId == c.pointId) {
                    auxAttIds.append(att.id);
                    if (const auto* tb = m_paramDoc->findBlock(att.toBlockId)) {
                        QUuid tsId = att.toSegmentId;
                        if (tsId.isNull()) tsId = tb->exitSegmentAtPoint(att.toPointId);
                        if (const auto* tsg = tb->findSegment(tsId)) {
                            QString t = cad::param::Serial::tag(tsg->serial);
                            if (!tsg->name.isEmpty()) t += QStringLiteral("·") + tsg->name;
                            const QString desc = QString::fromUtf8("跟随 %1").arg(t);
                            if (auxMountDesc.isEmpty())
                                auxMountDesc = desc;
                            else
                                auxMountDesc += QStringLiteral(", ") + desc;
                        }
                    }
                }
            }
            if (!auxAttIds.isEmpty()) break;
        }
    }

    if (!auxAttIds.isEmpty()) {
        actDetachAux = menu.addAction(QString::fromUtf8("拆开连接 (%1)").arg(auxMountDesc));
        menu.addSeparator();
    }

    QAction* actConfirmMove = nullptr;
    if (!m_selection.isEmpty()) {
        actConfirmMove = menu.addAction(QString::fromUtf8("✔ 确定选中移动"));
        menu.addSeparator();

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
            const cad::param::Layer* auxLayer = m_paramDoc->layersView().byId(auxId);
            if (!auxId.isNull() && auxId != activeLayerId) {
                if (!layerActions.isEmpty())
                    layerMenu->addSeparator();
                // 2026-12 审计 P1-1: 菜单标签取模型里的层名 — 用户重命名辅助层后
                // 与图层面板一致 (此前写死 "辅助层")。
                const QString auxLabel = auxLayer
                    ? auxLayer->name
                    : cad::param::LayerRegistry::kDefaultAuxLayerName;
                auto* act = layerMenu->addAction(auxLabel);
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
    if (overlapMenu == nullptr && actCancel == nullptr && actComponent == nullptr && layerMenu == nullptr && actDetachAux == nullptr) {
        if (m_selection.isEmpty() && hitBlock(pos).isNull())
            requestToolSwitch(ToolType::SmartPen);
        return;
    }
    QAction* chosen = menu.exec(QCursor::pos());
    if (chosen && chosen == actDetachAux) {
        deactivateOverlapContext();
        if (m_undoStack) {
            m_undoStack->beginMacro(QStringLiteral("拆开辅助点连接"));
            for (const QUuid& id : auxAttIds) {
                m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(m_paramDoc, id));
            }
            m_undoStack->endMacro();
        } else {
            for (const auto& id : auxAttIds)
                m_paramDoc->removeAttachment(id);
        }
        showToast(QStringLiteral("已彻底拆开辅助点上的连接"));
        m_scene->refreshAllBlockItems();
        return;
    }
    if (chosen && chosen == actConfirmMove) {
        deactivateOverlapContext();
        setSelectionConfirmed(true);
        showToast(QString::fromUtf8("已确定选中 %1 条线段，长按拖动即可整体移动").arg(m_selection.size()));
        return;
    }
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
}

void ToolSelect::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 clickPos(up.x(), up.y());
    double zoom = m_scene->safeZoom();
    if (zoom < cad::geo::kGeomEps) zoom = 1.0;

    // 1. Check if a placed point was double-clicked (via SnapEngine with generous radius)
    SnapEngine snapEngine;
    auto snap = snapEngine.findSnap(clickPos, m_paramDoc, zoom, 12.0, {}, nullptr, true);
    if (snap) {
        if (auto* blk = m_paramDoc->findBlock(snap->blockId)) {
            if (auto* pt = blk->findPoint(snap->pointId)) {
                if (pt->isPlaced) {
                    QWidget* parentWidget = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
                    auto* dlg = new cad::ui::PlacedPointDialog(snap->blockId, snap->pointId,
                                                               m_paramDoc, m_scene, parentWidget);
                    dlg->show();
                    return;
                }
            }
        }
    }

    // 2. Fall back to segment double-click
    const QUuid blockId = hitBlock(clickPos);
    if (blockId.isNull()) return;
    cad::param::Block* block = m_paramDoc->findBlock(blockId);
    if (!block || block->segments.empty()) return;
    // 拾取容差 = 画布统一的悬停半径 token（CAN-P0-5：不得再复制 8px 字面量）。
    const double tolerance = m_scene->style()->hoverRadiusPx() / zoom;

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

// ═══════════════════════════════════════════════════════════════════════════════
// 端点悬停指示器与右键确认移动
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::updateHoverEndpointRing(const cad::geo::Vec2& pos)
{
    if (!m_scene || !m_paramDoc || !overlays()) {
        hideHoverEndpointRing();
        return;
    }
    double zoom = m_scene->safeZoom();
    if (zoom < cad::geo::kGeomEps) zoom = 1.0;
    const double worldR = kConnectGrabRadiusPx / zoom;  // 端点悬停抓取半径
    cad::geo::Vec2 ptPos;
    if (m_hoverCtl.findEndpointNear(m_paramDoc, pos, worldR, &ptPos)) {
        overlays()->showEndpointHover(ptPos);
    } else {
        hideHoverEndpointRing();
    }
}

void ToolSelect::hideHoverEndpointRing()
{
    if (overlays()) {
        overlays()->clear(cad::canvas::OverlayTier::Hover);
    }
}

void ToolSelect::setSelectionConfirmed(bool confirmed)
{
    m_selectionConfirmed = confirmed;
    if (confirmed) {
        setState(SelectState::ConfirmedReady);
    } else if (m_state == SelectState::ConfirmedReady) {
        setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    }
    syncSelectionVisual();
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

