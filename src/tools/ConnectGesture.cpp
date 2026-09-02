#include "ConnectGesture.h"

#include <cmath>
#include <utility>

#include <QKeyEvent>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "ui/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 存储域 α ∈ [0, 360°)，跟随角度显示 = 带符号折角 [−180°, +180°]。
// 统一实现收口在 geometry/Angle.h（normalizeDeg360 / normalizeDeg180）与
// geometry/Units.h（formatDegValue），此处不再本地复制。
// 跨层连接 toast 文案统一在 tools/LayerFeedback.h。
// 连接半径常量 (kConnectSnapRadius / kConnectGrabRadius) 定义于
// ConnectGesture.h 顶部 — ToolSelect 悬停端点提示共享同一值 (2026-09)。

ConnectGesture::ConnectGesture(CanvasScene* scene, cad::param::ParamDocument* doc,
                               QUndoStack* undoStack,
                               const std::function<void(SelectState)>& setState,
                               const std::function<void(const QString&)>& showToast,
                               const std::function<void()>& clearSelectionAndIdle,
                               const std::function<bool()>& selectionEmpty,
                               const std::function<void(const QUuid&, const QUuid&,
                                                        const QUuid&, double)>& beginAngleSession,
                               const std::function<void(bool)>& angleValidity)
    : m_scene(scene)
    , m_paramDoc(doc)
    , m_undoStack(undoStack)
    , m_setState(setState)
    , m_showToast(showToast)
    , m_clearSelectionAndIdle(clearSelectionAndIdle)
    , m_selectionEmpty(selectionEmpty)
    , m_beginAngleSession(beginAngleSession)
    , m_angleValidity(angleValidity)
{
}

ConnectGesture::~ConnectGesture() = default;

void ConnectGesture::beginConnect(const QUuid& fromBlockId, const QUuid& fromPointId,
                                  const Vec2& pos)
{
    (void)pos;  // grab offset derives from the point's world position
    auto* blk = m_paramDoc->findBlock(fromBlockId);
    if (!blk || blk->isBridge) return;   // bridges are pinned at both ends

    // 组件级连接: 抓组件成员端点 → 组件整体作为 follower (借用端点连接).
    m_connectComponentId = QUuid();
    if (const auto* comp = m_paramDoc->componentsView().ofBlock(fromBlockId))
        m_connectComponentId = comp->id;

    m_connectFromBlock = fromBlockId;
    m_connectFromPoint = fromPointId;
    m_connectTarget.reset();
    m_componentSwitchCandidates.clear();
    m_reattachActive = false;   // 仅角度重挂标志: beginConnect 即失效
    m_reattachOldAtt = cad::param::Attachment();

    // 滑轨模式 (抽屉式滑动): reset the slide-drag snapshot. If this block's
    // attachment is in a slide mode it stays ACTIVE (never quick-detached);
    // snapshot its pre-drag rail coordinates so the commit macro can undo the
    // slide back to the pre-drag rail position.
    m_connectSlideAttId = QUuid();
    m_connectOldSlideAlong = 0.0;
    m_connectOldSlidePerp = 0.0;

    // Grab geometry: the block translates so its from-point tracks the cursor.
    m_connectOrigOrigin   = blk->transform.origin;
    m_connectOrigRotation = blk->transform.rotation;
    m_connectGrabOffset   = blk->transform.origin - blk->worldPos(fromPointId);
    // 组件级连接: 记录所有成员拖前 origin (整组跟随光标预览).
    m_connectOrigOrigins.clear();
    if (!m_connectComponentId.isNull()) {
        if (const auto* comp = m_paramDoc->componentsView().byId(m_connectComponentId))
            for (const QUuid& mid : comp->memberBlockIds)
                if (const auto* mb = m_paramDoc->findBlock(mid))
                    m_connectOrigOrigins.insert(mid, mb->transform.origin);
    }

    // 快拆 (quick-detach) 已移除 (2026-09): tryPointOperation 的 isFollower
    // 过滤保证 beginConnect 只对非 follower 块发起, 这里不存在需要释放的
    // 旧连接 (旧快拆循环是死代码)。拆开统一走: D 键快拆 / 属性面板取消
    // 位置吸附 / 拖跟随线 (未锁定)。
    // 滑轨模式连接不快拆: 记录其拖前锁/自由轴坐标,
    // 拖动期间由 move() 每帧回写自由轴 (updateSlideOffsetsFromCurrent)。
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == fromBlockId && !att.isPin
            && att.slideMode != cad::param::SlideMode::None) {
            m_connectSlideAttId = att.id;
            m_connectOldSlideAlong = att.slideAlongMm;
            m_connectOldSlidePerp = att.slidePerpMm;
            break;
        }
    }

    setState(SelectState::Connecting);
    updateConnectHalo();
}

