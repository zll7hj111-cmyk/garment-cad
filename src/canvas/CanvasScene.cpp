#include "CanvasScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QGraphicsView>
#include <QScrollBar>
#include <QPen>
#include <QTimer>
#include <cmath>
#include <utility>
#include <bit>

#include "CanvasView.h"
#include "OriginCrosshair.h"
#include "BlockItem.h"
#include "HudItem.h"
#include "geometry/Units.h"
#include "parametric/Block.h"
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
                this, [this] { syncBlockPositions(); refreshComponentBoxes(); });
        connect(m_paramDoc, &cad::param::ParamDocument::componentsChanged,
                this, &CanvasScene::refreshComponentBoxes);
        // Layer changes only affect display mode (normal/grayed/hidden), not
        // geometry — but rebuildCache recomputes the layer mode, so a refresh
        // is still required.
        connect(m_paramDoc, &cad::param::ParamDocument::layersChanged,
                this, &CanvasScene::refreshAllBlockItems);
        connect(m_paramDoc, &cad::param::ParamDocument::activeLayerChanged,
                this, [this](const QUuid&) { refreshAllBlockItems(); });
    }

    // Animator requests repaint → repaint the owning item's rect (and its
    // children: curve items animate through the parent as animator owner).
    // The animator hands out a const identity handle, so repaint goes through
    // scene-level update(rect) instead of the non-const QGraphicsItem::update().
    connect(&m_animator, &CanvasAnimator::invalidationRequested,
            this, [this](const QGraphicsItem* item) {
                update(item->sceneBoundingRect());
                for (QGraphicsItem* child : item->childItems())
                    update(child->sceneBoundingRect());
            });
}

CanvasScene::~CanvasScene() = default;

double CanvasScene::currentZoom() const
{
    if (views().isEmpty()) return 1.0;
    return views().first()->transform().m11();
}

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
    for (auto* box : m_componentBoxes) {
        removeItem(box);
        delete box;
    }
    m_componentBoxes.clear();
}

void CanvasScene::refreshComponentBoxes()
{
    if (!m_paramDoc) return;

    // Which components want a visible box right now.
    QSet<QUuid> wanted;
    for (const auto& c : m_paramDoc->components())
        if (c.showBoundingBox)
            wanted.insert(c.id);

    // Drop stale boxes.
    for (auto it = m_componentBoxes.begin(); it != m_componentBoxes.end(); ) {
        if (!wanted.contains(it.key())) {
            const QUuid id = it.key();  // erase 前快照 (erase 后 it = 下一项)
            removeItem(it.value());
            delete it.value();
            it = m_componentBoxes.erase(it);
            m_componentBoxSig.remove(id);
        } else {
            ++it;
        }
    }

    // Create / reposition the live boxes (dashed outline behind the blocks).
    for (const auto& c : m_paramDoc->components()) {
        if (!c.showBoundingBox) continue;
        QGraphicsRectItem* item = m_componentBoxes.value(c.id);
        if (!item) {
            item = new QGraphicsRectItem();
            QPen pen(QColor(47, 111, 237, 210));
            pen.setWidthF(1.0);
            pen.setCosmetic(true);
            pen.setStyle(Qt::DashLine);
            item->setPen(pen);
            item->setBrush(Qt::NoBrush);
            item->setZValue(-1.0);  // behind the geometry
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
            item->setFlag(QGraphicsItem::ItemIsFocusable, false);
            addItem(item);
            m_componentBoxes.insert(c.id, item);
        }

        // 2026-09 性能: geometry signature — bbox 只依赖成员 (epoch, origin,
        // rotation) 与 zoom(外扩像素); 没变则复用现缓存的 rect, 零重算.
        // FNV-1a mix; collision = 只多算一次, 安全。
        quint64 sig = 0x811c9dc5ULL;
        for (const QUuid& mid : c.memberBlockIds) {
            if (const auto* mb = m_paramDoc->findBlock(mid)) {
                const quint64 h[4] = {
                    mb->geometryEpoch(),
                    std::bit_cast<quint64>(mb->transform.origin.x),
                    std::bit_cast<quint64>(mb->transform.origin.y),
                    std::bit_cast<quint64>(mb->transform.rotation)
                };
                for (const quint64 v : h) {
                    sig ^= v;
                    sig *= 0x01000193ULL;
                }
            } else {
                sig ^= 0xDEADBEAFULL;  // 成员缺失 → sig 必变
                sig *= 0x01000193ULL;
            }
        }
        double zoom = currentZoom();
        if (zoom < 1e-9) zoom = 1.0;
        sig ^= std::bit_cast<quint64>(zoom) ^ std::bit_cast<quint64>(5.0 / zoom);

        if (m_componentBoxSig.value(c.id) == sig) {
            item->show();
            continue;   // 几何未变: 保留现缓存的 rect
        }

        const cad::param::BBox box = m_paramDoc->componentsView().boundingBoxOf(c.id);
        if (!box.valid) {
            m_componentBoxSig.remove(c.id);
            item->hide();
            continue;
        }
        m_componentBoxSig.insert(c.id, sig);
        // World (+Y up) → scene (+Y down): top-left = (min.x, -max.y).
        // 5px outward padding (用户要求 2026-09): 1 screen px = 1/zoom scene mm.
        const double pad = 5.0 / zoom;
        const QRectF r(QPointF(box.min.x - pad, -box.max.y - pad),
                       QPointF(box.max.x + pad, -box.min.y + pad));
        item->setRect(r);
        item->show();
    }
}

