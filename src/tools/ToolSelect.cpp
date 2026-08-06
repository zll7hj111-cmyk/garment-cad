#include "ToolSelect.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QKeyEvent>
#include <QMenu>
#include <QUndoStack>
#include <QPen>
#include <QLineEdit>
#include <QInputDialog>
#include <QWidget>
#include <QEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "canvas/GroupBadgeItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "parametric/FollowerAngle.h"
#include "AngleHud.h"
#include "LinePropertyDialog.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/GroupCommands.h"
#include "document/DeleteImpactConfirm.h"

namespace cad::tools {

namespace {

/// Snap radius (screen px) for the CONNECT GESTURE — ONE source of truth
/// for every radius: the source-point halo, the drag-time findSnap reach
/// and the target ring all use it. The halo on the dragged point IS the
/// reach: when its edge touches any target point, the snap fires and the
/// target ring appears (光环碰到任意点 = 可连接).
constexpr double kConnectSnapRadius = 7.5;

/// World-space tolerance (mm) for "the same spot": overlapping candidate
/// points / segment endpoints that count as one connection position.
constexpr double kOverlapEps = 0.5;

/// Toast text when a freshly established attachment crosses layers (合法方向:
/// aux follower → working leader): "已建立跨层连接（测量层→操作层1）" with
/// the real layer names. Empty for same-layer connections.
QString crossLayerToast(cad::param::ParamDocument* doc,
                        const cad::param::Block& from,
                        const cad::param::Block& to)
{
    if (!doc) return QString();
    if (doc->isAuxBlock(from) == doc->isAuxBlock(to)) return QString();
    const auto& layers = doc->layers();
    auto name = [&layers](int idx) {
        return (idx >= 0 && idx < static_cast<int>(layers.size()))
            ? layers[static_cast<size_t>(idx)].name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(from.layer), name(to.layer));
}

/// True if segment [p1,p2] intersects (or is contained in) rect r.
/// Liang-Barsky clipping; also counts either endpoint inside the rect.
bool segmentIntersectsRect(const cad::geo::Vec2& p1, const cad::geo::Vec2& p2,
                           const QRectF& r)
{
    if (r.contains(QPointF(p1.x, p1.y)) || r.contains(QPointF(p2.x, p2.y)))
        return true;

    double t0 = 0.0, t1 = 1.0;
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {p1.x - r.left(), r.right() - p1.x,
                         p1.y - r.top(),  r.bottom() - p1.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0) return false;   // parallel and outside
        } else {
            const double rr = q[i] / p[i];
            if (p[i] < 0) { if (rr > t1) return false; if (rr > t0) t0 = rr; }
            else          { if (rr < t0) return false; if (rr < t1) t1 = rr; }
        }
    }
    return t0 <= t1;
}

/// Format degrees for the HUD: integers without a trailing ".0".
QString formatDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = SelectState::Idle;
}

void ToolSelect::deactivate()
{
    if (m_state == SelectState::CopyDragging)
        removeCopyPreview();          // tool switch mid-copy: drop the clones
    m_copyResult = {};
    m_copyOrigins.clear();
    removeMarqueeRect();
    removeConnectMarker();
    removeConnectHalo();
    removeConfirmHighlight();
    m_confirmCandidates.clear();
    if (m_angleHud) { m_angleHud->hide(); delete m_angleHud; m_angleHud = nullptr; }

    m_selection.clear();
    m_confirmed = false;
    if (m_scene && m_paramDoc)
        syncSelectionVisual();
    if (m_scene)
        m_scene->clearSelection();

    m_scene = nullptr;
    m_paramDoc = nullptr;
    m_state = SelectState::Idle;
}

// ═══════════════════════════════════════════════════════════════════════════════
// State management
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::setState(SelectState s) { m_state = s; }

void ToolSelect::clearSelectionAndIdle()
{
    m_selection.clear();
    m_confirmed = false;
    syncSelectionVisual();
    setState(SelectState::Idle);
}

void ToolSelect::clearSelectionOnLayerChange()
{
    // Selection is scoped to the active layer: switching layers drops it so a
    // stale (now grayed) selection can never be dragged or edited.
    if (m_state == SelectState::CopyDragging)
        removeCopyPreview();
    removeMarqueeRect();
    removeConnectMarker();
    removeConnectHalo();
    clearSelectionAndIdle();
}

