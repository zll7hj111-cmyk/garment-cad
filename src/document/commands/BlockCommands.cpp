#include "BlockCommands.h"

#include <QColor>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/AngleMeasureVariable.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── AddBlockCommand ───

AddBlockCommand::AddBlockCommand(cad::param::ParamDocument* doc,
                                 cad::param::Block block,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
{
    setText(QStringLiteral("添加线段"));
}

void AddBlockCommand::redo()
{
    m_doc->addBlock(m_block);
}

void AddBlockCommand::undo()
{
    m_doc->removeBlock(m_block.id);
}

// ─── RemoveBlockCommand ───

RemoveBlockCommand::RemoveBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("删除线段"));

    // Save the block for undo.
    if (const auto* b = doc->findBlock(blockId))
        m_block = *b;

    // Cascade set: the block + every bridge pinned to it (the model layer
    // releases those bridges as independent segments when the host goes
    // away — their pre-deletion state is snapshotted for undo).
    QSet<QUuid> cascade{blockId};
    for (const QUuid& bridgeId : doc->attachmentsView().bridgesPinnedTo(blockId)) {
        if (cascade.contains(bridgeId)) continue;
        cascade.insert(bridgeId);
        if (const auto* bridge = doc->findBlock(bridgeId))
            m_bridges.push_back(*bridge);
    }

    // Snapshot every attachment touching any block in the cascade set.
    QSet<QUuid> seen;
    for (const auto& att : doc->attachments()) {
        if (!cascade.contains(att.fromBlockId) && !cascade.contains(att.toBlockId))
            continue;
        if (seen.contains(att.id)) continue;
        seen.insert(att.id);
        m_attachments.push_back(att);
    }

    // Linked variables sourced from the cascade set are auto-deleted with the
    // block, and their exact-match consumers (length-linked copies) get baked
    // to plain numbers (长度固化为数值). Snapshot both for undo.
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc->linkedVars())
            if (lv.sourceBlockId == srcId)
                m_linked.push_back(lv);
        for (const QUuid& cid : doc->linkedConsumerBlocks(srcId)) {
            if (cascade.contains(cid)) continue;   // removed & restored anyway
            const bool taken = std::any_of(
                m_bakedConsumers.begin(), m_bakedConsumers.end(),
                [&cid](const cad::param::Block& b) { return b.id == cid; });
            if (taken) continue;
            if (const auto* cb = doc->findBlock(cid))
                m_bakedConsumers.push_back(*cb);
        }
        // Measure variables referencing the cascade set (as an endpoint OR as
        // their owner bridge line) are auto-deleted by removeBlock(); snapshot
        // for undo restore.
        for (const auto& mv : doc->measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                m_measures.push_back(mv);
    }
}

void RemoveBlockCommand::redo()
{
    m_doc->removeBlock(m_block.id);
}

void RemoveBlockCommand::undo()
{
    // Bridges pinned to the removed block were RELEASED by redo() (converted
    // to independent segments, still present in the document). Remove the
    // released versions first, then restore the pristine snapshots.
    for (const auto& bridge : m_bridges)
        m_doc->removeBlock(bridge.id);
    m_doc->addBlock(m_block);
    for (const auto& bridge : m_bridges)
        m_doc->addBlock(bridge);
    // Verbatim restore: keep each attachment's snapshot isLocked (拖动保护
    // 默认开启只针对新建连接; 快照还原必须保留用户手动解锁的状态).
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_attachments);
    // Re-publish the auto-deleted linked variables, then restore the pristine
    // formulas of their consumers (baked to numbers by redo).
    for (const auto& lv : m_linked)
        m_doc->addLinked(lv);
    for (const auto& mv : m_measures)
        m_doc->addMeasure(mv);
    for (const auto& snap : m_bakedConsumers) {
        if (auto* b = m_doc->findBlock(snap.id))
            *b = snap;
    }
    if (!m_linked.empty() || !m_measures.empty() || !m_bakedConsumers.empty())
        m_doc->resolveAll();
}

// ─── MoveBlockCommand ───

MoveBlockCommand::MoveBlockCommand(cad::param::ParamDocument* doc,
                                   const QList<QUuid>& blockIds,
                                   const cad::geo::Vec2& delta,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockIds(blockIds)
    , m_delta(delta)
{
    setText(QStringLiteral("移动"));
}

void MoveBlockCommand::redo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin + m_delta;
    }
    m_doc->resolveAll();
}

void MoveBlockCommand::undo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin - m_delta;
    }
    m_doc->resolveAll();
}

bool MoveBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = dynamic_cast<const MoveBlockCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2)
    if (cmd->m_blockIds != m_blockIds)
        return false;
    m_delta = m_delta + cmd->m_delta;
    return true;
}

// ─── RotateBlockCommand ───

RotateBlockCommand::RotateBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const cad::param::Transform2D& oldTf,
                                       const cad::param::Transform2D& newTf,
                                       const QUuid& oldEndTargetBlock,
                                       const QUuid& oldEndTargetPoint,
                                       const QUuid& newEndTargetBlock,
                                       const QUuid& newEndTargetPoint,
                                       const QUuid& releasedAttId,
                                       const cad::param::Attachment& releasedAttBackup,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_oldTf(oldTf)
    , m_newTf(newTf)
    , m_oldEndTargetBlock(oldEndTargetBlock)
    , m_oldEndTargetPoint(oldEndTargetPoint)
    , m_newEndTargetBlock(newEndTargetBlock)
    , m_newEndTargetPoint(newEndTargetPoint)
    , m_releasedAttId(releasedAttId)
    , m_releasedAttBackup(releasedAttBackup)
{
    setText(QStringLiteral("旋转"));
}

void RotateBlockCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_newTf;
        b->endTargetBlockId = m_newEndTargetBlock;
        b->endTargetPointId = m_newEndTargetPoint;
    }
    // 旋转 = 放弃跟随: the pivot was moved off the attachment point, so the
    // rotation detaches the follower link (undo restores it).
    if (!m_releasedAttId.isNull())
        m_doc->removeAttachment(m_releasedAttId);
    m_doc->resolveAll();
}

void RotateBlockCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_oldTf;
        b->endTargetBlockId = m_oldEndTargetBlock;
        b->endTargetPointId = m_oldEndTargetPoint;
    }
    if (!m_releasedAttId.isNull())
        cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_releasedAttBackup);  // verbatim (keep snapshot isLocked)
    m_doc->resolveAll();
}

// ─── DuplicateBlocksCommand ───

DuplicateBlocksCommand::DuplicateBlocksCommand(cad::param::ParamDocument* doc,
                                               cad::param::DuplicateResult result,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
{
    setText(QStringLiteral("复制 %1 条线段").arg(m_result.blocks.size()));
}

void DuplicateBlocksCommand::redo()
{
    // Linked variables first: their refName must be in the parameter map
    // before the cloned blocks resolve their length formulas.
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    for (const auto& b : m_result.blocks)
        m_doc->addBlock(b);
    // Verbatim: cloned connections keep the ORIGINAL's isLocked (复制语义).
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_result.attachments);
}