void ConnectGesture::move(const Vec2& pos)
{
    if (m_state == SelectState::ConfirmTarget) {
        updateCandidateHighlight(pos, m_confirmCandidates);
        return;
    }
    if (m_state == SelectState::ConfirmSource) {
        // No yellow line for source selection: the selected line already uses
        // the normal selection highlight, and the source-endpoint marker stays.
        return;
    }
    if (m_state == SelectState::AngleInput) {
        // 组件级重叠切换 (用户要求 2026-09): hover 高亮重叠候选线段, 提示
        // 可点击切换跟随基准 (与 ConfirmTarget 同一视觉语言).
        updateCandidateHighlight(pos, m_componentSwitchCandidates);
        return;
    }
    if (m_state != SelectState::Connecting) return;
    if (!m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = m_scene ? m_scene->currentZoom() : 1.0;

    // Generous radius while connecting: dropping onto a target must feel easy.
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, kConnectSnapRadius,
                                      {}, &m_connectFromBlock);
    if (snap.has_value()) {
        if (snap->blockId == m_connectFromBlock) {
            snap.reset();
        }
    }

    // Only promise a connection (magnet + ring) when releasing would actually
    // attach — e.g. a descendant's point would close a cycle and is refused.
    bool willConnect = false;
    if (snap.has_value()) {
        if (isComponentConnect()) {
            // 组件级连接: 只要求组件还没有外部跟随线 (森林不变式 组件维度);
            // 成员线级名额不占用, 内部关系不受影响.
            willConnect = componentCanConnect();
        } else {
            // 线级连接: 常规 checkAttachment; 仅角度 (angleOnly) 跟随线
            // 的位置是自由的 — 磁铁按其原附件将被重挂的语义判定 (排除自身).
            willConnect = lineConnectValid(snap->blockId, snap->pointId);
        }
    }
    m_connectTarget = willConnect ? snap : std::nullopt;

    // The block physically follows the cursor: from-point lands on the cursor
    // (or exactly on the snap target). Children cascade via resolveForDrag —
    // only the dragged block's subgraph moves (the old attachment, if any, is
    // already out of the document during the gesture).
    const Vec2 anchor = m_connectTarget.has_value() ? m_connectTarget->worldPos : pos;
    const Vec2 newOrigin = anchor + m_connectGrabOffset;
    const Vec2 delta = newOrigin - m_connectOrigOrigin;
    if (isComponentConnect()) {
        // 组件级连接: 整组整体平移跟随光标 (预览), 内部相对几何不变.
        for (auto it = m_connectOrigOrigins.cbegin(); it != m_connectOrigOrigins.cend(); ++it) {
            if (auto* mb = m_paramDoc->findBlock(it.key()))
                mb->transform.origin = it.value() + delta;
        }
    } else {
        blk->transform.origin = newOrigin;
    }

    // 滑轨模式 (抽屉式滑动): 拖动沿滑轨走 — 从当前 (拖拽) 位置回写自由轴
    // 坐标, 锁轴坐标保持快照; 随后 resolveForDrag 按新坐标落位 (锁轴弹回).
    if (!m_connectSlideAttId.isNull())
        m_paramDoc->updateSlideOffsetsFromCurrent(m_connectSlideAttId);
    m_paramDoc->invalidateLayer(blk->layer);

    QList<QUuid> dragRoots{m_connectFromBlock};
    m_paramDoc->resolveForDrag(dragRoots);

    updateConnectMarker();
    updateConnectHalo();
}

void ConnectGesture::release(const Vec2& pos)
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
        double zoom = m_scene ? m_scene->currentZoom() : 1.0;
        const auto allCands = m_snapEngine.findSnapCandidates(
            pos, m_paramDoc, zoom, kConnectSnapRadius, {}, &m_connectFromBlock);
        std::vector<SnapResult> pool;
        for (const auto& c : allCands) {
            if (c.blockId == m_connectFromBlock) continue;
            if (isComponentConnect()) {
                // 组件级连接: 只检查组件维度的名额 (一组件一条外部跟随线).
                if (componentCanConnect())
                    pool.push_back(c);
                continue;
            }
            if (lineConnectValid(c.blockId, c.pointId))
                pool.push_back(c);
        }

        if (!pool.empty()) {
            const Vec2 refPos = pool.front().worldPos;
            // Overlap set = candidates at the same spot as the nearest one.
            std::vector<SnapResult> overlap;
            for (const auto& c : pool)
                if (c.worldPos.distanceTo(refPos) < kSnapOverlapEps)
                    overlap.push_back(c);

            if (overlap.size() > 1 && !isComponentConnect()) {
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
            // 组件级连接: 组件接谁谁就是基准线, 重叠时直接连最近的候选, 无需确认.
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
    }
}

void ConnectGesture::pressConfirmTarget(const Vec2& pos)
{
    if (!m_paramDoc || !m_scene) { cancel(); return; }
    double zoom = m_scene->currentZoom();

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
    cancel();
}

void ConnectGesture::beginSourceConfirm(std::vector<ConfirmCandidate> candidates,
                                        const Vec2& pos)
{
    if (candidates.empty() || !m_paramDoc || !m_scene) { cancel(); return; }

    // No block is being dragged yet — this state only disambiguates WHICH
    // member endpoint starts the connection.
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();

    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates = std::move(candidates);
    m_selectedSourceCandidate.reset();
    (void)pos;
    setState(SelectState::ConfirmSource);
    // 源端不再画黄线：候选线本身就带有选中效果，选定后再显示源端点小标记。
}

void ConnectGesture::pressConfirmSource(const Vec2& pos)
{
    if (!m_paramDoc || !m_scene) { cancel(); return; }
    double zoom = m_scene->currentZoom();

    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());

    // If a source line is already selected:
    //  - pressing the source POINT starts the connection;
    //  - pressing anywhere on the selected LINE also starts the connection;
    //  - pressing another candidate line switches the source.
    if (m_selectedSourceCandidate.has_value()) {
        const auto& sel = *m_selectedSourceCandidate;
        if (const auto* blk = m_paramDoc->findBlock(sel.blockId)) {
            const Vec2 wp = blk->worldPos(sel.pointId);
            const double worldRadius = kConnectGrabRadius / (zoom > 1e-9 ? zoom : 1.0);
            if (pos.distanceTo(wp) <= worldRadius) {
                beginConnect(sel.blockId, sel.pointId, pos);
                return;
            }
        }
        if (segSnap) {
            for (const auto& cand : m_confirmCandidates) {
                if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                    if (cand.blockId == sel.blockId && cand.segId == sel.segId
                        && cand.pointId == sel.pointId) {
                        beginConnect(sel.blockId, sel.pointId, pos);
                        return;
                    }
                    m_selectedSourceCandidate = cand;
                    updateSourcePortMarker();
                    return;
                }
            }
        }
        // Press neither on the selected line nor on another candidate: cancel.
        removeConfirmHighlight();
        removeSourcePortMarker();
        m_confirmCandidates.clear();
        m_selectedSourceCandidate.reset();
        cancel();
        return;
    }

    // No source selected yet: click a candidate line to choose it.
    if (segSnap) {
        for (const auto& cand : m_confirmCandidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                m_selectedSourceCandidate = cand;
                updateSourcePortMarker();
                return;
            }
        }
    }
    // Blank or non-candidate click: abort the whole gesture.
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    cancel();
}

