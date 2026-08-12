#include "ToolSelect.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsRectItem>
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
#include "LinePropertyDialog.h"
#include "ConnectGesture.h"
#include "CopyDragController.h"
#include "MarqueeGesture.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/GroupCommands.h"
#include "document/DeleteImpactConfirm.h"

namespace cad::tools {

namespace {

/// True if segment [p1,p2] intersects (or is contained in) rect r.
/// Liang-Barsky clipping; also counts either endpoint inside the rect.
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = SelectState::Idle;

    // (Re)create the extracted gestures with the current context. The tools
    // forward their state transitions / selection queries through callbacks.
    // (The document may be null during ToolManager construction; the gestures
    // no-op on missing doc until setParamDocument wires the real one in.)
    delete m_connectGesture;
    delete m_copyDrag;
    QUndoStack* const undo = m_paramDoc ? m_paramDoc->undoStack() : nullptr;
    m_connectGesture = new ConnectGesture(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this](const QString& t) { showToast(t); },
        [this]() { clearSelectionAndIdle(); },
        [this]() { return m_selection.isEmpty(); });
    m_copyDrag = new CopyDragController(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this]() {
            if (!m_selection.isEmpty())
                setState(m_confirmed ? SelectState::Confirmed
                                     : SelectState::Selecting);
            else
                setState(SelectState::Idle);
        },
        [this]() { clearSelectionAndIdle(); });
    m_marqueeGesture = new MarqueeGesture(m_scene, m_paramDoc);
}

void ToolSelect::deactivate()
{
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();       // tool switch mid-copy: drop the clones
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();  // abort any in-progress connection
    delete m_connectGesture;
    m_connectGesture = nullptr;
    delete m_copyDrag;
    m_copyDrag = nullptr;
    delete m_marqueeGesture;
    m_marqueeGesture = nullptr;

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
    notifyEditTarget();
}

void ToolSelect::clearSelectionOnLayerChange()
{
    // Selection is scoped to the active layer: switching layers drops it so a
    // stale (now grayed) selection can never be dragged or edited.
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
    // Abort any in-flight gesture first (same hygiene as a layer switch).
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_marqueeGesture) m_marqueeGesture->cancel();

    m_selection = MarqueeGesture::expandWithGroups(
        m_paramDoc, QSet<QUuid>(blockIds.begin(), blockIds.end()));
    m_confirmed = !m_selection.isEmpty();
    syncSelectionVisual();
    setState(m_confirmed ? SelectState::Confirmed : SelectState::Idle);
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
    notifyEditTarget();
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

    const QPointF up = event->scenePos();            // user coords (+Y up)
    const cad::geo::Vec2 pos(up.x(), up.y());

    // ── Connect gesture owns input while active (Connecting / ConfirmTarget /
    //    AngleInput): the HUD takes all input, ConfirmTarget clicks resolve
    //    the overlap, Connecting presses are button-already-down no-ops. ──
    if (m_connectGesture && m_connectGesture->active()) {
        if (m_state == SelectState::AngleInput) return;  // HUD owns input
        if (event->button() == Qt::RightButton) {
            m_connectGesture->cancel();  // right-click cancels the gesture
            return;
        }
        if (event->button() == Qt::LeftButton
            && m_state == SelectState::ConfirmTarget)
            m_connectGesture->pressConfirmTarget(pos);
        return;
    }

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
        && m_state == SelectState::Confirmed && m_copyDrag) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull() && m_selection.contains(blockHit)) {
            m_copyDrag->begin(m_selection, blockHit, pos);
            return;
        }
    }

    switch (m_state) {
    case SelectState::Idle:
    case SelectState::Selecting: {
        const QUuid blockHit = hitBlock(pos);
        if (m_selectionMode == SelectionMode::Single) {
            // Single mode: press SELECTS the object only — 右键确认后才可
            // 拖动/连接 (与多选同一套 选中→确认→操作 语义, 减少误操作).
            // 点击空白清选; 无框选.
            if (!blockHit.isNull()) {
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = wholeGroupSet(blockHit);
                m_confirmed = false;
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
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
        if (m_marqueeGesture) {
            m_marqueeGesture->begin(pos, m_selection);
            // MUST enter the Marquee state here — mouseMove/mouseRelease route
            // into the gesture only when m_state == Marquee. Regression: the
            // gesture extraction dropped this transition, leaving the rubber
            // band invisible and the toggle never applied (W 进多选后框选消失).
            setState(SelectState::Marquee);
        }
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
        break;  // Marquee / Dragging / CopyDragging / Connecting: button already down.
    }
}

void ToolSelect::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Extracted gestures first (connect → copy), then the anchor-drag
    // override, then the built-in states.
    if (m_connectGesture && m_connectGesture->active()) {
        m_connectGesture->move(pos);
        return;
    }
    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->move(pos);
        return;
    }

    // Curve anchor drag override
    if (m_anchorDragging) { updateAnchorDrag(pos); return; }

    switch (m_state) {
    case SelectState::Marquee:
        if (m_marqueeGesture) m_marqueeGesture->update(pos);
        break;
    case SelectState::Dragging:     updateDrag(pos);     break;
    default: break;
    }
}

