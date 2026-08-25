#include "SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QComboBox>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "PointRefEdit.h"
#include "tools/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {


// ── 连接生命周期：建立 / 拆除 / 重定向 / 位置吸附 / 拖动保护 / ──
// 角度独立 / 快拆 / 滑轨模式与偏移 (2026-08 拆分)。

void SegmentConnectionCard::onConnPointResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    // 「连接点」统一入口: 已有连接 → 重定向到新端点; 自由线 → 建立连接。
    if (findFollowerAttachment())
        onTargetResolved(blockId, pointId);
    else
        onConnectToResolved(blockId, pointId);
}

void SegmentConnectionCard::onTargetResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;

    // Locate the mutable follower attachment.
    cad::param::Attachment* att = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { att = m_doc->findAttachment(a.id); break; }
    }
    if (!att) return;

    // Validate: would the re-targeted attachment create a cycle?
    cad::param::Attachment candidate = *att;
    candidate.toBlockId = blockId;
    candidate.toPointId = pointId;
    std::vector<cad::param::Attachment> others;
    for (const auto& a : m_doc->attachments())
        if (a.id != att->id) others.push_back(a);
    if (cad::param::checkAttachment(others, candidate)
            != cad::param::AttachmentIssue::Ok) {
        refreshCard();  // Revert the widget display.
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    auto* block = m_doc->findBlock(m_blockId);
    if (!leader || !block) { refreshCard(); return; }

    // 仅角度拖动到新端点：旧角度基准保留为独立角度基准，位置挂到新端点，
    // 从而自动进入“双基准”。
    if (att->angleOnly) {
        if (att->angleRefBlockId.isNull()) {
            att->angleRefBlockId = att->toBlockId;
            att->angleRefSegmentId = att->toSegmentId;
            att->angleRefPointId = att->toPointId;
        }
        att->angleOnly = false;
        att->slideMode = cad::param::SlideMode::None;
        att->isLocked = true;
    }

    // Re-target.
    att->toBlockId = blockId;
    att->toPointId = pointId;
    att->toSegmentId = leader->exitSegmentAtPoint(pointId);

    // Back-solve the follower angle so the CURRENT world direction is
    // preserved (no visual jump on re-attach).
    if (auto* seg = block->findSegment(m_segmentId)) {
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(pointId, att->toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att->followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);
    }
    att->followerAngleFormula.clear();
    att->rotationMode = cad::param::RotationMode::Angle;
    att->arcLength = 0.0;
    att->arcLengthFormula.clear();

    // 滑轨模式重定向后: 锁轴坐标必须在**新**基准线局部系下重快照 (否则
    // 锁定的垂直/沿线偏移会沿用旧基准的坐标, 重定向瞬间跟随线会跳).
    if (att->slideMode != cad::param::SlideMode::None)
        m_doc->refreshSlideOffsets(att->id);

    refresh();
    emit changed(ChangeKind::Retargeted);
}

void SegmentConnectionCard::detachWithCache()
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att) return;

    // 快照完整连接配置 (宿主/角度/弧长/公式/角度基准/滑轨/角度独立/焊接),
    // 切回「跟随」时原样恢复 —— 不再裸删丢设置 (用户 2026-12 需求).
    m_modeCache = *att;
    // 记忆宿主 (用户拍板 2026-10) 同步更新.
    m_hostMemBlockId = att->toBlockId;
    m_hostMemPointId = att->toPointId;

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::RemoveAttachmentCommand(m_doc, att->id));
    else
        m_doc->removeAttachment(att->id);

    refresh();
    emit changed(ChangeKind::Disconnected);
}

