#include "ContextStrip.h"

#include <cmath>
#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QUndoStack>

#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Attachment.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Units.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"

namespace cad::app {

void ContextStrip::applyName()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->findSegment(m_segmentId)) return;

    auto st = snapshotState();
    st.segName = m_nameEdit->text().trimmed();
    commitState(std::move(st));
}

void ContextStrip::applyLength()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    if (block->isBridge) return;

    const QString text = m_lenEdit->text().trimmed();
    if (text.isEmpty()) return;
    if (!block->findPoint(seg->endPointId)) return;

    auto st = snapshotState();
    const auto parsed = cad::geo::parseNumberOrFormula(text);
    if (parsed.isNumber) {
        st.lengthFormula.clear();
        st.endDistanceFormula.clear();
        st.endDistance = cad::geo::Units::cmToMm(parsed.value);
    } else {
        st.lengthFormula = parsed.formula;
        st.endDistanceFormula = parsed.formula;
    }
    commitState(std::move(st));
}

void ContextStrip::applyAngle()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    if (block->isBridge) return;

    const QString text = m_angleEdit->text().trimmed();
    if (text.isEmpty()) return;

    const auto parsed = cad::geo::parseNumberOrFormula(text);
    double targetDeg = parsed.value;
    if (!parsed.isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
        if (!r.ok) return;
        targetDeg = r.value;
    }

    auto st = snapshotState();
    const cad::param::Attachment* att = findEditAttachment();
    if (att && !att->angleIndependent) {
        st.attId = att->id;
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            st.arcLength = cad::geo::Units::cmToMm(targetDeg);
            st.arcLengthFormula = parsed.isNumber ? QString() : parsed.formula;
        } else if (att->rotationMode == cad::param::RotationMode::ChordLength) {
            double chordMm = cad::geo::Units::cmToMm(targetDeg);
            const double radius = block->segmentLengthAtPoint(att->fromPointId);
            if (radius > 1e-9) {
                chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);
            }
            st.chordLength = chordMm;
            st.chordLengthFormula = parsed.isNumber ? QString() : parsed.formula;
        } else {
            st.followerAngle = targetDeg;
            st.followerAngleFormula = parsed.isNumber ? QString() : parsed.formula;
        }
    } else {
        auto* ep = block->findPoint(seg->endPointId);
        if (!ep) return;
        if (ep->constraint == cad::param::PointConstraint::OrthoOffset) {
            st.endConstraint = static_cast<int>(cad::param::PointConstraint::OrthoOffset);
            st.endRefPointId = seg->startPointId;
            st.orthoOffsetDist = ep->orthoOffsetDist;
            st.orthoOffsetDistFormula = ep->orthoOffsetDistFormula;
        } else if (ep->constraint != cad::param::PointConstraint::Polar) {
            const auto* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            st.endConstraint = static_cast<int>(cad::param::PointConstraint::Polar);
            st.endRefPointId = seg->startPointId;
            st.endDistance = sp->resolvedPos.distanceTo(ep->resolvedPos);
        }
        const double rotDeg = block->transform.rotation * 180.0 / M_PI;
        const double anchorOffset = (m_rotateAnchor.active && m_rotateAnchor.anchorIsEnd) ? 180.0 : 0.0;
        st.endAngle = (targetDeg - anchorOffset) - rotDeg;
        const double totalOffset = anchorOffset + rotDeg;
        st.endAngleFormula = parsed.isNumber
            ? QString()
            : ((std::abs(totalOffset) > 1e-9)
                   ? QStringLiteral("(%1)-%2").arg(parsed.formula).arg(totalOffset, 0, 'g', 12)
                   : parsed.formula);
        if (att && att->angleIndependent) st.attId = QUuid();
    }
    commitState(std::move(st));
}

void ContextStrip::onPasteLength()
{
    if (m_lenEdit->isReadOnly()) return;
    m_lenEdit->clear();
    const auto* cb = QApplication::clipboard();
    const QString clean = cb ? QString(cb->text()).remove(QLatin1Char('\r')).remove(QLatin1Char('\n')).trimmed() : QString();
    if (!clean.isEmpty()) {
        m_lenEdit->setText(clean);
    }
    m_debounce->stop();
    applyLength();
}

void ContextStrip::onPasteAngle()
{
    if (m_angleEdit->isReadOnly()) return;
    m_angleEdit->clear();
    const auto* cb = QApplication::clipboard();
    const QString clean = cb ? QString(cb->text()).remove(QLatin1Char('\r')).remove(QLatin1Char('\n')).trimmed() : QString();
    if (!clean.isEmpty()) {
        m_angleEdit->setText(clean);
    }
    if (m_connectSession) {
        // connectAngleTextChanged(clean) emit via textChanged
    } else {
        m_debounce->stop();
        applyAngle();
    }
}

void ContextStrip::onUnitToggled(bool wantArc)
{
    onUnitSelected(wantArc ? cad::param::RotationMode::ArcLength
                           : cad::param::RotationMode::Angle);
}

