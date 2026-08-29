#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
#include "ui/PointRefEdit.h"
#include "ui/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/Theme.h"

namespace cad::ui {


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

// 2026-12-15 用户拍板删模式下拉: 原 detachWithCache() (切「独立线段」= 快照+
// 拆除) 已并入「位置吸附」✗ /「清除」 — 见 onFollowHostToggled(false) / onClear。

void SegmentConnectionCard::onClear()
{
    if (!m_doc) return;

    // Locate the mutable follower attachment (same search as onTargetResolved).
    const cad::param::Attachment* found = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { found = &a; break; }
    }
    if (!found) { refreshCard(); return; }

    // 记忆宿主 + 快照完整连接配置 (用户拍板 2026-10 + 2026-12-15):
    // 「清除」与「位置吸附 ✗」同语义 — 断开后记忆最近宿主, 重新勾选
    // 「位置吸附」原样恢复 (公式/角度基准/滑轨/焊接都在, 无跳变).
    m_hostMemBlockId = found->toBlockId;
    m_hostMemPointId = found->toPointId;
    m_modeCache = *found;

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
    // 引用预填自动落库已移至 SegmentRefCard::refresh (2026-12 面板重设计)。

    refresh();
    emit changed(ChangeKind::Connected);
}

// 连接行「连接线段」编辑: 输入 L#/P# 名 → 已有连接=重定向; 自由线=填入连接点.
void SegmentConnectionCard::onLeaderSegEdited()
{
    if (!m_doc) return;
    const QString text = m_lblLeaderRef->text().trimmed();
    if (text.isEmpty()) return;

    QUuid blkId, segId, ptId;
    bool found = false;
    for (const auto& b : m_doc->blocks()) {
        if (b.id == m_blockId) continue;
        for (const auto& s : b.segments) {
            const QString label = cad::param::Serial::tag(s.serial);
            if (label.compare(text, Qt::CaseInsensitive) == 0
                || (!s.name.isEmpty() && s.name == text)) {
                blkId = b.id; segId = s.id;
                ptId = s.startPointId;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    if (!found) {
        for (const auto& b : m_doc->blocks()) {
            if (b.id == m_blockId) continue;
            for (const auto& p : b.points) {
                const QString label = cad::param::Serial::tag(p.serial);
                if (label.compare(text, Qt::CaseInsensitive) == 0
                    || (!p.name.isEmpty() && p.name == text)) {
                    const QUuid s = b.exitSegmentAtPoint(p.id);
                    if (!s.isNull()) {
                        blkId = b.id; segId = s; ptId = p.id;
                        found = true;
                    }
                    break;
                }
            }
            if (found) break;
        }
    }
    if (!found) { refreshCard(); return; }

    if (const auto* att = findFollowerAttachment()) {
        onTargetResolved(blkId, ptId);
    } else {
        m_refConnPoint->setPoint(blkId, ptId);
        refreshCard();
    }
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

        // 断开记忆快照 (用户 2026-12-15 拍板: 原模式切换缓存并入复选框):
        // 快照有效 → 原样恢复 (保留 公式/角度基准/滑轨/角度独立/焊接),
        // 避免"断开再勾选 = 新建"丢配置; 快照失效才退回记忆宿主新建。
        if (!existing && m_modeCache && m_modeCache->fromBlockId == m_blockId) {
            const auto* leaderC = m_doc->findBlock(m_modeCache->toBlockId);
            const auto* ptC = leaderC ? leaderC->findPoint(m_modeCache->toPointId)
                                      : nullptr;
            if (leaderC && ptC) {
                cad::param::Attachment cand = *m_modeCache;
                // 线段在宿主点可能已变化 → 按当前宿主点重新解析所在线段.
                const QUuid segId = leaderC->exitSegmentAtPoint(m_modeCache->toPointId);
                if (!segId.isNull()) {
                    cand.toSegmentId = segId;
                    // 森林/值环校验 (addAttachmentRaw 不校验, 恢复前预检).
                    std::vector<cad::param::Attachment> others;
                    for (const auto& a : m_doc->attachments())
                        others.push_back(a);
                    if (cad::param::checkAttachment(others, cand)
                            == cad::param::AttachmentIssue::Ok) {
                        if (auto* stack = m_doc->undoStack())
                            stack->push(new cad::cmd::AddAttachmentCommand(m_doc, cand));
                        else
                            m_doc->addAttachment(cand);
                        // 同步快照 + 记忆宿主为恢复后的最新连接.
                        if (const auto* now = findFollowerAttachment()) {
                            m_modeCache = *now;
                            m_hostMemBlockId = now->toBlockId;
                            m_hostMemPointId = now->toPointId;
                        }
                        refresh();
                        emit changed(ChangeKind::Connected);
                        return;
                    }
                }
            }
            // 快照失效 (宿主已删/线段无法解析/校验拒绝): 丢弃, 退回记忆宿主新建.
            m_modeCache.reset();
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
        // 引用预填自动落库已移至 SegmentRefCard::refresh (2026-12 面板重设计)。
        if (const auto* now = findFollowerAttachment()) {
            m_hostMemBlockId = now->toBlockId;
            m_hostMemPointId = now->toPointId;
        }
        refresh();
        emit changed(ChangeKind::Connected);
    } else {
        if (existing) {
            // 连接线段 ✗ = 彻底断开 (用户拍板 2026-10): 删除 attachment,
            // 位置吸附与角度跟随一并解除 — 不再退化为「仅角度」。
            // 终点指向 (endTarget) 是独立约束, 不随断开清空。
            // 2026-12-15: 断开前快照完整配置 (重新勾选原样恢复, 不丢设置)。
            m_hostMemBlockId = existing->toBlockId;
            m_hostMemPointId = existing->toPointId;
            m_modeCache = *existing;
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

void SegmentConnectionCard::onShadowResetClicked()
{
    // 影子偏转归零 (§2.6, 2026-08-27): offset → 0, 本线物理转向与基准当前
    // 方向对齐; SetAttachmentBaselineOffsetCommand 一步 undo。
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att || std::abs(att->baselineOffsetDeg) <= 1e-9) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentBaselineOffsetCommand(
            m_doc, att->id, 0.0));
    else {
        if (auto* mut = m_doc->findAttachment(att->id))
            mut->baselineOffsetDeg = 0.0;
        m_doc->resolveAll();
    }
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

    // 留空 = 0 (tooltip 承诺: "留空/0 表示不偏移") —— 只填一轴也是合法输入
    // (用户 2026-12 反馈: 只填「水平」回车后无任何生效痕迹, 原实现要求两轴
    // 都能解析, 留空直接 return + refreshCard 清空输入框 → 静默无效)。
    // 数值或公式 (cm 域, 用户 2026-12 提问): 纯数字 = 数值 (clear 公式); 其他 = 公式
    // (公式优先, 与长度/延长/角度卡同约定; 输入框 .00 只是数值回显)。
    const QString aRaw = m_editSlideAlong->text().trimmed();
    const QString pRaw = m_editSlidePerp->text().trimmed();
    bool okA = false, okP = false;
    const double alongCm = aRaw.isEmpty() ? 0.0 : aRaw.toDouble(&okA);
    const double perpCm  = pRaw.isEmpty() ? 0.0 : pRaw.toDouble(&okP);
    const bool aFormula = !aRaw.isEmpty() && !okA;   // 非数字 = 公式
    const bool pFormula = !pRaw.isEmpty() && !okP;
    // 卡片使用 CM，存储为 mm。
    const double along = cad::geo::Units::cmToMm(alongCm);
    const double perp  = cad::geo::Units::cmToMm(perpCm);

    // 有值 (数值或公式) 即算该轴生效; 双轴都空/0 = 退回全连接。
    const bool hasAlong = !aRaw.isEmpty();
    const bool hasPerp  = !pRaw.isEmpty();

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
        if (aFormula) {
            mut->slideAlongFormula = aRaw;
        } else {
            mut->slideAlongFormula.clear();
            mut->slideAlongMm = along;
        }
        if (pFormula) {
            mut->slidePerpFormula = pRaw;
        } else {
            mut->slidePerpFormula.clear();
            mut->slidePerpMm = perp;
        }
        m_doc->resolveAll();
    }
    refreshCard();
    // 不 emit changed(SlideModeChanged): 该信号经 changed → LinePropertyDialog::
    // onConnCardChanged 链会令对话框析构期触发 Qt 6.11 assertObjectType 断言
    // ("class destructor may have already run", 滑轨输入回车后关闭属性对话框必崩;
    // 空槽也复现, 实证为信号发放本身)。画布刷新不需要它: 上方的 resolveAll() →
    // ParamDocument::resolved → CanvasScene 观察者链自动同步; 对话框的卡片
    // refreshCard() 已就地刷新; onConnCardChanged 对 SlideModeChanged 本就只走
    // default no-op 分支。
}

} // namespace cad::ui
