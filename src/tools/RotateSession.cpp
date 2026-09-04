#include "tools/RotateSession.h"

#include <cmath>
#include <QList>

#include <QUndoStack>
#include "canvas/CanvasScene.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ParamDocumentRaw.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockTransformCommands.h"
#include "tools/RotateCopyGesture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace cad::tools {

void RotateSession::clear()
{
    m_blockId = QUuid();
    m_connected = false;
    m_attId = QUuid();
    m_pivot = cad::geo::Vec2();
    m_refWorldRad = 0.0;
    m_anchor = RotateAnchorState();
    m_shadow.reset();
    m_base = RotateBaseSnapshot();
    m_anchorLocal = cad::geo::Vec2();
    m_localDir = 0.0;
}

void RotateSession::setupTarget(cad::param::ParamDocument* doc,
                                const QUuid& blockId,
                                const std::optional<cad::geo::Vec2>& clickWorld)
{
    m_blockId = blockId;
    m_anchor.releaseAttHeld = false;
    m_anchor.releaseAttId = QUuid();

    m_anchor.isEnd = false;
    if (doc) {
        if (const auto* blk = doc->findBlock(blockId)) {
            if (!blk->segments.empty()) {
                const auto& seg = blk->segments.front();
                const bool hasStartAtt = attachmentAtPoint(doc, seg.startPointId) != nullptr;
                const bool hasEndAtt   = attachmentAtPoint(doc, seg.endPointId) != nullptr;
                if (hasStartAtt && !hasEndAtt) {
                    m_anchor.isEnd = false;
                } else if (hasEndAtt && !hasStartAtt) {
                    m_anchor.isEnd = true;
                } else if (clickWorld.has_value()) {
                    const cad::geo::Vec2 pStart = blk->worldPos(seg.startPointId);
                    const cad::geo::Vec2 pEnd   = blk->worldPos(seg.endPointId);
                    const double dStart = clickWorld->distanceTo(pStart);
                    const double dEnd   = clickWorld->distanceTo(pEnd);
                    m_anchor.isEnd = (dEnd < dStart);
                }
            }
        }
    }

    rebuildAnchorState(doc);
}

cad::param::Attachment* RotateSession::attachmentAtPoint(
    cad::param::ParamDocument* doc, const QUuid& pointId)
{
    if (!doc || m_blockId.isNull() || pointId.isNull()) return nullptr;
    for (const auto& a : doc->attachments()) {
        if (a.fromBlockId == m_blockId && a.fromPointId == pointId && !a.isPin)
            return doc->findAttachment(a.id);
    }
    return nullptr;
}

const cad::param::Attachment* RotateSession::editableAttachment(
    const cad::param::ParamDocument* doc) const
{
    return doc ? doc->attachmentsView().byId(m_attId) : nullptr;
}

cad::param::Attachment* RotateSession::editableAttachment(
    cad::param::ParamDocument* doc)
{
    return doc ? doc->findAttachment(m_attId) : nullptr;
}

cad::param::Attachment* RotateSession::followerAttachment(
    cad::param::ParamDocument* doc)
{
    if (!doc || m_blockId.isNull()) return nullptr;
    for (const auto& a : doc->attachments()) {
        if (a.fromBlockId == m_blockId && !a.isPin)
            return doc->findAttachment(a.id);
    }
    return nullptr;
}

void RotateSession::toggleAnchor(cad::param::ParamDocument* doc)
{
    if (!doc) return;
    const cad::param::Block* blk = doc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return;

    if (attachmentAtPoint(doc, blk->segments.front().startPointId)
        || attachmentAtPoint(doc, blk->segments.front().endPointId))
        return;

    m_anchor.isEnd = !m_anchor.isEnd;
    rebuildAnchorState(doc);
}