void ContextStrip::onUnitSelected(cad::param::RotationMode target)
{
    if (m_connectSession) {
        if (m_connectAttId.isNull()) return;
        emit connectAngleModeChanged(target);
        refreshChrome();
        return;
    }
    if (m_focus != StripFocus::Pinned || !m_paramDoc || m_blockId.isNull()) return;
    const cad::param::Attachment* att = findEditAttachment();
    if (!att) return;
    if (att->rotationMode == target) return;

    const double radius = m_paramDoc->findBlock(m_blockId)
                              ? m_paramDoc->findBlock(m_blockId)
                                    ->segmentLengthAtPoint(att->fromPointId)
                              : 0.0;
    const auto res = cad::param::followerModeSwitchValues(
        *att, radius, target, m_paramDoc->parameters(), m_paramDoc->conditions());

    auto st = snapshotState();
    st.attId = att->id;
    st.rotationMode = static_cast<int>(target);
    if (target == cad::param::RotationMode::ArcLength) {
        st.arcLength = res.arcMm;
        st.arcLengthFormula = res.arcFormula;
    } else if (target == cad::param::RotationMode::ChordLength) {
        st.chordLength = res.chordMm;
        st.chordLengthFormula = res.chordFormula;
    } else {
        st.followerAngle = res.angle;
        st.followerAngleFormula = res.angleFormula;
    }
    commitState(std::move(st));
}

void ContextStrip::onReverseClicked()
{
    if (m_focus != StripFocus::Pinned || !m_paramDoc || m_blockId.isNull()) return;
    if (m_rotateAnchor.active) {
        emit reverseRequested(m_blockId, m_segmentId);
        return;
    }
    QString reason;
    if (!cad::cmd::ReverseSegmentCommand::canReverse(
            m_paramDoc, m_blockId, m_segmentId, &reason))
        return;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::ReverseSegmentCommand(
            m_paramDoc, m_blockId, m_segmentId));
    } else {
        cad::cmd::ReverseSegmentCommand cmd(m_paramDoc, m_blockId, m_segmentId);
        cmd.redo();
    }
}

void ContextStrip::onPosDetachClicked()
{
    if (m_focus != StripFocus::Pinned || !m_paramDoc) return;
    const auto* att = findEditAttachment();
    if (!att) { refreshChrome(); return; }
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_paramDoc, att->id, !att->angleOnly));
    } else {
        m_paramDoc->setAttachmentAngleOnly(att->id, !att->angleOnly);
    }
}

void ContextStrip::onAngleDetachClicked()
{
    if (m_focus != StripFocus::Pinned || !m_paramDoc) return;
    const auto* att = findEditAttachment();
    if (!att) { refreshChrome(); return; }
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SetAttachmentAngleIndependentCommand(
            m_paramDoc, att->id, !att->angleIndependent));
    } else {
        m_paramDoc->setAttachmentAngleIndependent(
            att->id, !att->angleIndependent);
    }
}

const cad::param::Attachment* ContextStrip::findEditAttachment() const
{
    if (!m_paramDoc) return nullptr;
    if (m_connectSession && !m_connectAttId.isNull())
        return m_paramDoc->attachmentsView().byId(m_connectAttId);
    if (m_blockId.isNull()) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return nullptr;

    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId || att.isPin) continue;
        if (att.fromPointId == seg->startPointId
            || att.fromPointId == seg->endPointId)
            return &att;
        for (const auto& aid : seg->auxPointIds) {
            if (att.fromPointId == aid)
                return &att;
        }
    }
    return nullptr;
}

cad::cmd::SegmentEditBarCommand::State ContextStrip::snapshotState() const
{
    cad::cmd::SegmentEditBarCommand::State st;
    if (!m_paramDoc || m_blockId.isNull()) return st;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return st;

    st.segName = seg->name;
    st.lengthFormula = seg->lengthFormula;
    if (const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId)) {
        st.endDistance = ep->distance;
        st.endDistanceFormula = ep->distanceFormula;
        st.endAngle = ep->angle;
        st.endAngleFormula = ep->angleFormula;
        st.endConstraint = static_cast<int>(ep->constraint);
        st.endRefPointId = ep->refPointId;
        st.orthoOffsetDist = ep->orthoOffsetDist;
        st.orthoOffsetDistFormula = ep->orthoOffsetDistFormula;
    }
    if (const cad::param::Attachment* att = findEditAttachment()) {
        st.attId = att->id;
        st.followerAngle = att->followerAngle;
        st.followerAngleFormula = att->followerAngleFormula;
        st.arcLength = att->arcLength;
        st.arcLengthFormula = att->arcLengthFormula;
        st.chordLength = att->chordLength;
        st.chordLengthFormula = att->chordLengthFormula;
        st.rotationMode = static_cast<int>(att->rotationMode);
    }
    return st;
}

void ContextStrip::commitState(cad::cmd::SegmentEditBarCommand::State st)
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SegmentEditBarCommand(
            m_paramDoc, m_blockId, m_segmentId, std::move(st)));
    } else {
        cad::cmd::SegmentEditBarCommand cmd(m_paramDoc, m_blockId, m_segmentId,
                                            std::move(st));
        cmd.redo();
    }
}

} // namespace cad::app