void ToolSelect::selectBlocksExternally(const QList<QUuid>& blockIds)
{
    if (!m_paramDoc || blockIds.isEmpty()) return;
    // Abort any in-flight gesture first (same hygiene as a layer switch).
    if (m_state == SelectState::CopyDragging)
        removeCopyPreview();
    removeMarqueeRect();
    removeConnectMarker();
    removeConnectHalo();

    m_selection = expandedWithGroups(QSet<QUuid>(blockIds.begin(), blockIds.end()));
    m_confirmed = !m_selection.isEmpty();
    syncSelectionVisual();
    setState(m_confirmed ? SelectState::Confirmed : SelectState::Idle);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection mode (W toggle: 多选 ↔ 单选)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleSelectionMode()
{
    const bool switchingToMulti = (m_selectionMode == SelectionMode::Single);
    m_selectionMode = switchingToMulti ? SelectionMode::Multi
                                       : SelectionMode::Single;
    // Single → Multi PRESERVES the current selection so the user can keep
    // adding to it (选中一个后按 W 继续加选 — clearing it here made the
    // toggle look like a no-op and silently dropped the picked blocks).
    // Multi → Single clears, because a multi-set under single-click-replace
    // semantics would leave stale highlights the next click cannot undo.
    if (!switchingToMulti)
        clearSelectionAndIdle();
    showModeToast();
}

void ToolSelect::showModeToast()
{
    const QString text = (m_selectionMode == SelectionMode::Single)
        ? QString::fromUtf8("\xe5\x8d\x95\xe9\x80\x89\xe6\xa8\xa1\xe5\xbc\x8f\xef\xbc\x9a"
                            "\xe7\x82\xb9\xe5\x87\xbb\xe9\x80\x89\xe4\xb8\xad"
                            " | \xe5\x8f\xb3\xe9\x94\xae\xe7\xa1\xae\xe8\xae\xa4"
                            " | \xe5\x86\x8d\xe7\x82\xb9\xe6\x8b\x96\xe5\x8a\xa8/\xe7\xab\xaf\xe7\x82\xb9\xe9\x95\xbf\xe6\x8c\x89\xe8\xbf\x9e\xe6\x8e\xa5"
                            " | \xe5\x86\x8d\xe5\x8f\xb3\xe9\x94\xae\xe8\x8f\x9c\xe5\x8d\x95")   // 单选模式：点击选中 | 右键确认 | 再点拖动/端点长按连接 | 再右键菜单
        : QString::fromUtf8("\xe5\xa4\x9a\xe9\x80\x89\xe6\xa8\xa1\xe5\xbc\x8f\xef\xbc\x9a"
                            "\xe7\x82\xb9\xe5\x87\xbb\xe5\x8a\xa0\xe5\x87\x8f\xe9\x80\x89"
                            " | \xe6\xa1\x86\xe9\x80\x89 | \xe5\x8f\xb3\xe9\x94\xae\xe7\xa1\xae\xe8\xae\xa4"
                            " | \xe5\x86\x8d\xe5\x8f\xb3\xe9\x94\xae\xe6\x88\x90\xe7\xbb\x84\xe8\x8f\x9c\xe5\x8d\x95");  // 多选模式：点击加减选 | 框选 | 右键确认 | 再右键成组菜单
    showToast(text);
}

void ToolSelect::showToast(const QString& text)
{
    // Toast infrastructure lives on the scene (shared by every tool).
    if (m_scene)
        m_scene->showToast(text);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection management (toggle + red highlight)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleBlock(const QUuid& blockId)
{
    // Group = minimal selection unit: toggling a member toggles the WHOLE
    // group (整组进整组出).
    const QSet<QUuid> ids = wholeGroupSet(blockId);
    bool anySelected = false;
    for (const QUuid& id : ids)
        if (m_selection.contains(id)) { anySelected = true; break; }
    for (const QUuid& id : ids) {
        if (anySelected) m_selection.remove(id);
        else             m_selection.insert(id);
    }

    m_confirmed = false;  // any toggle invalidates confirmation
    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
}

void ToolSelect::syncSelectionVisual()
{
    if (!m_scene || !m_paramDoc) return;
    for (const auto& blk : m_paramDoc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            const bool inSel = m_selection.contains(blk.id);
            bi->setToolSelected(inSel);
            bi->setToolLocked(inSel && m_confirmed);  // confirmed = bold
        }
    }
    // Accent the badges of groups inside the selection (selection always
    // expands to whole groups, so any touched group is complete).
    QSet<QUuid> selGroups;
    for (const QUuid& id : m_selection) {
        const QUuid gid = m_paramDoc->groupOfBlock(id);
        if (!gid.isNull())
            selGroups.insert(gid);
    }
    m_scene->setGroupSelected(selGroups);
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
    if (m_state == SelectState::AngleInput) return;  // HUD owns input

    const QPointF up = event->scenePos();            // user coords (+Y up)
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Badge clicks belong to the badge (选中整组) — the tool must not also
    // toggle the block underneath.
    if (badgeAt(pos))
        return;

    // ── Right button: Idle 空白=切智能笔, 选中=确认, 已确认=上下文菜单 ──
    // 单选/多选共用同一套 选中→确认→操作 语义 (设计统一, 减少误操作):
    // 单击对象只选中; 右键把选中集锁定为可操作单元; 确认后再右键弹
    // 上下文菜单 (做成组 / 解散组 / 取消选择). 无选中时空白右键切回
    // 智能笔 —— 智能笔 ↔ 选择 two-way gesture 的反向腿.
    if (event->button() == Qt::RightButton) {
        if (m_state == SelectState::ConfirmTarget) {
            cancelConnect();  // right-click cancels the leader confirmation
            return;
        }
        if (m_state == SelectState::Idle && hitBlock(pos).isNull()) {
            requestToolSwitch(ToolType::SmartPen);
        } else if (m_state == SelectState::Selecting) {
            confirmSelection();
        } else if (m_state == SelectState::Confirmed) {
            showContextMenu();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    // ── Ctrl+press on a CONFIRMED selection → quick copy drag (快捷复制) ──
    // 复制仅对右键确认后的选择集生效 —— 与整组移动同一语法
    // (确认 = 锁定为可整体操作的单元), 单条线也需先选中并确认。
    if ((event->modifiers() & Qt::ControlModifier)
        && m_state == SelectState::Confirmed) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull() && m_selection.contains(blockHit)) {
            beginCopyDrag(blockHit, pos);
            return;
        }
    }

    switch (m_state) {
    case SelectState::ConfirmTarget:
        // Click a candidate segment to confirm the leader; blank cancels.
        tryConfirmTarget(pos);
        break;

    case SelectState::Idle:
    case SelectState::Selecting: {
        const QUuid blockHit = hitBlock(pos);
        if (m_selectionMode == SelectionMode::Single) {
            // Single mode: press SELECTS the object only — 右键确认后才可
            // 拖动/连接 (与多选同一套 选中→确认→操作 语义, 减少误操作).
            // 点击空白清选; 无框选.
            if (!blockHit.isNull()) {
                m_selection = wholeGroupSet(blockHit);
                m_confirmed = false;
                syncSelectionVisual();
                setState(SelectState::Selecting);
            } else if (!m_selection.isEmpty()) {
                clearSelectionAndIdle();
            }
            return;
        }
        // Multi mode: click toggles in/out of selection.
        if (!blockHit.isNull()) {
            toggleBlock(blockHit);
            return;
        }
        // Empty space → marquee.
        beginMarquee(pos);
        break;
    }

    case SelectState::Confirmed: {
        // Point-level press first (曲线锚点=曲线点拖动, 端点=连接拖拽);
        // otherwise fall through to block/blank handling below.
        if (tryPointOperation(pos))
            return;
        const QUuid blockHit = hitBlock(pos);
        if (m_selectionMode == SelectionMode::Single) {
            // Single mode confirmed: clicking the SAME block again starts a
            // drag ON the object (第一次点击=选中, 再次点击=拖动); a click on
            // a DIFFERENT block switches the selection to it (also confirmed);
            // empty space falls through to beginDrag below.
            if (!blockHit.isNull()) {
                if (!m_selection.contains(blockHit)) {
                    m_selection = wholeGroupSet(blockHit);
                    m_confirmed = true;
                    syncSelectionVisual();
                    setState(SelectState::Confirmed);
                } else {
                    beginDrag(pos);   // second click on the selected object
                }
                return;
            }
        } else {
            // Multi mode CONFIRMED: the set is locked as one operational
            // unit — clicking a member drags the WHOLE set (same as single
            // mode's second click); clicking a NON-member switches the
            // selection to it (also confirmed). 确认后不再减选.
            if (!blockHit.isNull()) {
                if (!m_selection.contains(blockHit)) {
                    m_selection = wholeGroupSet(blockHit);
                    m_confirmed = true;
                    syncSelectionVisual();
                    setState(SelectState::Confirmed);
                } else {
                    beginDrag(pos);   // drag the confirmed set
                }
                return;
            }
        }
        // Empty space → drag anchor for moving the confirmed selection.
        beginDrag(pos);
        break;
    }

    default:
        break;  // Marquee / Dragging / Connecting: button already down.
    }
}

void ToolSelect::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Curve anchor drag override
    if (m_anchorDragging) { updateAnchorDrag(pos); return; }

    switch (m_state) {
    case SelectState::Marquee:      updateMarquee(pos);  break;
    case SelectState::Dragging:     updateDrag(pos);     break;
    case SelectState::CopyDragging: updateCopyDrag(pos); break;
    case SelectState::Connecting:   updateConnect(pos);  break;
    case SelectState::ConfirmTarget: updateConfirmHighlight(pos); break;
    default: break;
    }
}

void ToolSelect::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Curve anchor drag override
    if (m_anchorDragging) { endAnchorDrag(); return; }

    switch (m_state) {
    case SelectState::Marquee:      endMarquee(pos);  break;
    case SelectState::Dragging:     endDrag(pos);     break;
    case SelectState::CopyDragging: endCopyDrag(pos); break;
    case SelectState::Connecting:   endConnect(pos);  break;
    default: break;
    }
}