void RotateSession::rebuildAnchorState(cad::param::ParamDocument* doc)
{
    if (!doc) return;
    m_shadow.reset();

    cad::param::Block* blk = doc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) { clear(); return; }

    const cad::param::Segment& seg = blk->segments.front();
    m_anchor.pointId = m_anchor.isEnd ? seg.endPointId : seg.startPointId;
    const cad::param::ParamPoint* ap = blk->findPoint(m_anchor.pointId);
    if (!ap || !ap->resolved) { clear(); return; }

    cad::param::Attachment* att = attachmentAtPoint(doc, m_anchor.pointId);
    const bool angleIndependent = att && att->angleIndependent;

    if (att && !angleIndependent) {
        m_connected = true;
        m_attId = att->id;
        const cad::param::Block* fromBlk = doc->findBlock(att->fromBlockId);
        m_pivot = fromBlk ? fromBlk->worldPos(att->fromPointId) : blk->worldPos(m_anchor.pointId);
        m_refWorldRad = cad::param::effectiveAngleRefWorld(doc, *att);
        m_base.baseAngle = att->followerAngle;
        m_base.baseFormula = att->followerAngleFormula;
        m_base.rotationMode = att->rotationMode;
        m_base.baseArcLength = att->arcLength;
        m_base.baseArcFormula = att->arcLengthFormula;
        m_anchorLocal = ap->resolvedPos;

        if (const auto* toBlk = doc->findBlock(att->toBlockId);
            toBlk && toBlk->isShadow) {
            m_shadow.shadowId = toBlk->id;
            for (const auto& a : doc->attachments()) {
                if (!a.isPin && a.fromBlockId == m_shadow.shadowId) {
                    m_shadow.att1Id = a.id;
                    m_shadow.isMounted = true;
                    m_shadow.shadowDelta0 = a.followerAngle;
                    break;
                }
            }
            if (auto* sh = doc->findBlock(m_shadow.shadowId)) {
                m_shadow.shadowRot0 = sh->transform.rotation;
                m_shadow.shadowTf0 = sh->transform;
            }
            m_shadow.followerTf0 = blk->transform;
        }
    } else {
        m_connected = false;
        m_attId = QUuid();
        m_anchorLocal = ap->resolvedPos;
        m_pivot = blk->worldPos(m_anchor.pointId);
        m_localDir = blk->directionAtPoint(m_anchor.pointId);
        m_refWorldRad = 0.0;
        m_base.baseTf = blk->transform;
        m_base.baseEndTargetBlock = blk->endTargetBlockId;
        m_base.baseEndTargetPoint = blk->endTargetPointId;
    }

    if (!m_anchor.releaseAttHeld) {
        m_anchor.releaseAttId = QUuid();
        if (!m_connected) {
            if (auto* fa = followerAttachment(doc)) {
                if (!fa->angleIndependent) {
                    m_anchor.releaseAttId = fa->id;
                    m_anchor.releaseAttBackup = *fa;
                }
            }
        }
    }
}

QUuid RotateSession::anchorPointAt(const cad::param::ParamDocument* doc,
                                   const cad::geo::Vec2& worldPos,
                                   double zoom) const
{
    if (!doc || m_blockId.isNull()) return QUuid();
    const cad::param::Block* blk = doc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return QUuid();

    const double tol = 8.0 / (zoom > 1e-9 ? zoom : 1.0);
    for (const QUuid& pid : {blk->segments.front().startPointId,
                             blk->segments.front().endPointId}) {
        const cad::param::ParamPoint* p = blk->findPoint(pid);
        if (p && p->resolved && blk->worldPos(pid).distanceTo(worldPos) <= tol)
            return pid;
    }
    return QUuid();
}

void RotateSession::releaseFollowerIfAnchorMoved(
    cad::param::ParamDocument* doc, CanvasScene* scene)
{
    if (!doc || m_anchor.releaseAttId.isNull() || m_anchor.releaseAttHeld) return;
    if (auto* a = doc->findAttachment(m_anchor.releaseAttId))
        m_anchor.releaseAttBackup = *a;
    doc->removeAttachment(m_anchor.releaseAttId);
    m_anchor.releaseAttHeld = true;
    m_connected = false;
    doc->resolveAll();
    if (scene) scene->refreshAllBlockItems();
}

