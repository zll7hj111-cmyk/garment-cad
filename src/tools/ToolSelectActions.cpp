#include "ToolSelect.h"

#include <QList>
#include <QUuid>
#include <QString>
#include <QUndoStack>
#include <QWidget>
#include <QGraphicsView>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "document/commands/ComponentCommands.h"
#include "document/commands/LayerCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/DeleteImpactConfirm.h"

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

    const QString name = QStringLiteral("组件 %1")
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
        targetName = QStringLiteral("其他图层");

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
            m_undoStack->push(new cad::cmd::DeleteBlockCommand(m_paramDoc, id));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& id : toRemove)
            m_paramDoc->removeBlock(id);
    }

    m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}

void ToolSelect::quickDetachSelection()
{
    if (!m_paramDoc || m_selection.isEmpty()) return;

    QList<QUuid> toDetach;
    for (const QUuid& blockId : m_selection) {
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.fromBlockId != blockId || att.isPin) continue;
            if (!att.angleOnly) toDetach.append(att.id);
            break;
        }
    }

    if (toDetach.isEmpty()) {
        showToast(QString::fromUtf8("选中线没有可拆开的连接"));
        return;
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(QStringLiteral("拆开 %1 个连接").arg(toDetach.size()));
        for (const QUuid& id : toDetach)
            m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                m_paramDoc, id, /*angleOnly=*/true));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& id : toDetach)
            m_paramDoc->setAttachmentAngleOnly(id, true);
    }

    m_scene->refreshAllBlockItems();
}

} // namespace cad::tools
