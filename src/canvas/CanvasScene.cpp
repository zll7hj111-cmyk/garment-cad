#include "CanvasScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsView>
#include <QPen>
#include <QTimer>

#include "OriginCrosshair.h"
#include "BlockItem.h"
#include "GroupBadgeItem.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/PerfProbe.h"

CanvasScene::CanvasScene(cad::param::ParamDocument* paramDoc, QObject* parent)
    : QGraphicsScene(parent)
    , m_paramDoc(paramDoc)
    , m_animator(&m_style, this)
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
        connect(m_paramDoc, &cad::param::ParamDocument::documentReset,
                this, &CanvasScene::clearAllBlockItems);
        connect(m_paramDoc, &cad::param::ParamDocument::resolved,
                this, [this] { syncBlockPositions(); });
        // Layer changes only affect display mode (normal/grayed/hidden), not
        // geometry — but rebuildCache recomputes the layer mode, so a refresh
        // is still required.
        connect(m_paramDoc, &cad::param::ParamDocument::layersChanged,
                this, &CanvasScene::refreshAllBlockItems);
        connect(m_paramDoc, &cad::param::ParamDocument::activeLayerChanged,
                this, [this](const QUuid&) { refreshAllBlockItems(); });
        connect(m_paramDoc, &cad::param::ParamDocument::groupsChanged,
                this, &CanvasScene::reconcileGroupBadges);
        connect(m_paramDoc, &cad::param::ParamDocument::documentReset,
                this, &CanvasScene::reconcileGroupBadges);
    }

    // Animator requests repaint → forward to the owning item (and its
    // children: curve items animate through the parent as animator owner).
    connect(&m_animator, &CanvasAnimator::invalidationRequested,
            this, [](QGraphicsItem* item) {
                item->update();
                for (QGraphicsItem* child : item->childItems())
                    child->update();
            });
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
        m_animator.removeOwner(it.value());
        removeItem(it.value());
        delete it.value();
        m_blockItems.erase(it);
    }
}

void CanvasScene::clearAllBlockItems()
{
    for (auto* item : m_blockItems) {
        m_animator.removeOwner(item);
        removeItem(item);
        delete item;
    }
    m_blockItems.clear();
}

void CanvasScene::refreshAllBlockItems()
{
    GCAD_PERF_SCOPE("scene.refreshAll");
    for (auto* item : m_blockItems) {
        item->updateFromBlock();
    }
    // Layer visibility changes affect badge visibility — full reconcile.
    reconcileGroupBadges();
}

void CanvasScene::syncBlockPositions()
{
    GCAD_PERF_SCOPE("scene.sync");
    for (auto* item : m_blockItems) {
        item->syncFromBlock();
    }
    updateGroupBadgePositions();   // badges track live drags (positions only)
}

void CanvasScene::syncBlockPositions(const QList<QUuid>& blockIds)
{
    for (const auto& id : blockIds) {
        auto it = m_blockItems.constFind(id);
        if (it != m_blockItems.constEnd())
            it.value()->syncFromBlock();
    }
    updateGroupBadgePositions();
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

void CanvasScene::setForceShowName(bool on)
{
    if (m_forceShowName == on) return;
    m_forceShowName = on;
    // Repaint only: geometry/caches are untouched (pure display overlay).
    for (auto* item : m_blockItems)
        item->update();
    emit forceShowChanged(m_forceShowName, m_forceShowLength);
}

void CanvasScene::setForceShowLength(bool on)
{
    if (m_forceShowLength == on) return;
    m_forceShowLength = on;
    for (auto* item : m_blockItems)
        item->update();
    emit forceShowChanged(m_forceShowName, m_forceShowLength);
}

void CanvasScene::notifyGroupInfoChanged()
{
    emit groupInfoChanged();
}

void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QPointF pos = event->scenePos();
    emit sceneMouseMoved(pos.x(), pos.y());
    QGraphicsScene::mouseMoveEvent(event);
}

void CanvasScene::setStyle(const CanvasStyle& s)
{
    m_style = s;
    // Full scene repaint with new tokens.
    update();
    // Pattern-paper ground follows the theme: the background brush lives on
    // each view (only the view paints it), so sync every attached view here —
    // the single authoritative theme-switch path (constructor + toggleTheme).
    // QGraphicsView base is enough: no CanvasView-specific API is needed.
    for (QGraphicsView* v : views())
        v->setBackgroundBrush(m_style.canvasBackground);
}