void ToolSelect::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    if (badgeAt(cad::geo::Vec2(up.x(), up.y())))
        return;   // badge clicks never open the property dialog underneath
    const QList<QGraphicsItem*> hits = m_scene->items(cad::geo::Coord::toScene(up.x(), up.y()));
    BlockItem* blockItem = nullptr;
    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        if (auto* bi = BlockItem::containingItem(item)) { blockItem = bi; break; }
    }
    if (!blockItem) return;

    cad::param::Block* block = m_paramDoc->findBlock(blockItem->blockId());
    if (!block || block->segments.empty()) return;
    // Only the active layer opens the property dialog (same rule as
    // hitBlock): grayed reference layers stay non-editable via the canvas.
    if (block->layer != m_paramDoc->activeLayer()) return;

    const cad::geo::Vec2 clickPos(up.x(), up.y());
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
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
            // Curve: distance to the actual Bézier path, not the chord — a bent
            // curve sits far from its chord, so a chord-only test would fail to
            // open the dialog when double-clicking the visible curve.
            std::vector<cad::geo::Vec2> pts;
            std::vector<cad::geo::Vec2> tIn, tOut;
            std::vector<bool> autoTan;
            pts.push_back(sp->resolvedPos);
            tIn.push_back(sp->tangentIn); tOut.push_back(sp->tangentOut); autoTan.push_back(sp->autoTangent);
            for (const auto& ppId : seg.passPointIds) {
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                pts.push_back(pp->resolvedPos);
                tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
            }
            pts.push_back(ep->resolvedPos);
            tIn.push_back(ep->tangentIn); tOut.push_back(ep->tangentOut); autoTan.push_back(ep->autoTangent);
            auto spans = cad::geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                                   cad::geo::AutoCurveMode::Hobby);
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
    auto* dlg = new LinePropertyDialog(blockItem->blockId(), bestSegId, m_paramDoc,
                                       m_scene, parentWidget);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void ToolSelect::keyPress(QKeyEvent* event)
{
    if (m_state == SelectState::AngleInput) {
        // Fallback path: the key reached the view instead of the HUD widget.
        if (event->key() == Qt::Key_Escape) {
            cancelAngle();
            event->accept();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitAngle();
            event->accept();
        }
        return;  // HUD owns all other keys
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedBlocks();
        event->accept();
    } else if (event->key() == Qt::Key_W) {
        // W toggles 多选 ↔ 单选 mode (same leader key as other tools use for
        // their in-tool mode switches).
        toggleSelectionMode();
        event->accept();
    } else if (event->key() == Qt::Key_G) {
        // G = 做成组 (same validation as the context menu); Shift+G = 解散组.
        if (event->modifiers() & Qt::ShiftModifier)
            ungroupSelection();
        else
            makeGroupFromSelection();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        if (m_state == SelectState::Connecting || m_state == SelectState::ConfirmTarget) {
            cancelConnect();
        } else if (m_state == SelectState::CopyDragging) {
            cancelCopyDrag();
        } else if (m_state == SelectState::Dragging) {
            // Abort the move: restore pre-drag origins (cross-boundary
            // attachments are only broken at endDrag, so nothing to re-add).
            for (const QUuid& id : m_dragBlockIds) {
                if (auto* b = m_paramDoc->findBlock(id))
                    b->transform.origin = m_dragOrigins.value(id);
            }
            m_paramDoc->resolveAll();
            m_scene->refreshAllBlockItems();
            setState(SelectState::Confirmed);
        } else {
            clearSelectionAndIdle();
        }
        event->accept();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Marquee selection (intersect = select; toggle add/remove)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::beginMarquee(const cad::geo::Vec2& pos)
{
    m_marqueeStart = pos;
    m_marqueeBase = m_selection;   // snapshot for toggle semantics
    setState(SelectState::Marquee);

    if (!m_marqueeItem) {
        m_marqueeItem = new QGraphicsRectItem();
        QPen pen(QColor(0, 120, 215), 0);      // cosmetic 1px dash
        pen.setStyle(Qt::DashLine);
        m_marqueeItem->setPen(pen);
        m_marqueeItem->setBrush(QColor(0, 120, 215, 25));
        m_marqueeItem->setZValue(9999);
        m_scene->addItem(m_marqueeItem);
    }
    m_marqueeItem->setRect(QRectF());
    m_marqueeItem->show();
}

void ToolSelect::updateMarquee(const cad::geo::Vec2& pos)
{
    if (!m_marqueeItem || !m_paramDoc) return;

    const QRectF userRect = QRectF(QPointF(m_marqueeStart.x, m_marqueeStart.y),
                                   QPointF(pos.x, pos.y)).normalized();
    // User → scene coords for the on-screen rectangle.
    const QPointF stl = cad::geo::Coord::toScene(userRect.left(),  userRect.top());
    const QPointF sbr = cad::geo::Coord::toScene(userRect.right(), userRect.bottom());
    m_marqueeItem->setRect(QRectF(stl, sbr).normalized());

    // Live preview of the prospective toggle result (base XOR intersecting),
    // expanded to whole groups (group = minimal selection unit).
    const QList<QUuid> hits = blocksIntersectingRect(userRect);
    QSet<QUuid> prospective = m_marqueeBase;
    for (const QUuid& id : hits) {
        if (prospective.contains(id)) prospective.remove(id);
        else                          prospective.insert(id);
    }
    prospective = expandedWithGroups(prospective);
    for (const auto& blk : m_paramDoc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            bi->setToolSelected(prospective.contains(blk.id));
            bi->setToolLocked(false);  // marquee invalidates confirmation
        }
    }
}

void ToolSelect::endMarquee(const cad::geo::Vec2& pos)
{
    const QRectF userRect = QRectF(QPointF(m_marqueeStart.x, m_marqueeStart.y),
                                   QPointF(pos.x, pos.y)).normalized();
    removeMarqueeRect();

    // Apply the toggle: intersecting blocks flip their membership; the result
    // is expanded to whole groups (group = minimal selection unit).
    const QList<QUuid> hits = blocksIntersectingRect(userRect);
    for (const QUuid& id : hits) {
        if (m_selection.contains(id)) m_selection.remove(id);
        else                          m_selection.insert(id);
    }
    m_selection = expandedWithGroups(m_selection);
    m_marqueeBase.clear();
    m_confirmed = false;
    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
}

QList<QUuid> ToolSelect::blocksIntersectingRect(const QRectF& rectUser) const
{
    QList<QUuid> result;
    if (!m_paramDoc) return result;

    for (const auto& blk : m_paramDoc->blocks()) {
        // Marquee only selects blocks on the active layer.
        if (blk.layer != m_paramDoc->activeLayer()) continue;
        for (const auto& seg : blk.segments) {
            const auto* sp = blk.findPoint(seg.startPointId);
            const auto* ep = blk.findPoint(seg.endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
            const cad::geo::Vec2 w1 = blk.transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = blk.transform.toWorld(ep->resolvedPos);
            if (segmentIntersectsRect(w1, w2, rectUser)) {
                if (!result.contains(blk.id))
                    result.append(blk.id);
                break;  // one intersecting segment is enough for this block
            }
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Confirmation
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::confirmSelection()
{
    if (m_selection.isEmpty()) return;
    m_confirmed = true;
    syncSelectionVisual();  // selected → locked (bold strokes)
    setState(SelectState::Confirmed);
}

// ═════════════════════════════════════════════════════════════════════════
// User groups (成组): selection expansion, context menu, make/ungroup
// ═════════════════════════════════════════════════════════════════════════

QSet<QUuid> ToolSelect::wholeGroupSet(const QUuid& blockId) const
{
    if (m_paramDoc) {
        const QUuid gid = m_paramDoc->groupOfBlock(blockId);
        if (!gid.isNull()) {
            const QList<QUuid> members = m_paramDoc->blocksInGroup(gid);
            return QSet<QUuid>(members.begin(), members.end());
        }
    }
    return {blockId};
}

QSet<QUuid> ToolSelect::expandedWithGroups(const QSet<QUuid>& ids) const
{
    QSet<QUuid> result = ids;
    if (!m_paramDoc) return result;
    for (const QUuid& id : ids) {
        const QUuid gid = m_paramDoc->groupOfBlock(id);
        if (gid.isNull()) continue;
        const QList<QUuid> members = m_paramDoc->blocksInGroup(gid);
        for (const QUuid& memberId : members)
            result.insert(memberId);
    }
    return result;
}

QString ToolSelect::groupLabel(const QUuid& groupId) const
{
    if (!m_paramDoc) return QString();
    // findGroup is non-const on ParamDocument — const-cast for display only.
    auto* doc = const_cast<cad::param::ParamDocument*>(m_paramDoc);
    const cad::param::Group* g = doc->findGroup(groupId);
    if (!g) return QString();
    return g->name.isEmpty() ? g->serial : g->name;
}

bool ToolSelect::canMakeGroupWithToast()
{
    if (!m_paramDoc) return false;
    if (m_selection.size() < 2) {
        showToast(QString::fromUtf8("\xe8\x87\xb3\xe5\xb0\x91\xe9\x80\x89\xe4\xb8\xad\xe4\xb8\xa4\xe6\x9d\xa1\xe7\xba\xbf\xe6\x89\x8d\xe8\x83\xbd\xe6\x88\x90\xe7\xbb\x84"));  // 至少选中两条线才能成组
        return false;
    }
    int layer = -1;
    for (const QUuid& id : m_selection) {
        const auto* b = m_paramDoc->findBlock(id);
        if (!b) return false;
        const QUuid gid = m_paramDoc->groupOfBlock(id);
        if (!gid.isNull()) {
            showToast(QString::fromUtf8("\xe9\x80\x89\xe4\xb8\xad\xe9\x9b\x86\xe5\x8c\x85\xe5\x90\xab\xe7\xbb\x84 ")  // 选中集包含组
                      + groupLabel(gid)
                      + QString::fromUtf8("\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe8\xa7\xa3\xe6\x95\xa3\xe5\x86\x8d\xe9\x87\x8d\xe6\x96\xb0\xe6\x88\x90\xe7\xbb\x84"));  // ，请先解散再重新成组
            return false;
        }
        if (layer < 0) layer = b->layer;
        else if (b->layer != layer) {
            showToast(QString::fromUtf8("\xe7\xbb\x84\xe6\x88\x90\xe5\x91\x98\xe5\xbf\x85\xe9\xa1\xbb\xe5\x9c\xa8\xe5\x90\x8c\xe4\xb8\x80\xe5\x9b\xbe\xe5\xb1\x82"));  // 组成员必须在同一图层
            return false;
        }
    }
    return true;
}

QUuid ToolSelect::wholeSelectedGroup() const
{
    if (!m_paramDoc || m_selection.isEmpty()) return QUuid();
    const QUuid gid = m_paramDoc->groupOfBlock(*m_selection.cbegin());
    if (gid.isNull()) return QUuid();
    const QList<QUuid> members = m_paramDoc->blocksInGroup(gid);
    const QSet<QUuid> memberSet(members.begin(), members.end());
    return (memberSet == m_selection) ? gid : QUuid();
}

void ToolSelect::showContextMenu()
{
    if (!m_paramDoc || m_selection.isEmpty()) return;

    QMenu menu;
    QAction* actGroup   = menu.addAction(QString::fromUtf8("\xe5\x81\x9a\xe6\x88\x90\xe7\xbb\x84"));   // 做成组
    QAction* actUngroup = menu.addAction(QString::fromUtf8("\xe8\xa7\xa3\xe6\x95\xa3\xe7\xbb\x84"));   // 解散组
    QAction* actRename  = menu.addAction(QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"));  // 重命名组
    menu.addSeparator();
    QAction* actCancel  = menu.addAction(QString::fromUtf8("\xe5\x8f\x96\xe6\xb6\x88\xe9\x80\x89\xe6\x8b\xa9"));  // 取消选择

    // Enable states: 做成组 needs >= 2 ungrouped same-layer blocks; 解散组
    // needs the selection to be EXACTLY one whole group.
    {
        bool ok = m_selection.size() >= 2;
        int layer = -1;
        for (const QUuid& id : m_selection) {
            const auto* b = m_paramDoc->findBlock(id);
            if (!b || !m_paramDoc->groupOfBlock(id).isNull()) { ok = false; break; }
            if (layer < 0) layer = b->layer;
            else if (b->layer != layer) { ok = false; break; }
        }
        actGroup->setEnabled(ok);
    }
    actUngroup->setEnabled(!wholeSelectedGroup().isNull());
    actRename->setEnabled(!wholeSelectedGroup().isNull());

    QAction* picked = menu.exec(QCursor::pos());
    if (picked == actGroup)          makeGroupFromSelection();
    else if (picked == actUngroup)   ungroupSelection();
    else if (picked == actRename)    renameSelectedGroup();
    else if (picked == actCancel)    clearSelectionAndIdle();
}

void ToolSelect::makeGroupFromSelection()
{
    if (!m_paramDoc || !m_undoStack) return;
    if (!canMakeGroupWithToast()) return;

    m_undoStack->push(new cad::cmd::MakeGroupCommand(m_paramDoc, m_selection.values()));
    if (m_scene) m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}

void ToolSelect::ungroupSelection()
{
    if (!m_paramDoc || !m_undoStack) return;
    const QUuid gid = wholeSelectedGroup();
    if (gid.isNull()) {
        showToast(QString::fromUtf8("\xe8\xaf\xb7\xe9\x80\x89\xe4\xb8\xad\xe6\x95\xb4\xe4\xb8\xaa\xe7\xbb\x84\xe5\x86\x8d\xe8\xa7\xa3\xe6\x95\xa3"));  // 请选中整个组再解散
        return;
    }
    m_undoStack->push(new cad::cmd::UngroupCommand(m_paramDoc, gid));
    if (m_scene) m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}

void ToolSelect::renameSelectedGroup()
{
    if (!m_paramDoc) return;
    const QUuid gid = wholeSelectedGroup();
    if (gid.isNull()) return;
    QString current;
    if (const auto* g = m_paramDoc->findGroup(gid))
        current = g->name;
    bool ok = false;
    const QString name = QInputDialog::getText(
        nullptr,
        QString::fromUtf8("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xbb\x84"),   // 重命名组
        QString::fromUtf8("\xe7\xbb\x84\xe5\x90\x8d\xe7\xa7\xb0"),               // 组名称
        QLineEdit::Normal, current, &ok);
    if (ok) {
        if (m_undoStack)
            m_undoStack->push(new cad::cmd::RenameGroupCommand(m_paramDoc, gid, name.trimmed()));
        else
            m_paramDoc->setGroupName(gid, name.trimmed());
    }
}

bool ToolSelect::badgeAt(const cad::geo::Vec2& pos) const
{
    if (!m_scene) return false;
    const QPointF scenePt = cad::geo::Coord::toScene(pos.x, pos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits)
        if (item->type() == GroupBadgeItem::Type)
            return true;
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drag (move confirmed selection; anchor = blank-space press point)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::beginDrag(const cad::geo::Vec2& pos)
{
    m_dragStartPos = pos;
    m_detachedAttachments.clear();

    // Drag set = the confirmed selection, EXPANDED by locked connections
    // (锁定 = 焊接): dragging either side of a locked pair moves the whole
    // pair, recursively (A锁B、B锁C → 拖A时B、C一起走). This also makes the
    // locked attachment span the drag set, so it is never torn apart below.
    const QSet<QUuid> dragSet = m_paramDoc->lockedClosure(m_selection);
    m_dragBlockIds = dragSet.values();
    m_dragOrigins.clear();
    for (const QUuid& id : m_dragBlockIds) {
        if (const auto* b = m_paramDoc->findBlock(id))
            m_dragOrigins.insert(id, b->transform.origin);
    }

    // Auto-disconnect (方向感知拆除): only when the FOLLOWER itself is
    // dragged away from its leader (fromIn && !toIn) is a connection torn
    // apart — 拖跟随线 = 拆散 (门开着); dragging the LEADER keeps the
    // follower attached so it follows (跟随线跟随, 不拆). Locked
    // attachments are welded: never detached by a drag. Groups stay
    // zero-restriction — 组内连接与自由连接同样处理 (组只是选择快捷方式).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isLocked) continue;
        const bool fromIn = dragSet.contains(att.fromBlockId);
        const bool toIn   = dragSet.contains(att.toBlockId);
        if (fromIn && !toIn)
            m_detachedAttachments.append(att.id);
    }

    setState(SelectState::Dragging);
}

void ToolSelect::updateDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_dragStartPos;
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_dragOrigins.value(id) + delta;
    }
    // Live resolve every frame: followers cascade in real time (same pattern
    // as updateConnect / updateAnchorDrag). resolveForDrag() emits resolved()
    // → the scene syncs block positions; panels stay silent until the gesture
    // commits (endDrag pushes an undo command whose resolveAll() refreshes).
    // Seeds = the dragged set; cross-selection attachments pending removal are
    // ignored so a dragged follower is not pulled back to its leader.
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            m_paramDoc->invalidateLayer(b->layer);
    }
    m_paramDoc->resolveForDrag(m_dragBlockIds, m_detachedAttachments);
}

void ToolSelect::endDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_dragStartPos;

    // Restore originals, then re-apply through the undo stack.
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_dragOrigins.value(id);
    }

    // A pure click (press + release without moving) is NOT a drag: keep the
    // selection confirmed — 单击已选对象保持选中, 按住移动才是拖动
    // (second-click-to-drag semantics; also keeps double-click edit intact).
    if (delta.lengthSquared() <= 1e-10) {
        if (m_scene) m_scene->refreshAllBlockItems();
        setState(SelectState::Confirmed);
        return;
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe7\xa7\xbb\xe5\x8a\xa8 %1 \xe4\xb8\xaa\xe5\xaf\xb9\xe8\xb1\xa1").arg(m_dragBlockIds.size()));
        for (const QUuid& attId : m_detachedAttachments)
            m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(m_paramDoc, attId));
        m_undoStack->push(new cad::cmd::MoveBlockCommand(m_paramDoc, m_dragBlockIds, delta));
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();

    // Requirement: after a move, drop the selection and return to Idle.
    clearSelectionAndIdle();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick copy (Ctrl+drag 快捷复制): clone → preview-drag → replay via undo
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::beginCopyDrag(const QUuid& hitBlockId, const cad::geo::Vec2& pos)
{
    // Copy set: the CONFIRMED selection — the gesture is only dispatched when
    // the hit belongs to it (复制仅对已确认选择集生效, 见 mousePress).
    QList<QUuid> ids = m_selection.values();
    if (!ids.contains(hitBlockId))
        ids = {hitBlockId};   // safety net — not reachable via normal dispatch

    m_copyResult = cad::param::duplicateBlocks(*m_paramDoc, ids);
    if (m_copyResult.isEmpty()) return;

    // Live preview: add the clones directly (no undo). The whole gesture is
    // replayed through the undo stack at release (restore-then-replay, same
    // pattern as finalizeConnection).
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->addLinked(lv);
    for (const auto& b : m_copyResult.blocks)
        m_paramDoc->addBlock(b);
    for (const auto& att : m_copyResult.attachments)
        m_paramDoc->addAttachment(att);
    // Group clone preview (副本成新组): register the clone group so guards
    // see it during the gesture; the command re-registers it on replay.
    if (m_copyResult.newGroup)
        m_paramDoc->restoreGroup(*m_copyResult.newGroup, m_copyResult.newGroupMembers);

    // Anchor the drag on the LIVE origins (followers were just resolver-placed).
    m_copyStartPos = pos;
    m_copyOrigins.clear();
    for (const auto& b : m_copyResult.blocks) {
        if (const auto* live = m_paramDoc->findBlock(b.id))
            m_copyOrigins.insert(b.id, live->transform.origin);
    }
    m_scene->refreshAllBlockItems();
    setState(SelectState::CopyDragging);
}

void ToolSelect::updateCopyDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_copyStartPos;
    for (auto it = m_copyOrigins.cbegin(); it != m_copyOrigins.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform.origin = it.value() + delta;
    }
    // Live resolve: copied followers cascade every frame (see updateDrag).
    // Seeds = the cloned set (their attachments live entirely inside it).
    for (auto it = m_copyOrigins.cbegin(); it != m_copyOrigins.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            m_paramDoc->invalidateLayer(b->layer);
    }
    m_paramDoc->resolveForDrag(m_copyOrigins.keys());
}

void ToolSelect::endCopyDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_copyStartPos;

    // Remove the preview clones, then replay the copy as ONE undo step at
    // the drop position. A zero-delta release (accidental Ctrl+click) drops
    // the copy — an exact overlap of the original would be invisible.
    removeCopyPreview();
    if (delta.lengthSquared() > 1e-10 && m_undoStack) {
        for (auto& b : m_copyResult.blocks)
            b.transform.origin = b.transform.origin + delta;
        m_undoStack->push(new cad::cmd::DuplicateBlocksCommand(
            m_paramDoc, std::move(m_copyResult)));
        // Same as a completed move: drop the selection lock (用户拍板：
        // 复制完成后退出选中锁定状态).
        m_copyResult = {};
        m_copyOrigins.clear();
        if (m_scene) m_scene->refreshAllBlockItems();
        clearSelectionAndIdle();
        return;
    }
    finishCopyDrag();
}

