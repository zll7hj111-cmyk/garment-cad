#include "AttachmentCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── AddAttachmentCommand ───

AddAttachmentCommand::AddAttachmentCommand(cad::param::ParamDocument* doc,
                                           cad::param::Attachment att,
                                           QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_att(std::move(att))
{
    setText(QStringLiteral("添加连接"));
}

void AddAttachmentCommand::redo()
{
    // 快照完整性 (用户拍板 2026-09): verbatim 插入, 不经过 addAttachment 的
    // 强制 isLocked=true — undo/redo 必须原样还原用户状态 (与
    // RemoveAttachmentCommand::undo 的 addAttachmentRaw 对称)。
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att);
    m_doc->resolveAll();
}

void AddAttachmentCommand::undo()
{
    m_doc->removeAttachment(m_att.id);
}

// ─── RemoveAttachmentCommand ───

RemoveAttachmentCommand::RemoveAttachmentCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& attId,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("断开连接"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) { m_att = a; break; }
    }

    // Removing a bridge pin releases the bridge (the model layer converts it
    // to an independent segment) — snapshot its pristine state and every
    // attachment touching it so undo can restore the full bridge.
    if (m_att.isPin) {
        if (const auto* b = doc->findBlock(m_att.fromBlockId);
            b && b->isBridge) {
            m_bridge = *b;
            m_hasBridge = true;
            for (const auto& a : doc->attachments()) {
                if (a.fromBlockId == b->id || a.toBlockId == b->id)
                    m_bridgeAtts.push_back(a);
            }
        }
    }
}

void RemoveAttachmentCommand::redo()
{
    m_doc->removeAttachment(m_att.id);
}

void RemoveAttachmentCommand::undo()
{
    if (m_hasBridge) {
        // The bridge was released by redo() — replace the converted version
        // with the pristine snapshot, then restore all of its attachments
        // (both pins + any follower led by its auxiliary points).
        m_doc->removeBlock(m_bridge.id);
        m_doc->addBlock(m_bridge);
        for (const auto& a : m_bridgeAtts)
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, a);  // verbatim (keep snapshot isLocked)
        m_doc->resolveAll();
        return;
    }
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att);  // verbatim (keep snapshot isLocked)
    m_doc->resolveAll();
}

// ─── SetAttachmentAngleOnlyCommand ───

SetAttachmentAngleOnlyCommand::SetAttachmentAngleOnlyCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool angleOnly,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAngleOnly(angleOnly)
    , m_oldAngleOnly(false)
    , m_oldLocked(false)
    , m_oldSlideMode(cad::param::SlideMode::None)
{
    setText(QStringLiteral("\xe6\x8b\x86\xe5\xbc\x80\xe4\xbf\x9d\xe7\x95\x99\xe8\xa7\x92\xe5\xba\xa6"));  // 拆开保留角度

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldAngleOnly = a.angleOnly;
            m_oldLocked = a.isLocked;
            m_oldSlideMode = a.slideMode;
            break;
        }
    }
}

void SetAttachmentAngleOnlyCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->angleOnly = m_newAngleOnly;
        if (m_newAngleOnly) {
            a->isLocked = false;      // 拆开 = 位置自由: 与焊接互斥
            a->slideMode = cad::param::SlideMode::None;  // 与滑轨互斥
        } else {
            // 恢复完整连接 = 位置重新吸附回宿主点 + 重新焊接 (与 doc API
            // setAttachmentAngleOnly(false) 一致; 不得沿用 m_oldLocked —
            // 仅角度态 isLocked 恒为 false, 沿用会画出"✓ 拖动保护但可拖拆").
            a->isLocked = true;
        }
    }
    m_doc->resolveAll();
}

void SetAttachmentAngleOnlyCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->angleOnly = m_oldAngleOnly;
        a->isLocked = m_oldLocked;
        a->slideMode = m_oldSlideMode;
    }
    m_doc->resolveAll();
}

// ─── SetAttachmentAngleIndependentCommand ───