void ConnectGesture::cancel()
{
    // Abort the in-progress connect drag: restore the exact pre-drag state
    // (transform + old attachment) as if the gesture never started.
    if (m_paramDoc) {
        if (auto* blk = m_paramDoc->findBlock(m_connectFromBlock)) {
            blk->transform.origin   = m_connectOrigOrigin;
            blk->transform.rotation = m_connectOrigRotation;
        }
        // 滑轨模式: 拖动中回写的自由轴坐标也要还原 (cancel = 撤销整个手势).
        if (!m_connectSlideAttId.isNull()) {
            if (auto* a = m_paramDoc->findAttachment(m_connectSlideAttId)) {
                a->slideAlongMm = m_connectOldSlideAlong;
                a->slidePerpMm = m_connectOldSlidePerp;
            }
        }
        m_paramDoc->resolveAll();
    }
    if (m_scene) m_scene->refreshAllBlockItems();

    removeConnectMarker();
    removeConnectHalo();
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    m_componentSwitchCandidates.clear();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    endAngleSession();
    setState(SelectState::Confirmed);
}

bool ConnectGesture::keyPress(QKeyEvent* event)
{
    if (m_state == SelectState::AngleInput) {
        // Fallback path: the key reached the view instead of the HUD widget.
        if (event->key() == Qt::Key_Escape) {
            cancelAngle();
            return true;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitAngle();
            return true;
        }
        return false;  // HUD owns all other keys
    }
    if (event->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

bool ConnectGesture::attachToTarget(const QUuid& toBlockId, const QUuid& toPointId,
                                    const QUuid& toSegmentId)
{
    if (!m_paramDoc) return false;
    auto* toBlk = m_paramDoc->findBlock(toBlockId);
    if (!toBlk) return false;
    if (toBlk->isShadow) return false;  // 影子不可作为连接目标 (R4, 拆开影子基准)

    cad::param::Attachment att;
    att.fromPointId = m_connectFromPoint;
    att.toBlockId   = toBlockId;
    att.toPointId   = toPointId;
    att.toSegmentId = !toSegmentId.isNull()
        ? toSegmentId : toBlk->exitSegmentAtPoint(toPointId);

    if (isComponentConnect()) {
        // ── 组件级连接: 组件整体作为 follower, 借用暴露端点 + 端点线段方向 ──
        const cad::param::Component* comp =
            m_paramDoc->componentsView().byId(m_connectComponentId);
        cad::param::Block* exposed = comp
            ? m_paramDoc->findBlock(
                  m_paramDoc->componentsView().memberOwningPoint(*comp, m_connectFromPoint))
            : nullptr;
        if (!comp || !exposed) return false;
        att.fromComponentId = m_connectComponentId;

        // Orientation-preserving follower angle (the exposed member is the
        // component's pose carrier): rotation = refWorld + angle − localDir.
        const double refWorld = toBlk->transform.rotation
            + toBlk->exitDirectionAtPoint(toPointId, att.toSegmentId);
        const double localDir = exposed->directionAtPoint(m_connectFromPoint);
        const double angleDeg = cad::param::backSolveFollowerAngle(
            exposed->transform.rotation, localDir, refWorld);
        att.followerAngle = angleDeg;

        // 森林不变式 (组件维度): 一个组件至多一条外部跟随线.
        if (!componentCanConnect())
            return false;
        if (!m_paramDoc->addAttachment(att)) return false;
        m_editingAttachmentId = att.id;
        m_initialAngle = angleDeg;
        // 组件级重叠切换: 连接点若有其他线段端点重叠, 记住候选线段 — 角度
        // 窗口内点击重叠线段即可切换跟随对象 (用户要求 2026-09).
        m_componentSwitchCandidates = collectComponentSwitchCandidates(
            toBlk->worldPos(toPointId), toBlockId, att.toSegmentId);
        m_paramDoc->resolveAll();
        if (m_scene) {
            m_scene->refreshAllBlockItems();
            QString toast = cad::ui::crossLayerToast(m_paramDoc, *exposed, *toBlk);
            if (!m_componentSwitchCandidates.empty()) {
                const QString hint = QString::fromUtf8(
                    "连接点有重叠：点击重叠线段可切换跟随基准");
                toast = toast.isEmpty() ? hint : toast + QStringLiteral("；") + hint;
            }
            if (!toast.isEmpty())
                m_showToast(toast);
            beginAngleSession(att.id, angleDeg);
        }
        setState(SelectState::AngleInput);
        return true;
    }

    // ── 线级连接 (原有) ──
    auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!fromBlk) return false;

    // 仅角度 (angleOnly) 跟随线: 位置自由 — 拖动端点 = 重新建立位置连接.
    // 原附件重挂到新目标 (旧角度基准保留为独立角度基准 → 双基准), 不新建
    // 附件 (森林不变式: 每块至多一条线级跟随连接).
    const QUuid angleOnlyAttId = angleOnlyAttachmentId(m_connectFromBlock);
    if (!angleOnlyAttId.isNull())
        return reattachAngleOnly(angleOnlyAttId, toBlockId, toPointId,
                                 toSegmentId);

    att.fromBlockId = m_connectFromBlock;

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
    if (!m_paramDoc->addAttachment(att)) return false;
    m_editingAttachmentId = att.id;
    m_initialAngle = angleDeg;
    m_paramDoc->resolveAll();
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        if (const QString toast = cad::ui::crossLayerToast(m_paramDoc, *fromBlk, *toBlk);
            !toast.isEmpty())
            m_showToast(toast);
        beginAngleSession(att.id, angleDeg);
    }
    setState(SelectState::AngleInput);
    return true;
}

// ── 仅角度线拖端点重挂 (用户报告: 使用了引用线段但无连接线段的线段,
//    拖动端点没有吸附反应, 无法建立连接) ─────────────────────────────────────
// 快速拆开 (D 键/拖拆) 后连接处于 angleOnly: 位置自由、角度仍跟随旧基准线
// (= 使用引用线段但没有位置连接)。旧实现 tryPointOperation 的 isFollower
// 过滤把这类块整体拦下 — 端点拖不动、无磁铁。修复: 放行 angleOnly 源; 释放
// 到目标时把原附件原地重挂 (不新建, 保持每块至多一条线级跟随连接):
// 旧角度基准保留为独立角度基准 (双基准), 位置挂到新端点, 恢复完整连接并
// 重新焊接 (与 SegmentConnectionCard::onTargetResolved 语义一致)。