void RotateSession::applyAngleDeg(cad::param::ParamDocument* doc,
                                  CanvasScene* scene,
                                  double deg,
                                  RotateCopyGesture* copyGesture,
                                  double dragAngle0)
{
    if (!doc) return;
    if (const auto* scopeBlk = doc->findBlock(m_blockId))
        doc->invalidateLayer(scopeBlk->layer);

    if (copyGesture && copyGesture->active()) {
        copyGesture->applyAngle(deg);
        return;
    }

    if (m_connected) {
        if (isAngleLocked(copyGesture) && m_shadow.active()) {
            applyShadowAngleDeg(doc, deg, dragAngle0);
        } else if (auto* a = editableAttachment(doc)) {
            const double alpha = cad::geo::normalizeDeg360(deg);
            if (m_base.rotationMode == cad::param::RotationMode::ArcLength) {
                const double radius = segmentRadius(doc);
                a->arcLength = cad::geo::degToArcMm(alpha, radius);
                a->arcLengthFormula.clear();
                a->rotationMode = cad::param::RotationMode::ArcLength;
            } else {
                a->followerAngle = alpha;
                a->followerAngleFormula.clear();
            }
        }
    } else {
        cad::param::Block* blk = doc->findBlock(m_blockId);
        if (!blk) return;
        const double anchorOffsetRad = m_anchor.isEnd ? M_PI : 0.0;
        const double newRot = deg * M_PI / 180.0 - anchorOffsetRad - m_localDir;
        blk->transform.rotation = newRot;
        blk->transform.origin = m_pivot - m_anchorLocal.rotated(newRot);
    }

    QList<QUuid> rotSeeds{m_blockId};
    if (!m_shadow.shadowId.isNull()) rotSeeds.append(m_shadow.shadowId);
    doc->resolveForDrag(rotSeeds);
    if (scene) scene->syncBlockPositions();
}

void RotateSession::applyShadowAngleDeg(cad::param::ParamDocument* doc,
                                        double deg,
                                        double dragAngle0)
{
    if (!doc) return;
    const double deltaDeg = cad::geo::normalizeDeg180(dragAngle0 - deg);
    const double deltaRad = deltaDeg * M_PI / 180.0;
    if (m_shadow.isMounted) {
        if (auto* att1 = doc->findAttachment(m_shadow.att1Id))
            att1->followerAngle = cad::geo::normalizeDeg180(m_shadow.shadowDelta0 - deltaDeg);
    } else {
        if (auto* sh = doc->findBlock(m_shadow.shadowId))
            sh->transform.rotation = m_shadow.shadowRot0 + deltaRad;
        if (auto* blk = doc->findBlock(m_blockId)) {
            const double newRot = m_shadow.followerTf0.rotation + deltaRad;
            blk->transform.rotation = newRot;
            blk->transform.origin = m_pivot - m_anchorLocal.rotated(newRot);
        }
    }
    if (const auto* sh = doc->findBlock(m_shadow.shadowId))
        doc->invalidateLayer(sh->layer);
}

void RotateSession::applyModeValue(cad::param::ParamDocument* doc,
                                   CanvasScene* scene,
                                   double value,
                                   RotateCopyGesture* copyGesture)
{
    if (m_base.rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        if (auto* a = editableAttachment(doc)) {
            a->arcLength = cad::geo::Units::cmToMm(value);
            a->arcLengthFormula.clear();
        }
        if (doc) doc->resolveAll();
        if (scene) scene->refreshAllBlockItems();
    } else {
        applyAngleDeg(doc, scene, value, copyGesture);
    }
}

double RotateSession::segmentRadius(const cad::param::ParamDocument* doc) const
{
    if (!doc || !m_connected) return 0.0;
    const cad::param::Block* blk = doc->findBlock(m_blockId);
    if (!blk) return 0.0;
    if (const auto* a = editableAttachment(doc))
        return blk->segmentLengthAtPoint(a->fromPointId);
    return 0.0;
}