void ToolSelect::cancelCopyDrag()
{
    removeCopyPreview();
    finishCopyDrag();
}

void ToolSelect::removeCopyPreview()
{
    if (!m_paramDoc) return;
    // Clone group first (its members vanish right after).
    if (m_copyResult.newGroup)
        m_paramDoc->dissolveGroup(m_copyResult.newGroup->id);
    // Attachments first (removeBlock would drop them anyway); new linked
    // variables last — they reference ORIGINAL blocks and survive removeBlock.
    for (const auto& att : m_copyResult.attachments)
        m_paramDoc->removeAttachment(att.id);
    for (const auto& b : m_copyResult.blocks)
        m_paramDoc->removeBlock(b.id);
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->removeLinked(lv.id);
}

void ToolSelect::finishCopyDrag()
{
    m_copyResult = {};
    m_copyOrigins.clear();
    if (m_scene) m_scene->refreshAllBlockItems();
    // Cancelled / zero-delta copy: keep the confirmed selection so the user
    // can retry the gesture (a COMPLETED copy exits via clearSelectionAndIdle).
    if (!m_selection.isEmpty())
        setState(m_confirmed ? SelectState::Confirmed : SelectState::Selecting);
    else
        setState(SelectState::Idle);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Connection (attach) + angle HUD
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::beginConnect(const QUuid& fromBlockId, const QUuid& fromPointId,
                              const cad::geo::Vec2& pos)
{
    (void)pos;  // grab offset derives from the point's world position
    auto* blk = m_paramDoc->findBlock(fromBlockId);
    if (!blk || blk->isBridge) return;   // bridges are pinned at both ends

    m_connectFromBlock = fromBlockId;
    m_connectFromPoint = fromPointId;
    m_connectTarget.reset();
    m_connectOldAtt.reset();

    // Grab geometry: the block translates so its from-point tracks the cursor.
    m_connectOrigOrigin   = blk->transform.origin;
    m_connectOrigRotation = blk->transform.rotation;
    m_connectGrabOffset   = blk->transform.origin - blk->worldPos(fromPointId);

    // 快拆 (quick-detach): if this block currently follows a leader, release it
    // immediately so it can move freely. 组内连接同样可拆 (组零限制). The
    // removal is re-wrapped into the final undo macro at commit time
    // (restore-then-replay pattern).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == fromBlockId && !att.isPin && !att.isLocked) {
            m_connectOldAtt = att;
            m_paramDoc->removeAttachment(att.id);
            break;
        }
    }

    setState(SelectState::Connecting);
    updateConnectHalo();
}