QUuid ConnectGesture::angleOnlyAttachmentId(const QUuid& fromBlockId) const
{
    if (!m_paramDoc) return QUuid();
    for (const auto& att : m_paramDoc->attachments())
        if (!att.isPin && att.fromBlockId == fromBlockId && att.angleOnly)
            return att.id;
    return QUuid();
}

bool ConnectGesture::lineConnectValid(const QUuid& toBlockId,
                                      const QUuid& toPointId) const
{
    if (!m_paramDoc) return false;
    cad::param::Attachment cand;
    cand.fromBlockId = m_connectFromBlock;
    cand.fromPointId = m_connectFromPoint;
    cand.toBlockId   = toBlockId;
    cand.toPointId   = toPointId;

    const QUuid angleOnlyAttId = angleOnlyAttachmentId(m_connectFromBlock);
    if (angleOnlyAttId.isNull())
        return cad::param::checkAttachment(m_paramDoc->attachments(), cand)
               == cad::param::AttachmentIssue::Ok;

    // 重挂 = 原附件换代: 校验时排除它 (不占新 follower 名额), 其余同规
    // (环 / 重复跟随照旧被拒)。
    std::vector<cad::param::Attachment> others;
    for (const auto& a : m_paramDoc->attachments())
        if (a.id != angleOnlyAttId) others.push_back(a);
    if (cad::param::checkAttachment(others, cand)
            != cad::param::AttachmentIssue::Ok)
        return false;
    // 跨层单向契约 (工作层 follower → 辅助层 leader 拒绝, 与 addAttachment
    // 同规 — 重挂绕过 addAttachment 校验, 这里显式补齐)。
    const auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    const auto* toBlk = m_paramDoc->findBlock(toBlockId);
    if (!fromBlk || !toBlk) return false;
    if (!m_paramDoc->isAuxBlock(*fromBlk) && m_paramDoc->isAuxBlock(*toBlk))
        return false;
    return true;
}

bool ConnectGesture::reattachAngleOnly(const QUuid& attId,
                                       const QUuid& toBlockId,
                                       const QUuid& toPointId,
                                       const QUuid& toSegmentId)
{
    if (!m_paramDoc) return false;
    auto* toBlk = m_paramDoc->findBlock(toBlockId);
    const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(attId);
    auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!toBlk || !att || !fromBlk || toBlockId == fromBlk->id) return false;
    const cad::param::ParamPoint* toPt = toBlk->findPoint(toPointId);
    if (!toPt || !toPt->resolved) return false;

    // 影子基准拓扑 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4): 拖影子
    // 基准跟随线端点 → 挂回本体 (⑤) 或影子挂载新宿主 (③), 不走旧活引用路径。
    if (const auto* curTo = m_paramDoc->findBlock(att->toBlockId);
        curTo && curTo->isShadow) {
        if (toBlockId == curTo->id) return false;  // 拖回影子自身: 非法 (影子不可交互)
        return reattachShadowBased(attId, *curTo, toBlockId, toPointId, toSegmentId);
    }

    // 目标线段: 未指定时取连接点所在线段 (与 attachToTarget 同规)。
    const QUuid segId = !toSegmentId.isNull()
        ? toSegmentId : toBlk->exitSegmentAtPoint(toPointId);
    if (segId.isNull()) return false;

    // 校验 (排除将被重挂的自身附件) + 跨层单向契约。
    if (!lineConnectValid(toBlockId, toPointId)) return false;

    // 仅角度重挂 = 原附件换代: undo 需还原旧附件态 (finalizeConnection 的
    // ReconnectAttachmentCommand); 先快照旧态再原地变换。
    const cad::param::Attachment oldState = *att;
    cad::param::Attachment* mut = m_paramDoc->findAttachment(attId);
    if (!mut) return false;
    // 重连保持角度基准 (用户拍板 2026-09): 自动态下把旧所连线段固化为两点
    // 基准 (点1 = 旧目标点, 点2 = 旧线段另一端) —— 此前只固化点1, 点2 留空,
    // 两点连线方向退化为单点出口方向。已自定义的基准原样保留。
    cad::param::preserveAngleRefOnReattach(m_paramDoc, *mut);
    mut->angleOnly = false;
    mut->slideMode = cad::param::SlideMode::None;
    mut->isLocked = true;   // 仅角度重挂 = 恢复完整连接并重新焊接
    mut->toBlockId = toBlockId;
    mut->toPointId = toPointId;
    mut->toSegmentId = segId;

    // 仅角度重挂 = 仅连接 (用户拍板 2026-12): 位置挂到新端点, 角度基准与
    // followerAngle/followerAngleFormula 原样保留 (旧基准 L2 + 变量 GG 继续
    // 驱动角度), 不反算覆盖、不清公式、不进入角度输入会话 (回车 = 确定连接
    // 而非确定角度)。旧实现曾在此反算 followerAngle 并 clear 公式, 导致:
    // ①变量 GG 被强制换算成数值 90° 并消失; ②基准不一致 ——
    // preserveAngleRefOnReattach 已把角度基准固化为旧宿主 L2, 反算却用新宿主
    // L3 的 refWorld → L1 方向翻转。
    m_paramDoc->resolveAll();

    // 有 undo 栈: 记录旧态供 finalizeConnection 单步撤销 (含 HUD 角度调整).
    if (m_undoStack) {
        m_reattachActive = true;
        m_reattachOldAtt = oldState;
    }

    const cad::param::Attachment* reatt = m_paramDoc->attachmentsView().byId(attId);
    if (!reatt) return false;
    m_editingAttachmentId = attId;
    m_initialAngle = reatt->followerAngle;
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        if (const QString toast = cad::ui::crossLayerToast(m_paramDoc, *fromBlk, *toBlk);
            !toast.isEmpty())
            m_showToast(toast);
    }
    // 仅连接: 直接收尾 (finalizeConnection 走 undo 宏, 单步撤销)。
    finalizeConnection();
    return true;
}