void CanvasScene::refreshAllBlockItems()
{
    GCAD_PERF_SCOPE("scene.refreshAll");
    for (auto* item : m_blockItems) {
        item->updateFromBlock();
    }
}

void CanvasScene::syncBlockPositions()
{
    GCAD_PERF_SCOPE("scene.sync");
    for (auto* item : m_blockItems) {
        item->syncFromBlock();
    }
}

void CanvasScene::syncBlockPositions(const QList<QUuid>& blockIds)
{
    for (const auto& id : blockIds) {
        auto it = m_blockItems.constFind(id);
        if (it != m_blockItems.constEnd())
            it.value()->syncFromBlock();
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

void CanvasScene::setDirectionArrowsEnabled(bool on)
{
    if (m_directionArrowsEnabled == on) return;
    m_directionArrowsEnabled = on;
    // Repaint only: geometry/caches untouched (pure display overlay).
    for (auto* item : m_blockItems)
        item->update();
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

    // Build (or reuse) the toast item. HudItem (DarkPill) 自带 1/zoom 补偿
    // —— 原 QGraphicsRectItem 方案把字体像素直接写进场景 rect, 放大 toast
    // 缩成一点、缩小则巨大遮画布 (TOOL_SYSTEM_AUDIT M1)。
    if (!m_toastItem) {
        m_toastItem = new HudItem();
        m_toastItem->setLook(HudItem::Look::DarkPill);
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

    m_toastItem->setText(text);

    // Anchor at top-center of the current viewport (scene coords); 水平居中
    // 与 14px 下沉都走 HudItem 的屏幕像素偏移参数 (÷zoom 落地, WYSIWYG)。
    const QRectF viewScene = view->mapToScene(view->viewport()->rect()).boundingRect();
    const QSizeF sz = m_toastItem->size();
    m_toastItem->placeAtScene(QPointF(viewScene.center().x(), viewScene.top()), view,
                              QPointF(-sz.width() / 2.0, 14.0));
    m_toastItem->show();

    m_toastTimer->start(1400);
}

void CanvasScene::setModeBadge(const QString& text)
{
    if (text.isEmpty()) {                       // 撤下 (默认态 / 切工具)
        if (m_modeBadge) m_modeBadge->hide();
        return;
    }
    // 无头场合 (单测直连 scene) 没有视口可锚定, 角标无从谈起 —— 但不算错,
    // 状态栏 L1 照常工作。
    if (views().isEmpty()) return;

    if (!m_modeBadge) {
        m_modeBadge = new HudItem();
        m_modeBadge->setLook(HudItem::Look::DarkPill);
        m_modeBadge->setZValue(9999.0);   // 低于 toast (10000): 角标不该盖住提示
        addItem(m_modeBadge);
        connectModeBadgeViewSignals();
    }
    m_modeBadge->setText(text);
    repositionModeBadge();
    m_modeBadge->show();
}

QString CanvasScene::modeBadgeText() const
{
    return (m_modeBadge && m_modeBadge->isVisible()) ? m_modeBadge->text() : QString();
}

QPointF CanvasScene::modeBadgeScenePos() const
{
    return m_modeBadge ? m_modeBadge->pos() : QPointF();
}

void CanvasScene::repositionModeBadge()
{
    if (!m_modeBadge || views().isEmpty()) return;
    QGraphicsView* view = views().first();
    // 与 toast 同一套锚定: 先取"当前视口矩形"的场景坐标, 再用 HudItem 的
    // 屏幕像素偏移 (÷zoom 落地, 缩放下位置恒定)。
    const QRectF viewScene = view->mapToScene(view->viewport()->rect()).boundingRect();
    m_modeBadge->placeAtScene(viewScene.topLeft(), view, QPointF(14.0, 14.0));
}

void CanvasScene::connectModeBadgeViewSignals()
{
    // 角标是"钉在视口左上角"的, 而它实际是场景图元 —— 视口一滚动/缩放,
    // 同一屏幕位置对应的场景坐标就变了, 必须显式重定位 (toast 是 1.4 秒就
    // 消失的瞬态, 漂移无所谓; 常驻的不行)。
    //
    // 不用 QWidget 覆盖层省掉这件事: QGraphicsView 的 viewport 是 OpenGL
    // widget, 上面叠普通 QWidget 会碰合成刷新问题。
    QGraphicsView* view = views().isEmpty() ? nullptr : views().first();
    if (!view) return;

    if (QScrollBar* h = view->horizontalScrollBar())
        connect(h, &QScrollBar::valueChanged, this, [this] { repositionModeBadge(); });
    if (QScrollBar* v = view->verticalScrollBar())
        connect(v, &QScrollBar::valueChanged, this, [this] { repositionModeBadge(); });
    // 缩放时滚动条 value 不一定变 (缩放中心不变), 所以必须单独接。
    if (auto* cv = qobject_cast<CanvasView*>(view))
        connect(cv, &CanvasView::zoomFactorChanged, this, [this] { repositionModeBadge(); });
}

bool CanvasScene::flashMeasure(const QUuid& blockA, const QUuid& pointA,
                                const QUuid& blockB, const QUuid& pointB,
                                cad::param::MeasureKind kind)
{
    if (!m_paramDoc) return false;

    // Both endpoints must exist AND be resolved, otherwise the caller falls
    // back to the whole-block highlight path.
    const cad::param::Block* bA = m_paramDoc->blocksView().byId(blockA);
    const cad::param::Block* bB = m_paramDoc->blocksView().byId(blockB);
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
    // Mirror the ToolMeasure preview: distance draws a straight connector;
    // horizontal/vertical draw the projected span line (the "already built"
    // dimension-style display was only on the live measure tool, not on card
    // flashes).
    QPointF lineStart = sa;
    switch (kind) {
        case cad::param::MeasureKind::Horizontal:
            lineStart = QPointF(sa.x(), sb.y());
            break;
        case cad::param::MeasureKind::Vertical:
            lineStart = QPointF(sb.x(), sa.y());
            break;
        case cad::param::MeasureKind::Distance:
            break;
    }
    auto* line = new QGraphicsLineItem(QLineF(lineStart, sb));
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

bool CanvasScene::flashAngleMeasure(const QUuid& blockA, const QUuid& segmentA,
                                    const QUuid& blockB, const QUuid& segmentB)
{
    if (!m_paramDoc) return false;

    const cad::param::Block* bA = m_paramDoc->blocksView().byId(blockA);
    const cad::param::Block* bB = m_paramDoc->blocksView().byId(blockB);
    if (!bA || !bB) return false;
    const auto* segA = bA->findSegment(segmentA);
    const auto* segB = bB->findSegment(segmentB);
    if (!segA || !segB) return false;
    const cad::param::ParamPoint* a0 = bA->findPoint(segA->startPointId);
    const cad::param::ParamPoint* a1 = bA->findPoint(segA->endPointId);
    const cad::param::ParamPoint* b0 = bB->findPoint(segB->startPointId);
    const cad::param::ParamPoint* b1 = bB->findPoint(segB->endPointId);
    if (!a0 || !a1 || !b0 || !b1 || !a0->resolved || !a1->resolved ||
        !b0->resolved || !b1->resolved)
        return false;

    const QPointF a0s = cad::geo::Coord::toScene(bA->worldPos(segA->startPointId));
    const QPointF a1s = cad::geo::Coord::toScene(bA->worldPos(segA->endPointId));
    const QPointF b0s = cad::geo::Coord::toScene(bB->worldPos(segB->startPointId));
    const QPointF b1s = cad::geo::Coord::toScene(bB->worldPos(segB->endPointId));

    // Vertex = intersection of the two infinite lines. Parallel/coincident
    // fallbacks keep the visual useful instead of dropping the flash.
    const QPointF da = a1s - a0s;
    const QPointF db = b1s - b0s;
    const double denom = da.x() * db.y() - da.y() * db.x();
    QPointF pivot;
    bool havePivot = false;
    if (std::abs(denom) > 1e-9) {
        const double t = ((b0s.x() - a0s.x()) * db.y() -
                          (b0s.y() - a0s.y()) * db.x()) / denom;
        pivot = a0s + t * da;
        havePivot = true;
    }
    if (!havePivot) {
        // Prefer a literally shared endpoint; otherwise anchor at the middle
        // of segment A so the arc is still drawn near the measured pair.
        const QPointF cand[4] = { a0s, a1s, b0s, b1s };
        for (int i = 0; i < 4 && !havePivot; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                if ((cand[i] - cand[j]).manhattanLength() < 0.5) {
                    pivot = (cand[i] + cand[j]) / 2.0;
                    havePivot = true;
                    break;
                }
            }
        }
        if (!havePivot)
            pivot = (a0s + a1s) / 2.0;
    }

    const QColor amber(0xFF, 0x98, 0x00);
    QList<QGraphicsItem*> overlay;

    // Two source segments.
    for (const auto& [p0, p1] : { std::pair<QPointF, QPointF>{a0s, a1s},
                                  std::pair<QPointF, QPointF>{b0s, b1s} }) {
        auto* seg = new QGraphicsLineItem(QLineF(p0, p1));
        QPen pen(amber, 2.0);
        pen.setCosmetic(true);
        seg->setPen(pen);
        seg->setZValue(102.0);
        addItem(seg);
        overlay.append(seg);
    }

    // Half arc. Use the rays from the intersection towards the segments'
    // actual endpoint bodies (not the raw start->end direction). When the
    // vertex lies inside a segment, pick the pair that forms the smaller
    // (acute) angle, which is the natural dimension visual.
    const QPointF aOpts[2] = { a0s, a1s };
    const QPointF bOpts[2] = { b0s, b1s };
    constexpr double kZeroEps = 0.5;
    bool haveRays = false;
    double bestAbs = M_PI;
    double dirA = 0.0;
    double dirB = 0.0;
    for (int i = 0; i < 2; ++i) {
        if ((aOpts[i] - pivot).manhattanLength() < kZeroEps) continue;
        for (int j = 0; j < 2; ++j) {
            if ((bOpts[j] - pivot).manhattanLength() < kZeroEps) continue;
            const double da = std::atan2(aOpts[i].y() - pivot.y(),
                                         aOpts[i].x() - pivot.x());
            const double db = std::atan2(bOpts[j].y() - pivot.y(),
                                         bOpts[j].x() - pivot.x());
            double span = db - da;
            while (span >  M_PI) span -= 2.0 * M_PI;
            while (span < -M_PI) span += 2.0 * M_PI;
            if (!haveRays || std::abs(span) < bestAbs) {
                haveRays = true;
                bestAbs = std::abs(span);
                dirA = da;
                dirB = db;
            }
        }
    }
    if (!haveRays) {
        // Degenerate fallback: anchor at the middle of segment A.
        dirA = 0.0;
        dirB = 0.0;
    }
    double span = dirB - dirA;
    while (span >  M_PI) span -= 2.0 * M_PI;
    while (span < -M_PI) span += 2.0 * M_PI;

    double zoom = currentZoom();
    const double arcR = 40.0 / zoom;
    QPainterPath arcPath;
    constexpr int kSamples = 40;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = dirA + span * i / kSamples;
        const QPointF p(pivot.x() + arcR * std::cos(t),
                        pivot.y() + arcR * std::sin(t));
        if (i == 0)
            arcPath.moveTo(p);
        else
            arcPath.lineTo(p);
    }
    auto* arc = new QGraphicsPathItem(arcPath);
    QPen arcPen(amber, 2.0);
    arcPen.setCosmetic(true);
    arc->setPen(arcPen);
    arc->setZValue(102.0);
    addItem(arc);
    overlay.append(arc);

    QTimer::singleShot(1500, this, [this, overlay]() {
        for (QGraphicsItem* item : overlay) {
            if (item->scene() == this)
                removeItem(item);
            delete item;
        }
    });
    return true;
}


void CanvasScene::notifyLineCreated(const QUuid& blockId, const QUuid& segmentId)
{
    emit lineCreated(blockId, segmentId);
}

void CanvasScene::notifyLinePreview(double lenCm, double angleDeg)
{
    emit linePreviewChanged(lenCm, angleDeg);
}