void ToolSelect::updateConnect(const cad::geo::Vec2& pos)
{
    if (!m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Generous radius while connecting: dropping onto a target must feel easy.
    // Exclude the dragged block's OWN points — otherwise a nearby point of
    // the dragged line (e.g. its other endpoint) would shadow the real
    // target: findSnap returns the NEAREST point, and the old code reset the
    // snap AFTER the search, silently dropping the actual target.
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, kConnectSnapRadius,
                                      {}, &m_connectFromBlock);
    if (snap.has_value() && snap->blockId == m_connectFromBlock)
        snap.reset();                       // never snap to self

    // Only promise a connection (magnet + ring) when releasing would actually
    // attach — e.g. a descendant's point would close a cycle and is refused.
    bool willConnect = false;
    if (snap.has_value()) {
        cad::param::Attachment cand;
        cand.fromBlockId = m_connectFromBlock;
        cand.fromPointId = m_connectFromPoint;
        cand.toBlockId   = snap->blockId;
        cand.toPointId   = snap->pointId;
        willConnect = cad::param::checkAttachment(m_paramDoc->attachments(), cand)
                      == cad::param::AttachmentIssue::Ok;
    }
    m_connectTarget = willConnect ? snap : std::nullopt;

    // The block physically follows the cursor: from-point lands on the cursor
    // (or exactly on the snap target). Children cascade via resolveForDrag —
    // only the dragged block's subgraph moves (the old attachment, if any, is
    // already out of the document during the gesture).
    const cad::geo::Vec2 anchor = m_connectTarget.has_value() ? m_connectTarget->worldPos : pos;
    blk->transform.origin = anchor + m_connectGrabOffset;
    m_paramDoc->invalidateLayer(blk->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveForDrag({m_connectFromBlock});
    // resolveForDrag() emits resolved → scene syncs positions automatically;
    // no explicit full refresh needed here (was the main per-frame cost).
    updateConnectMarker();
    updateConnectHalo();
}

void ToolSelect::endConnect(const cad::geo::Vec2& pos)
{
    (void)pos;
    removeConnectMarker();
    removeConnectHalo();
    bool connected = false;

    if (m_connectTarget.has_value() && m_paramDoc) {
        // Overlapping-target disambiguation: gather every candidate within the
        // snap radius that would actually attach, then check whether several
        // of them sit on the SAME spot (e.g. endpoints of different blocks
        // stacked at one position). Ambiguous → ConfirmTarget: the user clicks
        // the intended leader segment (its endpoint on the connection spot is
        // the anchor, its id becomes toSegmentId).
        double zoom = 1.0;
        if (m_scene && !m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();
        const auto allCands = m_snapEngine.findSnapCandidates(
            pos, m_paramDoc, zoom, kConnectSnapRadius, {}, &m_connectFromBlock);
        std::vector<SnapResult> pool;
        for (const auto& c : allCands) {
            if (c.blockId == m_connectFromBlock) continue;
            cad::param::Attachment cand;
            cand.fromBlockId = m_connectFromBlock;
            cand.fromPointId = m_connectFromPoint;
            cand.toBlockId   = c.blockId;
            cand.toPointId   = c.pointId;
            if (cad::param::checkAttachment(m_paramDoc->attachments(), cand)
                    == cad::param::AttachmentIssue::Ok)
                pool.push_back(c);
        }

        if (!pool.empty()) {
            const cad::geo::Vec2 refPos = pool.front().worldPos;
            // Overlap set = candidates at the same spot as the nearest one.
            std::vector<SnapResult> overlap;
            for (const auto& c : pool)
                if (c.worldPos.distanceTo(refPos) < kOverlapEps)
                    overlap.push_back(c);

            if (overlap.size() > 1) {
                // Multiple points stacked here — ask the user to confirm the
                // leader by clicking one of the candidate segments.
                m_confirmCandidates = collectConfirmCandidates(refPos);
                if (!m_confirmCandidates.empty()) {
                    setState(SelectState::ConfirmTarget);
                    if (m_scene)
                        m_scene->showToast(QString::fromUtf8(
                            "连接位置存在多个重叠点：点选基准线段确认连接"));  // 连接位置存在多个重叠点：点选基准线段确认连接
                    return;
                }
            }
            // Single unambiguous target: connect directly.
            const SnapResult& target = overlap.empty() ? pool.front() : overlap.front();
            connected = attachToTarget(target.blockId, target.pointId, QUuid());
        }
    }

    if (!connected)
        commitConnectMove();   // released away from any target: plain move

    if (m_state != SelectState::AngleInput) {
        m_connectFromBlock = QUuid();
        m_connectFromPoint = QUuid();
        m_connectTarget.reset();
        m_connectOldAtt.reset();
    }
}

bool ToolSelect::attachToTarget(const QUuid& toBlockId, const QUuid& toPointId,
                                const QUuid& toSegmentId)
{
    if (!m_paramDoc) return false;
    auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    auto* toBlk   = m_paramDoc->findBlock(toBlockId);
    if (!fromBlk || !toBlk) return false;

    cad::param::Attachment att;
    att.fromBlockId = m_connectFromBlock;
    att.fromPointId = m_connectFromPoint;
    att.toBlockId   = toBlockId;
    att.toPointId   = toPointId;
    att.toSegmentId = !toSegmentId.isNull()
        ? toSegmentId : toBlk->exitSegmentAtPoint(toPointId);

    // Orientation-preserving follower angle: the Resolver drives
    //   rotation = refWorld + angle·π/180 − localDir
    // so choosing angle = (rotation + localDir − refWorld)·180/π keeps
    // the block's CURRENT world direction — zero visual jump on attach.
    const double refWorld = toBlk->transform.rotation
        + toBlk->exitDirectionAtPoint(toPointId, att.toSegmentId);
    const double localDir = fromBlk->directionAtPoint(m_connectFromPoint);
    const double angleDeg = cad::param::backSolveFollowerAngle(
        fromBlk->transform.rotation, localDir, refWorld);
    att.followerAngle = angleDeg;

    if (cad::param::checkAttachment(m_paramDoc->attachments(), att)
            != cad::param::AttachmentIssue::Ok)
        return false;

    // NOTE: 组对连接零限制 —— 无主连接预算, 组员自由建立连接.
    if (!m_paramDoc->addAttachment(att)) return false;
    // Added directly for live angle preview; finalizeConnection()
    // re-wraps everything into a single undoable macro.
    m_editingAttachmentId = att.id;
    m_initialAngle = angleDeg;
    m_paramDoc->resolveAll();
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        // Cross-layer feedback: toast at the gesture's success point (closest
        // to the user action; never fires on undo/redo replay).
        if (const QString toast = crossLayerToast(m_paramDoc, *fromBlk, *toBlk);
            !toast.isEmpty())
            showToast(toast);
        const cad::geo::Vec2 anchor = m_connectTarget.has_value()
            ? m_connectTarget->worldPos : toBlk->worldPos(toPointId);
        showAngleHud(anchor);
    }
    setState(SelectState::AngleInput);
    return true;
}

std::vector<ConfirmCandidate> ToolSelect::collectConfirmCandidates(
    const cad::geo::Vec2& connWorldPos) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == m_connectFromBlock) continue;
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const cad::geo::Vec2 local = block.transform.toLocal(connWorldPos);
            if (sp && sp->resolved
                && sp->resolvedPos.distanceTo(local) < kOverlapEps)
                out.push_back({block.id, seg.id, sp->id});
            if (ep && ep->resolved
                && ep->resolvedPos.distanceTo(local) < kOverlapEps)
                out.push_back({block.id, seg.id, ep->id});
        }
    }
    return out;
}