// ── 影子基准重挂路由 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4) ───────────
// 拖影子基准跟随线 (Att2 → 影子) 的端点释放到目标线:
//   · 目标 = 影子本体 (master) → ⑤ 挂回本体: 删影子 + Att2 转普通活引用
//     (SetAttachmentAngleOnlyCommand(false) 的 ReconnectMaster 模式, 显式
//     落点 = 用户拖到的点);
//   · 目标 = 其他线 → ③ 影子挂载 (ShadowMountCommand): Att1 = 影子→目标
//     (Δ 反算保向) + Att2 恢复位置钉点重新焊接 —— L3→影子→L2 双连接链。
// 仅连接语义 (用户拍板 2026-12 同源): 不进入角度输入会话, undo 单步。
bool ConnectGesture::reattachShadowBased(const QUuid& attId,
                                         const cad::param::Block& shadow,
                                         const QUuid& toBlockId,
                                         const QUuid& toPointId,
                                         const QUuid& toSegmentId)
{
    if (!m_paramDoc) return false;
    auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!fromBlk || toBlockId == fromBlk->id) return false;
    const bool toMaster = (toBlockId == shadow.shadowMasterBlockId);

    if (m_undoStack) {
        m_undoStack->beginMacro(toMaster ? QStringLiteral("重新挂接")
                                         : QStringLiteral("影子挂载"));
        if (toMaster) {
            m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                m_paramDoc, attId, /*angleOnly=*/false, toPointId, toSegmentId));
        } else {
            m_undoStack->push(new cad::cmd::ShadowMountCommand(
                m_paramDoc, shadow.id, toBlockId, toPointId, toSegmentId));
        }
        m_undoStack->endMacro();
    } else if (toMaster) {
        // 无 undo 栈兜底: 门面 ⑤ 路由 (显式落点 = 用户拖到的点)。
        m_paramDoc->reattachShadowToMaster(attId, toPointId, toSegmentId);
    } else {
        m_paramDoc->mountShadowTo(shadow.id, toBlockId, toPointId, toSegmentId);
    }

    if (m_scene) {
        m_scene->refreshAllBlockItems();
        if (auto* toBlk = m_paramDoc->findBlock(toBlockId);
            toBlk && fromBlk) {
            if (const QString toast = cad::ui::crossLayerToast(m_paramDoc, *fromBlk, *toBlk);
                !toast.isEmpty())
                m_showToast(toast);
        }
    }
    // 仅连接: 不设 m_editingAttachmentId (不走 finalize 的重挂宏), 直接收尾。
    finalizeConnection();
    return true;
}

bool ConnectGesture::componentCanConnect() const
{
    if (!m_paramDoc) return false;
    if (m_connectComponentId.isNull()) return false;
    for (const auto& a : m_paramDoc->attachments())
        if (a.fromComponentId == m_connectComponentId)
            return false;  // 组件已有外部跟随线
    return true;
}

std::vector<ConfirmCandidate> ConnectGesture::collectConfirmCandidates(
    const Vec2& connWorldPos) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == m_connectFromBlock) continue;
        if (block.isShadow) continue;  // 影子不可作为连接目标 (R4, 拆开影子基准)
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const Vec2 local = block.transform.toLocal(connWorldPos);
            if (sp && sp->resolved
                && sp->resolvedPos.distanceTo(local) < kSnapOverlapEps)
                out.push_back({block.id, seg.id, sp->id});
            if (ep && ep->resolved
                && ep->resolvedPos.distanceTo(local) < kSnapOverlapEps)
                out.push_back({block.id, seg.id, ep->id});
        }
    }
    return out;
}

// ── 组件级连接重叠切换 (AngleInput 窗口, 用户要求 2026-09) ──────────────────
// 组件级连接释放时直接连最近候选 (2026-09 定稿: 组件接谁谁就是基准线); 若连接
// 点有多个端点重叠, 角度窗口内点击重叠线段的线身即可把跟随对象切换过去 —
// 先点选线段、再确定基准, 与 ConfirmTarget 的视觉语言一致 (橙线高亮).

std::vector<ConfirmCandidate> ConnectGesture::collectComponentSwitchCandidates(
    const Vec2& connWorldPos, const QUuid& curBlockId, const QUuid& curSegId) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc || m_connectComponentId.isNull()) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        // 组件自身成员不能作为基准 (组件跟随自身成员 = 环, addAttachment 会拒).
        if (block.id == m_connectFromBlock) continue;
        if (const auto* bcomp = m_paramDoc->componentsView().ofBlock(block.id);
            bcomp && bcomp->id == m_connectComponentId)
            continue;
        const Vec2 local = block.transform.toLocal(connWorldPos);
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const QUuid hit =
                (sp && sp->resolved
                 && sp->resolvedPos.distanceTo(local) < kSnapOverlapEps) ? sp->id
              : (ep && ep->resolved
                 && ep->resolvedPos.distanceTo(local) < kSnapOverlapEps) ? ep->id
              : QUuid();
            if (hit.isNull()) continue;
            if (block.id == curBlockId && seg.id == curSegId) continue;  // 当前基准不提示
            out.push_back({block.id, seg.id, hit});
        }
    }
    return out;
}

void ConnectGesture::pressAngleTarget(const Vec2& pos)
{
    if (m_componentSwitchCandidates.empty() || !m_paramDoc || !m_scene) return;
    double zoom = m_scene->currentZoom();

    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (!segSnap) return;
    for (const auto& cand : m_componentSwitchCandidates) {
        if (cand.blockId == segSnap->blockId
            && cand.segId == segSnap->segmentId) {
            switchComponentTarget(cand);
            return;
        }
    }
}

