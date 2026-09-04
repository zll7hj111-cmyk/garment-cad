#include "tools/MultiRotateSession.h"

#include <cmath>
#include <QUndoStack>
#include "canvas/CanvasScene.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ParamDocumentRaw.h"
#include "document/commands/BlockTransformCommands.h"

namespace cad::tools {

void MultiRotateSession::clear()
{
    m_selection.clear();
    m_isMarqueeSelected = false;
    m_accumulatedAngleDeg = 0.0;
    m_multiBaseTf.clear();
    m_multiReleasedAtts.clear();
}

void MultiRotateSession::adoptSelection(cad::param::ParamDocument* doc, const QSet<QUuid>& blockIds)
{
    clear();
    if (!doc) return;
    for (const QUuid& id : blockIds) {
        if (const auto* blk = doc->findBlock(id)) {
            if (!blk->isBridge) {
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

    for (const auto& a : doc->attachments()) {
        if (m_selection.contains(a.fromBlockId) && !a.isPin) {
            m_multiReleasedAtts.push_back(a);
        }
    }
    for (const auto& a : m_multiReleasedAtts) {
        doc->removeAttachment(a.id);
    }
    if (!m_multiReleasedAtts.empty()) {
        doc->resolveAll();
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
    for (const auto& a : m_multiReleasedAtts) {
        cad::param::RawModelAccess::addAttachmentRaw(*doc, a);
    }
    m_multiReleasedAtts.clear();
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
    if (doc) doc->resolveForDrag(m_selection.values());
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
        if (std::abs(curTf.rotation - base.tf.rotation) > 1e-9 ||
            curTf.origin.distanceTo(base.tf.origin) > 1e-6) {
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
    for (const auto& a : m_multiReleasedAtts) {
        cad::param::RawModelAccess::addAttachmentRaw(*doc, a);
    }
    undoStack->push(new cad::cmd::RotateBlocksCommand(
        doc, snapshots, m_multiReleasedAtts));

    captureBase(doc);
    return true;
}

} // namespace cad::tools
