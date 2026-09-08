#include "MainWindow.h"

#include <QAction>
#include <QMap>
#include <QSet>

#include "ElaMenu.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/InputDispatcher.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/EndpointCommands.h"
#include "document/commands/VariableCommands.h"
#include "geometry/Units.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "ui/QuickAuxDialog.h"

void MainWindow::onSegmentContextMenu(const cad::canvas::SegmentHit& hit)
{
    const auto* blk = m_paramDoc->findBlock(hit.blockId);
    const auto* seg = blk ? blk->findSegment(hit.segmentId) : nullptr;
    if (!blk || !seg) return;

    ElaMenu menu(this);

    // --- 拆开连接操作项 (全层穿透扫描) ---
    const auto userPos = cad::geo::Coord::toUser(hit.scenePos);
    const double zoom = m_canvasView ? m_canvasView->zoomFactor() : 1.0;
    const double tol = 16.0 / (zoom > 1e-4 ? zoom : 1.0);

    QSet<QUuid> nearBlockIds;
    nearBlockIds.insert(hit.blockId);
    for (const auto& b : m_paramDoc->blocks()) {
        for (const auto& s : b.segments) {
            const auto* sp = b.findPoint(s.startPointId);
            const auto* ep = b.findPoint(s.endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
            const auto w1 = b.transform.toWorld(sp->resolvedPos);
            const auto w2 = b.transform.toWorld(ep->resolvedPos);
            if (cad::geo::Vec2::distanceToSegment(userPos, w1, w2) <= tol) {
                nearBlockIds.insert(b.id);
                break;
            }
        }
    }

    struct DetachOption {
        QUuid attId;
        bool isAuxMount;
        QString label;
    };
    QList<DetachOption> detachOptions;
    QSet<QUuid> seenAtts;

    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isPin || att.angleOnly) continue;
        if (!nearBlockIds.contains(att.fromBlockId) && !nearBlockIds.contains(att.toBlockId))
            continue;
        if (seenAtts.contains(att.id)) continue;

        const auto* fromBlk = m_paramDoc->findBlock(att.fromBlockId);
        const auto* toBlk = m_paramDoc->findBlock(att.toBlockId);
        if (!fromBlk || !toBlk) continue;

        const auto* toPt = toBlk->findPoint(att.toPointId);
        const bool isAux = (toPt && toPt->isAuxiliary);

        QString fromName = fromBlk->name;
        const QUuid fromSegId = fromBlk->exitSegmentAtPoint(att.fromPointId);
        if (const auto* fs = fromBlk->findSegment(fromSegId)) {
            fromName = cad::param::Serial::tag(fs->serial);
            if (!fs->name.isEmpty()) fromName += QStringLiteral("·") + fs->name;
        }

        QString toName = toBlk->name;
        if (isAux && toPt) {
            toName = cad::param::Serial::tag(toPt->serial);
            if (!toPt->name.isEmpty()) toName += QStringLiteral("·") + toPt->name;
        } else if (const auto* ts = toBlk->findSegment(att.toSegmentId)) {
            toName = cad::param::Serial::tag(ts->serial);
            if (!ts->name.isEmpty()) toName += QStringLiteral("·") + ts->name;
        }

        QString menuText;
        if (isAux) {
            menuText = QStringLiteral("⚡ 拆开挂载连接: %1 (挂于 %2)").arg(fromName, toName);
        } else {
            menuText = QStringLiteral("⚡ 拆开连接: %1 ↔ %2").arg(fromName, toName);
        }

        seenAtts.insert(att.id);
        detachOptions.append({att.id, isAux, menuText});
    }

    QMap<QAction*, DetachOption> detachActionMap;
    for (const auto& opt : detachOptions) {
        QAction* act = menu.addAction(opt.label);
        detachActionMap.insert(act, opt);
    }
    if (!detachOptions.isEmpty()) {
        menu.addSeparator();
    }

    // Check if already published.
    const bool alreadyPublished =
        m_paramDoc->findLinkedBySource(hit.blockId, hit.segmentId) != nullptr;
    QAction* publishAction = menu.addAction(QStringLiteral("发布长度参数"));
    publishAction->setEnabled(!alreadyPublished);
    if (alreadyPublished)
        publishAction->setText(QStringLiteral("已发布长度参数"));

    // --- 添加辅助点 ---
    QAction* auxPointAction = menu.addAction(QStringLiteral("添加辅助点"));

    // --- 烘焙到操作层 (measure line on the aux layer only) ---
    if (m_paramDoc->measurementsView().measureByOwner(hit.blockId)
        && m_paramDoc->layersView().isAuxLayer(blk->layer)) {
        QMenu* bakeMenu = menu.addMenu(QStringLiteral("烘焙到操作层"));
        const auto& layerList = m_paramDoc->layers();
        for (int i = 0; i < static_cast<int>(layerList.size()); ++i) {
            if (m_paramDoc->layersView().isAuxLayer(layerList[static_cast<size_t>(i)].id))
                continue;
            QAction* act = bakeMenu->addAction(layerList[static_cast<size_t>(i)].name);
            act->setProperty("bakeTargetLayer",
                             layerList[static_cast<size_t>(i)].id.toString());
        }
    }

    QAction* chosen = menu.exec(hit.globalPos);
    if (!chosen) return;

    if (detachActionMap.contains(chosen)) {
        const auto& opt = detachActionMap[chosen];
        if (opt.isAuxMount) {
            m_paramDoc->undoStack()->push(
                new cad::cmd::RemoveAttachmentCommand(m_paramDoc, opt.attId));
            m_canvasScene->showToast(QStringLiteral("已彻底释放挂载连接"));
        } else {
            m_paramDoc->undoStack()->push(
                new cad::cmd::SetAttachmentAngleOnlyCommand(m_paramDoc, opt.attId, true));
            m_canvasScene->showToast(QStringLiteral("已拆开连接（保留角度）"));
        }
        m_canvasScene->refreshAllBlockItems();
        return;
    }

    if (chosen == publishAction && !alreadyPublished) {
        cad::param::LinkedVariable lv =
            cad::param::LinkedVariable::fromSegment(*blk, *seg);
        m_paramDoc->undoStack()->push(
            new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    } else if (chosen == auxPointAction) {
        const auto* pSp = blk->findPoint(seg->startPointId);
        const auto* pEp = blk->findPoint(seg->endPointId);
        if (!pSp || !pEp) return;

        cad::param::ParamPoint pt;
        pt.constraint = cad::param::PointConstraint::Interpolated;
        pt.hostSegmentId = seg->id;
        pt.isAuxiliary = true;
        pt.visible = true;
        pt.showName = false;
        pt.interpPercent = hit.paramT;
        pt.serial = m_paramDoc->newPointSerial();

        cad::ui::QuickAuxDialog* dlg = new cad::ui::QuickAuxDialog(pt, pSp, pEp, this);
        QObject::connect(dlg, &QDialog::finished, dlg,
            [dlg, this, hit = hit](int result) {
                if (result == QDialog::Accepted) {
                    m_paramDoc->undoStack()->push(new cad::cmd::AddAuxPointCommand(
                        m_paramDoc, hit.blockId, hit.segmentId, dlg->point()));
                }
                dlg->deleteLater();
            });
    } else if (chosen->property("bakeTargetLayer").isValid()) {
        const QUuid targetLayer =
            QUuid::fromString(chosen->property("bakeTargetLayer").toString());
        auto* bakeCmd = new cad::cmd::BakeMeasureCopyCommand(
            m_paramDoc, hit.blockId, targetLayer);
        if (!bakeCmd->isValid()) {
            delete bakeCmd;
            return;
        }
        const QUuid bakedId = bakeCmd->newBlockId();
        m_paramDoc->undoStack()->push(bakeCmd);
        m_paramDoc->setActiveLayer(targetLayer);
        m_toolManager->switchTool(cad::tools::ToolType::Select);
        if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                m_toolManager->activeTool()))
            ts->selectBlocksExternally(QList<QUuid>{bakedId});
        m_canvasScene->showToast(
            QStringLiteral("已烘焙到操作层「%1」").arg(chosen->text()));
    }
}