bool ConnectGesture::switchComponentTarget(const ConfirmCandidate& cand)
{
    if (!m_paramDoc) return false;
    cad::param::Attachment* cur = m_paramDoc->findAttachment(m_editingAttachmentId);
    if (!cur || cur->fromComponentId != m_connectComponentId) return false;
    if (cur->toBlockId == cand.blockId && cur->toPointId == cand.pointId
        && cur->toSegmentId == cand.segId)
        return false;  // 同一目标: no-op

    auto* toBlk = m_paramDoc->findBlock(cand.blockId);
    const cad::param::Component* comp = m_paramDoc->componentsView().byId(m_connectComponentId);
    cad::param::Block* exposed = comp
        ? m_paramDoc->findBlock(
              m_paramDoc->componentsView().memberOwningPoint(*comp, m_connectFromPoint))
        : nullptr;
    if (!toBlk || !exposed) return false;

    // 新 attachment: 跟随端不动, 只换基准线; 角度反算保向 (零视觉跳变),
    // 与 attachToTarget 同一公式. 先拷贝旧值 (removeAttachment 会废掉指针).
    const cad::param::Attachment old = *cur;
    cad::param::Attachment att = old;
    att.toBlockId = cand.blockId;
    att.toPointId = cand.pointId;
    att.toSegmentId = cand.segId;
    const double refWorld = toBlk->transform.rotation
        + toBlk->exitDirectionAtPoint(cand.pointId, cand.segId);
    const double localDir = exposed->directionAtPoint(m_connectFromPoint);
    const double angleDeg = cad::param::backSolveFollowerAngle(
        exposed->transform.rotation, localDir, refWorld);
    att.followerAngle = angleDeg;
    att.followerAngleFormula.clear();
    att.rotationMode = cad::param::RotationMode::Angle;
    att.arcLength = 0.0;
    att.arcLengthFormula.clear();

    // 组件维度森林不变式: 先删旧连接 (一组件一条外部跟随线), 再经
    // addAttachment 全量校验 (环/跨层); 校验失败原样加回 (撤销切换).
    m_paramDoc->removeAttachment(old.id);
    m_editingAttachmentId = QUuid();
    if (!m_paramDoc->addAttachment(att)) {
        m_paramDoc->addAttachment(old);
        return false;
    }
    m_editingAttachmentId = att.id;
    m_initialAngle = angleDeg;
    m_paramDoc->resolveAll();
    // 基准换了, 候选集随之刷新 (旧基准重新成为可切回的候选).
    m_componentSwitchCandidates = collectComponentSwitchCandidates(
        toBlk->worldPos(cand.pointId), cand.blockId, cand.segId);
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        beginAngleSession(att.id, angleDeg);
    }
    return true;
}