double RotateSession::currentModeValue(const cad::param::ParamDocument* doc,
                                       RotateCopyGesture* copyGesture) const
{
    if (m_base.rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        const auto* a = editableAttachment(doc);
        if (!a) return 0.0;
        double arcMm = a->arcLength;
        if (doc) {
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->arcLengthFormula, doc->parameters(), {}, arcMm);
        }
        const double radius = segmentRadius(doc);
        const double alphaDeg = (radius > 1e-9)
            ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
        const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
        return cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius));
    }
    return currentAngleDeg(doc, copyGesture);
}

double RotateSession::currentAngleDeg(const cad::param::ParamDocument* doc,
                                      RotateCopyGesture* copyGesture) const
{
    if (!doc) return 0.0;

    if (copyGesture && copyGesture->active())
        return copyGesture->currentRelativeAngle();

    if (m_connected) {
        const auto* a = editableAttachment(doc);
        if (!a) return 0.0;
        if (a->rotationMode == cad::param::RotationMode::ArcLength) {
            double arcMm = a->arcLength;
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->arcLengthFormula, doc->parameters(), {}, arcMm);
            const double radius = segmentRadius(doc);
            double deg = (radius > 1e-9)
                ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
            return cad::geo::normalizeDeg180(deg);
        }
        if (!a->followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                a->followerAngleFormula, doc->parameters(), {});
            if (r.ok) return cad::geo::normalizeDeg180(r.value);
        }
        return cad::geo::normalizeDeg180(a->followerAngle);
    }

    const cad::param::Block* blk = doc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return 0.0;
    const cad::param::Segment& seg = blk->segments.front();
    const auto* sp = blk->findPoint(seg.startPointId);
    const auto* ep = blk->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    const cad::geo::Vec2 w1 = blk->transform.toWorld(sp->resolvedPos);
    const cad::geo::Vec2 w2 = blk->transform.toWorld(ep->resolvedPos);
    double deg = (w2 - w1).angle() * 180.0 / M_PI;
    if (m_anchor.isEnd) deg += 180.0;
    deg = cad::geo::normalizeDeg360(deg);
    return deg;
}

bool RotateSession::isAngleLocked(RotateCopyGesture* copyGesture) const
{
    if (copyGesture && copyGesture->active()) return false;
    if (!m_connected) return false;
    if (m_base.rotationMode == cad::param::RotationMode::ArcLength)
        return !m_base.baseArcFormula.isEmpty();
    return !m_base.baseFormula.isEmpty();
}

void RotateSession::restoreBase(cad::param::ParamDocument* doc, CanvasScene* scene)
{
    if (!doc) return;

    if (m_connected) {
        if (isAngleLocked(nullptr) && m_shadow.active()) {
            if (m_shadow.isMounted) {
                if (auto* att1 = doc->findAttachment(m_shadow.att1Id))
                    att1->followerAngle = m_shadow.shadowDelta0;
            } else {
                if (auto* sh = doc->findBlock(m_shadow.shadowId))
                    sh->transform = m_shadow.shadowTf0;
                if (auto* blk = doc->findBlock(m_blockId))
                    blk->transform = m_shadow.followerTf0;
            }
        } else if (auto* a = editableAttachment(doc)) {
            a->followerAngle = m_base.baseAngle;
            a->followerAngleFormula = m_base.baseFormula;
            a->rotationMode = m_base.rotationMode;
            a->arcLength = m_base.baseArcLength;
            a->arcLengthFormula = m_base.baseArcFormula;
        }
    } else {
        if (auto* blk = doc->findBlock(m_blockId)) {
            blk->transform = m_base.baseTf;
            blk->endTargetBlockId = m_base.baseEndTargetBlock;
            blk->endTargetPointId = m_base.baseEndTargetPoint;
        }
    }

    if (m_anchor.releaseAttHeld && !m_anchor.releaseAttId.isNull()) {
        cad::param::RawModelAccess::addAttachmentRaw(*doc, m_anchor.releaseAttBackup);
        m_anchor.releaseAttId = QUuid();
        m_anchor.releaseAttHeld = false;
    }
    doc->resolveAll();
    if (scene) scene->refreshAllBlockItems();
}

