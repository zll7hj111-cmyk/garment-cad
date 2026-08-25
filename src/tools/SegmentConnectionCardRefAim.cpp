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


// ── 角度基准分离 (引用线段/引用点) 与 终点指向 (aim) 槽函数 (2026-08 拆分) ──

void SegmentConnectionCard::onAngleRefPointResolved(const QUuid& blockId,
                                                     const QUuid& pointId)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att) {
        // 自由态 (统一视图): 引用行是「预填」—— 从「位置吸附」连入时由
        // onConnectToResolved 自动落库为角度基准 (五态时代的"仅角度/双基准
        // 自由接入"分支已删除: 下拉只剩两态, 那些分支永远走不到)。
        m_angleRefPoint->setPoint(blockId, pointId);
        refreshCard();
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader) { refreshCard(); return; }
    const QUuid segId = leader->exitSegmentAtPoint(pointId);
    if (segId.isNull()) { refreshCard(); return; }

    // 不能把自己作为角度基准 (位置/角度都自己会冲突)。
    if (blockId == att->fromBlockId) { refreshCard(); return; }

    // 若已经指定了角度基准线段，基准点必须属于该线段，不允许跨线段选点。
    if (!att->angleRefSegmentId.isNull()) {
        if (const auto* refSeg = leader->findSegment(att->angleRefSegmentId)) {
            const bool belongs =
                pointId == refSeg->startPointId || pointId == refSeg->endPointId
                || std::find(refSeg->auxPointIds.begin(), refSeg->auxPointIds.end(),
                             pointId) != refSeg->auxPointIds.end();
            if (!belongs) { refreshCard(); return; }
        }
    }

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, blockId, segId, pointId));
    else
        m_doc->setAttachmentAngleRef(att->id, blockId, segId, pointId);

    refresh();
    emit changed(ChangeKind::AngleRefChanged);
}

void SegmentConnectionCard::onClearAngleRef()
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att) { refreshCard(); return; }

    if (att->angleRefBlockId.isNull() && att->angleRefSegmentId.isNull()) {
        refreshCard();
        return;
    }

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, QUuid(), QUuid(), QUuid()));
    else
        m_doc->setAttachmentAngleRef(att->id, QUuid(), QUuid(), QUuid());

    refresh();
    emit changed(ChangeKind::AngleRefChanged);
}

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

void SegmentConnectionCard::onAngleRefSegEdited()
{
    if (!m_doc) return;
    const QString text = m_lblAngleRefSeg->text().trimmed();
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
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id, blkId, segId, ptId));
        else
            m_doc->setAttachmentAngleRef(att->id, blkId, segId, ptId);
    } else {
        m_angleRefPoint->setPoint(blkId, ptId);
    }
    refresh();
    emit changed(ChangeKind::AngleRefChanged);
}

void SegmentConnectionCard::onAimTargetResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    // 辅助层隔离：工作层线段不能指向辅助层点。
    if (const auto* target = m_doc->findBlock(blockId)) {
        if (m_doc->isAuxBlock(*target) != m_doc->isAuxBlock(*block))
            return;
    }
    block->endTargetBlockId = blockId;
    block->endTargetPointId = pointId;
    refresh();
    emit changed(ChangeKind::ConnectionModeChanged);
}

void SegmentConnectionCard::onAimOffsetApply()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) return;
    const QString text = m_editAimOffset->text().trimmed();
    bool isNum = false;
    const double val = text.toDouble(&isNum);
    if (isNum) {
        block->endTargetOffset = val;
        block->endTargetOffsetFormula.clear();
    } else if (!text.isEmpty()) {
        block->endTargetOffsetFormula = text;
    }
    refresh();
    emit changed(ChangeKind::ConnectionModeChanged);
}

void SegmentConnectionCard::onClearAim()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    block->endTargetBlockId = QUuid();
    block->endTargetPointId = QUuid();
    block->endTargetOffset = 0.0;
    block->endTargetOffsetFormula.clear();
    refresh();
    emit changed(ChangeKind::ConnectionModeChanged);
}

} // namespace cad::tools