SetAttachmentAngleIndependentCommand::SetAttachmentAngleIndependentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool angleIndependent,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newIndependent(angleIndependent)
    , m_oldIndependent(false)
{
    setText(QStringLiteral("角度独立"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldIndependent = a.angleIndependent;
            m_oldAngleOnly = a.angleOnly;
            m_oldSlideMode = a.slideMode;
            m_oldLocked = a.isLocked;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAttachmentAngleIndependentCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    a->angleIndependent = m_newIndependent;
    if (m_newIndependent) {
        // 2026-xx 两维独立 (用户拍板): 角度独立只拆角度维度 —— 不再清除
        // angleOnly (位置维度由「连接点」按钮独立控制)。滑轨需角度跟随,
        // 进独立角时清除滑轨模式 (与 slideMode 仍互斥)。
        a->slideMode = cad::param::SlideMode::None;
    } else {
        // 退出独立角度: 反算当前世界方向对应的 followerAngle, 恢复角度跟随
        // 时不会跳线。若原来有公式/弧长模式则清掉, 以反算值为准。
        // 有效基准方向 = 与 Resolver 同构 (两点基准一并生效, 2026-09 审核
        // F6) —— 此前退出独立角的反算漏了 angleRef2。
        const auto* from = m_doc->findBlock(a->fromBlockId);
        const auto* to = m_doc->findBlock(a->toBlockId);
        if (from && to) {
            const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *a);
            const double localDir = from->directionAtPoint(a->fromPointId);
            a->followerAngle = cad::param::backSolveFollowerAngle(
                from->transform.rotation, localDir, refWorld);
            a->followerAngleFormula.clear();
            a->rotationMode = cad::param::RotationMode::Angle;
            a->arcLength = 0.0;
            a->arcLengthFormula.clear();
        }
        a->slideMode = cad::param::SlideMode::None;
    }
    m_doc->resolveAll();
}

void SetAttachmentAngleIndependentCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->angleIndependent = m_oldIndependent;
    a->angleOnly = m_oldAngleOnly;
    a->slideMode = m_oldSlideMode;
    a->isLocked = m_oldLocked;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}


// ─── SetAttachmentAngleRefCommand ───

SetAttachmentAngleRefCommand::SetAttachmentAngleRefCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newRefBlockId, const QUuid& newRefSegmentId,
    const QUuid& newRefPointId,
    const QUuid& newRef2BlockId, const QUuid& newRef2PointId,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newRefBlockId(newRefBlockId)
    , m_newRefSegmentId(newRefSegmentId)
    , m_newRefPointId(newRefPointId)
    , m_newRef2BlockId(newRef2BlockId)
    , m_newRef2PointId(newRef2PointId)
{
    setText(QStringLiteral("修改角度基准"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldRefBlockId = a.angleRefBlockId;
            m_oldRefSegmentId = a.angleRefSegmentId;
            m_oldRefPointId = a.angleRefPointId;
            m_oldRef2BlockId = a.angleRef2BlockId;
            m_oldRef2PointId = a.angleRef2PointId;
            m_oldAngleIndependent = a.angleIndependent;
            m_oldAngleOnly = a.angleOnly;
            m_oldSlideMode = a.slideMode;
            m_oldLocked = a.isLocked;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAttachmentAngleRefCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    a->angleRefBlockId = m_newRefBlockId;
    a->angleRefSegmentId = m_newRefSegmentId;
    a->angleRefPointId = m_newRefPointId;
    a->angleRef2BlockId = m_newRef2BlockId;
    a->angleRef2PointId = m_newRef2PointId;
    // 设置了独立角度基准时取消“角度独立”，因为角度现在受另一条线段约束。
    a->angleIndependent = false;

    // 反算当前世界方向对应的 followerAngle，避免切换角度基准时跳线。
    // 有效基准方向 = 与 Resolver 同构 (两点基准一并生效, 2026-09 审核 F6)
    // —— 此前 redo 反算漏了 angleRef2。
    const auto* from = m_doc->findBlock(a->fromBlockId);
    const auto* to = m_doc->findBlock(a->toBlockId);
    if (from && to) {
        const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *a);
        const double localDir = from->directionAtPoint(a->fromPointId);
        a->followerAngle = cad::param::backSolveFollowerAngle(
            from->transform.rotation, localDir, refWorld);
    }
    a->followerAngleFormula.clear();
    a->rotationMode = cad::param::RotationMode::Angle;
    a->arcLength = 0.0;
    a->arcLengthFormula.clear();
    m_doc->resolveAll();
}

void SetAttachmentAngleRefCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->angleRefBlockId = m_oldRefBlockId;
    a->angleRefSegmentId = m_oldRefSegmentId;
    a->angleRefPointId = m_oldRefPointId;
    a->angleRef2BlockId = m_oldRef2BlockId;
    a->angleRef2PointId = m_oldRef2PointId;
    a->angleIndependent = m_oldAngleIndependent;
    a->angleOnly = m_oldAngleOnly;
    a->slideMode = m_oldSlideMode;
    a->isLocked = m_oldLocked;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}