void DuplicateBlocksCommand::undo()
{
    // removeBlock also drops any attachment touching the clone; cloned
    // bridges may get mutated (released) when their first pin goes away,
    // but they are removed right after, so the mutation is irrelevant.
    for (const auto& att : m_result.attachments)
        m_doc->removeAttachment(att.id);
    for (const auto& b : m_result.blocks)
        m_doc->removeBlock(b.id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

// ─── RotateCopyCommand (旋转复制) ───

RotateCopyCommand::RotateCopyCommand(cad::param::ParamDocument* doc,
                                     cad::param::DuplicateResult result,
                                     const QUuid& originalBlockId,
                                     const QUuid& pivotPointId,
                                     const QUuid& clonePivotPointId,
                                     const QUuid& leaderSegmentId,
                                     double followerAngle,
                                     const QString& followerAngleFormula,
                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
    , m_originalBlockId(originalBlockId)
    , m_pivotPointId(pivotPointId)
    , m_clonePivotPointId(clonePivotPointId)
    , m_leaderSegmentId(leaderSegmentId)
{
    setText(QStringLiteral("旋转复制"));
    // The clone→original attachment is a normal follower whose follower angle
    // is measured RELATIVE to the original's current direction (so a
    // later rotation of the original keeps the copy's relative angle).
    m_att.fromBlockId = m_result.blocks.empty() ? QUuid() : m_result.blocks.front().id;
    m_att.fromPointId = m_clonePivotPointId;
    m_att.toBlockId = m_originalBlockId;
    m_att.toPointId = m_pivotPointId;
    m_att.toSegmentId = m_leaderSegmentId;
    m_att.followerAngle = followerAngle;
    m_att.followerAngleFormula = followerAngleFormula;
    m_att.rotationMode = cad::param::RotationMode::Angle;
}

void RotateCopyCommand::redo()
{
    if (m_result.blocks.empty()) return;
    // Linked variables first (their refName must resolve before the clone).
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    m_doc->addBlock(m_result.blocks.front());
    m_doc->addAttachment(m_att);
    m_doc->resolveAll();
}

void RotateCopyCommand::undo()
{
    if (m_result.blocks.empty()) return;
    m_doc->removeAttachment(m_att.id);
    m_doc->removeBlock(m_result.blocks.front().id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

bool RotateBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = dynamic_cast<const RotateBlockCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2)
    if (cmd->m_blockId != m_blockId)
        return false;
    // A rotation that ALSO released a follower / changed the endpoint-aim
    // must stay a separate command: the merged snapshot only carries ONE
    // attachment backup + ONE aim pair, so absorbing the second state would
    // silently drop the first release / aim change from undo and redo.
    if (cmd->m_releasedAttId != m_releasedAttId
        || cmd->m_newEndTargetBlock != m_newEndTargetBlock
        || cmd->m_newEndTargetPoint != m_newEndTargetPoint)
        return false;
    m_newTf = cmd->m_newTf;  // keep the oldest m_oldTf
    return true;
}

// ─── SetSegmentPropertyCommand ───

namespace {
/// Apply a property snapshot to a segment; reports whether anything changed.
/// Display attributes (name/visible/style/...) are mirrored in the canvas
/// item cache (BlockItem::m_lines), which is only rebuilt when
/// block->geometryEpoch changes — a property-only edit moves no points, so
/// the resolve pass alone would never invalidate the cache (same rationale
/// as ParamDocument::setOwnerMeasureName).
bool applySegmentProps(cad::param::Segment* s,
                       const SetSegmentPropertyCommand::Props& p)
{
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->name, p.name);
    upd(s->role, p.role);
    upd(s->lineStyle, p.lineStyle);
    upd(s->color, p.color);
    upd(s->weight, p.weight);
    upd(s->visible, p.visible);
    upd(s->showName, p.showName);
    upd(s->showLength, p.showLength);
    upd(s->lengthFormula, p.lengthFormula);
    return changed;
}
} // namespace

SetSegmentPropertyCommand::SetSegmentPropertyCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& segmentId,
    const Props& newProps,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newProps(newProps)
{
    setText(QStringLiteral("修改线段属性"));

    // Capture old state
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* s = b->findSegment(segmentId)) {
            m_oldProps.name = s->name;
            m_oldProps.role = s->role;
            m_oldProps.lineStyle = s->lineStyle;
            m_oldProps.color = s->color;
            m_oldProps.weight = s->weight;
            m_oldProps.visible = s->visible;
            m_oldProps.showName = s->showName;
            m_oldProps.showLength = s->showLength;
            m_oldProps.lengthFormula = s->lengthFormula;
        }
    }
}

void SetSegmentPropertyCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            // Bump geometryEpoch so the canvas rebuilds its cached display
            // state on the next resolve — visibility/name/style edits move no
            // geometry, so Block::resolve would not invalidate the cache.
            if (applySegmentProps(s, m_newProps))
                b->touchGeometry();
        }
    }
    m_doc->resolveAll();
}

void SetSegmentPropertyCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            if (applySegmentProps(s, m_oldProps))
                b->touchGeometry();
        }
    }
    m_doc->resolveAll();
}

// ─── SetSegmentExtendCommand (端点延长量, EXTEND_LINE_DESIGN.md) ───

bool SetSegmentExtendCommand::apply(cad::param::Segment* s, const Values& v)
{
    if (!s) return false;
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->extendStartMm, v.startMm);
    upd(s->extendStartFormula, v.startFormula);
    upd(s->extendEndMm, v.endMm);
    upd(s->extendEndFormula, v.endFormula);
    return changed;
}

SetSegmentExtendCommand::SetSegmentExtendCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId,
                                                 const QUuid& segmentId,
                                                 const Values& newValues,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newValues(newValues)
{
    setText(QStringLiteral("修改延长量"));
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* s = b->findSegment(segmentId)) {
            m_oldValues.startMm = s->extendStartMm;
            m_oldValues.startFormula = s->extendStartFormula;
            m_oldValues.endMm = s->extendEndMm;
            m_oldValues.endFormula = s->extendEndFormula;
        }
    }
}

void SetSegmentExtendCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        auto* s = b->findSegment(m_segmentId);
        // 本体不动但可视尾巴变 → 显式 +epoch（画布重绘铁律）。
        if (apply(s, m_newValues))
            b->touchGeometry();
    }
    m_doc->resolveAll();
}

void SetSegmentExtendCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        auto* s = b->findSegment(m_segmentId);
        if (apply(s, m_oldValues))
            b->touchGeometry();
    }
    m_doc->resolveAll();
}

// ─── ReverseSegmentCommand (线段换向, 角度基准视角切换) ───