void SegmentConnectionCard::onClear()
{
    if (!m_doc) return;

    // Locate the mutable follower attachment (same search as onTargetResolved).
    const cad::param::Attachment* found = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { found = &a; break; }
    }
    if (!found) { refreshCard(); return; }

    // 记忆宿主 (用户拍板 2026-10): 「清除」与「位置吸附 ✗」同语义 — 断开后
    // 记忆最近宿主, 勾选「位置吸附」可一键恢复.
    m_hostMemBlockId = found->toBlockId;
    m_hostMemPointId = found->toPointId;
    // 显式断开 = 模式切换缓存失效 (「清除」后切 独立↔跟随 不得复活旧连接).
    m_modeCache.reset();

    // Remove the attachment: the line becomes free (world angle preserved —
    // removeAttachment resolves, so the block simply stops being driven).
    m_doc->removeAttachment(found->id);

    refresh();
    emit changed(ChangeKind::Disconnected);
}

void SegmentConnectionCard::onConnectToResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Block* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refreshCard(); return; }

    // Build the new attachment (same back-solve as onFollowHostToggled).
    cad::param::Attachment att;
    att.fromBlockId = m_blockId;
    att.fromPointId = seg->startPointId;
    att.toBlockId   = blockId;
    att.toPointId   = pointId;
    att.toSegmentId = leader->exitSegmentAtPoint(pointId);

    const double refWorld = leader->transform.rotation
        + leader->exitDirectionAtPoint(pointId, att.toSegmentId);
    const double localDir = block->directionAtPoint(seg->startPointId);
    att.followerAngle = cad::param::backSolveFollowerAngle(
        block->transform.rotation, localDir, refWorld);

    // May be rejected (cycle / conflicting follower).
    const bool added = m_doc->addAttachment(att);
    if (added && m_scene) {
        // Toast only for genuinely NEW cross-layer connections.
        if (const QString toast = crossLayerToast(m_doc, *block, *leader);
            !toast.isEmpty())
            m_scene->showToast(toast);
        // 记忆宿主 (用户拍板 2026-10): 建立/重连成功后刷新记忆, 断开后可一键恢复.
        m_hostMemBlockId = blockId;
        m_hostMemPointId = pointId;
    }
    if (added) {
        // 如果双基准模式下已填了角度引用点，连接成功时立即落库，避免被自动回填覆盖。
        const auto* now = findFollowerAttachment();
        const QUuid rb = m_angleRefPoint->resolvedBlockId();
        const QUuid rp = m_angleRefPoint->resolvedPointId();
        if (now && !rb.isNull() && !rp.isNull() && (rb != blockId || rp != pointId)) {
            const auto* refBlk = m_doc->findBlock(rb);
            const QUuid rs = refBlk ? refBlk->exitSegmentAtPoint(rp) : QUuid();
            if (!rs.isNull()) {
                if (auto* stack = m_doc->undoStack())
                    stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                        m_doc, now->id, rb, rs, rp));
                else
                    m_doc->setAttachmentAngleRef(now->id, rb, rs, rp);
            }
        }
    }

    refresh();
    emit changed(ChangeKind::Connected);
}

