#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>
#include <vector>

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


// ── 连接生命周期：建立 / 重定向 / 拆开↔重连 (位置维度) / ──
// 滑轨模式与偏移 (2026-08 拆分)。
// 2026-xx: 「连接线段」复选框与「清除」按钮已删 —— 连接 = 输入 P#,
// 断开/恢复 = 连接点「拆开/重连」(位置维度) + 基准点「拆开/重连」(角度维度,
// SegmentRefCard), 两维独立。

void SegmentConnectionCard::onConnPointResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    // 「连接点」统一入口: 已有连接 → 重定向; 自由线 → 建立连接。
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

void SegmentConnectionCard::onConnectToResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Block* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refreshCard(); return; }

    // Build the new attachment (same back-solve as the old checkbox path).
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

// 拆开/重连 双面按钮 (位置维度, 2026-xx 用户拍板两维独立):
//   · 拆开 → SetAttachmentAngleOnlyCommand(true): 位置自由、角度仍跟随基准线
//     (与 D 键快拆/拖拆同语义, 一次 undo)。不碰角度维度 (angleIndependent)。
//   · 重连 → SetAttachmentAngleOnlyCommand(false): 位置重新吸附回原宿主点并
//     重新焊接 (isLocked=true), 角度基准保留。
// 无连接禁用 (refreshUnifiedState 已置灰)。
void SegmentConnectionCard::onDetachClicked()
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_doc, att->id, /*angleOnly=*/!att->angleOnly));
    else
        m_doc->setAttachmentAngleOnly(att->id, !att->angleOnly);
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
    if (!att || att->angleOnly || att->angleIndependent) { refreshCard(); return; }  // 拆开/独立角态禁用

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
    // 拆开/独立角态滑轨输入禁用 (refreshUnifiedState 已置灰; 防御拒绝).
    if (!att || att->angleOnly || att->angleIndependent) { refreshCard(); return; }

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
