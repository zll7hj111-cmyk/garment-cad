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
        m_componentSwitchCandidates = m_overlap.collectComponentSwitchCandidates(
            toBlk->worldPos(toPointId), toBlockId, att.toSegmentId,
            m_connectFromBlock, m_connectComponentId);
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

// ── 组件级连接重叠切换 (AngleInput 窗口, 用户要求 2026-09) ──────────────────
// 组件级连接释放时直接连最近候选 (2026-09 定稿: 组件接谁谁就是基准线); 若连接
// 点有多个端点重叠, 角度窗口内点击重叠线段的线身即可把跟随对象切换过去 —
// 先点选线段、再确定基准, 与 ConfirmTarget 的视觉语言一致 (橙线高亮).

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
    m_componentSwitchCandidates = m_overlap.collectComponentSwitchCandidates(
        toBlk->worldPos(cand.pointId), cand.blockId, cand.segId,
        m_connectFromBlock, m_connectComponentId);
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        beginAngleSession(att.id, angleDeg);
    }
    return true;
}
} // namespace cad::tools
