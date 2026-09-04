#include "RotateCopyGesture.h"

#include <cmath>

#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Angle.h"
#include "tools/ToolRotate.h"
#include "canvas/CanvasScene.h"
#include "document/commands/BlockCommands.h"

namespace cad::tools {

RotateCopyGesture::RotateCopyGesture(ToolRotate* owner)
    : m_owner(owner)
{
}

void RotateCopyGesture::begin(const Vec2& pos)
{
    ToolRotate& o = *m_owner;
    if (o.m_state != RotateState::Ready || !o.m_paramDoc) return;

    const cad::param::Block* orig = o.m_paramDoc->findBlock(o.m_session.blockId());
    if (!orig || orig->segments.empty()) return;

    // 挂接点 = 当前锚心点（起点或终点，X/点击可切换）。
    // 克隆挂回原块，跟随角度 = 相对原线当前朝向的角度。
    m_pivotPointId = o.m_session.anchor().pointId;
    // 参考线段 = pivot 点的出口线段（闭合基准 2026-08: followerAngle
    // 0° = 折叠重叠、180° = 沿原线继续直行）。
    m_leaderSegmentId = orig->exitSegmentAtPoint(m_pivotPointId);
    if (m_leaderSegmentId.isNull() && !orig->segments.empty())
        m_leaderSegmentId = orig->segments.front().id;

    // Clone the single target block: outside attachments/groups are dropped,
    // so the clone starts as an independent copy overlapping the original.
    m_copyResult = cad::param::duplicateBlocks(*o.m_paramDoc, {o.m_session.blockId()});
    if (m_copyResult.isEmpty()) return;
    const cad::param::Block& clone = m_copyResult.blocks.front();
    m_cloneBlockId = clone.id;

    // 副本对齐点对齐旋转锚点（用户拍板 2026-09）:
    // 旋转锚心是起点时，副本以自身起点为对齐点；锚心是终点时，副本以自身终点为对齐点。
    if (clone.segments.empty()) { m_copyResult = {}; return; }
    const auto& origSeg = orig->segments.front();
    const auto& cloneSeg = clone.segments.front();
    if (m_pivotPointId == origSeg.endPointId) {
        m_clonePivotPointId = cloneSeg.endPointId;
    } else {
        m_clonePivotPointId = cloneSeg.startPointId;
    }

    // The clone must NOT inherit the original's endpoint-aim constraint — the
    // resolver would pull the copy straight back to the target point on every
    // frame (旋转复制 = 副本相对原线自由转动, 用户拍板).
    m_copyResult.blocks.front().endTargetBlockId = QUuid();
    m_copyResult.blocks.front().endTargetPointId = QUuid();
    m_copyResult.blocks.front().endTargetOffset = 0.0;
    m_copyResult.blocks.front().endTargetOffsetFormula.clear();

    // Live preview (no undo): linked vars → clone → clone→original attachment.
    // 复制基准 2026-08 定稿: 副本的相对 0° 恒 = 与原线当前朝向重叠，与锚心
    // 无关（起点/终点锚心、自由线/连接线统一）。基准偏移 = 原线世界朝向 −
    // 挂接点出口世界方向（= 克隆 Resolver 的 refWorld）：闭合基准存储
    // followerAngle = 180° − (baseOffset + relDeg) → Resolver 世界角 =
    // attachExit + π − FA = attachExit + baseOffset + rel = 原线朝向 + rel。
    // （旧实现用"锚心偏移 0/180°"近似：水平自由线碰巧正确，斜线与连接线
    // 差 180°−α —— 用户报告"复制以 180° 创建"。）
    m_attachExitRad = 0.0;
    if (!orig->segments.empty())
        m_attachExitRad = orig->transform.rotation
                          + orig->exitDirectionAtPoint(m_pivotPointId, m_leaderSegmentId);
    m_baseOffsetDeg = o.originalWorldRotDeg() - cad::geo::radToDeg(m_attachExitRad);
    for (const auto& lv : m_copyResult.newLinked)
        o.m_paramDoc->addLinked(lv);
    o.m_paramDoc->addBlock(clone);
    cad::param::Attachment cloneAtt;
    cloneAtt.fromBlockId = m_cloneBlockId;
    cloneAtt.fromPointId = m_clonePivotPointId;
    cloneAtt.toBlockId = o.m_session.blockId();
    cloneAtt.toPointId = m_pivotPointId;
    cloneAtt.toSegmentId = m_leaderSegmentId;
    cloneAtt.followerAngle = 180.0 - m_baseOffsetDeg;   // 闭合基准存储
    cloneAtt.rotationMode = cad::param::RotationMode::Angle;
    m_cloneAttId = cloneAtt.id;
    if (!o.m_paramDoc->addAttachment(cloneAtt)) {
        // Rejected (should not happen for same-layer original→clone): drop.
        removeCopyPreview();
        m_copyResult = {};
        m_cloneBlockId = QUuid();
        m_cloneAttId = QUuid();
        return;
    }

    // 拖动基准：克隆从"当前相对角 0°"开始拖（相对角 = 工具参考方向 +
    // 锚心偏移，显示空间即拖动空间，2026-08 v3 定稿）。m_refWorldRad 保持
    // 原样——endpointAtAngle / gizmo 直接读工具参考方向。
    const Vec2 d = pos - o.m_session.pivot();
    o.m_dragCursorAngle0 = std::atan2(d.y, d.x);
    o.m_dragCursorAnglePrev = o.m_dragCursorAngle0;
    o.m_accumulatedAngleDeg = 0.0;
    o.m_dragAngle0 = 0.0;   // relative angle starts at 0 (clone on original)

    m_copyMode = true;
    o.m_state = RotateState::Rotating;
    // 复制态: 条带解除锁定 (拍板 —— 复制显示的是"绕锚心相对角", 与跟随角/
    // 绝对角是三套语义, 塞进同一个角度框必然出错; 相对角读数走状态栏)。
    o.reportPinnedTarget(QUuid(), QUuid());
    o.updateGizmo();   // 内含 updateStatusHint → 状态栏显示相对角
    if (o.m_scene) o.m_scene->refreshAllBlockItems();
}

void RotateCopyGesture::convert(const Vec2& pos)
{
    ToolRotate& o = *m_owner;
    if (o.m_state != RotateState::Rotating || m_copyMode || !o.m_paramDoc) return;
    // 跟随模式 / 已释放挂接的普通旋转语义复杂，中途转换不做（保持原样）。
    if (o.m_session.isConnected() || o.m_session.anchor().releaseAttHeld) return;

    cad::param::Block* blk = o.m_paramDoc->findBlock(o.m_session.blockId());
    if (!blk || blk->segments.empty()) return;

    // 已转的相对角度 = 当前世界方向 − 原块初始世界方向。
    const double relRad = cad::geo::normalizeRad(
        (blk->transform.rotation + o.m_session.localDir())
        - (o.m_session.base().baseTf.rotation + o.m_session.localDir()));
    const double relDeg = cad::geo::radToDeg(relRad);

    // 与 beginRotateCopy 相同的克隆/挂接流程。
    m_pivotPointId = o.m_session.anchor().pointId;
    m_leaderSegmentId = blk->exitSegmentAtPoint(m_pivotPointId);
    if (m_leaderSegmentId.isNull() && !blk->segments.empty())
        m_leaderSegmentId = blk->segments.front().id;

    m_copyResult = cad::param::duplicateBlocks(*o.m_paramDoc, {o.m_session.blockId()});
    if (m_copyResult.isEmpty()) return;
    const cad::param::Block& clone = m_copyResult.blocks.front();
    m_cloneBlockId = clone.id;

    // 副本对齐点对齐旋转锚点（与 begin() 一致，用户拍板 2026-09）。
    if (clone.segments.empty()) { m_copyResult = {}; return; }
    const auto& origSeg = blk->segments.front();
    const auto& cloneSeg = clone.segments.front();
    if (m_pivotPointId == origSeg.endPointId) {
        m_clonePivotPointId = cloneSeg.endPointId;
    } else {
        m_clonePivotPointId = cloneSeg.startPointId;
    }

    // 复制基准 = 原线旋转前（base 姿态）的世界朝向（相对 0° = 与原线 base
    // 姿态重叠）——此时原块尚未回弹（仍处于旋转后的姿态），不能读 live
    // 姿态，须用 m_baseTf + 首段局部方向计算（与 begin() 的
    // originalWorldRotDeg() 等价）。挂接点出口方向取 base 姿态旋转角
    // （exitDirectionAtPoint 返回局部方向，与姿态无关）。
    double baseOrigRotDeg = 0.0;
    if (!blk->segments.empty()) {
        const auto& seg0 = blk->segments.front();
        const auto* sp = blk->findPoint(seg0.startPointId);
        const auto* ep = blk->findPoint(seg0.endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            const cad::geo::Vec2 d = ep->resolvedPos - sp->resolvedPos;  // 局部方向
            baseOrigRotDeg = std::atan2(d.y, d.x) * 180.0 / M_PI;
        }
    }
    baseOrigRotDeg += o.m_session.base().baseTf.rotation * 180.0 / M_PI;
    m_attachExitRad = o.m_session.base().baseTf.rotation + blk->exitDirectionAtPoint(m_pivotPointId,
                                                                      m_leaderSegmentId);
    m_baseOffsetDeg = baseOrigRotDeg - cad::geo::radToDeg(m_attachExitRad);

    // 副本不继承终点指向（与 beginRotateCopy 一致）。
    m_copyResult.blocks.front().endTargetBlockId = QUuid();
    m_copyResult.blocks.front().endTargetPointId = QUuid();
    m_copyResult.blocks.front().endTargetOffset = 0.0;
    m_copyResult.blocks.front().endTargetOffsetFormula.clear();

    for (const auto& lv : m_copyResult.newLinked)
        o.m_paramDoc->addLinked(lv);
    o.m_paramDoc->addBlock(clone);
    cad::param::Attachment cloneAtt;
    cloneAtt.fromBlockId = m_cloneBlockId;
    cloneAtt.fromPointId = m_clonePivotPointId;
    cloneAtt.toBlockId = o.m_session.blockId();
    cloneAtt.toPointId = m_pivotPointId;
    cloneAtt.toSegmentId = m_leaderSegmentId;
    cloneAtt.followerAngle = 180.0 - (m_baseOffsetDeg + relDeg);   // 闭合基准存储
    cloneAtt.rotationMode = cad::param::RotationMode::Angle;
    m_cloneAttId = cloneAtt.id;
    if (!o.m_paramDoc->addAttachment(cloneAtt)) {
        removeCopyPreview();
        m_copyResult = {};
        m_cloneBlockId = QUuid();
        m_cloneAttId = QUuid();
        return;
    }

    // addBlock 可能使 m_blocks 重新分配，原 blk 指针已悬挂——重新查找。
    blk = o.m_paramDoc->findBlock(o.m_session.blockId());
    if (!blk) { removeCopyPreview(); m_copyResult = {}; return; }
    // 原块回弹到旋转前姿态（含终点指向：普通旋转已清除的约束一并恢复），
    // 已转角度转移到副本——与从开始就 Ctrl+press 的复制效果一致。
    blk->transform = o.m_session.base().baseTf;
    blk->endTargetBlockId = o.m_session.base().baseEndTargetBlock;
    blk->endTargetPointId = o.m_session.base().baseEndTargetPoint;
    // 原块回弹 base 姿态后跟随线由 Resolver 按基准自动回位, 无需工具回写。
    o.m_paramDoc->resolveAll();

    // 拖动基准切换到当前位置：后续 delta 从转换点继续（相对角 = 原线 base
    // 朝向 + 相对角，2026-08 定稿）。
    const Vec2 d = pos - o.m_session.pivot();
    o.m_dragCursorAngle0 = std::atan2(d.y, d.x);
    o.m_dragCursorAnglePrev = o.m_dragCursorAngle0;
    o.m_accumulatedAngleDeg = 0.0;
    // 相对角显示 = relDeg（逆时针为正，2026-08 v3 定稿），显示空间即拖动空间。
    o.m_dragAngle0 = relDeg;

    m_copyMode = true;
    // 复制态: 条带解除锁定 (同上 —— 相对角不与跟随角/绝对角共用一个框)。
    o.reportPinnedTarget(QUuid(), QUuid());
    o.updateGizmo();
    if (o.m_scene) o.m_scene->refreshAllBlockItems();
}

void RotateCopyGesture::applyAngle(double deg)
{
    ToolRotate& o = *m_owner;
    if (!o.m_paramDoc) return;
    // Rotate-copy: deg 即相对角（0 = 与原线重叠，逆时针为正，2026-08 定稿），
    // 直接入存储域: followerAngle = 180° − (baseOffset + relDeg)
    // （闭合基准存储）。Formula cleared by direct manipulation.
    if (auto* a = o.m_paramDoc->findAttachment(m_cloneAttId)) {
        a->followerAngle = 180.0 - (m_baseOffsetDeg + deg);
        a->followerAngleFormula.clear();
    }
    // Per-frame hot path (旋转复制拖动每帧): resolve ONLY the clone's dirty
    // subgraph; the original stays at its base pose (frozen, out of affected).
    // Old resolveAll() + refreshAllBlockItems() re-resolved the whole document
    // and rebuilt every block item per frame.
    o.m_paramDoc->resolveForDrag(QList<QUuid>{m_cloneBlockId});
    if (o.m_scene) o.m_scene->syncBlockPositions();
}

void RotateCopyGesture::applyFormulaValue(double value)
{
    ToolRotate& o = *m_owner;
    if (!o.m_paramDoc) return;
    // Formula typed into the copy HUD: the clone never keeps a formula
    // (用户拍板: 副本自动去公式) — apply the value as a pivot-relative angle
    // （逆时针为正，2026-08 v3 定稿）。
    if (auto* a = o.m_paramDoc->findAttachment(m_cloneAttId)) {
        a->followerAngle = 180.0 - (m_baseOffsetDeg + value);
        a->followerAngleFormula.clear();
    }
    o.m_paramDoc->resolveForDrag(QList<QUuid>{m_cloneBlockId});
    if (o.m_scene) o.m_scene->syncBlockPositions();
}

double RotateCopyGesture::currentRelativeAngle() const
{
    const ToolRotate& o = *m_owner;
    if (!o.m_paramDoc) return 0.0;
    // Rotate-copy: the clone's pivot-relative angle, normalized to [0, 360°)
    // （逆时针为正，2026-08 v3 定稿; relDeg = 180° − offset − followerAngle;
    // 0 = overlap, 闭合基准存储）。显示空间即相对角空间，无镜像。
    const auto& atts = o.m_paramDoc->attachments();
    for (const auto& a : atts) {
        if (a.id != m_cloneAttId) continue;
        double deg = cad::geo::normalizeDeg360(180.0 - m_baseOffsetDeg - a.followerAngle);
        return deg;
    }
    return 0.0;
}

double RotateCopyGesture::relToWorldRad(double relDeg) const
{
    // 克隆世界方向 = 挂接点出口世界方向 + 复制基准偏移 + 相对角（=
    // 原线当前朝向 + relDeg）。FIX 2026-08: 旧基准 m_copyRefWorldRad − π
    // 使自由线差 180°、连接线差 α —— 黄弧端点、瞄准吸附全偏（用户报告
    // 复制 HUD 画外弧/整圈）。followerAngle = 180 − (baseOffset + rel)，
    // 世界角 = attachExit + π − followerAngle = attachExit + baseOffset +
    // rel = 原线朝向 + rel，恒等。
    return m_attachExitRad + (m_baseOffsetDeg + relDeg) * M_PI / 180.0;
}

double RotateCopyGesture::worldRadToRel(double worldRad) const
{
    return (worldRad - m_attachExitRad) * 180.0 / M_PI - m_baseOffsetDeg;
}

double RotateCopyGesture::currentWorldRad() const
{
    // 相对角显示 = relDeg（逆时针为正，2026-08 v3 定稿），显示空间即
    // rel 空间，直接换算世界角。
    return relToWorldRad(currentRelativeAngle());
}

void RotateCopyGesture::commit()
{
    ToolRotate& o = *m_owner;
    if (o.m_state != RotateState::Rotating || !m_copyMode) return;

    // Read the final clone-attachment state (angle and optional formula).
    double finalAngle = 0.0;
    QString finalFormula;
    const auto& atts = o.m_paramDoc->attachments();
    for (const auto& a : atts) {
        if (a.id == m_cloneAttId) {
            finalAngle = a.followerAngle;
            finalFormula = a.followerAngleFormula;
            break;
        }
    }
    // Normalise to (-180, 180] so a full 360° swing back counts as "no
    // rotation" (转回原位 = 不复制). Pivot-relative 0° = stored angle
    // 180° − offset（闭合基准存储 2026-08）。
    double n = cad::geo::normalizeDeg180(180.0 - m_baseOffsetDeg - finalAngle);
    const bool zeroAngle = std::abs(n) < 1e-6 && finalFormula.isEmpty();

    removeCopyPreview();   // drop the live clone (original untouched)

    if (zeroAngle || !o.m_undoStack) {
        reset();   // discard: back to Ready, nothing created
        // 复制语义终结: 条带重新锁定回原线段 (复制期间它是解除锁定的)。
        // 旧的 hideHud 是为了防止输入框目标悄然切回原线角度; 现在精确输入
        // 归条带, 只要把焦点锁回原线段即可, 条带显示的就是原线的角度。
        o.reportStripTarget();
        o.updateGizmo();
        return;
    }

    // Restore-then-replay: ONE undo step re-creates clone + attachment with
    // the final relative angle.
    o.m_undoStack->push(new cad::cmd::RotateCopyCommand(
        o.m_paramDoc, std::move(m_copyResult), o.m_session.blockId(), m_pivotPointId,
        m_clonePivotPointId, m_leaderSegmentId, finalAngle, finalFormula));
    reset();
    // 同上: 副本已落地, 相对角语义结束 —— 条带锁定回原线段。
    o.reportStripTarget();
    o.updateGizmo();
}

void RotateCopyGesture::cancel()
{
    ToolRotate& o = *m_owner;
    removeCopyPreview();
    reset();
    o.clearAimCandidate();
    // 复制已放弃: 条带锁定回原线段。
    o.reportStripTarget();
    o.updateGizmo();
}

void RotateCopyGesture::removeCopyPreview()
{
    ToolRotate& o = *m_owner;
    if (!o.m_paramDoc) return;
    if (!m_cloneAttId.isNull())
        o.m_paramDoc->removeAttachment(m_cloneAttId);
    if (!m_cloneBlockId.isNull())
        o.m_paramDoc->removeBlock(m_cloneBlockId);
    for (const auto& lv : m_copyResult.newLinked)
        o.m_paramDoc->removeLinked(lv.id);
    if (o.m_scene) o.m_scene->refreshAllBlockItems();
}

void RotateCopyGesture::reset()
{
    m_copyResult = {};
    m_cloneBlockId = QUuid();
    m_cloneAttId = QUuid();
    m_clonePivotPointId = QUuid();
    m_copyMode = false;
    m_owner->m_state = RotateState::Ready;
}

} // namespace cad::tools