void CanvasScene::showToast(const QString& text)
{
    if (views().isEmpty()) return;
    QGraphicsView* view = views().first();

    // Build (or reuse) the toast item.
    if (!m_toastItem) {
        m_toastItem = new QGraphicsRectItem();
        m_toastItem->setZValue(10000);
        addItem(m_toastItem);
    }
    if (!m_toastTimer) {
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        QObject::connect(m_toastTimer, &QTimer::timeout, this, [this] {
            if (m_toastItem) m_toastItem->hide();
        });
    }

    // Measure the text and size the pill.
    QFont font;
    font.setPixelSize(12);
    QFontMetrics fm(font);
    const QRectF textRect = fm.boundingRect(text);
    const double padX = 14.0, padY = 7.0;
    const double w = textRect.width() + padX * 2.0;
    const double h = textRect.height() + padY * 2.0;

    // Anchor at top-center of the current viewport (scene coords).
    const QRectF viewScene = view->mapToScene(view->viewport()->rect()).boundingRect();
    const double x = viewScene.center().x() - w / 2.0;
    const double y = viewScene.top() + 14.0;

    m_toastItem->setRect(x, y, w, h);
    m_toastItem->setPen(QPen(QColor(0, 0, 0, 30)));
    m_toastItem->setBrush(QColor(38, 50, 56, 225));
    m_toastItem->show();

    // Text: reuse a child QGraphicsSimpleTextItem parented to the pill.
    QGraphicsSimpleTextItem* textItem = nullptr;
    for (QGraphicsItem* child : m_toastItem->childItems()) {
        if (auto* sti = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(child)) {
            textItem = sti;
            break;
        }
    }
    if (!textItem) {
        textItem = new QGraphicsSimpleTextItem(m_toastItem);
        textItem->setBrush(QColor(255, 255, 255));
    }
    textItem->setFont(font);
    textItem->setText(text);
    const QRectF tiRect = textItem->boundingRect();
    textItem->setPos(x + (w - tiRect.width()) / 2.0,
                     y + (h - tiRect.height()) / 2.0);

    m_toastTimer->start(1400);
}

bool CanvasScene::flashMeasure(const QUuid& blockA, const QUuid& pointA,
                               const QUuid& blockB, const QUuid& pointB)
{
    if (!m_paramDoc) return false;

    // Both endpoints must exist AND be resolved, otherwise the caller falls
    // back to the whole-block highlight path.
    const cad::param::Block* bA = m_paramDoc->blockById(blockA);
    const cad::param::Block* bB = m_paramDoc->blockById(blockB);
    if (!bA || !bB) return false;
    const cad::param::ParamPoint* pA = bA->findPoint(pointA);
    const cad::param::ParamPoint* pB = bB->findPoint(pointB);
    if (!pA || !pB || !pA->resolved || !pB->resolved) return false;

    const QPointF sa = cad::geo::Coord::toScene(bA->worldPos(pointA));
    const QPointF sb = cad::geo::Coord::toScene(bB->worldPos(pointB));

    // Transient overlay set: two amber rings + one amber dashed connector
    // (style mirrors the ToolMeasure preview).
    QList<QGraphicsItem*> overlay;
    const QColor amber(0xFF, 0x98, 0x00);
    constexpr double ringR = 5.0;
    for (const QPointF& p : { sa, sb }) {
        auto* ring = new QGraphicsEllipseItem(-ringR, -ringR, ringR * 2.0, ringR * 2.0);
        QPen pen(amber, 2.0);
        pen.setCosmetic(true);
        ring->setPen(pen);
        ring->setBrush(Qt::NoBrush);
        ring->setZValue(102.0);
        ring->setPos(p);
        addItem(ring);
        overlay.append(ring);
    }
    auto* line = new QGraphicsLineItem(QLineF(sa, sb));
    QPen linePen(amber, 1.4);
    linePen.setCosmetic(true);
    linePen.setStyle(Qt::DashLine);
    line->setPen(linePen);
    line->setZValue(102.0);
    addItem(line);
    overlay.append(line);

    // Self-destruct after 1.5 s (remove BEFORE delete — never rely on
    // QObject parenting for QGraphicsItems).
    QTimer::singleShot(1500, this, [this, overlay]() {
        for (QGraphicsItem* item : overlay) {
            if (item->scene() == this)
                removeItem(item);
            delete item;
        }
    });
    return true;
}

QSet<QUuid> CanvasScene::groupMemberSet(const QUuid& blockId) const
{
    QSet<QUuid> result;
    if (blockId.isNull() || !m_paramDoc) return result;
    const QUuid gid = m_paramDoc->groupOfBlock(blockId);
    if (gid.isNull()) return result;
    const QList<QUuid> members = m_paramDoc->blocksInGroup(gid);
    return QSet<QUuid>(members.begin(), members.end());
}

void CanvasScene::setGroupHoverSource(const QUuid& blockId)
{
    if (m_groupHoverSource == blockId) return;

    const QSet<QUuid> oldMembers = groupMemberSet(m_groupHoverSource);
    m_groupHoverSource = blockId;
    const QSet<QUuid> newMembers = groupMemberSet(blockId);

    // Diff-apply: only items whose membership flipped are touched.
    for (const QUuid& id : oldMembers) {
        if (!newMembers.contains(id))
            if (BlockItem* bi = findBlockItem(id))
                bi->setGroupHovered(false);
    }
    for (const QUuid& id : newMembers) {
        if (!oldMembers.contains(id))
            if (BlockItem* bi = findBlockItem(id))
                bi->setGroupHovered(true);
    }
    updateBadgeAccents();
}

// ═════════════════════════════════════════════════════════════════════════
// Group visual markers (组包围框): one dashed bounding box per group,
// anchored at the member union bounds top-left
// ═════════════════════════════════════════════════════════════════════════