bool RotateSession::commit(cad::param::ParamDocument* doc, QUndoStack* undoStack)
{
    if (!doc || !undoStack) return false;

    if (m_connected) {
        if (isAngleLocked(nullptr) && m_shadow.active()) {
            if (m_shadow.isMounted) {
                auto* att1 = doc->findAttachment(m_shadow.att1Id);
                if (!att1) return false;
                const double curDelta = att1->followerAngle;
                if (std::abs(curDelta - m_shadow.shadowDelta0) <= 1e-9)
                    return false;
                att1->followerAngle = m_shadow.shadowDelta0;
                undoStack->push(new cad::cmd::SetFollowerAngleCommand(
                        doc, m_shadow.att1Id, curDelta));
                m_shadow.shadowDelta0 = curDelta;
                return true;
            } else {
                auto* shBlk = doc->findBlock(m_shadow.shadowId);
                auto* blk = doc->findBlock(m_blockId);
                if (!shBlk || !blk) return false;
                const auto shNew = shBlk->transform;
                const auto blkNew = blk->transform;
                const bool changed =
                    std::abs(shNew.rotation - m_shadow.shadowTf0.rotation) > 1e-9
                    || shNew.origin.distanceTo(m_shadow.shadowTf0.origin) > 1e-6
                    || std::abs(blkNew.rotation - m_shadow.followerTf0.rotation) > 1e-9
                    || blkNew.origin.distanceTo(m_shadow.followerTf0.origin) > 1e-6;
                if (!changed) return false;
                shBlk->transform = m_shadow.shadowTf0;
                blk->transform = m_shadow.followerTf0;
                undoStack->push(new cad::cmd::ShadowRotateCommand(
                        doc, m_shadow.shadowId, m_shadow.shadowTf0, shNew,
                        m_blockId, m_shadow.followerTf0, blkNew));
                m_shadow.shadowTf0 = shNew;
                m_shadow.shadowRot0 = shNew.rotation;
                m_shadow.followerTf0 = blkNew;
                return true;
            }
        }

        cad::param::Attachment* att = editableAttachment(doc);
        if (!att) return false;

        const double curAngle = att->followerAngle;
        const QString curFormula = att->followerAngleFormula;
        const auto curMode = att->rotationMode;
        const double curArc = att->arcLength;
        const QString curArcFormula = att->arcLengthFormula;

        const bool changed = std::abs(curAngle - m_base.baseAngle) > 1e-9
                          || curFormula != m_base.baseFormula
                          || curMode != m_base.rotationMode
                          || std::abs(curArc - m_base.baseArcLength) > 1e-6
                          || curArcFormula != m_base.baseArcFormula;
        if (!changed) return false;

        att->followerAngle = m_base.baseAngle;
        att->followerAngleFormula = m_base.baseFormula;
        att->rotationMode = m_base.rotationMode;
        att->arcLength = m_base.baseArcLength;
        att->arcLengthFormula = m_base.baseArcFormula;
        undoStack->push(new cad::cmd::SetFollowerAngleCommand(
                doc, m_attId, curAngle, curFormula,
                curMode, curArc, curArcFormula));
        m_base.baseAngle = curAngle;
        m_base.baseFormula = curFormula;
        m_base.rotationMode = curMode;
        m_base.baseArcLength = curArc;
        m_base.baseArcFormula = curArcFormula;
        return true;
    } else {
        cad::param::Block* blk = doc->findBlock(m_blockId);
        if (!blk) return false;

        const cad::param::Transform2D curTf = blk->transform;
        const QUuid curEndBlock = blk->endTargetBlockId;
        const QUuid curEndPoint = blk->endTargetPointId;
        const bool changed = std::abs(curTf.rotation - m_base.baseTf.rotation) > 1e-9
                          || curTf.origin.distanceTo(m_base.baseTf.origin) > 1e-6
                          || curEndBlock != m_base.baseEndTargetBlock
                          || curEndPoint != m_base.baseEndTargetPoint
                          || m_anchor.releaseAttHeld;
        if (!changed) return false;

        blk->transform = m_base.baseTf;
        blk->endTargetBlockId = m_base.baseEndTargetBlock;
        blk->endTargetPointId = m_base.baseEndTargetPoint;
        if (m_anchor.releaseAttHeld && !m_anchor.releaseAttId.isNull())
            cad::param::RawModelAccess::addAttachmentRaw(*doc, m_anchor.releaseAttBackup);
        undoStack->push(new cad::cmd::RotateBlockCommand(
            doc, m_blockId, m_base.baseTf, curTf,
            m_base.baseEndTargetBlock, m_base.baseEndTargetPoint,
            curEndBlock, curEndPoint,
            m_anchor.releaseAttHeld ? m_anchor.releaseAttId : QUuid(),
            m_anchor.releaseAttBackup));
        m_base.baseTf = curTf;
        m_base.baseEndTargetBlock = curEndBlock;
        m_base.baseEndTargetPoint = curEndPoint;
        m_anchor.releaseAttId = QUuid();
        m_anchor.releaseAttHeld = false;
        return true;
    }
}