void ConnectGesture::updateCandidateHighlight(
    const Vec2& pos, const std::vector<ConfirmCandidate>& candidates)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = m_scene->currentZoom();

    QUuid hitBlock, hitSeg;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : candidates) {
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

void ConnectGesture::removeConfirmHighlight()
{
    if (m_confirmHighlight) {
        m_confirmHighlight->setVisible(false);
        delete m_confirmHighlight;
        m_confirmHighlight = nullptr;
    }
}

void ConnectGesture::updateSourcePortMarker()
{
    if (!m_scene || !m_paramDoc || !m_selectedSourceCandidate.has_value()) {
        removeSourcePortMarker();
        return;
    }
    const auto& sel = *m_selectedSourceCandidate;
    const auto* blk = m_paramDoc->findBlock(sel.blockId);
    if (!blk) { removeSourcePortMarker(); return; }

    const QPointF c = cad::geo::Coord::toScene(
        blk->worldPos(sel.pointId).x, blk->worldPos(sel.pointId).y);
    constexpr double kRadius = 5.0;
    if (!m_sourcePortMarker) {
        m_sourcePortMarker = new QGraphicsEllipseItem();
        m_sourcePortMarker->setPen(QPen(cad::ui::Theme::tokens().accent, 2.0));
        m_sourcePortMarker->setBrush(QColor(47, 111, 237, 120));
        m_sourcePortMarker->setZValue(100.0);
        m_scene->addItem(m_sourcePortMarker);
    }
    m_sourcePortMarker->setRect(c.x() - kRadius, c.y() - kRadius,
                                2.0 * kRadius, 2.0 * kRadius);
    m_sourcePortMarker->show();
}

void ConnectGesture::removeSourcePortMarker()
{
    if (m_sourcePortMarker) {
        if (m_scene) m_scene->removeItem(m_sourcePortMarker);
        delete m_sourcePortMarker;
        m_sourcePortMarker = nullptr;
    }
}

void ConnectGesture::commitConnectMove()
{
    // A failed/plain-move end of the gesture also clears any source selection
    // highlight (the connection did not complete).
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    m_componentSwitchCandidates.clear();

    auto* blk = m_paramDoc ? m_paramDoc->findBlock(m_connectFromBlock) : nullptr;
    if (!blk) {
        setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
        return;
    }

    const Vec2 delta = blk->transform.origin - m_connectOrigOrigin;

    // 滑轨模式 (抽屉式滑动): 拖动期间已每帧回写自由轴坐标 — 记录拖后值,
    // 提交宏里用 SetSlideOffsetsCommand 把 old→new 与 MoveBlockCommand 一起
    // 入栈 (undo 整体回到拖前滑轨位置)。
    double curSlideAlong = 0.0, curSlidePerp = 0.0;
    bool slideAlive = false;
    if (!m_connectSlideAttId.isNull()) {
        if (const auto* a = m_paramDoc->attachmentsView().byId(m_connectSlideAttId)) {
            curSlideAlong = a->slideAlongMm;
            curSlidePerp = a->slidePerpMm;
            slideAlive = true;
        }
    }
    const bool offsetsChanged = slideAlive &&
        (std::abs(curSlideAlong - m_connectOldSlideAlong) > 1e-9 ||
         std::abs(curSlidePerp - m_connectOldSlidePerp) > 1e-9);

    // Restore the pre-drag state, then replay through the undo stack so the
    // whole gesture (move / slide) is one undo step.
    blk->transform.origin   = m_connectOrigOrigin;
    blk->transform.rotation = m_connectOrigRotation;

    if (m_undoStack && (offsetsChanged || delta.lengthSquared() > 1e-10)) {
        m_undoStack->beginMacro(m_connectSlideAttId.isNull()
            ? QStringLiteral("\xe7\xa7\xbb\xe5\x8a\xa8")  // 移动
            : QStringLiteral("\xe6\xbb\x91\xe5\x8a\xa8\xe5\xb9\xb6\xe7\xa7\xbb\xe5\x8a\xa8"));  // 滑动并移动
        if (offsetsChanged)
            m_undoStack->push(new cad::cmd::SetSlideOffsetsCommand(
                m_paramDoc, m_connectSlideAttId,
                m_connectOldSlideAlong, m_connectOldSlidePerp,
                curSlideAlong, curSlidePerp));
        if (delta.lengthSquared() > 1e-10) {
            QList<QUuid> moveBlocks{m_connectFromBlock};
            m_undoStack->push(new cad::cmd::MoveBlockCommand(
                m_paramDoc, moveBlocks, delta));
        }
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();
    setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
}

// ── 连接角度会话 (二期: 输入面 = 上下文属性条) ──
// 旧浮动 AngleHud (viewport overlay) 整体退场: 条带显示跟随线段的名称/长度,
// 角度框可编辑, 击键经 ToolHost 回传 onAngleTextChanged 实时预览, Enter/Esc
// 回传 commitAngle/cancelAngle 收尾。tools 层不再持有任何 QWidget。

void ConnectGesture::beginAngleSession(const QUuid& attachmentId, double initialAngle)
{
    if (!m_beginAngleSession || !m_paramDoc) return;

    // 会话开始即复位输入状态 (旧 showAngleHud 同款): 上一会话残留的
    // 无效标记/弧长模式不得带入 —— commitAngle 靠 m_angleValid 放行,
    // onAngleTextChanged 靠 m_angleMode 分流角度/弧长, 残留会让新会话
    // Enter 失效或把数值写进错误的存储域。
    m_angleValid = true;
    m_angleMode = cad::param::RotationMode::Angle;

    // 跟随线段 = 源端点所在线段 (exitSegmentAtPoint: 该点为端点的第一段)。
    // 组件级连接同样成立: m_connectFromBlock = 被抓住的成员块。
    QUuid segId;
    if (const auto* blk = m_paramDoc->findBlock(m_connectFromBlock))
        segId = blk->exitSegmentAtPoint(m_connectFromPoint);
    if (segId.isNull()) {
        // 端点不属于任何线段 (理论不可达): 会话无显示目标, 直接结束。
        m_beginAngleSession(QUuid(), QUuid(), QUuid(), 0.0);
        return;
    }
    m_beginAngleSession(m_connectFromBlock, segId, attachmentId, initialAngle);
}

void ConnectGesture::endAngleSession()
{
    if (m_beginAngleSession)
        m_beginAngleSession(QUuid(), QUuid(), QUuid(), 0.0);
}

void ConnectGesture::onAngleTextChanged(const QString& text)
{
    if (!m_paramDoc) return;

    // Locate the attachment being tuned.
    cad::param::Attachment* att = m_paramDoc->findAttachment(m_editingAttachmentId);
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
        const auto parsed = cad::geo::parseNumberOrFormula(t);
        if (parsed.isNumber) {
            const double numVal = parsed.value;
            if (m_angleMode == cad::param::RotationMode::ArcLength) {
                // 输入 = 带符号折角弧长（v3 定稿）→ 存储 α ∈ [0, 360°) 弧长。
                const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
                const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double foldDeg = (radius > 1e-9)
                    ? cad::geo::arcMmToDeg(cad::geo::Units::cmToMm(numVal), radius) : 0.0;
                const double alphaDeg = cad::geo::normalizeDeg360(foldDeg);
                att->rotationMode = cad::param::RotationMode::ArcLength;
                att->arcLength = cad::geo::degToArcMm(alphaDeg, radius);
                att->arcLengthFormula.clear();
            } else {
                // 输入 = 带符号折角 → 存储 α（v3 定稿）。
                att->rotationMode = cad::param::RotationMode::Angle;
                att->followerAngle = cad::geo::normalizeDeg360(numVal);
                att->followerAngleFormula.clear();
            }
            m_angleValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                parsed.formula, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_angleMode == cad::param::RotationMode::ArcLength) {
                    att->rotationMode = cad::param::RotationMode::ArcLength;
                    att->arcLength = geo::Units::cmToMm(r.value);
                    att->arcLengthFormula = parsed.formula;
                } else {
                    att->rotationMode = cad::param::RotationMode::Angle;
                    att->followerAngle = r.value;
                    att->followerAngleFormula = parsed.formula;
                }
                m_angleValid = true;
            } else {
                m_angleValid = false;             // keep last valid geometry
            }
        }
    }

    if (m_angleValidity) m_angleValidity(m_angleValid);
    if (m_angleValid) {
        // Per-frame preview (角度 HUD 击键): resolve ONLY the connected
        // subgraph and sync cheaply — the old resolveAll() +
        // refreshAllBlockItems() re-resolved the whole document and rebuilt
        // every block item on EVERY keystroke.
        QList<QUuid> seeds;
        if (!att->fromComponentId.isNull()) {
            // 组件级连接: 整个组件是 follower — 借暴露端点成员作种子
            // (resolveForDrag 的 componentClosure 展开到全组件).
            if (const auto* comp = m_paramDoc->componentsView().byId(att->fromComponentId)) {
                const QUuid mb = m_paramDoc->componentsView().memberOwningPoint(*comp, att->fromPointId);
                if (!mb.isNull()) seeds.push_back(mb);
            }
        } else if (!att->fromBlockId.isNull()) {
            seeds.push_back(att->fromBlockId);
        }
        for (const QUuid& s : seeds)
            if (const auto* fb = m_paramDoc->findBlock(s))
                m_paramDoc->invalidateLayer(fb->layer);
        if (!seeds.isEmpty())
            m_paramDoc->resolveForDrag(seeds);
        if (m_scene) m_scene->syncBlockPositions();
    }
}