void ToolSelect::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Extracted gestures first (copy → connect), then the anchor-drag
    // override, then the built-in states.
    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->release(pos);
        return;
    }
    if (m_connectGesture && m_connectGesture->active()
        && m_state == SelectState::Connecting) {
        m_connectGesture->release(pos);
        return;
    }

    // Curve anchor drag override
    if (m_anchorDragging) { endAnchorDrag(); return; }

    switch (m_state) {
    case SelectState::Marquee: {
        // The gesture applies the toggle (base XOR hits, group-expanded);
        // the tool owns the selection-state transitions.
        if (m_marqueeGesture)
            m_selection = m_marqueeGesture->end(pos);
        m_confirmed = false;
        syncSelectionVisual();
        setState(m_selection.isEmpty() ? SelectState::Idle
                                       : SelectState::Selecting);
        break;
    }
    case SelectState::Dragging:     endDrag(pos);     break;
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
    // NOTE: no WA_DeleteOnClose — the dialog schedules its own deleteLater()
    // on accept/reject (ElaAppBar's default-close path would delete it while
    // its own handler still reads window->windowHandle()).
    dlg->show();
}

void ToolSelect::keyPress(QKeyEvent* event)
{
    // Connect gesture owns keys while active (AngleInput HUD + Esc cancels).
    // Only keys the gesture CONSUMED are swallowed — anything else (Ctrl+Z /
    // Delete / tool hotkeys) must fall through to the normal handling.
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
        if (m_state == SelectState::Dragging) {
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
            if (m_marqueeGesture) m_marqueeGesture->cancel();
            clearSelectionAndIdle();
        }
        event->accept();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Marquee selection (intersect = select; toggle add/remove)
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

QString ToolSelect::groupLabel(const QUuid& groupId)
{
    if (!m_paramDoc) return QString();
    const cad::param::Group* g = m_paramDoc->findGroup(groupId);
    if (!g) return QString();
    return g->name.isEmpty() ? g->serial : g->name;
}

bool ToolSelect::selectionGroupable() const
{
    // Pure rule (no toast): >= 2 ungrouped blocks on one layer. The menu
    // enable state and canMakeGroupWithToast share this single rule — keep
    // the two loops in sync when the grouping rules change.
    if (!m_paramDoc || m_selection.size() < 2) return false;
    QUuid layer;
    for (const QUuid& id : m_selection) {
        const auto* b = m_paramDoc->findBlock(id);
        if (!b || !m_paramDoc->groupOfBlock(id).isNull()) return false;
        if (layer.isNull()) layer = b->layer;
        else if (b->layer != layer) return false;
    }
    return true;
}

bool ToolSelect::canMakeGroupWithToast()
{
    // Same rules as selectionGroupable, but reports the specific reason via
    // a toast (the caller wants feedback, not just a boolean).
    if (!m_paramDoc) return false;
    if (m_selection.size() < 2) {
        showToast(QString::fromUtf8("\xe8\x87\xb3\xe5\xb0\x91\xe9\x80\x89\xe4\xb8\xad\xe4\xb8\xa4\xe6\x9d\xa1\xe7\xba\xbf\xe6\x89\x8d\xe8\x83\xbd\xe6\x88\x90\xe7\xbb\x84"));  // 至少选中两条线才能成组
        return false;
    }
    QUuid layer;
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
        if (layer.isNull()) layer = b->layer;
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
    actGroup->setEnabled(selectionGroupable());
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

    // Drag set = the confirmed selection, EXPANDED by protected connections
    // (拖动保护 = 焊接): dragging either side of a protected pair moves the
    // whole pair, recursively (A保B、B保C → 拖A时B、C一起走). This also makes
    // the protected attachment span the drag set, so it is never torn apart
    // below.
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
    // follower attached so it follows (跟随线跟随, 不拆). 拖动保护
    // (isLocked) attachments are welded: never detached by a drag.
    // Groups stay zero-restriction — 组内连接与自由连接同样处理 (组只是选择
    // 快捷方式). 注: 新建连接默认已勾选拖动保护 (addAttachment 统一置位),
    // 此处只对用户手动取消保护的连接生效.
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
    const auto pt = m_connectGesture ? m_connectGesture->hitPoint(pos)
                                     : std::optional<SnapResult>();
    if (pt.has_value() && m_selection.contains(pt->blockId)) {
        if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(pt->blockId) : nullptr;
            blk && blk->isBridge)
            return false;
        // Welded endpoint: the point participates in a LOCKED attachment —
        // either as the follower block (拖动保护焊接: the endpoint drag would
        // need to detach first) or as the leader's TARGET point (a welded
        // joint is dragged as a whole closure, never re-connected from the
        // joint). Fall through so the caller starts a block drag instead
        // (the whole welded pair/closure moves together).
        bool weldedJoint = false;
        for (const auto& att : m_paramDoc->attachments()) {
            if (!att.isLocked) continue;
            if (att.fromBlockId == pt->blockId
                || (att.toBlockId == pt->blockId && att.toPointId == pt->pointId)) {
                weldedJoint = true;
                break;
            }
        }
        if (weldedJoint)
            return false;
        m_connectGesture->beginConnect(pt->blockId, pt->pointId, pos);
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

QUuid ToolSelect::hitSegmentAt(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits) {
        if (auto* bi = BlockItem::containingItem(item)) {
            if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(bi->blockId()) : nullptr;
                blk && blk->layer == m_paramDoc->activeLayer())
                return bi->hitSegmentAtScene(scenePt);
        }
    }
    return QUuid();
}

void ToolSelect::notifyEditTarget()
{
    if (!m_editTargetCb) return;
    QUuid blockId, segId;
    if (m_selection.size() == 1) {
        blockId = *m_selection.begin();
        segId = m_lastHitSegmentId;
        if (segId.isNull() && m_paramDoc) {
            // Fallback: first segment of the block (e.g. external selection).
            if (const auto* blk = m_paramDoc->findBlock(blockId); blk && !blk->segments.empty())
                segId = blk->segments.front().id;
        }
    }
    m_editTargetCb(blockId, segId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visual helpers
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::deleteSelectedBlocks()
{
    if (!m_scene || !m_paramDoc || m_selection.isEmpty()) return;

    const QList<QUuid> toRemove = m_selection.values();

    // NOTE: 组对删除零限制 —— 选中集可包含组的任意部分成员, 删除照常执行
    // (组只是选择快捷方式, 不提供结构保护; 剩余成员不足 2 时组自动解散).

    // Show the aggregated delete-impact report before committing (批量删除
    // 时善后可能合并, 报告计数为上限). The cascade itself is unchanged.
    // Parent = the scene view so the confirm box stays on the right screen.
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