double RotateSession::originalWorldRotDeg(const cad::param::ParamDocument* doc) const
{
    if (m_connected) {
        double alpha = m_base.baseAngle;
        if (doc) {
            if (const auto* a = editableAttachment(doc))
                alpha = a->followerAngle;
        }
        return (m_refWorldRad + M_PI - alpha * M_PI / 180.0) * 180.0 / M_PI;
    }
    double baseDeg = 0.0;
    if (doc) {
        if (const auto* blk = doc->findBlock(m_blockId);
            blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            const auto* sp = blk->findPoint(seg.startPointId);
            const auto* ep = blk->findPoint(seg.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const cad::geo::Vec2 wd =
                    blk->transform.toWorld(ep->resolvedPos)
                    - blk->transform.toWorld(sp->resolvedPos);
                baseDeg = wd.angle() * 180.0 / M_PI;
            }
        }
    }
    return baseDeg;
}

QString RotateSession::anchorTag(const cad::param::ParamDocument* doc) const
{
    if (!doc) return QStringLiteral("?");
    const auto* blk = doc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return QStringLiteral("?");
    const auto& seg = blk->segments.front();
    const auto* ap = blk->findPoint(m_anchor.pointId);
    if (ap) return cad::param::Serial::tag(ap->serial);
    const auto* fallback = blk->findPoint(m_anchor.isEnd ? seg.endPointId : seg.startPointId);
    return fallback ? cad::param::Serial::tag(fallback->serial) : QStringLiteral("?");
}

RotateSession::GizmoAngles RotateSession::calculateGizmoAngles(
    double deg, bool isRotating, double dragAngle0) const
{
    GizmoAngles out{};
    if (m_connected) {
        out.dashRad = m_refWorldRad + M_PI;
        const double aRad = cad::geo::normalizeDeg360(deg) * M_PI / 180.0;
        out.arcStart = m_refWorldRad + M_PI - aRad;
        out.arcEnd = out.dashRad;
        double span = out.arcEnd - out.arcStart;
        while (span >  M_PI) span -= 2.0 * M_PI;
        while (span < -M_PI) span += 2.0 * M_PI;
        out.arcEnd = out.arcStart + span;
    } else {
        const double curRad = deg * M_PI / 180.0;
        if (isRotating) {
            const double dragStartRad = dragAngle0 * M_PI / 180.0;
            out.dashRad = dragStartRad;
            out.arcStart = dragStartRad;
            double span = curRad - out.arcStart;
            while (span >  M_PI) span -= 2.0 * M_PI;
            while (span < -M_PI) span += 2.0 * M_PI;
            out.arcEnd = out.arcStart + span;
        } else {
            out.dashRad = curRad;
            out.arcStart = curRad;
            out.arcEnd = curRad;
        }
    }
    return out;
}

} // namespace cad::tools