void ToolSelect::tryConfirmTarget(const cad::geo::Vec2& pos)
{
    if (!m_paramDoc || !m_scene) { cancelConnect(); return; }
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_confirmCandidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                removeConfirmHighlight();
                m_confirmCandidates.clear();
                if (attachToTarget(cand.blockId, cand.pointId, cand.segId))
                    return;   // → AngleInput
                break;        // rejected (cycle etc.) → cancel below
            }
        }
    }
    // Blank or non-candidate click: abort the whole gesture.
    removeConfirmHighlight();
    m_confirmCandidates.clear();
    cancelConnect();
}

void ToolSelect::updateConfirmHighlight(const cad::geo::Vec2& pos)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    QUuid hitBlock, hitSeg;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_confirmCandidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                hitBlock = cand.blockId;
                hitSeg = cand.segId;
                break;
            }
        }
    }

    if (hitSeg.isNull()) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }
    const auto* blk = m_paramDoc->findBlock(hitBlock);
    const auto* seg = blk ? blk->findSegment(hitSeg) : nullptr;
    const auto* sp = seg ? blk->findPoint(seg->startPointId) : nullptr;
    const auto* ep = seg ? blk->findPoint(seg->endPointId) : nullptr;
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    if (!m_confirmHighlight) {
        m_confirmHighlight = new QGraphicsPathItem();
        QPen pen(QColor(0xF39C12), 3.0);
        pen.setCosmetic(true);
        m_confirmHighlight->setPen(pen);
        m_confirmHighlight->setBrush(Qt::NoBrush);
        m_confirmHighlight->setZValue(101.0);
        m_scene->addItem(m_confirmHighlight);
    }
    QPainterPath path;
    path.moveTo(cad::geo::Coord::toScene(blk->worldPos(sp->id)));
    path.lineTo(cad::geo::Coord::toScene(blk->worldPos(ep->id)));
    m_confirmHighlight->setPath(path);
    m_confirmHighlight->setVisible(true);
}

void ToolSelect::removeConfirmHighlight()
{
    if (m_confirmHighlight) {
        m_confirmHighlight->setVisible(false);
        delete m_confirmHighlight;
        m_confirmHighlight = nullptr;
    }
}

void ToolSelect::commitConnectMove()
{
    auto* blk = m_paramDoc ? m_paramDoc->findBlock(m_connectFromBlock) : nullptr;
    if (!blk) {
        setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Confirmed);
        return;
    }

    const cad::geo::Vec2 delta = blk->transform.origin - m_connectOrigOrigin;

    // Restore the pre-drag state, then replay through the undo stack so the
    // whole gesture (quick-detach + move) is one undo step.
    blk->transform.origin   = m_connectOrigOrigin;
    blk->transform.rotation = m_connectOrigRotation;
    if (m_connectOldAtt)
        m_paramDoc->addAttachment(*m_connectOldAtt);

    if (m_undoStack && (m_connectOldAtt || delta.lengthSquared() > 1e-10)) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe6\x8b\x86\xe5\xbc\x80\xe5\xb9\xb6\xe7\xa7\xbb\xe5\x8a\xa8"));  // 拆开并移动
        if (m_connectOldAtt)
            m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                m_paramDoc, m_connectOldAtt->id));
        if (delta.lengthSquared() > 1e-10)
            m_undoStack->push(new cad::cmd::MoveBlockCommand(
                m_paramDoc, {m_connectFromBlock}, delta));
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Confirmed);
}

void ToolSelect::cancelConnect()
{
    // Abort the in-progress connect drag: restore the exact pre-drag state
    // (transform + old attachment) as if the gesture never started.
    if (m_paramDoc) {
        if (auto* blk = m_paramDoc->findBlock(m_connectFromBlock)) {
            blk->transform.origin   = m_connectOrigOrigin;
            blk->transform.rotation = m_connectOrigRotation;
        }
        if (m_connectOldAtt)
            m_paramDoc->addAttachment(*m_connectOldAtt);
        m_paramDoc->resolveAll();
    }
    if (m_scene) m_scene->refreshAllBlockItems();

    removeConnectMarker();
    removeConnectHalo();
    removeConfirmHighlight();
    m_confirmCandidates.clear();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    m_connectOldAtt.reset();
    setState(SelectState::Confirmed);
}

// ── Angle HUD ──