// ─── ReattachAttachmentCommand ───

ReattachAttachmentCommand::ReattachAttachmentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newToBlockId, const QUuid& newToPointId,
    const QUuid& newToSegmentId, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newToBlockId(newToBlockId)
    , m_newToPointId(newToPointId)
    , m_newToSegmentId(newToSegmentId)
{
    setText(QStringLiteral("重新挂接"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldAtt = a;
            m_hasOldAtt = true;
            if (const auto* b = doc->findBlock(a.fromBlockId)) {
                m_oldOrigin = b->transform.origin;
                m_oldRotation = b->transform.rotation;
            }
            break;
        }
    }
}

void ReattachAttachmentCommand::redo()
{
    if (!m_hasOldAtt) return;
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    cad::param::Attachment newAtt = *a;
    // 重连保持角度基准 (用户拍板 2026-09): 自动态下把旧所连线段固化为两点
    // 基准 (点1 = 旧目标点, 点2 = 旧线段另一端) —— 此前只固化点1, 点2 留空,
    // 两点连线方向退化为单点出口方向。已自定义的基准原样保留。
    // **必须在改写 toBlockId/toPointId 之前调用** (旧宿主信息仍在 newAtt 上)。
    cad::param::preserveAngleRefOnReattach(m_doc, newAtt);
    newAtt.toBlockId   = m_newToBlockId;
    newAtt.toPointId   = m_newToPointId;
    newAtt.toSegmentId = m_newToSegmentId;
    // 重新挂接 = 位置重新吸附, 退出仅角度/滑轨。
    newAtt.angleOnly = false;
    newAtt.slideMode = cad::param::SlideMode::None;
    // 仅角度 (拆开保留角度) 线拖回重挂 = 恢复完整连接 + 重新焊接 (与面板
    // 「拖动保护」恢复语义一致: 恢复完整连接必须重新焊接; 普通解焊连接
    // 重挂保持原焊接态不变)。
    if (m_oldAtt.angleOnly)
        newAtt.isLocked = true;

    // 按保留的角度基准反算 followerAngle, 保持当前世界方向 (无跳变)。
    // 基准方向 = 有效角度基准 (与 Resolver 同构): 重连保持基准后 = 旧基准
    // 两点连线方向, 自动态 = 新宿主出口方向 —— 此前手写 refWorld 只覆盖
    // 单点出口/线段 start→end 两分支, 两点基准 (angleRef2) 漏算 → 固化
    // 基准后重挂瞬间跳线。
    const auto* from = m_doc->findBlock(newAtt.fromBlockId);
    if (from && m_doc->findBlock(newAtt.toBlockId)) {
        const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, newAtt);
        const double localDir = from->directionAtPoint(newAtt.fromPointId);
        newAtt.followerAngle = cad::param::backSolveFollowerAngle(
            from->transform.rotation, localDir, refWorld);
    }
    newAtt.followerAngleFormula.clear();
    newAtt.rotationMode = cad::param::RotationMode::Angle;
    newAtt.arcLength = 0.0;
    newAtt.arcLengthFormula.clear();

    // 先删旧连接, 再以同一 id 原样插入新连接 (保持 isLocked 等快照字面量)。
    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, newAtt);
    m_doc->resolveAll();
}

void ReattachAttachmentCommand::undo()
{
    if (!m_hasOldAtt) return;
    if (auto* a = m_doc->findAttachment(m_attId))
        m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt);
    if (auto* b = m_doc->findBlock(m_oldAtt.fromBlockId)) {
        b->transform.origin = m_oldOrigin;
        b->transform.rotation = m_oldRotation;
    }
    m_doc->resolveAll();
}

// ─── SetAlignPointCommand (对齐点, 2026-09 设计修正) ───

SetAlignPointCommand::SetAlignPointCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newFromPointId, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newFromPointId(newFromPointId)
{
    setText(QStringLiteral("设置对齐点"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldFromPointId = a.fromPointId;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAlignPointCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a || a->fromPointId == m_newFromPointId) return;
    // 只改吸附端: 本线方向 (start→end) 与角度基准均不变, 跟随角/公式原样
    // 存活 —— Resolver 在重解时把新对齐点钉回目标点 (origin 平移落位),
    // 旋转零跳变。公式驱动的角度表达式不能被覆盖 (2026-09 设计修正)。
    a->fromPointId = m_newFromPointId;
    m_doc->resolveAll();
}

void SetAlignPointCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->fromPointId = m_oldFromPointId;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}

