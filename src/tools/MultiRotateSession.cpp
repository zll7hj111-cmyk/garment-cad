#include "tools/MultiRotateSession.h"

#include "tools/HitTester.h"  // isInteractiveBlock（TOOL-P1-12）

#include <cmath>
#include <QUndoStack>
#include "canvas/CanvasScene.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/CrossSelectionPolicy.h"
#include "document/commands/BlockTransformCommands.h"
#include "geometry/Epsilon.h"

namespace cad::tools {

void MultiRotateSession::clear()
{
    m_selection.clear();
    m_isMarqueeSelected = false;
    m_accumulatedAngleDeg = 0.0;
    m_multiBaseTf.clear();
    m_multiReleasedAtts.clear();
    m_releasedAttIds.clear();
}

void MultiRotateSession::adoptSelection(cad::param::ParamDocument* doc, const QSet<QUuid>& blockIds)
{
    clear();
    if (!doc) return;
    for (const QUuid& id : blockIds) {
        if (const auto* blk = doc->findBlock(id)) {
            if (!blk->isBridge && isInteractiveBlock(*blk, *doc)) {
                m_selection.insert(id);
            }
        }
    }
    if (m_selection.size() > 1) {
        m_isMarqueeSelected = true;
    }
}

void MultiRotateSession::captureBase(cad::param::ParamDocument* doc)
{
    m_multiBaseTf.clear();
    m_multiReleasedAtts.clear();
    if (!doc) return;

    for (const QUuid& bId : m_selection) {
        if (const auto* blk = doc->findBlock(bId)) {
            m_multiBaseTf[bId] = MultiBlockBase{
                blk->transform,
                blk->endTargetBlockId,
                blk->endTargetPointId
            };
        }
    }

    m_releasedAttIds.clear();
    for (const auto& a : doc->attachments()) {
        const bool fromIn = m_selection.contains(a.fromBlockId);
        const bool toIn   = m_selection.contains(a.toBlockId);
        // 跨选集连接策略唯一来源（2026-12 审计 TOOL-P0-8 / U10）：旋转释放
        // 非 pin 的跨边界连接；桥接 pin 保持被动拉伸（桥接线不进旋转选集）。
        if (!cad::param::isReleasedAcrossSelection(
                fromIn, toIn, a.isPin, cad::param::CrossSelectionOp::Rotate)) {
            continue;
        }
        m_multiReleasedAtts.push_back(a);
        m_releasedAttIds.append(a.id);
        if (auto* child = doc->findBlock(a.fromBlockId)) {
            child->preservedBenchmarkAngle = a.followerAngle;
        }
    }
    // 会话期间不真删：只从解算中忽略（TOOL-P1-32，与 SelectDragController 的
    // ignoredAttachments 同一机制）——序列化/观察者仍看到连接存在。
    if (!m_releasedAttIds.isEmpty()) {
        doc->resolveForDrag({}, m_releasedAttIds);
    }
}

void MultiRotateSession::restoreBase(cad::param::ParamDocument* doc, CanvasScene* scene)
{
    if (!doc) return;
    for (const QUuid& bId : m_selection) {
        if (auto* blk = doc->findBlock(bId)) {
            if (m_multiBaseTf.contains(bId)) {
                const auto& base = m_multiBaseTf[bId];
                blk->transform = base.tf;
                blk->endTargetBlockId = base.endTargetBlock;
                blk->endTargetPointId = base.endTargetPoint;
                blk->touchGeometry();
            }
        }
    }
    // 会话期间从未真删，只需清空忽略列表（TOOL-P1-32）。
    m_multiReleasedAtts.clear();
    m_releasedAttIds.clear();
    doc->resolveAll();
    if (scene) scene->refreshAllBlockItems();
}

void MultiRotateSession::applyModeValue(cad::param::ParamDocument* doc,
                                        CanvasScene* scene,
                                        const cad::geo::Vec2& pivot,
                                        double value)
{
    m_accumulatedAngleDeg = value;
    const double deltaRad = cad::geo::degToRad(value);
    for (const QUuid& bId : m_selection) {
        auto* b = doc ? doc->findBlock(bId) : nullptr;
        if (!b || !m_multiBaseTf.contains(bId)) continue;
        const auto& base = m_multiBaseTf[bId];
        b->transform.rotation = base.tf.rotation + deltaRad;
        b->transform.origin = pivot + (base.tf.origin - pivot).rotated(deltaRad);
        b->touchGeometry();
    }
    if (doc) doc->resolveForDrag(m_selection.values(), m_releasedAttIds);
    if (scene) scene->syncBlockPositions();
}

bool MultiRotateSession::commit(cad::param::ParamDocument* doc, QUndoStack* undoStack)
{
    if (!doc || !undoStack) return false;
    std::vector<cad::cmd::RotateBlocksCommand::BlockTransformSnapshot> snapshots;
    bool anyChanged = false;
    for (const QUuid& bId : m_selection) {
        auto* blk = doc->findBlock(bId);
        if (!blk || !m_multiBaseTf.contains(bId)) continue;
        const auto& base = m_multiBaseTf[bId];
        const auto curTf = blk->transform;
        if (std::abs(curTf.rotation - base.tf.rotation) > cad::geo::kGeomEps ||
            curTf.origin.distanceTo(base.tf.origin) > cad::geo::kGeomEpsLoose) {
            anyChanged = true;
        }
        snapshots.push_back({
            bId,
            base.tf,
            curTf,
            base.endTargetBlock,
            base.endTargetPoint,
            blk->endTargetBlockId,
            blk->endTargetPointId
        });
    }
    if (!anyChanged && m_multiReleasedAtts.empty()) {
        return false;
    }

    for (const auto& s : snapshots) {
        if (auto* b = doc->findBlock(s.blockId)) {
            b->transform = s.oldTf;
            b->endTargetBlockId = s.oldEndTargetBlock;
            b->endTargetPointId = s.oldEndTargetPoint;
            b->touchGeometry();
        }
    }
    // 连接仍在文档中（会话期间只忽略解算）：直接交给命令在 redo() 里删除、
    // undo() 里恢复 —— 不再需要先 addAttachmentRaw 补回（TOOL-P1-32）。
    undoStack->push(new cad::cmd::RotateBlocksCommand(
        doc, snapshots, m_multiReleasedAtts));

    captureBase(doc);
    return true;
}

} // namespace cad::tools