void SegmentConnectionCard::onFollowHostToggled(bool on)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) { refreshCard(); return; }

    const cad::param::Attachment* existing = findFollowerAttachment();
    if (on) {
          // 单一位置连接开关: 已有仅角度连接 → 勾选恢复完整位置连接。
          if (existing) {
              if (existing->angleOnly) {
                  if (auto* stack = m_doc->undoStack())
                      stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                          m_doc, existing->id, /*angleOnly=*/false));
                  else
                      m_doc->setAttachmentAngleOnly(existing->id, false);
                  refresh();
                  emit changed(ChangeKind::Connected);
                  return;
              }
              refresh();
              return;  // 已是完整连接，无需重复创建。
          }

        // 勾选 = 建立连接: 宿主优先取记忆 (断开后一键恢复), 其次自由态输入框
        // 的解析值。角度反算保持当前世界方向 (无跳变)。新建默认带拖动保护
        // (addAttachment 统一置位)。
        const QUuid hostBlock = existing
            ? existing->toBlockId
            : (!m_hostMemBlockId.isNull() ? m_hostMemBlockId
                                          : m_refConnPoint->resolvedBlockId());
        const QUuid hostPoint = existing
            ? existing->toPointId
            : (!m_hostMemPointId.isNull() ? m_hostMemPointId
                                          : m_refConnPoint->resolvedPointId());
        const cad::param::Block* leader = m_doc->findBlock(hostBlock);
        if (!leader || !leader->findPoint(hostPoint)) {
            refreshCard();
            return;  // no host yet — the check rolls back
        }

        cad::param::Attachment att;
        att.fromBlockId = m_blockId;
        att.fromPointId = seg->startPointId;
        att.toBlockId   = hostBlock;
        att.toPointId   = hostPoint;
        att.toSegmentId = leader->exitSegmentAtPoint(hostPoint);
        // Back-solve the follower angle so the CURRENT world direction is
        // preserved (no jump on attach).
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(hostPoint, att.toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att.followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);

        const bool added = m_doc->addAttachment(att);
        if (added && m_scene && leader) {
            if (const QString toast = crossLayerToast(m_doc, *block, *leader);
                !toast.isEmpty())
                m_scene->showToast(toast);
        }
        // 记忆宿主: 勾选成功即刷新记忆 (断开后可继续一键恢复).
        const auto* now = findFollowerAttachment();
        if (now) {
            m_hostMemBlockId = now->toBlockId;
            m_hostMemPointId = now->toPointId;
            // 如果双基准模式下已填了角度引用点，连接成功时立即落库，避免被自动回填覆盖。
            const QUuid rb = m_angleRefPoint->resolvedBlockId();
            const QUuid rp = m_angleRefPoint->resolvedPointId();
            if (!rb.isNull() && !rp.isNull()
                && (rb != now->toBlockId || rp != now->toPointId)) {
                const auto* refBlk = m_doc->findBlock(rb);
                const QUuid rs = refBlk ? refBlk->exitSegmentAtPoint(rp) : QUuid();
                if (!rs.isNull()) {
                    if (auto* stack = m_doc->undoStack())
                        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                            m_doc, now->id, rb, rs, rp));
                    else
                        m_doc->setAttachmentAngleRef(now->id, rb, rs, rp);
                }
            }
        }
        refresh();
        emit changed(ChangeKind::Connected);
    } else {
        if (existing) {
            // 位置吸附 ✗ = 彻底断开 (用户拍板 2026-10): 删除 attachment,
            // 位置吸附与角度跟随一并解除 — 不再退化为「仅角度」。
            // 终点指向 (endTarget) 是独立约束, 不随断开清空。
            m_hostMemBlockId = existing->toBlockId;
            m_hostMemPointId = existing->toPointId;
            m_modeCache.reset();  // 显式断开 = 模式切换缓存失效.
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::RemoveAttachmentCommand(m_doc, existing->id));
            else
                m_doc->removeAttachment(existing->id);
            refresh();
            emit changed(ChangeKind::Disconnected);
        }
    }
}

void SegmentConnectionCard::onLockToggled(bool on)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    // 无连接 (断开态): 复选框禁用不会触发; 防御性刷新.
    if (!att) { refreshCard(); return; }
    // 语义 (用户拍板 2026-08 复旧): 「拖动保护」= isLocked 开关 (焊接: 拖任一端
    // 整对移动不拆)。**新建连接默认勾选 (焊接)**; 取消 = 解焊仍完整连接
    // (拖跟随线可拆散, 拆散 = D 键快拆)。
    //   · 勾选: 未焊完整连接 → SetAttachmentLockedCommand(true) 焊上;
    //           仅角度态 → SetAttachmentAngleOnlyCommand(false) 恢复完整连接
    //           (位置重新吸附回宿主点 + 重新焊接, redo 置 isLocked=true)。
    //   · 取消: 已焊完整连接 → SetAttachmentLockedCommand(false) 解除焊接
    //           (连接保持, 回到可拖拆) — 不是切换为仅角度。
    if (on) {
        if (att->angleOnly) {
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                    m_doc, att->id, /*angleOnly=*/false));
            else
                m_doc->setAttachmentAngleOnly(att->id, false);
        } else if (!att->isLocked) {
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::SetAttachmentLockedCommand(
                    m_doc, att->id, /*locked=*/true));
            else
                m_doc->setAttachmentLocked(att->id, true);
        } else {
            refreshCard();
            return;
        }
    } else {
        if (!att->angleOnly && att->isLocked) {
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::SetAttachmentLockedCommand(
                    m_doc, att->id, /*locked=*/false));
            else
                m_doc->setAttachmentLocked(att->id, false);
        } else {
            refreshCard();
            return;
        }
    }
    refreshCard();
    emit changed(ChangeKind::LockToggled);
}

