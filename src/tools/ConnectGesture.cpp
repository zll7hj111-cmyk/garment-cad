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
    , m_overlap(scene, doc)
{
}

ConnectGesture::~ConnectGesture()
{
    // 图元与候选收集器同生同灭: 析构时统一释放 marker/halo/高亮 (旧实现
    // 4 个 QGraphicsItem 指针散落, 靠 removeConfirmHighlight 等收尾)。
    m_overlap.dispose();
}

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

// ── Visual helpers (阶段 3: 图元管理已迁入 ConnectOverlapResolver) ──
// 以下均为薄转发: 调用点 (move/release/cancel/finalize) 无感知, 图元生命周期
// 统一由 resolver 持有 (与手势同生同灭, 析构 dispose)。

void ConnectGesture::updateConnectMarker()
{
    if (m_connectTarget.has_value())
        m_overlap.showConnectMarker(m_connectTarget->worldPos);
    else
        m_overlap.removeConnectMarker();
}

void ConnectGesture::removeConnectMarker()
{
    m_overlap.removeConnectMarker();
}

void ConnectGesture::removeConnectHalo()
{
    m_overlap.removeConnectHalo();
}

void ConnectGesture::updateConnectHalo()
{
    if (!m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;
    const Vec2 src = blk->worldPos(m_connectFromPoint);
    m_overlap.updateConnectHalo(src);
}

void ConnectGesture::updateCandidateHighlight(
    const Vec2& pos, const std::vector<ConfirmCandidate>& candidates)
{
    m_overlap.updateHighlightAt(pos, candidates);
}

void ConnectGesture::removeConfirmHighlight()
{
    m_overlap.removeConfirmHighlight();
}

void ConnectGesture::updateSourcePortMarker()
{
    if (m_selectedSourceCandidate.has_value())
        m_overlap.setSourcePortMarker(*m_selectedSourceCandidate);
    else
        m_overlap.removeSourcePortMarker();
}

void ConnectGesture::removeSourcePortMarker()
{
    m_overlap.removeSourcePortMarker();
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
                m_confirmCandidates = m_overlap.collectConfirmCandidates(
                    refPos, m_connectFromBlock);
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