void ConnectGesture::onAngleModeChanged(cad::param::RotationMode mode)
{
    if (!m_paramDoc) return;
    cad::param::Attachment* att = m_paramDoc->findAttachment(m_editingAttachmentId);
    if (!att) return;

    // Geometry-preserving switch: compute current effective angle, then
    // convert to the new mode's value. 2026-12: 公式驱动不再拒绝切换 ——
    // 公式跨域换算保留变量链接 (半径烘焙为常数, FollowerAngle.h); 旧语义
    // "公式驱动拒绝 °/⌒"已由用户拍板废除。
    const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
    const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
    const auto res = cad::param::followerModeSwitchValues(
        *att, radius, mode, m_paramDoc->parameters(), {});

    if (mode == cad::param::RotationMode::ArcLength) {
        att->rotationMode = cad::param::RotationMode::ArcLength;
        att->arcLength = res.arcMm;
        att->arcLengthFormula = res.arcFormula;
    } else {
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = res.angle;
        att->followerAngleFormula = res.angleFormula;
    }
    m_angleMode = mode;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();

    // 模式切换后条带角度框回显换算值由条带自身的 resolved 处理 (refreshFields
    // 焦点保护: 用户点击 °/⌒ 时角度框已失焦, 回填生效)。旧 HUD 的直写文本
    // 已随 AngleHud 退场。
}

void ConnectGesture::commitAngle()
{
    if (!m_angleValid) return;   // ignore Enter on an invalid formula
    finalizeConnection();
}

void ConnectGesture::cancelAngle()
{
    // Keep the connection but revert to the orientation-preserving angle that
    // was computed at attach time (the block keeps its dragged orientation).
    if (m_paramDoc) {
        if (auto* a = m_paramDoc->findAttachment(m_editingAttachmentId)) {
            a->rotationMode = cad::param::RotationMode::Angle;
            a->followerAngle = m_initialAngle;
            a->followerAngleFormula.clear();
            a->arcLength = 0.0;
            a->arcLengthFormula.clear();
        }
        m_paramDoc->resolveAll();
    }
    finalizeConnection();
}

void ConnectGesture::finalizeConnection()
{
    endAngleSession();
    // Connection committed: remove the persistent source-selection highlight.
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    m_componentSwitchCandidates.clear();

    // Snapshot the tuned attachment, then restore the COMPLETE pre-drag state
    // (transform + old attachment) and replay the whole gesture through the
    // undo stack: "quick-detach + connect + angle" becomes one undo step.
    // 仅角度重挂 (m_reattachActive): 原附件换代 — 新态 (含 HUD 角度调整) 与
    // 旧仅角度态都固化进 ReconnectAttachmentCommand, 单次 undo 即回到拖前
    // 仅角度态 (位置/角度原样), 与普通连接的宏撤销体验一致。
    if (m_paramDoc && m_undoStack) {
        cad::param::Attachment snapshot;
        bool found = false;
        for (const auto& a : m_paramDoc->attachments()) {
            if (a.id == m_editingAttachmentId) { snapshot = a; found = true; break; }
        }
        if (found) {
            if (m_reattachActive) {
                m_undoStack->push(new cad::cmd::ReconnectAttachmentCommand(
                    m_paramDoc, m_editingAttachmentId, snapshot,
                    m_reattachOldAtt, m_connectOrigOrigin, m_connectOrigRotation));
            } else {
                m_paramDoc->removeAttachment(m_editingAttachmentId);
                if (!snapshot.fromComponentId.isNull()) {
                    // 组件级连接: 恢复所有成员拖前 origin (宏 redo 再整体落位).
                    for (auto it = m_connectOrigOrigins.cbegin(); it != m_connectOrigOrigins.cend(); ++it) {
                        if (auto* mb = m_paramDoc->findBlock(it.key()))
                            mb->transform.origin = it.value();
                    }
                } else if (auto* blk = m_paramDoc->findBlock(snapshot.fromBlockId)) {
                    blk->transform.origin   = m_connectOrigOrigin;
                    blk->transform.rotation = m_connectOrigRotation;
                }

                m_undoStack->beginMacro(QStringLiteral(
                    "\xe5\xbb\xba\xe7\xab\x8b\xe8\xbf\x9e\xe6\x8e\xa5"));  // 建立连接
                // 暴露端点 (自动暴露) 由引擎在 addAttachment/addAttachmentRaw/
                // removeAttachment 统一维护 — 连接宏只需 AddAttachmentCommand.
                m_undoStack->push(new cad::cmd::AddAttachmentCommand(m_paramDoc, snapshot));
                m_undoStack->endMacro();
            }
        }
    }

    m_editingAttachmentId = QUuid();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    if (m_scene) m_scene->refreshAllBlockItems();
    m_clearSelectionAndIdle();
    // The owner tool is now Idle — sync the gesture's own state too, or
    // active() would stay true (AngleInput) and swallow every later press
    // in ToolSelect::mousePress's connect branch (regression: 连接完成后
    // 线段/画布无法再选中或移动).
    setState(SelectState::Idle);
}

// ── Visual helpers ──

void ConnectGesture::updateConnectMarker()
{
    if (!m_scene) return;
    if (!m_connectTarget.has_value()) {
        removeConnectMarker();
        return;
    }

    double zoom = m_scene->currentZoom();
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

void ConnectGesture::removeConnectMarker()
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

void ConnectGesture::updateConnectHalo()
{
    if (!m_scene || !m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = m_scene->currentZoom();
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

    const Vec2 src = blk->worldPos(m_connectFromPoint);
    const QPointF c = cad::geo::Coord::toScene(src.x, src.y);
    m_connectHalo->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectHalo->show();
}

void ConnectGesture::removeConnectHalo()
{
    if (m_connectHalo) {
        if (m_scene) m_scene->removeItem(m_connectHalo);
        delete m_connectHalo;
        m_connectHalo = nullptr;
    }
}

std::optional<SnapResult> ConnectGesture::hitPoint(const Vec2& worldPos) const
{
    if (!m_paramDoc) return std::nullopt;
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    // Source-grab radius (kConnectGrabRadius) is deliberately more generous
    // than the drop radius: grabbing must feel easy, dropping stays precise.
    return m_snapEngine.findSnap(worldPos, m_paramDoc, zoom, kConnectGrabRadius);
}

std::vector<SnapResult> ConnectGesture::hitPointCandidates(const Vec2& worldPos) const
{
    if (!m_paramDoc) return {};
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    return m_snapEngine.findSnapCandidates(worldPos, m_paramDoc, zoom, kConnectGrabRadius);
}

} // namespace cad::tools