void SegmentConnectionCard::onAngleIndependentToggled(bool on)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att) { refreshCard(); return; }

    // 已在目标状态则只刷新（避免 undo 栈里堆积无效命令）。
    if (att->angleIndependent == on) { refreshCard(); return; }

    // 仅角度 (位置自由) / 滑轨 (一轴滑轨) 与角度独立互斥；入口 UI 已禁用，
    // 这里再防御性拒绝。
    if (on && (att->angleOnly || att->slideMode != cad::param::SlideMode::None)) {
        refreshCard();
        return;
    }

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleIndependentCommand(
            m_doc, att->id, on));
    else
        m_doc->setAttachmentAngleIndependent(att->id, on);

    refresh();
    emit changed(ChangeKind::AngleIndependentToggled);
}

void SegmentConnectionCard::onAngleOnlyClicked()
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) { refresh(); return; }
    if (att->angleOnly) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_doc, att->id, true));
    else
        m_doc->setAttachmentAngleOnly(att->id, true);
    refresh();
    emit changed(ChangeKind::ConnectionModeChanged);
}

void SegmentConnectionCard::onSlideModeChanged(int index)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || att->angleOnly) { refreshCard(); return; }  // 拆开态禁用

    // Combo order mirrors cad::param::SlideMode (None=0 / AlongLeader=1 /
    // PerpLeader=2).
    const auto mode = static_cast<cad::param::SlideMode>(index);
    if (att->slideMode == mode) { refreshCard(); return; }
    m_doc->setAttachmentSlideMode(att->id, mode);
    refreshCard();
    emit changed(ChangeKind::SlideModeChanged);
}

void SegmentConnectionCard::onSlideOffsetEdited()
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att) { refreshCard(); return; }

    bool okA = false, okP = false;
    const double alongCm = m_editSlideAlong->text().trimmed().toDouble(&okA);
    const double perpCm  = m_editSlidePerp->text().trimmed().toDouble(&okP);
    if (!okA || !okP) { refreshCard(); return; }
    // 卡片使用 CM，存储为 mm。
    const double along = alongCm * 10.0;
    const double perp  = perpCm * 10.0;

    const bool hasAlong = std::abs(along) > 1e-9;
    const bool hasPerp  = std::abs(perp)  > 1e-9;

    cad::param::SlideMode mode = cad::param::SlideMode::None;
    if (hasAlong || hasPerp) {
        if (hasAlong && !hasPerp)
            mode = cad::param::SlideMode::AlongLeader;
        else if (!hasAlong && hasPerp)
            mode = cad::param::SlideMode::PerpLeader;
        else
            mode = att->slideMode != cad::param::SlideMode::None
                ? att->slideMode : cad::param::SlideMode::AlongLeader;
    }

    if (att->slideMode != mode)
        m_doc->setAttachmentSlideMode(att->id, mode);

    auto* mut = m_doc->findAttachment(att->id);
    if (mut) {
        mut->slideAlongMm = along;
        mut->slidePerpMm = perp;
        m_doc->resolveAll();
    }
    refreshCard();
    emit changed(ChangeKind::SlideModeChanged);
}

} // namespace cad::tools