// ─── ReconnectAttachmentCommand (仅角度线拖端点重挂, 2026-12) ───

ReconnectAttachmentCommand::ReconnectAttachmentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const cad::param::Attachment& newAtt,
    const cad::param::Attachment& oldAtt,
    const cad::geo::Vec2& oldOrigin, double oldRotation, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAtt(newAtt)
    , m_oldAtt(oldAtt)
    , m_oldOrigin(oldOrigin)
    , m_oldRotation(oldRotation)
{
    setText(QStringLiteral("\xe9\x87\x8d\xe6\x96\xb0\xe6\x8c\x82\xe6\x8e\xa5"));  // 重新挂接
}

void ReconnectAttachmentCommand::redo()
{
    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_newAtt);
    m_doc->resolveAll();
}

void ReconnectAttachmentCommand::undo()
{
    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt);
    if (auto* b = m_doc->findBlock(m_oldAtt.fromBlockId)) {
        b->transform.origin = m_oldOrigin;
        b->transform.rotation = m_oldRotation;
    }
    m_doc->resolveAll();
}

// ─── SetAttachmentSlideModeCommand ───

SetAttachmentSlideModeCommand::SetAttachmentSlideModeCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    cad::param::SlideMode mode, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newMode(mode)
{
    setText(QStringLiteral("\xe6\xbb\x91\xe8\xbd\xa8\xe6\xa8\xa1\xe5\xbc\x8f"));  // 滑轨模式

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldMode = a.slideMode;
            m_oldAlongMm = a.slideAlongMm;
            m_oldPerpMm = a.slidePerpMm;
            m_oldAngleOnly = a.angleOnly;
            m_oldLocked = a.isLocked;
            break;
        }
    }
}

void SetAttachmentSlideModeCommand::redo()
{
    m_doc->setAttachmentSlideMode(m_attId, m_newMode);
}

void SetAttachmentSlideModeCommand::undo()
{
    // Restore the pre-switch mode, lock-axis snapshots and flags verbatim
    // (快照完整性 — re-running the doc API would re-snapshot offsets and lose
    // the pre-switch locked coordinate).
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_oldMode;
        a->slideAlongMm = m_oldAlongMm;
        a->slidePerpMm = m_oldPerpMm;
        a->angleOnly = m_oldAngleOnly;
        a->isLocked = m_oldLocked;
    }
    m_doc->resolveAll();
}

// ─── SetAttachmentLockedCommand ───

SetAttachmentLockedCommand::SetAttachmentLockedCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool locked,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newLocked(locked)
    , m_oldLocked(false)
{
    setText(QStringLiteral("\xe6\x8b\x96\xe5\x8a\xa8\xe4\xbf\x9d\xe6\x8a\xa4"));  // 拖动保护

    for (const auto& a : doc->attachments())
        if (a.id == attId) { m_oldLocked = a.isLocked; break; }
}

void SetAttachmentLockedCommand::redo()
{
    m_doc->setAttachmentLocked(m_attId, m_newLocked);
}

void SetAttachmentLockedCommand::undo()
{
    m_doc->setAttachmentLocked(m_attId, m_oldLocked);
}

// ─── SetSlideOffsetsCommand ───

SetSlideOffsetsCommand::SetSlideOffsetsCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    double oldAlong, double oldPerp, double newAlong, double newPerp,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_oldAlong(oldAlong)
    , m_oldPerp(oldPerp)
    , m_newAlong(newAlong)
    , m_newPerp(newPerp)
{
    setText(QStringLiteral("\xe6\xbb\x91\xe5\x8a\xa8\xe5\xbe\xae\xe8\xb0\x83"));  // 滑动微调
}

void SetSlideOffsetsCommand::redo()
{
    // 只回写坐标, **不 resolve**: 本命令永远与 MoveBlockCommand 同宏使用,
    // redo 路径由 MoveBlockCommand 的 redo (自带 resolveAll) 统一落位 ——
    // 若偏移命令先 resolve 会把块钉到新位, 后面的移动命令再叠一次 delta
    // (双倍位移)。
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideAlongMm = m_newAlong;
        a->slidePerpMm = m_newPerp;
    }
}