void ToolSelect::showAngleHud(const cad::geo::Vec2& anchorUser)
{
    if (!m_scene || m_scene->views().isEmpty()) return;
    QGraphicsView* view = m_scene->views().first();
    QWidget* viewport = view->viewport();

    if (!m_angleHud) {
        m_angleHud = new AngleHud(viewport);
        m_angleHud->onTextChanged = [this](const QString& t) { onAngleTextChanged(t); };
        m_angleHud->onCommit      = [this] { commitAngle(); };
        m_angleHud->onCancel      = [this] { cancelAngle(); };
        m_angleHud->onModeChanged = [this](cad::param::RotationMode m) { onAngleModeChanged(m); };
    } else {
        m_angleHud->setParent(viewport);
    }

    // Start in angle mode (fresh connection always begins as angle).
    m_angleMode = cad::param::RotationMode::Angle;
    m_angleHud->setMode(m_angleMode);

    // Position near the connection point (user → scene → viewport pixels).
    const QPointF scenePt = cad::geo::Coord::toScene(anchorUser.x, anchorUser.y);
    const QPoint vpPt = view->mapFromScene(scenePt);
    m_angleHud->move(vpPt + QPoint(16, 16));
    m_angleHud->adjustSize();

    m_angleValid = true;
    m_angleHud->setValid(true);
    m_angleHud->edit()->blockSignals(true);
    m_angleHud->edit()->setText(formatDeg(m_initialAngle));  // pre-fill current angle
    m_angleHud->edit()->blockSignals(false);
    m_angleHud->show();
    m_angleHud->edit()->setFocus();
    m_angleHud->edit()->selectAll();  // typing immediately replaces the value
}

void ToolSelect::hideAngleHud()
{
    if (m_angleHud) {
        m_angleHud->hide();
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->setFocus();
    }
}

void ToolSelect::onAngleTextChanged(const QString& text)
{
    if (!m_paramDoc) return;

    // Locate the attachment being tuned.
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    cad::param::Attachment* att = nullptr;
    for (auto& a : atts) {
        if (a.id == m_editingAttachmentId) { att = &a; break; }
    }
    if (!att) return;

    const QString t = text.trimmed();
    if (t.isEmpty()) {
        // Empty input = keep the orientation-preserving initial angle.
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = m_initialAngle;
        att->followerAngleFormula.clear();
        m_angleMode = cad::param::RotationMode::Angle;
        m_angleValid = true;
    } else {
        bool isNumber = false;
        const double numVal = t.toDouble(&isNumber);
        if (isNumber) {
            if (m_angleMode == cad::param::RotationMode::ArcLength) {
                att->rotationMode = cad::param::RotationMode::ArcLength;
                att->arcLength = geo::Units::cmToMm(numVal);
                att->arcLengthFormula.clear();
            } else {
                att->rotationMode = cad::param::RotationMode::Angle;
                att->followerAngle = numVal;
                att->followerAngleFormula.clear();
            }
            m_angleValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                t, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_angleMode == cad::param::RotationMode::ArcLength) {
                    att->rotationMode = cad::param::RotationMode::ArcLength;
                    att->arcLength = geo::Units::cmToMm(r.value);
                    att->arcLengthFormula = t;
                } else {
                    att->rotationMode = cad::param::RotationMode::Angle;
                    att->followerAngle = r.value;
                    att->followerAngleFormula = t;
                }
                m_angleValid = true;
            } else {
                m_angleValid = false;             // keep last valid geometry
            }
        }
    }

    if (m_angleHud) m_angleHud->setValid(m_angleValid);
    if (m_angleValid) {
        // Per-frame preview: only the connected block's layer group moves.
        if (const auto* fb = m_paramDoc->findBlock(att->fromBlockId))
            m_paramDoc->invalidateLayer(fb->layer);
        m_paramDoc->resolveAll();                 // live rotation preview
        m_scene->refreshAllBlockItems();
    }
}

void ToolSelect::onAngleModeChanged(cad::param::RotationMode mode)
{
    if (!m_paramDoc) return;
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    cad::param::Attachment* att = nullptr;
    for (auto& a : atts) {
        if (a.id == m_editingAttachmentId) { att = &a; break; }
    }
    if (!att) return;

    // Geometry-preserving switch: compute current effective angle, then
    // convert to the new mode's value.
    double curDeg = att->followerAngle;
    if (att->rotationMode == cad::param::RotationMode::ArcLength) {
        // Current is arc length → derive angle. Arc is measured from the
        // REVERSE direction: 弧长 0 = 角度 180°.
        const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
        double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
        double arcMm = att->arcLength;
        if (!att->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att->arcLengthFormula, m_paramDoc->parameters(), {});
            if (r.ok) arcMm = geo::Units::cmToMm(r.value);
        }
        curDeg = (radius > 1e-9) ? 180.0 + (arcMm / radius) * 180.0 / M_PI : 180.0;
    } else if (!att->followerAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            att->followerAngleFormula, m_paramDoc->parameters(), {});
        if (r.ok) curDeg = r.value;
    }

    if (mode == cad::param::RotationMode::ArcLength) {
        const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
        double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
        att->rotationMode = cad::param::RotationMode::ArcLength;
        // Arc is measured from the REVERSE direction (弧长 0 = 角度 180°),
        // normalized to [0, 360°).
        double degFromReverse = std::fmod(curDeg - 180.0, 360.0);
        if (degFromReverse < 0.0) degFromReverse += 360.0;
        att->arcLength = degFromReverse * M_PI / 180.0 * radius;
        att->arcLengthFormula.clear();
    } else {
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = curDeg;
        att->followerAngleFormula.clear();
    }
    m_angleMode = mode;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();

    // Refresh HUD text to show the converted value.
    if (m_angleHud) {
        m_angleHud->edit()->blockSignals(true);
        if (mode == cad::param::RotationMode::ArcLength) {
            m_angleHud->edit()->setText(formatDeg(geo::Units::mmToCm(att->arcLength)));
        } else {
            m_angleHud->edit()->setText(formatDeg(att->followerAngle));
        }
        m_angleHud->edit()->blockSignals(false);
    }
}

void ToolSelect::commitAngle()
{
    if (!m_angleValid) return;   // ignore Enter on an invalid formula
    finalizeConnection();
}

void ToolSelect::cancelAngle()
{
    // Keep the connection but revert to the orientation-preserving angle that
    // was computed at attach time (the block keeps its dragged orientation).
    if (m_paramDoc) {
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id == m_editingAttachmentId) {
                a.rotationMode = cad::param::RotationMode::Angle;
                a.followerAngle = m_initialAngle;
                a.followerAngleFormula.clear();
                a.arcLength = 0.0;
                a.arcLengthFormula.clear();
                break;
            }
        }
        m_paramDoc->resolveAll();
    }
    finalizeConnection();
}

void ToolSelect::finalizeConnection()
{
    hideAngleHud();

    // Snapshot the tuned attachment, then restore the COMPLETE pre-drag state
    // (transform + old attachment) and replay the whole gesture through the
    // undo stack: "quick-detach + connect + angle" becomes one undo step.
    if (m_paramDoc && m_undoStack) {
        cad::param::Attachment snapshot;
        bool found = false;
        for (const auto& a : m_paramDoc->attachments()) {
            if (a.id == m_editingAttachmentId) { snapshot = a; found = true; break; }
        }
        if (found) {
            m_paramDoc->removeAttachment(m_editingAttachmentId);
            if (auto* blk = m_paramDoc->findBlock(snapshot.fromBlockId)) {
                blk->transform.origin   = m_connectOrigOrigin;
                blk->transform.rotation = m_connectOrigRotation;
            }
            if (m_connectOldAtt)
                m_paramDoc->addAttachment(*m_connectOldAtt);

            m_undoStack->beginMacro(QStringLiteral(
                "\xe5\xbb\xba\xe7\xab\x8b\xe8\xbf\x9e\xe6\x8e\xa5"));  // 建立连接
            if (m_connectOldAtt)
                m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                    m_paramDoc, m_connectOldAtt->id));
            m_undoStack->push(new cad::cmd::AddAttachmentCommand(m_paramDoc, snapshot));
            m_undoStack->endMacro();
        }
    }

    m_editingAttachmentId = QUuid();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    m_connectOldAtt.reset();
    if (m_scene) m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hit testing
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<SnapResult> ToolSelect::hitPoint(const cad::geo::Vec2& worldPos) const
{
    if (!m_paramDoc) return std::nullopt;
    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    // Same generous radius as the connect snap: grabbing a point to start
    // a connection must feel as easy as dropping onto a target.
    return m_snapEngine.findSnap(worldPos, m_paramDoc, zoom, kConnectSnapRadius);
}