namespace {

/// 换向方向翻转分析 (canReverse / 快照构造 / 补偿 共用同一判定):
///   · exitDirectionAtPoint 语义: 端点 = 离体方向 (换向不变); 宿主辅助点/
///     曲线锚点 = 弦向/切向 (换向翻转 180°)。
///   · directionAtPoint 语义 (跟随侧 localDir): 端点/辅助点 = start→end 弦向
///     (换向翻转); 曲线锚点返回 0 (不翻转)。
struct AttachmentFlip {
    int  k = 0;                  ///< 方向参照翻转次数 (两次翻转相互抵消 → 偶数不补偿)
    bool needsBackfill = false;  ///< 旧档空角度基准点 → 回填旧终点吸收翻转
};

AttachmentFlip attachmentFlip(const cad::param::Block& block,
                              const QUuid& segId,
                              const QUuid& startId, const QUuid& endId,
                              const cad::param::Attachment& att)
{
    AttachmentFlip r;
    auto exitFlips = [&](const QUuid& pid) {
        if (pid.isNull() || pid == startId || pid == endId) return false;
        const auto* pt = block.findPoint(pid);
        return pt && pt->hostSegmentId == segId &&
               (pt->constraint == cad::param::PointConstraint::Interpolated ||
                pt->constraint == cad::param::PointConstraint::CurveAnchor);
    };
    auto dirAtFlips = [&](const QUuid& pid) {
        if (pid == startId || pid == endId) return true;
        const auto* pt = block.findPoint(pid);
        return pt && pt->hostSegmentId == segId &&
               pt->constraint == cad::param::PointConstraint::Interpolated;
    };

    // 跟随侧: rotation = refWorld + π − angle − localDir, localDir 翻转 → angle +180。
    if (att.fromBlockId == block.id && !att.isPin && dirAtFlips(att.fromPointId))
        ++r.k;
    // 角度基准侧: 位置宿主 (angleRefBlockId 空) 或独立角度基准为本块时,
    // 参照方向翻转 → angle +180; 旧档空基准点 (start→end) 由回填吸收。
    if (att.angleRefBlockId.isNull()) {
        if (att.toBlockId == block.id && exitFlips(att.toPointId)) ++r.k;
    } else if (att.angleRefBlockId == block.id && att.angleRefSegmentId == segId) {
        if (!att.angleRefPointId.isNull()) {
            if (exitFlips(att.angleRefPointId)) ++r.k;
        } else {
            r.needsBackfill = true;
        }
    }
    return r;
}

} // namespace

bool ReverseSegmentCommand::canReverse(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const QUuid& segmentId,
                                       QString* reason)
{
    auto fail = [reason](const QString& r) {
        if (reason) *reason = r;
        return false;
    };
    if (!doc) return fail(QStringLiteral("文档为空"));
    const auto* block = doc->findBlock(blockId);
    if (!block) return fail(QStringLiteral("线段不存在"));
    const auto* seg = block->findSegment(segmentId);
    if (!seg) return fail(QStringLiteral("线段不存在"));
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || sp->id == ep->id)
        return fail(QString::fromUtf8("端点缺失或重合"));
    if (block->isBridge)
        return fail(QString::fromUtf8("桥接线两端被动, 无换向意义"));
    if (block->isDart())
        return fail(QString::fromUtf8("省道线由约束算出, 不可换向"));
    if (!block->endTargetPointId.isNull())
        return fail(QString::fromUtf8("终点指向在驱动方向, 先清除指向再换向"));

    // 连接 (v2 放开): 跟随角 +180·k 补偿 / 旧档角度基准回填, 世界姿态不变。
    // 仍拒绝: 滑轨 (基准线局部系快照会镜像)、需补偿的弧长模式 (πr 不可
    // 参数化表达, 冻结常数会在段长变化后失真)。
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != blockId &&
            !(att.angleRefBlockId == blockId && att.angleRefSegmentId == segmentId))
            continue;
        if (att.fromBlockId == blockId &&
            att.slideMode != cad::param::SlideMode::None)
            return fail(QString::fromUtf8("滑轨模式连接暂不支持换向"));
        const auto flip = attachmentFlip(*block, segmentId,
                                         seg->startPointId, seg->endPointId, att);
        if (att.rotationMode == cad::param::RotationMode::ArcLength && (flip.k % 2) != 0)
            return fail(QString::fromUtf8("弧长模式连接换向需改写弧长, 暂不支持"));
    }

    // 端点被块内其他线段共享 → 换向会重写共享点的驱动约束。
    for (const auto& other : block->segments) {
        if (other.id == segmentId) continue;
        if (other.startPointId == sp->id || other.startPointId == ep->id ||
            other.endPointId == sp->id || other.endPointId == ep->id)
            return fail(QString::fromUtf8("端点被其他线段共享, 暂不支持换向"));
    }

    // 标准驱动结构: 恰一端 Polar(ref=另一端, 无基准段), 另一端 Free。
    auto isPolarTo = [](const cad::param::ParamPoint* p, const QUuid& other) {
        return p->constraint == cad::param::PointConstraint::Polar &&
               p->refPointId == other && p->refSegmentId.isNull();
    };
    const bool standardStructure =
        (isPolarTo(ep, sp->id) && sp->constraint == cad::param::PointConstraint::Free) ||
        (isPolarTo(sp, ep->id) && ep->constraint == cad::param::PointConstraint::Free);
    if (!standardStructure)
        return fail(QString::fromUtf8("两端不是锚点+驱动的标准结构, 暂不支持换向"));

    // 角度测量以段 start→end 为基准方向, 换向会改变测量值 (v2 仍拒绝)。
    for (const auto& am : doc->angleMeasures())
        if ((am.blockA == blockId && am.segmentA == segmentId) ||
            (am.blockB == blockId && am.segmentB == segmentId))
            return fail(QString::fromUtf8("角度测量引用了本线, 换向会改变测量值"));
    return true;
}