void SetSlideOffsetsCommand::undo()
{
    // undo 路径**必须 resolve**: 宏撤销时 MoveBlockCommand 的 undo 先跑
    // (此时附件仍是新坐标, 会把块钉回拖后位置), 本命令随后恢复旧坐标 —
    // 不 resolve 的话块会留在错误位置 (用户 undo 后跟随线回不到拖前滑轨位)。
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideAlongMm = m_oldAlong;
        a->slidePerpMm = m_oldPerp;
    }
    m_doc->resolveAll();
}

// ─── SetFollowerAngleCommand ───

SetFollowerAngleCommand::SetFollowerAngleCommand(cad::param::ParamDocument* doc,
                                             const QUuid& attId, double newAngle,
                                             const QString& newFormula,
                                             cad::param::RotationMode newMode,
                                             double newArcLength,
                                             const QString& newArcFormula,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAngle(newAngle)
    , m_oldAngle(0.0)
    , m_newFormula(newFormula)
    , m_newMode(newMode)
    , m_oldMode(cad::param::RotationMode::Angle)
    , m_newArcLength(newArcLength)
    , m_oldArcLength(0.0)
    , m_newArcFormula(newArcFormula)
{
    setText(QStringLiteral("\xe4\xbf\xae\xe6\x94\xb9\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xa7\x92\xe5\xba\xa6"));  // 修改跟随角度

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldAngle = a.followerAngle;
            m_oldFormula = a.followerAngleFormula;
            m_oldMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetFollowerAngleCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->followerAngle = m_newAngle;
        a->followerAngleFormula = m_newFormula;
        a->rotationMode = m_newMode;
        a->arcLength = m_newArcLength;
        a->arcLengthFormula = m_newArcFormula;
    }
    m_doc->resolveAll();
}

void SetFollowerAngleCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->followerAngle = m_oldAngle;
        a->followerAngleFormula = m_oldFormula;
        a->rotationMode = m_oldMode;
        a->arcLength = m_oldArcLength;
        a->arcLengthFormula = m_oldArcFormula;
    }
    m_doc->resolveAll();
}

bool SetFollowerAngleCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    const auto* cmd = dynamic_cast<const SetFollowerAngleCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2): never cast a stranger
    if (cmd->m_attId != m_attId) return false;
    // Only plain numeric drags merge; formula changes are discrete edits.
    if (!m_newFormula.isEmpty() || !cmd->m_newFormula.isEmpty()) return false;
    if (!m_newArcFormula.isEmpty() || !cmd->m_newArcFormula.isEmpty()) return false;
    m_newAngle = cmd->m_newAngle;
    m_newMode = cmd->m_newMode;
    m_newArcLength = cmd->m_newArcLength;
    return true;
}

// ─── SetAttachmentSlideOffsetsCommand (2026-09 审核收口) ───

SetAttachmentSlideOffsetsCommand::SetAttachmentSlideOffsetsCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    cad::param::SlideMode newMode,
    double newAlongMm, const QString& newAlongFormula,
    double newPerpMm, const QString& newPerpFormula,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newMode(newMode)
    , m_newAlongMm(newAlongMm)
    , m_newAlongFormula(newAlongFormula)
    , m_newPerpMm(newPerpMm)
    , m_newPerpFormula(newPerpFormula)
{
    setText(QStringLiteral("滑轨偏移"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldMode = a.slideMode;
            m_oldAlongMm = a.slideAlongMm;
            m_oldAlongFormula = a.slideAlongFormula;
            m_oldPerpMm = a.slidePerpMm;
            m_oldPerpFormula = a.slidePerpFormula;
            break;
        }
    }
}

void SetAttachmentSlideOffsetsCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_newMode;
        a->slideAlongMm = m_newAlongMm;
        a->slideAlongFormula = m_newAlongFormula;
        a->slidePerpMm = m_newPerpMm;
        a->slidePerpFormula = m_newPerpFormula;
    }
    m_doc->resolveAll();
}

void SetAttachmentSlideOffsetsCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_oldMode;
        a->slideAlongMm = m_oldAlongMm;
        a->slideAlongFormula = m_oldAlongFormula;
        a->slidePerpMm = m_oldPerpMm;
        a->slidePerpFormula = m_oldPerpFormula;
    }
    m_doc->resolveAll();
}

} // namespace cad::cmd