void CanvasScene::reconcileGroupBadges()
{
    if (!m_paramDoc) return;

    // Reconcile: drop badges whose group vanished.
    QSet<QUuid> live;
    for (const auto& g : m_paramDoc->groups())
        live.insert(g.id);
    for (auto it = m_groupBadges.begin(); it != m_groupBadges.end(); ) {
        if (!live.contains(it.key())) {
            removeItem(it.value());
            delete it.value();
            it = m_groupBadges.erase(it);
        } else {
            ++it;
        }
    }

    // Create / label / position the survivors.
    for (const auto& g : m_paramDoc->groups()) {
        GroupBadgeItem* badge = m_groupBadges.value(g.id, nullptr);
        if (!badge) {
            badge = new GroupBadgeItem(g.id);
            m_groupBadges.insert(g.id, badge);
            addItem(badge);
            connect(badge, &GroupBadgeItem::hoverChanged, this,
                    [this, gid = g.id](bool hovered) { onBadgeHover(gid, hovered); });
            connect(badge, &GroupBadgeItem::clicked, this,
                    &CanvasScene::groupBadgeClicked);
        }

        const QString label = g.name.isEmpty() ? g.serial : g.name;
        const int count = m_paramDoc->blocksInGroup(g.id).size();
        // 前片 · 3条
        badge->setText(QString::fromUtf8("%1 \xc2\xb7 %2\xe6\x9d\xa1")
                           .arg(label).arg(count));
        // 组名（序列号）· 成员数
        badge->setToolTip(QString::fromUtf8("%1\xef\xbc\x88%2\xef\xbc\x89")
                              .arg(label, g.serial));

        badge->setShowBoundingBox(g.showBoundingBox);

        // A badge whose members are ALL on manually hidden layers is hidden
        // too — a floating label over invisible geometry would look orphaned.
        bool anyVisible = false;
        for (const QUuid& memberId : m_paramDoc->blocksInGroup(g.id)) {
            if (const auto* blk = m_paramDoc->findBlock(memberId))
                if (m_paramDoc->layerVisible(blk->layer)) {
                    anyVisible = true;
                    break;
                }
        }
        if (!anyVisible) badge->hide();
        else badge->show();
    }
    updateGroupBadgePositions();
    updateBadgeAccents();
}

void CanvasScene::updateGroupBadgePositions()
{
    if (!m_paramDoc) return;
    for (auto it = m_groupBadges.cbegin(); it != m_groupBadges.cend(); ++it) {
        GroupBadgeItem* badge = it.value();
        const auto* g = m_paramDoc->findGroup(it.key());
        if (g)
            badge->setShowBoundingBox(g->showBoundingBox);
        QRectF bounds;
        bool first = true;
        for (const QUuid& memberId : m_paramDoc->blocksInGroup(it.key())) {
            if (BlockItem* bi = findBlockItem(memberId)) {
                const QRectF b = bi->sceneBoundingRect();
                bounds = first ? b : bounds.united(b);
                first = false;
            }
        }
        if (first) {
            badge->setMemberSceneBounds(QRectF());
            continue;
        }
        badge->setPos(bounds.topLeft());
        badge->setMemberSceneBounds(bounds);
    }
}

GroupBadgeItem* CanvasScene::groupBadge(const QUuid& groupId) const
{
    return m_groupBadges.value(groupId, nullptr);
}

void CanvasScene::setGroupSelected(const QSet<QUuid>& groupIds)
{
    if (m_selectedGroups == groupIds) return;
    m_selectedGroups = groupIds;
    updateBadgeAccents();
}

void CanvasScene::notifyLineCreated(const QUuid& blockId, const QUuid& segmentId)
{
    emit lineCreated(blockId, segmentId);
}

void CanvasScene::notifyLinePreview(double lenCm, double angleDeg)
{
    emit linePreviewChanged(lenCm, angleDeg);
}

void CanvasScene::onBadgeHover(const QUuid& groupId, bool hovered)
{
    if (!m_paramDoc) return;
    if (hovered) {
        // Badge hover is now driven by GroupBadgeItem's own hover events;
        // the badge uses the first member as the hover source so BlockItem's
        // hover broadcast and the badge accent stay in sync.
        const QList<QUuid> members = m_paramDoc->blocksInGroup(groupId);
        if (!members.isEmpty())
            setGroupHoverSource(members.first());
    } else {
        // Withdraw ONLY when the current source belongs to this badge's group
        // (the cursor may have moved to a block, which owns the source now).
        if (!m_groupHoverSource.isNull()
            && m_paramDoc->groupOfBlock(m_groupHoverSource) == groupId)
            setGroupHoverSource(QUuid());
    }
}

void CanvasScene::updateBadgeAccents()
{
    QUuid hoverGid;
    if (!m_groupHoverSource.isNull() && m_paramDoc)
        hoverGid = m_paramDoc->groupOfBlock(m_groupHoverSource);
    for (auto it = m_groupBadges.cbegin(); it != m_groupBadges.cend(); ++it)
        it.value()->setAccent(m_selectedGroups.contains(it.key())
                              || it.key() == hoverGid);
}