ReverseSegmentCommand::ReverseSegmentCommand(cad::param::ParamDocument* doc,
                                             const QUuid& blockId,
                                             const QUuid& segmentId,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
{
    setText(QString::fromUtf8("线段换向"));
    const auto* block = doc->findBlock(blockId);
    const auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return;

    auto capture = [](const cad::param::ParamPoint* p, PointSnapshot& s) {
        s.constraint     = p->constraint;
        s.freePos        = p->freePos;
        s.refPointId     = p->refPointId;
        s.distance       = p->distance;
        s.angle          = p->angle;
        s.refSegmentId   = p->refSegmentId;
        s.distanceFormula = p->distanceFormula;
        s.angleFormula   = p->angleFormula;
        s.interpFromEnd  = p->interpFromEnd;
        s.tangentIn      = p->tangentIn;
        s.tangentOut     = p->tangentOut;
        s.autoTangent    = p->autoTangent;
    };
    m_oldStartId = seg->startPointId;
    m_oldEndId   = seg->endPointId;
    if (const auto* p = block->findPoint(m_oldStartId)) capture(p, m_oldStart);
    if (const auto* p = block->findPoint(m_oldEndId))   capture(p, m_oldEnd);
    m_extendStartMm      = seg->extendStartMm;
    m_extendStartFormula = seg->extendStartFormula;
    m_extendEndMm        = seg->extendEndMm;
    m_extendEndFormula   = seg->extendEndFormula;
    for (const auto& pt : block->points)
        if (pt.constraint == cad::param::PointConstraint::Interpolated &&
            pt.hostSegmentId == segmentId)
            m_auxFromEnd.emplace_back(pt.id, pt.interpFromEnd);
    m_passPoints = seg->passPointIds;
    // v2 补偿快照: 基准消费者 (+180) / 曲线锚点 (冻结同解切线) / 连接。
    // 曲线: Hobby 自动切线求解不保证换序对称 (实测换向后弧长漂移), 与打断
    // 冻结同范式 —— 换向前从曲线缓存捕获每点的"同解"有效切线 (缓存 spans
    // 就是当前渲染几何), 换向后镜像存储并冻结 autoTangent=false, 形状精确不变。
    if (seg->isCurve()) {
        const auto* entry = block->curveSpanEntry(segmentId);
        const std::vector<geo::Vec2>* anchorTanIn = nullptr;
        const std::vector<geo::Vec2>* anchorTanOut = nullptr;
        geo::Vec2 endTanIn{0.0, 0.0}, startTanOut{0.0, 0.0};
        bool haveCache = false;
        if (entry && entry->spans.size() == seg->passPointIds.size() + 1) {
            // 有效切线还原: ctrl1 = P0 + out/3 → out = 3(ctrl1 − P0);
            // ctrl2 = P1 − in/3 → in = 3(P1 − ctrl2)。
            startTanOut = (entry->spans.front().ctrl1 - entry->spans.front().p0) * 3.0;
            endTanIn    = (entry->spans.back().p3 - entry->spans.back().ctrl2) * 3.0;
            // 内部锚点 (n 个 span 有 n-1 个拼接点 k=1..n-1): 拼接点 k =
            // 过点 k-1 (passPointIds 序)。槽位约定: m_innerEff* [k-1] =
            // 过点 k-1 的有效切线 —— in 取左 span 终点、out 取右 span 起点。
            const size_t n = entry->spans.size();
            m_innerEffIn.assign(n - 1, geo::Vec2{0.0, 0.0});
            m_innerEffOut.assign(n - 1, geo::Vec2{0.0, 0.0});
            for (size_t k = 1; k < n; ++k) {
                m_innerEffIn[k - 1] =
                    (entry->spans[k - 1].p3 - entry->spans[k - 1].ctrl2) * 3.0;
                m_innerEffOut[k - 1] =
                    (entry->spans[k].ctrl1 - entry->spans[k].p0) * 3.0;
            }
            anchorTanIn = &entry->anchors;  // 仅标志缓存可用 (anchors 未用)
            haveCache = true;
        }
        Q_UNUSED(anchorTanIn);
        Q_UNUSED(anchorTanOut);
        m_curveCacheValid = haveCache;
        m_effStartTangentOut = startTanOut;
        m_effEndTangentIn = endTanIn;
    }
    for (const auto& pt : block->points) {
        if (pt.refSegmentId == segmentId ||
            (pt.constraint == cad::param::PointConstraint::Intersection &&
             pt.hostSegmentId == segmentId && !pt.interUseWorldAngle &&
             !pt.interBidirectional && pt.interAimPointId.isNull())) {
            m_consumers.push_back({pt.id, pt.angle, pt.angleFormula,
                                   pt.interAngle, pt.interAngleFormula});
        }
        if (pt.constraint == cad::param::PointConstraint::CurveAnchor &&
            pt.hostSegmentId == segmentId) {
            CurveAnchorSnapshot s;
            s.pointId = pt.id;
            s.autoTangent = pt.autoTangent;
            s.tangentIn = pt.tangentIn;
            s.tangentOut = pt.tangentOut;
            s.interpPercent = pt.interpPercent;
            s.interpOffsetDist = pt.interpOffsetDist;
            // 同解有效切线 (按过点在 passPointIds 中的位置取缓存还原值)。
            auto it = std::find(seg->passPointIds.begin(), seg->passPointIds.end(), pt.id);
            if (m_curveCacheValid && it != seg->passPointIds.end()) {
                const size_t idx = static_cast<size_t>(it - seg->passPointIds.begin());
                s.effTangentIn = m_innerEffIn[idx];
                s.effTangentOut = m_innerEffOut[idx];
            }
            m_curveAnchors.push_back(std::move(s));
        }
    }
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        // 本块是 follower (跟随侧 localDir 翻转) 或 本段是其独立角度基准
        // (旧档空基准点回填) 都要快照。
        if (att.fromBlockId != blockId &&
            !(att.angleRefBlockId == blockId && att.angleRefSegmentId == segmentId))
            continue;
        const auto flip = attachmentFlip(*block, segmentId,
                                         m_oldStartId, m_oldEndId, att);
        const bool compensate = (flip.k % 2) != 0 && !att.angleIndependent;
        if (!compensate && !flip.needsBackfill) continue;
        AttachmentSnapshot s;
        s.attId = att.id;
        s.compensateAngle = compensate;
        s.backfill = flip.needsBackfill;
        s.followerAngle = att.followerAngle;
        s.followerAngleFormula = att.followerAngleFormula;
        s.angleRefPointId = att.angleRefPointId;
        m_attComp.push_back(std::move(s));
    }
}