bool ToolSelect::tryPointOperation(const cad::geo::Vec2& pos)
{
    if (m_selection.isEmpty()) return false;

    // Curve anchor first: press near a pass point of any SELECTED curve.
    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    const auto anchorHit = hitCurveAnchor(pos, zoom);
    if (anchorHit) {
        beginAnchorDrag(anchorHit->first, anchorHit->second);
        return true;
    }

    // Endpoint: begin a connection drag. The hit point must belong to the
    // SELECTED set — multi-select members connect individually (长按选中集内
    // 任一端点 = 该块的连接手势), while an unselected block's point falls
    // through so the caller starts a plain drag / selection switch instead.
    // Bridges cannot connect — fall through so the caller starts a plain
    // drag instead.
    const auto pt = hitPoint(pos);
    if (pt.has_value() && m_selection.contains(pt->blockId)) {
        if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(pt->blockId) : nullptr;
            blk && blk->isBridge)
            return false;
        // Locked follower: the endpoint drag would need to detach first, but
        // locked connections are welded — fall through so the caller starts
        // a block drag instead (the whole locked pair moves together).
        bool lockedFollower = false;
        for (const auto& att : m_paramDoc->attachments())
            if (att.isLocked && att.fromBlockId == pt->blockId) { lockedFollower = true; break; }
        if (lockedFollower)
            return false;
        beginConnect(pt->blockId, pt->pointId, pos);
        return true;
    }
    return false;
}

QUuid ToolSelect::hitBlock(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        if (auto* bi = BlockItem::containingItem(item)) {
            // Only blocks on the active layer are selectable.
            if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(bi->blockId()) : nullptr;
                blk && blk->layer == m_paramDoc->activeLayer())
                return bi->blockId();
        }
    }
    return QUuid();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visual helpers
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::removeMarqueeRect()
{
    if (m_marqueeItem) {
        if (m_scene) m_scene->removeItem(m_marqueeItem);
        delete m_marqueeItem;
        m_marqueeItem = nullptr;
    }
}

void ToolSelect::updateConnectMarker()
{
    if (!m_scene) return;
    if (!m_connectTarget.has_value()) {
        removeConnectMarker();
        return;
    }

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    // The ring is the SAME size as the snap radius — the magnet's reach made
    // visible: releasing anywhere inside this ring connects to this point.
    const double r = kConnectSnapRadius / zoom;

    if (!m_connectMarker) {
        m_connectMarker = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));          // teal: "release = connect"
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_connectMarker->setPen(pen);
        m_connectMarker->setBrush(QColor(38, 166, 154, 50));
        m_connectMarker->setZValue(9999);
        m_scene->addItem(m_connectMarker);
    }
    const QPointF c = cad::geo::Coord::toScene(m_connectTarget->worldPos.x,
                                               m_connectTarget->worldPos.y);
    m_connectMarker->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectMarker->show();
}

void ToolSelect::removeConnectMarker()
{
    if (m_connectMarker) {
        if (m_scene) m_scene->removeItem(m_connectMarker);
        delete m_connectMarker;
        m_connectMarker = nullptr;
    }
}

// ── Source-point halo (连接源点光环) ──
// The dashed ring around the dragged point IS the connect reach: its radius
// equals the snap radius, so the moment its edge touches any target point
// the snap fires (and the target ring appears on that point). 所见即所判.

void ToolSelect::updateConnectHalo()
{
    if (!m_scene || !m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    const double r = kConnectSnapRadius / zoom;  // halo == connect reach

    if (!m_connectHalo) {
        m_connectHalo = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_connectHalo->setPen(pen);
        m_connectHalo->setBrush(QColor(38, 166, 154, 20));  // faint fill (~8%)
        m_connectHalo->setZValue(9998);                     // under the snap ring
        m_scene->addItem(m_connectHalo);
    }

    const cad::geo::Vec2 src = blk->worldPos(m_connectFromPoint);
    const QPointF c = cad::geo::Coord::toScene(src.x, src.y);
    m_connectHalo->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectHalo->show();
}

void ToolSelect::removeConnectHalo()
{
    if (m_connectHalo) {
        if (m_scene) m_scene->removeItem(m_connectHalo);
        delete m_connectHalo;
        m_connectHalo = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Delete
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::deleteSelectedBlocks()
{
    if (!m_scene || !m_paramDoc || m_selection.isEmpty()) return;

    const QList<QUuid> toRemove = m_selection.values();

    // NOTE: 组对删除零限制 —— 选中集可包含组的任意部分成员, 删除照常执行
    // (组只是选择快捷方式, 不提供结构保护; 剩余成员不足 2 时组自动解散).

    // Show the aggregated delete-impact report before committing (批量删除
    // 时善后可能合并, 报告计数为上限). The cascade itself is unchanged.
    if (!cad::doc::confirmDeleteImpact(nullptr, m_paramDoc, toRemove))
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

// ---------------------------------------------------------------------------
// Curve anchor dragging
// ---------------------------------------------------------------------------

std::optional<std::pair<QUuid, QUuid>> ToolSelect::hitCurveAnchor(
    const cad::geo::Vec2& worldPos, double zoom) const
{
    if (!m_paramDoc || m_selection.isEmpty()) return std::nullopt;

    const double radius = 10.0 / std::max(zoom, 1e-9);  // 10px screen radius
    const double rSq = radius * radius;

    for (const QUuid& blockId : m_selection) {
        const auto* block = m_paramDoc->findBlock(blockId);
        if (!block) continue;

        for (const auto& seg : block->segments) {
            if (!seg.isCurve()) continue;
            for (const auto& ppId : seg.passPointIds) {
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                // CurveAnchor points are dragged with the SmartPen (parametric
                // percent/offset); dragging them here would convert them to Free
                // points and break the chord link. Skip them.
                if (pp->constraint == cad::param::PointConstraint::CurveAnchor) continue;
                cad::geo::Vec2 w = block->transform.toWorld(pp->resolvedPos);
                if (worldPos.distanceSquaredTo(w) < rSq)
                    return std::make_pair(blockId, ppId);
            }
        }
    }
    return std::nullopt;
}

void ToolSelect::beginAnchorDrag(const QUuid& blockId, const QUuid& pointId)
{
    auto* block = m_paramDoc->findBlock(blockId);
    if (!block) return;
    auto* pt = block->findPoint(pointId);
    if (!pt) return;

    m_anchorDragging = true;
    m_anchorBlockId = blockId;
    m_anchorPointId = pointId;
    m_anchorOrigPos = pt->freePos;
}

void ToolSelect::updateAnchorDrag(const cad::geo::Vec2& worldPos)
{
    if (!m_anchorDragging || !m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_anchorBlockId);
    if (!block) return;
    auto* pt = block->findPoint(m_anchorPointId);
    if (!pt) return;

    // Convert world cursor to local coords and set as new freePos
    cad::geo::Vec2 local = block->transform.toLocal(worldPos);
    pt->freePos = local;
    pt->constraint = cad::param::PointConstraint::Free;

    m_paramDoc->invalidateLayer(block->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveForDrag({m_anchorBlockId});
    // resolveForDrag() emits resolved() → CanvasScene::syncBlockPositions(), which
    // rebuilds ONLY the blocks whose geometryEpoch changed. The old
    // refreshAllBlockItems() here rebuilt EVERY block item (all curves' C2
    // solve + cache) on every mouse-move — a full-document duplicate of the
    // sync that the signal already performs.
}

void ToolSelect::endAnchorDrag()
{
    if (!m_anchorDragging) return;
    m_anchorDragging = false;

    // Push undo command (old position → new position)
    if (m_undoStack && m_paramDoc) {
        auto* block = m_paramDoc->findBlock(m_anchorBlockId);
        auto* pt = block ? block->findPoint(m_anchorPointId) : nullptr;
        if (pt) {
            m_undoStack->push(new cad::cmd::MovePointCommand(
                m_paramDoc, m_anchorBlockId, m_anchorPointId,
                m_anchorOrigPos, pt->freePos));
        }
    }

    m_anchorBlockId = QUuid();
    m_anchorPointId = QUuid();
}

} // namespace cad::tools
