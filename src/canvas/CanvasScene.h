#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QUuid>

namespace cad::param { class ParamDocument; }

class BlockItem;

/// The scene that holds all entity items and the origin crosshair.
class CanvasScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit CanvasScene(cad::param::ParamDocument* paramDoc, QObject* parent = nullptr);
    ~CanvasScene() override;

    [[nodiscard]] cad::param::ParamDocument* paramDocument() const { return m_paramDoc; }

    /// Create and add a BlockItem for the given block ID.
    void addBlockItem(const QUuid& blockId);

    /// Remove the BlockItem for the given block ID.
    void removeBlockItem(const QUuid& blockId);

    /// Refresh all BlockItems (call after resolve).
    void refreshAllBlockItems();

    /// Select exactly the given block on canvas (clears previous selection).
    /// Drives group highlighting via selectionChanged.
    void selectBlock(const QUuid& blockId);

    /// Find the BlockItem for a block (nullptr if none).
    [[nodiscard]] BlockItem* findBlockItem(const QUuid& blockId) const;

    /// Notify listeners (e.g. group panel) that group display info (names or
    /// construction angles) changed without a structural topology change.
    void notifyGroupInfoChanged();

signals:
    void sceneMouseMoved(qreal x, qreal y);
    void groupInfoChanged();

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

private:
    /// Recompute the soft group highlight from the current selection.
    void updateGroupHighlight();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QHash<QUuid, BlockItem*> m_blockItems;
};