void ReverseSegmentCommand::applyState(bool reversed)
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    auto* sp = block ? block->findPoint(m_oldStartId) : nullptr;
    auto* ep = block ? block->findPoint(m_oldEndId) : nullptr;
    if (!block || !seg || !sp || !ep) return;

    auto restore = [](cad::param::ParamPoint* p, const PointSnapshot& s) {
        p->constraint      = s.constraint;
        p->freePos         = s.freePos;
        p->refPointId      = s.refPointId;
        p->distance        = s.distance;
        p->angle           = s.angle;
        p->refSegmentId    = s.refSegmentId;
        p->distanceFormula = s.distanceFormula;
        p->angleFormula    = s.angleFormula;
        p->interpFromEnd   = s.interpFromEnd;
        p->tangentIn       = s.tangentIn;
        p->tangentOut      = s.tangentOut;
    };
    // v2: 角度数值 +180 / 公式包一层 (formula + 180), 保世界方向。
    auto bumpAngle = [](double& angle, QString& formula) {
        if (formula.isEmpty()) {
            angle = cad::geo::normalizeDeg360(angle + 180.0);
        } else {
            formula = QStringLiteral("(%1)+180").arg(formula);
        }
    };

    if (!reversed) {
        seg->startPointId = m_oldStartId;
        seg->endPointId   = m_oldEndId;
        seg->passPointIds = m_passPoints;
        seg->extendStartMm      = m_extendStartMm;
        seg->extendStartFormula = m_extendStartFormula;
        seg->extendEndMm        = m_extendEndMm;
        seg->extendEndFormula   = m_extendEndFormula;
        restore(sp, m_oldStart);
        restore(ep, m_oldEnd);
        sp->autoTangent = m_oldStart.autoTangent;
        ep->autoTangent = m_oldEnd.autoTangent;
        for (const auto& [auxId, fromEnd] : m_auxFromEnd)
            if (auto* aux = block->findPoint(auxId)) aux->interpFromEnd = fromEnd;
        for (const auto& c : m_consumers) {
            if (auto* pt = block->findPoint(c.pointId)) {
                if (pt->refSegmentId == m_segmentId) {
                    pt->angle = c.angle;
                    pt->angleFormula = c.angleFormula;
                } else {
                    pt->interAngle = c.interAngle;
                    pt->interAngleFormula = c.interAngleFormula;
                }
            }
        }
        for (const auto& ca : m_curveAnchors) {
            if (auto* pt = block->findPoint(ca.pointId)) {
                pt->autoTangent = ca.autoTangent;   // 恢复冻结前标志
                pt->tangentIn = ca.tangentIn;
                pt->tangentOut = ca.tangentOut;
                pt->interpPercent = ca.interpPercent;
                pt->interpOffsetDist = ca.interpOffsetDist;
            }
        }
        for (const auto& ac : m_attComp) {
            auto* att = m_doc->findAttachment(ac.attId);
            if (!att) continue;
            att->followerAngle = ac.followerAngle;
            att->followerAngleFormula = ac.followerAngleFormula;
            att->angleRefPointId = ac.angleRefPointId;
        }
    } else {
        // 驱动端快照 (canReverse 保证恰一端是 Polar-ref-另一端)。
        const bool drivenWasEnd =
            m_oldEnd.constraint == cad::param::PointConstraint::Polar &&
            m_oldEnd.refPointId == m_oldStartId;
        const PointSnapshot& driven = drivenWasEnd ? m_oldEnd : m_oldStart;

        seg->startPointId = m_oldEndId;    // 新起点 = 旧终点
        seg->endPointId   = m_oldStartId;  // 新终点 = 旧起点 (驱动端)
        if (seg->isCurve()) {
            // 曲线保形 (打断冻结同范式): Hobby 自动切线求解不保证换序对称,
            // 用换向前捕获的同解有效切线镜像后冻结为手动 → 形状精确不变。
            auto& pp = seg->passPointIds;
            std::reverse(pp.begin(), pp.end());
            if (m_curveCacheValid) {
                for (const auto& ca : m_curveAnchors) {
                    if (auto* pt = block->findPoint(ca.pointId)) {
                        pt->autoTangent = false;   // 冻结 (undo 恢复原标志)
                        pt->tangentIn  = {-ca.effTangentOut.x, -ca.effTangentOut.y};
                        pt->tangentOut = {-ca.effTangentIn.x,  -ca.effTangentIn.y};
                        pt->interpPercent    = 1.0 - ca.interpPercent;
                        pt->interpOffsetDist = -ca.interpOffsetDist;  // 左侧 → 右侧
                    }
                }
                ep->autoTangent = false;
                // 新起点 (= 旧终点): 其 out 驱动新首段 = 旧末段反向,
                // 故 out = −旧终点 in。同理新终点 in = −旧起点 out。
                // 坑: Hobby 求解器对手动点按 atan2(tangentOut) 统一取方向、
                // 按 |tangentIn| 取长度 —— 端点 out 必须是与 in 同向的非零
                // 向量, 写零向量会让方向塌成 +x (形状漂移)。
                ep->tangentOut = {-m_effEndTangentIn.x, -m_effEndTangentIn.y};
                ep->tangentIn  = {0.0, 0.0};
                sp->autoTangent = false;
                sp->tangentIn  = {-m_effStartTangentOut.x, -m_effStartTangentOut.y};
                sp->tangentOut = sp->tangentIn;   // 同向非零 (方向源)
            } else {
                // 缓存冷/不一致: 退回存储切线镜像 (手动曲线保形, 自动曲线可能微漂)。
                for (const auto& ca : m_curveAnchors) {
                    if (auto* pt = block->findPoint(ca.pointId)) {
                        pt->tangentIn  = {-ca.tangentOut.x, -ca.tangentOut.y};
                        pt->tangentOut = {-ca.tangentIn.x,  -ca.tangentIn.y};
                        pt->interpPercent    = 1.0 - ca.interpPercent;
                        pt->interpOffsetDist = -ca.interpOffsetDist;
                    }
                }
            }
        }
        seg->extendStartMm      = m_extendEndMm;       // 物理尾巴不动:
        seg->extendStartFormula = m_extendEndFormula;  // 延长量随端点角色互换
        seg->extendEndMm        = m_extendStartMm;
        seg->extendEndFormula   = m_extendStartFormula;

        // 新终点 (旧起点) 接管驱动: 角度 = 原角 + 180 (换向视角补偿),
        // 距离/距离公式原样转移 (|AB| = |BA|)。
        sp->constraint      = cad::param::PointConstraint::Polar;
        sp->refPointId      = m_oldEndId;
        sp->refSegmentId    = QUuid();
        sp->distance        = driven.distance;
        sp->distanceFormula = driven.distanceFormula;
        sp->angle           = cad::geo::normalizeDeg360(driven.angle + 180.0);
        sp->angleFormula.clear();
        // 新起点 (旧终点) 落为自由锚点, 停在原求解位置。
        // (旧终点本就是锚点时 freePos 已正确, 不动; 旧终点是驱动点时
        //  求解位置 = 锚点 freePos + dist·dir(angle), 换算落位。)
        ep->constraint = cad::param::PointConstraint::Free;
        if (drivenWasEnd) {
            ep->freePos = m_oldStart.freePos +
                geo::Vec2{driven.distance * std::cos(driven.angle * M_PI / 180.0),
                          driven.distance * std::sin(driven.angle * M_PI / 180.0)};
        }
        // 曲线切线已在上方曲线块冻结 (缓存同解镜像), 直线切线恒零无需处理。

        // 宿主辅助点: 起终点互换 + fromEnd 翻转 = 求解坐标系不变, 位置零漂移。
        for (const auto& [auxId, fromEnd] : m_auxFromEnd)
            if (auto* aux = block->findPoint(auxId)) aux->interpFromEnd = !fromEnd;

        // v2 补偿: 基准段/相对交点消费者 +180 保世界方向; 连接跟随角 +180·k、
        // 旧档空角度基准点回填旧终点 (出方向 = 原 start→end 基准)。
        for (const auto& c : m_consumers) {
            auto* pt = block->findPoint(c.pointId);
            if (!pt) continue;
            if (pt->refSegmentId == m_segmentId) {
                bumpAngle(pt->angle, pt->angleFormula);
            } else {
                bumpAngle(pt->interAngle, pt->interAngleFormula);
            }
        }
        for (const auto& ac : m_attComp) {
            auto* att = m_doc->findAttachment(ac.attId);
            if (!att) continue;
            if (ac.compensateAngle) {
                if (att->followerAngleFormula.isEmpty()) {
                    att->followerAngle = cad::geo::normalizeDeg360(
                        ac.followerAngle + 180.0);
                } else {
                    att->followerAngleFormula =
                        QStringLiteral("(%1)+180").arg(ac.followerAngleFormula);
                }
            }
            if (ac.backfill) att->angleRefPointId = m_oldEndId;
        }
    }

    block->touchGeometry();
    m_doc->resolveAll();
}

