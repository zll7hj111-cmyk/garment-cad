#include "GroupPanel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QKeyEvent>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/GroupModel.h"
#include "tools/LinePropertyDialog.h"
#include "ui/SerialDelegate.h"

GroupPanel::GroupPanel(cad::param::ParamDocument* paramDoc, CanvasScene* scene,
                       QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
    , m_scene(scene)
{
    setupUi();

    // Group registry changes (attach/detach/add/remove, rename) rebuild the tree.
    if (m_paramDoc)
        connect(m_paramDoc, &cad::param::ParamDocument::groupsChanged,
                this, &GroupPanel::refresh);
    // Segment name / angle edits (no topology change) also refresh the labels.
    if (m_scene)
        connect(m_scene, &CanvasScene::groupInfoChanged,
                this, &GroupPanel::refresh);

    refresh();
}

void GroupPanel::setupUi()
{
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QString::fromUtf8("ID"),
                             QString::fromUtf8("名称"),
                             QString::fromUtf8("数量")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    // Two-color readable serials in the ID column.
    m_tree->setItemDelegateForColumn(0, new cad::ui::SerialDelegate(m_tree));
    // Inline name editing via F2 / typing (mouse double-click is handled manually
    // to avoid clashing with "double-click opens dialog").
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    m_tree->installEventFilter(this);

    connect(m_tree, &QTreeWidget::itemClicked,
            this, &GroupPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &GroupPanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &GroupPanel::onItemChanged);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
}

void GroupPanel::refresh()
{
    if (!m_tree || !m_paramDoc) return;

    m_refreshing = true;
    m_tree->clear();

    for (const auto& group : m_paramDoc->groups()) {
        auto* gItem = new QTreeWidgetItem(m_tree);
        gItem->setData(0, cad::ui::SerialRole, group.serial);
        gItem->setText(0, group.serial);
        gItem->setText(1, group.name);
        gItem->setData(0, RoleGroupId, group.id);
        gItem->setData(0, RoleIsGroup, true);
        gItem->setFlags(gItem->flags() | Qt::ItemIsEditable);

        const QList<QUuid> members = m_paramDoc->blocksInGroup(group.id);
        gItem->setText(2, QString::number(members.size())
                              + QString::fromUtf8("根"));

        if (!members.isEmpty()) {
            const cad::param::GroupNode root =
                cad::param::buildGroupTree(*m_paramDoc, members.first());
            addMemberNode(root, gItem);
        }
        gItem->setExpanded(true);
    }

    m_refreshing = false;
}

void GroupPanel::addMemberNode(const cad::param::GroupNode& node,
                               QTreeWidgetItem* parent)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setData(0, cad::ui::SerialRole, node.serial);
    item->setText(0, node.serial);
    item->setText(1, node.name);
    item->setData(0, RoleBlockId, node.blockId);
    item->setData(0, RoleSegmentId, node.segmentId);
    item->setData(0, RoleIsGroup, false);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    for (const auto& child : node.children)
        addMemberNode(child, item);
}

void GroupPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    (void)column;
    if (!item || !m_scene) return;
    if (item->data(0, RoleIsGroup).toBool()) return;  // group row: no canvas select
    const QUuid blockId = item->data(0, RoleBlockId).toUuid();
    if (!blockId.isNull())
        m_scene->selectBlock(blockId);  // selected segment turns red on canvas
}

void GroupPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    if (!item) return;

    // Double-clicking the name cell edits it in place.
    if (column == 1) {
        m_tree->editItem(item, 1);
        return;
    }

    // Double-clicking a member's ID cell opens the segment property dialog.
    if (item->data(0, RoleIsGroup).toBool()) return;
    if (!m_paramDoc || !m_scene) return;
    const QUuid blockId = item->data(0, RoleBlockId).toUuid();
    const QUuid segmentId = item->data(0, RoleSegmentId).toUuid();
    if (blockId.isNull() || segmentId.isNull()) return;

    cad::tools::LinePropertyDialog dlg(blockId, segmentId, m_paramDoc, m_scene, this);
    dlg.exec();
    refresh();
}

void GroupPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_refreshing || column != 1 || !item || !m_paramDoc) return;

    const QString newName = item->text(1).trimmed();
    if (item->data(0, RoleIsGroup).toBool()) {
        const QUuid gid = item->data(0, RoleGroupId).toUuid();
        m_paramDoc->setGroupName(gid, newName);  // emits groupsChanged -> refresh
    } else {
        const QUuid blockId = item->data(0, RoleBlockId).toUuid();
        const QUuid segId = item->data(0, RoleSegmentId).toUuid();
        if (auto* b = m_paramDoc->findBlock(blockId)) {
            if (auto* s = b->findSegment(segId)) {
                s->name = newName;
                if (m_scene) {
                    m_scene->refreshAllBlockItems();
                    m_scene->notifyGroupInfoChanged();  // emits groupInfoChanged -> refresh
                }
            }
        }
    }
}

bool GroupPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_tree && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            kickOutSelected();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void GroupPanel::kickOutSelected()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || !m_paramDoc) return;
    if (item->data(0, RoleIsGroup).toBool()) return;  // only segments can be kicked out

    const QUuid blockId = item->data(0, RoleBlockId).toUuid();
    if (blockId.isNull()) return;

    // Remove every attachment of this block; recomputeGroups() then dissolves
    // any sub-chain that drops below two segments. Geometry is preserved.
    m_paramDoc->removeAttachmentsOfBlock(blockId);  // emits groupsChanged -> refresh
    if (m_scene)
        m_scene->refreshAllBlockItems();
}
