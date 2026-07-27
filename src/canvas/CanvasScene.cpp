#include "CanvasScene.h"

#include <QGraphicsSceneMouseEvent>

#include "OriginCrosshair.h"
#include "BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/GroupModel.h"

CanvasScene::CanvasScene(cad::param::ParamDocument* paramDoc, QObject* parent)
    : QGraphicsScene(parent)
    , m_paramDoc(paramDoc)
{
    // Add origin crosshair
    auto* crosshair = new OriginCrosshair();
    addItem(crosshair);

    // Connect signals: when a block is added/removed, update scene items
    if (m_paramDoc) {
        connect(m_paramDoc, &cad::param::ParamDocument::blockAdded,
                this, &CanvasScene::addBlockItem);
        connect(m_paramDoc, &cad::param::ParamDocument::blockRemoved,
                this, &CanvasScene::removeBlockItem);
        connect(m_paramDoc, &cad::param::ParamDocument::resolved,
                this, &CanvasScene::refreshAllBlockItems);
        connect(m_paramDoc, &cad::param::ParamDocument::structureChanged,
                this, &CanvasScene::updateGroupHighlight);
    }

    // Recompute the soft group highlight whenever the selection changes.
    connect(this, &QGraphicsScene::selectionChanged,
            this, &CanvasScene::updateGroupHighlight);
}

CanvasScene::~CanvasScene() = default;

void CanvasScene::addBlockItem(const QUuid& blockId)
{
    if (m_blockItems.contains(blockId)) return;

    auto* item = new BlockItem(blockId, m_paramDoc);
    addItem(item);
    m_blockItems.insert(blockId, item);
}

void CanvasScene::removeBlockItem(const QUuid& blockId)
{
    auto it = m_blockItems.find(blockId);
    if (it != m_blockItems.end()) {
        removeItem(it.value());
        delete it.value();
        m_blockItems.erase(it);
    }
}

void CanvasScene::refreshAllBlockItems()
{
    for (auto* item : m_blockItems) {
        item->updateFromBlock();
    }
}

BlockItem* CanvasScene::findBlockItem(const QUuid& blockId) const
{
    return m_blockItems.value(blockId, nullptr);
}

void CanvasScene::selectBlock(const QUuid& blockId)
{
    clearSelection();
    if (BlockItem* item = findBlockItem(blockId))
        item->setSelected(true);
}

void CanvasScene::notifyGroupInfoChanged()
{
    emit groupInfoChanged();
}

void CanvasScene::updateGroupHighlight()
{
    // Reset all group highlights first.
    for (auto* item : m_blockItems)
        item->setGroupHighlight(false);

    if (!m_paramDoc)
        return;

    // Highlight the whole attachment group of every selected block.
    for (auto* item : m_blockItems) {
        if (!item->isSelected())
            continue;
        const QList<QUuid> group =
            cad::param::collectGroupBlockIds(*m_paramDoc, item->blockId());
        for (const QUuid& id : group) {
            if (BlockItem* member = findBlockItem(id))
                member->setGroupHighlight(true);
        }
    }
}

void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QPointF pos = event->scenePos();
    emit sceneMouseMoved(pos.x(), pos.y());
    QGraphicsScene::mouseMoveEvent(event);
}