void ReverseSegmentCommand::redo()
{
    applyState(true);
}

void ReverseSegmentCommand::undo()
{
    applyState(false);
}

// ─── AddAuxPointCommand ───

AddAuxPointCommand::AddAuxPointCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const QUuid& segmentId,
                                       cad::param::ParamPoint pt,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pt(std::move(pt))
{
    setText(QStringLiteral("新建辅助点"));
}

void AddAuxPointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    seg->auxPointIds.push_back(m_pt.id);
    m_doc->resolveAll();
}

void AddAuxPointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // Linear undo guarantees any line that borrowed this point (pushed AFTER
    // this command) has already been undone — no dangling attachment remains.
    if (auto* seg = block->findSegment(m_segmentId)) {
        auto& ids = seg->auxPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), m_pt.id), ids.end());
    }
    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pt.id; }),
        pts.end());
    block->rebuildPointIndex();
    m_doc->resolveAll();
}

// ─── MovePointCommand ───

MovePointCommand::MovePointCommand(cad::param::ParamDocument* doc,
                                   const QUuid& blockId, const QUuid& pointId,
                                   const cad::geo::Vec2& oldPos,
                                   const cad::geo::Vec2& newPos,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldPos(oldPos)
    , m_newPos(newPos)
{
    setText(QStringLiteral("移动锚点"));
}

void MovePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* pt = block->findPoint(m_pointId);
    if (!pt) return;
    pt->freePos = m_newPos;
    pt->constraint = cad::param::PointConstraint::Free;
    m_doc->resolveAll();
}

void MovePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* pt = block->findPoint(m_pointId);
    if (!pt) return;
    pt->freePos = m_oldPos;
    pt->constraint = cad::param::PointConstraint::Free;
    m_doc->resolveAll();
}

// ─── AddCurvePointCommand ───

AddCurvePointCommand::AddCurvePointCommand(cad::param::ParamDocument* doc,
                                           const QUuid& blockId,
                                           const QUuid& segmentId,
                                           cad::param::ParamPoint pt,
                                           QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pt(std::move(pt))
    , m_oldType(cad::param::SegmentType::Line)
{
    setText(QStringLiteral("添加曲线点"));
    if (auto* b = m_doc->findBlock(m_blockId))
        if (const auto* s = b->findSegment(m_segmentId))
            m_oldType = s->type;
}

void AddCurvePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    // Insert keeping passPointIds ordered by chord fraction (interpPercent) so
    // the spline passes through the anchors in order along the curve rather
    // than looping back on itself when a point is added mid-curve via Ctrl.
    auto& ids = seg->passPointIds;
    int insertAt = static_cast<int>(ids.size());
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        const auto* pp = block->findPoint(ids[i]);
        if (pp && pp->interpPercent > m_pt.interpPercent) { insertAt = i; break; }
    }
    ids.insert(ids.begin() + insertAt, m_pt.id);
    seg->type = cad::param::SegmentType::Bezier;
    // Curve structure changed WITHOUT any point necessarily moving — bump the
    // epoch explicitly so Block::resolve's stale-cache gate rebuilds the curve
    // cache (and the canvas rebuilds) in the resolveAll below.
    block->touchGeometry();
    m_doc->resolveAll();
}

void AddCurvePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    if (auto* seg = block->findSegment(m_segmentId)) {
        auto& ids = seg->passPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), m_pt.id), ids.end());
        seg->type = m_oldType;
    }
    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pt.id; }),
        pts.end());
    block->rebuildPointIndex();
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── RemoveCurvePointCommand ───

RemoveCurvePointCommand::RemoveCurvePointCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId,
                                                 const QUuid& segmentId,
                                                 const QUuid& pointId,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_pointId(pointId)
    , m_oldType(cad::param::SegmentType::Bezier)
{
    setText(QStringLiteral("删除曲线点"));
    // Capture the point's data and its passPointIds slot now (before redo
    // removes it) so undo can restore it exactly.
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (const auto* p = b->findPoint(m_pointId))
            m_pt = *p;
        if (const auto* s = b->findSegment(m_segmentId)) {
            m_oldType = s->type;
            const auto& ids = s->passPointIds;
            auto it = std::find(ids.begin(), ids.end(), m_pointId);
            m_index = (it == ids.end()) ? 0 : static_cast<int>(it - ids.begin());
        }
    }
}

void RemoveCurvePointCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    auto& ids = seg->passPointIds;
    ids.erase(std::remove(ids.begin(), ids.end(), m_pointId), ids.end());
    if (ids.empty())
        seg->type = cad::param::SegmentType::Line;  // last curve point → straight

    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [this](const cad::param::ParamPoint& p) { return p.id == m_pointId; }),
        pts.end());
    block->rebuildPointIndex();
    block->touchGeometry();
    m_doc->resolveAll();
}

void RemoveCurvePointCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    block->addPoint(m_pt);
    auto& ids = seg->passPointIds;
    const int idx = std::clamp(m_index, 0, static_cast<int>(ids.size()));
    ids.insert(ids.begin() + idx, m_pointId);
    seg->type = m_oldType;
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── MoveCurveAnchorCommand ───

MoveCurveAnchorCommand::MoveCurveAnchorCommand(cad::param::ParamDocument* doc,
                                               const QUuid& blockId,
                                               const QUuid& pointId,
                                               double oldPercent, double oldOffset,
                                               double newPercent, double newOffset,
                                               const QUuid& oldFollowBlockId,
                                               const QUuid& oldFollowPointId,
                                               const cad::geo::Vec2& oldFollowOffset,
                                               const QUuid& newFollowBlockId,
                                               const QUuid& newFollowPointId,
                                               const cad::geo::Vec2& newFollowOffset,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldPercent(oldPercent), m_oldOffset(oldOffset)
    , m_newPercent(newPercent), m_newOffset(newOffset)
    , m_oldFollowBlockId(oldFollowBlockId), m_oldFollowPointId(oldFollowPointId)
    , m_oldFollowOffset(oldFollowOffset)
    , m_newFollowBlockId(newFollowBlockId), m_newFollowPointId(newFollowPointId)
    , m_newFollowOffset(newFollowOffset)
{
    setText(QStringLiteral("调整曲线点"));
}

void MoveCurveAnchorCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->interpPercent = m_newPercent;
    pt->interpOffsetDist = m_newOffset;
    pt->followBlockId = m_newFollowBlockId;
    pt->followPointId = m_newFollowPointId;
    pt->followOffset = m_newFollowOffset;
    m_doc->resolveAll();
}

void MoveCurveAnchorCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->interpPercent = m_oldPercent;
    pt->interpOffsetDist = m_oldOffset;
    pt->followBlockId = m_oldFollowBlockId;
    pt->followPointId = m_oldFollowPointId;
    pt->followOffset = m_oldFollowOffset;
    m_doc->resolveAll();
}

// ─── SetCurveTangentCommand ───

SetCurveTangentCommand::SetCurveTangentCommand(cad::param::ParamDocument* doc,
                                               const QUuid& blockId, const QUuid& pointId,
                                               const cad::geo::Vec2& oldTanIn, const cad::geo::Vec2& oldTanOut, bool oldAuto,
                                               const cad::geo::Vec2& newTanIn, const cad::geo::Vec2& newTanOut, bool newAuto,
                                               bool oldLocked, bool newLocked,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
    , m_oldTanIn(oldTanIn), m_oldTanOut(oldTanOut)
    , m_newTanIn(newTanIn), m_newTanOut(newTanOut)
    , m_oldAuto(oldAuto)
    , m_newAuto(newAuto)
    , m_oldLocked(oldLocked)
    , m_newLocked(newLocked)
{
    setText(QStringLiteral("调整曲线手柄"));
}

void SetCurveTangentCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->tangentIn = m_newTanIn;
    pt->tangentOut = m_newTanOut;
    pt->autoTangent = m_newAuto;
    pt->tangentLocked = m_newLocked;  // Alt+drag may break the lock persistently
    block->touchGeometry();
    m_doc->resolveAll();
}

void SetCurveTangentCommand::undo()
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* pt = block ? block->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->tangentIn = m_oldTanIn;
    pt->tangentOut = m_oldTanOut;
    pt->autoTangent = m_oldAuto;
    pt->tangentLocked = m_oldLocked;  // restore the pre-drag lock state
    block->touchGeometry();
    m_doc->resolveAll();
}

// ─── SegmentEditBarCommand ───

namespace {

/// Apply one edit-strip state snapshot to the model (name/length/angle).
/// Missing block/segment/attachment → no-op (deleted concurrently).
void applyEditStripState(cad::param::ParamDocument& doc,
                         const QUuid& blockId, const QUuid& segmentId,
                         const SegmentEditBarCommand::State& s)
{
    auto* b = doc.findBlock(blockId);
    auto* seg = b ? b->findSegment(segmentId) : nullptr;
    if (!b || !seg) return;
    if (seg->name != s.segName) {
        seg->name = s.segName;
        b->touchGeometry();
    }
    seg->lengthFormula = s.lengthFormula;
    // The owned measure variable's display name follows the segment name.
    doc.setOwnerMeasureName(blockId, s.segName);
    if (auto* ep = b->findPoint(seg->endPointId)) {
        ep->distance = s.endDistance;
        ep->distanceFormula = s.endDistanceFormula;
        ep->angle = s.endAngle;
        ep->angleFormula = s.endAngleFormula;
        ep->constraint = static_cast<cad::param::PointConstraint>(s.endConstraint);
        ep->refPointId = s.endRefPointId;
    }
    if (!s.attId.isNull()) {
        if (auto* a = doc.findAttachment(s.attId)) {
            a->followerAngle = s.followerAngle;
            a->followerAngleFormula = s.followerAngleFormula;
            a->arcLength = s.arcLength;
            a->arcLengthFormula = s.arcLengthFormula;
            a->rotationMode = static_cast<cad::param::RotationMode>(s.rotationMode);
        }
    }
}

} // namespace

SegmentEditBarCommand::SegmentEditBarCommand(cad::param::ParamDocument* doc,
                                             const QUuid& blockId,
                                             const QUuid& segmentId,
                                             State newState,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newState(std::move(newState))
{
    setText(QStringLiteral("编辑线段属性"));
    // Snapshot the pre-edit state from the model.
    if (const auto* b = doc->findBlock(blockId)) {
        if (const auto* seg = b->findSegment(segmentId)) {
            m_oldState.segName = seg->name;
            m_oldState.lengthFormula = seg->lengthFormula;
            if (const auto* ep = b->findPoint(seg->endPointId)) {
                m_oldState.endDistance = ep->distance;
                m_oldState.endDistanceFormula = ep->distanceFormula;
                m_oldState.endAngle = ep->angle;
                m_oldState.endAngleFormula = ep->angleFormula;
                m_oldState.endConstraint = static_cast<int>(ep->constraint);
                m_oldState.endRefPointId = ep->refPointId;
            }
            // Follower attachment snapshot: the attachment anchored at THIS
            // segment's start/end point (a block may own one attachment while
            // having several lines — the first block-wide match would snapshot
            // the WRONG line's attachment; same rule as SegmentEditBar).
            for (const auto& att : doc->attachments()) {
                if (att.fromBlockId != blockId || att.isPin) continue;
                if (att.fromPointId != seg->startPointId
                    && att.fromPointId != seg->endPointId)
                    continue;
                m_oldState.attId = att.id;
                m_oldState.followerAngle = att.followerAngle;
                m_oldState.followerAngleFormula = att.followerAngleFormula;
                m_oldState.arcLength = att.arcLength;
                m_oldState.arcLengthFormula = att.arcLengthFormula;
                m_oldState.rotationMode = static_cast<int>(att.rotationMode);
                break;
            }
        }
    }
}

void SegmentEditBarCommand::redo()
{
    applyEditStripState(*m_doc, m_blockId, m_segmentId, m_newState);
    m_doc->resolveAll();
}

void SegmentEditBarCommand::undo()
{
    applyEditStripState(*m_doc, m_blockId, m_segmentId, m_oldState);
    m_doc->resolveAll();
}

// ─── RotateBlocksCommand (选集刚体旋转 + 影子偏转, 2026-08-27) ───

RotateBlocksCommand::RotateBlocksCommand(
    cad::param::ParamDocument* doc,
    const QHash<QUuid, cad::param::Transform2D>& oldTf,
    const QHash<QUuid, cad::param::Transform2D>& newTf,
    std::vector<ShadowAtt> shadowAtts,
    std::vector<cad::param::Attachment> releasedAtts,
    std::vector<cad::cmd::AimRelease> releasedTargets,
    std::vector<cad::cmd::DartRelease> releasedDarts,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_oldTf(oldTf)
    , m_newTf(newTf)
    , m_shadowAtts(std::move(shadowAtts))
    , m_releasedAtts(std::move(releasedAtts))
    , m_releasedTargets(std::move(releasedTargets))
    , m_releasedDarts(std::move(releasedDarts))
{
    setText(QStringLiteral("\xe9\x80\x89\xe9\x9b\x86\xe6\x97\x8b\xe8\xbd\xac"));  // 选集旋转
}

void RotateBlocksCommand::redo()
{
    if (!m_doc) return;
    // 1) 全体写入新刚体位姿.
    for (auto it = m_newTf.cbegin(); it != m_newTf.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    // 2) 跨界连接: 半拆降级 angleOnly (位置跟随解除、角度跟随保留) +
    //    影子偏转角累加 —— 姿态守得住, 公式与一键恢复全活 (§2.6).
    for (const auto& s : m_shadowAtts) {
        if (auto* a = m_doc->findAttachment(s.attId)) {
            *a = s.demoted;             // verbatim 降级快照 (含旧 offset)
            a->angleOnly = true;        // 半拆: 只松位置
            a->isLocked = false;        // 与 angleOnly 互斥 (置位自动解焊)
            a->slideMode = cad::param::SlideMode::None;
            a->baselineOffsetDeg = s.newOffset;
        }
    }
    // 3) 跨界释放的连接整条移除 (pin/滑轨/组件级, D7 旧语义路径).
    if (!m_releasedAtts.empty()) {
        QList<QUuid> ids;
        ids.reserve(static_cast<int>(m_releasedAtts.size()));
        for (const auto& a : m_releasedAtts) ids << a.id;
        m_doc->removeAttachments(ids);
    }
    // 4) 清指向 S 外的 endTarget (同 D7).
    for (const auto& r : m_releasedTargets) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->endTargetBlockId = QUuid();
            b->endTargetPointId = QUuid();
            b->endTargetOffset = 0.0;
            b->endTargetOffsetFormula.clear();
        }
    }
    // 4) 引用 S 外的省道线降级普通线 (清 start/ref; 偏移/角度字段保留).
    for (const auto& r : m_releasedDarts) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->dartStartBlockId = QUuid();
            b->dartStartPointId = QUuid();
            b->dartRefBlockId = QUuid();
            b->dartRefPointId = QUuid();
            b->dartRefSegmentId = QUuid();
        }
    }
    m_doc->resolveAll();
}

void RotateBlocksCommand::undo()
{
    if (!m_doc) return;
    // 还原全体位姿.
    for (auto it = m_oldTf.cbegin(); it != m_oldTf.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    // 跨界连接 verbatim 还原 (完整焊接 + 旧影子偏转) —— 快照完整性铁律.
    for (const auto& s : m_shadowAtts) {
        if (auto* a = m_doc->findAttachment(s.attId))
            *a = s.demoted;             // 含原 baselineOffsetDeg / isLocked / slideMode
    }
    if (!m_releasedAtts.empty())
        cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_releasedAtts);
    for (const auto& r : m_releasedTargets) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->endTargetBlockId = r.endTargetBlockId;
            b->endTargetPointId = r.endTargetPointId;
            b->endTargetOffset = r.endTargetOffset;
            b->endTargetOffsetFormula = r.endTargetOffsetFormula;
        }
    }
    for (const auto& r : m_releasedDarts) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->dartStartBlockId = r.dartStartBlockId;
            b->dartStartPointId = r.dartStartPointId;
            b->dartRefBlockId = r.dartRefBlockId;
            b->dartRefPointId = r.dartRefPointId;
            b->dartRefSegmentId = r.dartRefSegmentId;
            b->dartOffsetMm = r.dartOffsetMm;
            b->dartOffsetFormula = r.dartOffsetFormula;
            b->dartAngleDeg = r.dartAngleDeg;
            b->dartAngleFormula = r.dartAngleFormula;
        }
    }
    m_doc->resolveAll();
}

// ─── ReleaseCurveFollowCommand (P0-3: 曲线锚点跟随释放 — 原直写不可撤销) ───

ReleaseCurveFollowCommand::ReleaseCurveFollowCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& pointId,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_pointId(pointId)
{
    setText(QStringLiteral("释放跟随"));
    // Snapshot the pre-release follow state from the model.
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* pt = b->findPoint(pointId)) {
            m_oldFollowBlockId = pt->followBlockId;
            m_oldFollowPointId = pt->followPointId;
            m_oldFollowOffset = pt->followOffset;
        }
    }
}

void ReleaseCurveFollowCommand::redo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* pt = b ? b->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->followBlockId = {};
    pt->followPointId = {};
    pt->followOffset = cad::geo::Vec2::zero();
    m_doc->resolveAll();
}

void ReleaseCurveFollowCommand::undo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* pt = b ? b->findPoint(m_pointId) : nullptr;
    if (!pt) return;
    pt->followBlockId = m_oldFollowBlockId;
    pt->followPointId = m_oldFollowPointId;
    pt->followOffset = m_oldFollowOffset;
    m_doc->resolveAll();
}

// ─── SetLinePropertiesCommand (P0-3: LinePropertyDialog 会话收口) ───

bool SetLinePropertiesCommand::Props::operator==(const Props& o) const
{
    return name == o.name && role == o.role
        && showName == o.showName && showLength == o.showLength
        && visible == o.visible && color == o.color
        && lineStyle == o.lineStyle && weight == o.weight
        && lengthFormula == o.lengthFormula
        && distance == o.distance && distanceFormula == o.distanceFormula
        && startName == o.startName && startAnno == o.startAnno
        && startShowName == o.startShowName
        && endName == o.endName && endAnno == o.endAnno
        && endShowName == o.endShowName;
}

bool SetLinePropertiesCommand::apply(cad::param::ParamDocument* doc,
                                     cad::param::Block* b,
                                     cad::param::Segment* s,
                                     const Props& p)
{
    if (!doc || !b || !s) return false;
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->name, p.name);
    upd(s->role, p.role);
    upd(s->showName, p.showName);
    upd(s->showLength, p.showLength);
    upd(s->visible, p.visible);
    upd(s->color, p.color);
    upd(s->lineStyle, p.lineStyle);
    upd(s->weight, p.weight);
    upd(s->lengthFormula, p.lengthFormula);
    if (auto* ep = b->findPoint(s->endPointId)) {
        upd(ep->distance, p.distance);
        upd(ep->distanceFormula, p.distanceFormula);
        upd(ep->name, p.endName);
        upd(ep->showName, p.endShowName);
        upd(ep->annotation, p.endAnno);
    }
    if (auto* sp = b->findPoint(s->startPointId)) {
        upd(sp->name, p.startName);
        upd(sp->showName, p.startShowName);
        upd(sp->annotation, p.startAnno);
    }
    // The owned measure variable's display name follows the segment name
    // (same coupling as LinePropertyDialog::applyToModel / SegmentEditBar).
    if (doc)
        doc->setOwnerMeasureName(b->id, p.name);
    return changed;
}

SetLinePropertiesCommand::SetLinePropertiesCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& segmentId,
    Props oldProps, Props newProps,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_oldProps(std::move(oldProps))
    , m_newProps(std::move(newProps))
{
    setText(QStringLiteral("修改线条属性"));
}

void SetLinePropertiesCommand::redo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (apply(m_doc, b, s, m_newProps) && b)
        b->touchGeometry();
    m_doc->resolveAll();
}

void SetLinePropertiesCommand::undo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (apply(m_doc, b, s, m_oldProps) && b)
        b->touchGeometry();
    m_doc->resolveAll();
}

} // namespace cad::cmd
