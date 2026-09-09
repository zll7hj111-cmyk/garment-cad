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
#include "tools/RotateDragMath.h"
#include "geometry/Epsilon.h"

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
    m_detachedFollowerAttIds.clear();
}

void RotateSession::setupTarget(cad::param::ParamDocument* doc, const QUuid& blockId)
{
    m_blockId = blockId;
    m_localDir = 0.0;               // 目标切换自清理: 连接分支从不写它, 旧目标的自由线值不得残留
    m_anchor.releaseAttHeld = false;
    m_anchor.releaseAttId = QUuid();

    m_detachedFollowerAttIds.clear();
    if (doc) {
        for (const auto& a : doc->attachments()) {
            if (a.toBlockId == m_blockId && a.fromBlockId != m_blockId && !a.isPin) {
                m_detachedFollowerAttIds.append(a.id);
            }
        }
    }

    // 2026-09 统一（S3 入口统一）：默认支点端只由附件决定，自由线恒取起点。
    // 旧「点击最近端翻转锚心」启发式已删 —— 它让单选（带 clickWorld）与框选 /
    // 选择工具移交（无 clickWorld）两条入口的默认支点不同。
    m_anchor.isEnd = false;
    if (doc) {
        if (const auto* blk = doc->findBlock(blockId)) {
            if (!blk->segments.empty()) {
                const auto& seg = blk->segments.front();
                const bool hasStartAtt = attachmentAtPoint(doc, seg.startPointId) != nullptr;
                const bool hasEndAtt   = attachmentAtPoint(doc, seg.endPointId) != nullptr;
                if (hasEndAtt && !hasStartAtt) m_anchor.isEnd = true;
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

    // 2026-09 拍板 D2：X 键 / 点另一端 = 「把枢轴移到另一端」（不是换角度基准）。
    // rebuildAnchorState 会把 m_pivot 设为新锚心端的世界坐标。
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
        m_base.baseChordLength = att->chordLength;
        m_base.baseChordFormula = att->chordLengthFormula;
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

    // 2026-12 审计 P1-3: 端点拾取半径 = canvas 悬停 token (本类无 scene, 用 fallback)。
    const double tol = CanvasStyle::fallback().hoverRadiusPx() / cad::canvas::safeZoomOr(zoom);
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
        if (m_shadow.active()) {
            applyShadowAngleDeg(doc, deg, dragAngle0);
        } else if (auto* a = editableAttachment(doc)) {
            // 2026-12 审计 P0-4: 三分支写域收口到 FollowerAngle.h 唯一入口
            cad::param::writeFollowerAngleForMode(*a, m_base.rotationMode, deg, segmentRadius(doc));
        }
    } else {
        // 自由线 = 绕任意枢轴的刚体旋转（2026-09 拍板 D2/D4，设计稿 D2 公式）。
        // 起手姿态取 m_base.baseTf 快照：拖动期本函数已实时改写文档，现读文档会
        // 得到「当前」姿态（点 1 黄圈根因）。旧式
        //   origin = m_pivot − m_anchorLocal.rotated(newRot)
        // 把锚心端点钉死在枢轴上 —— 枢轴不在端点时整块平移把端点搬过去 = 点 4 跳变。
        // 枢轴 = 端点时两式等价，故既有 pin 测试语义不变。
        cad::param::Block* blk = doc->findBlock(m_blockId);
        if (!blk || blk->segments.empty()) return;
        const auto& seg = blk->segments.front();
        const auto* sp = blk->findPoint(seg.startPointId);
        const auto* ep = blk->findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) return;
        // 局部姿态方向（圆 = 圆心→接缝半径方向，其它 = 弦向）在旋转下不变，可
        // 现读；世界向 = baseTf.rotation + 局部姿态方向。圆若沿用弦向则恒为 0，
        // 拖动 delta 会多算 a₀（圆多转一个起始角）。
        const double baseWorldDeg = cad::geo::radToDeg(m_base.baseTf.rotation)
            + cad::geo::radToDeg(localPoseDirRad(*blk, seg));
        const double deltaRad = cad::geo::degToRad(deg - baseWorldDeg);
        blk->transform.rotation = m_base.baseTf.rotation + deltaRad;
        blk->transform.origin = m_pivot
            + (m_base.baseTf.origin - m_pivot).rotated(deltaRad);
    }

    QList<QUuid> rotSeeds{m_blockId};
    if (!m_shadow.shadowId.isNull()) rotSeeds.append(m_shadow.shadowId);
    doc->resolveForDrag(rotSeeds, m_detachedFollowerAttIds);
    if (scene) scene->syncBlockPositions();
}

void RotateSession::applyShadowAngleDeg(cad::param::ParamDocument* doc,
                                        double deg,
                                        double dragAngle0)
{
    if (!doc) return;
    const double deltaDeg = cad::geo::normalizeDeg180(dragAngle0 - deg);
    const double deltaRad = cad::geo::degToRad(deltaDeg);
    if (m_shadow.isMounted) {
        if (auto* att1 = doc->findAttachment(m_shadow.att1Id))
            att1->followerAngle = cad::param::followerAngleToStorage(
                m_shadow.shadowDelta0 - deltaDeg);
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
    } else if (m_base.rotationMode == cad::param::RotationMode::ChordLength && m_connected) {
        if (auto* a = editableAttachment(doc)) {
            double chordMm = cad::geo::Units::cmToMm(value);
            const double radius = segmentRadius(doc);
            if (radius > cad::geo::kGeomEps) chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);
            a->chordLength = chordMm;
            a->chordLengthFormula.clear();
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
                a->arcLengthFormula, doc->parameters(), doc->conditions(), arcMm);
        }
        const double radius = segmentRadius(doc);
        const double alphaDeg = (radius > cad::geo::kGeomEps)
            ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
        const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
        return cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius));
    }
    if (m_base.rotationMode == cad::param::RotationMode::ChordLength && m_connected) {
        const auto* a = editableAttachment(doc);
        if (!a) return 0.0;
        double chordMm = a->chordLength;
        if (doc) {
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->chordLengthFormula, doc->parameters(), doc->conditions(), chordMm);
        }
        const double radius = segmentRadius(doc);
        const double alphaDeg = (radius > cad::geo::kGeomEps)
            ? cad::geo::chordMmToDeg(chordMm, radius) : 0.0;
        const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
        return cad::geo::Units::mmToCm(cad::geo::degToChordMm(foldDeg, radius));
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
        if (m_shadow.active()) {
            if (m_shadow.isMounted) {
                if (const auto* att1 = doc->attachmentsView().byId(m_shadow.att1Id))
                    return cad::geo::normalizeDeg180(att1->followerAngle);
            } else {
                if (const auto* sh = doc->findBlock(m_shadow.shadowId)) {
                    const auto* att2 = editableAttachment(doc);
                    const double world = sh->transform.rotation
                        + (att2 ? sh->exitDirectionAtPoint(att2->toPointId, att2->toSegmentId) : 0.0);
                    return cad::geo::normalizeDeg180(cad::geo::radToDeg(world));
                }
            }
        }

        const auto* a = editableAttachment(doc);
        if (!a) return 0.0;
        if (a->rotationMode == cad::param::RotationMode::ArcLength) {
            double arcMm = a->arcLength;
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->arcLengthFormula, doc->parameters(), doc->conditions(), arcMm);
            const double radius = segmentRadius(doc);
            double deg = (radius > cad::geo::kGeomEps)
                ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
            return cad::geo::normalizeDeg180(deg);
        }
        if (a->rotationMode == cad::param::RotationMode::ChordLength) {
            double chordMm = a->chordLength;
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->chordLengthFormula, doc->parameters(), doc->conditions(), chordMm);
            const double radius = segmentRadius(doc);
            double deg = (radius > cad::geo::kGeomEps)
                ? cad::geo::chordMmToDeg(chordMm, radius) : 0.0;
            return cad::geo::normalizeDeg180(deg);
        }
        if (!a->followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                a->followerAngleFormula, doc->parameters(), doc->conditions());
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
    // 2026-09 拍板 D1：自由段姿态 = 世界方向角，统一 [0,360)，不再因「锚心在
    // 终点」加 180°（旧式让同一个姿态在两条入口下差 180°）。
    // 2026-12：姿态方向走 localPoseDirRad —— 圆段 = 圆心→接缝半径方向，
    // 整圆弦向退化会让黄虚线/黄弧/徽标全部塌到世界 0°。
    const double deg = cad::geo::radToDeg(blk->transform.rotation)
        + cad::geo::radToDeg(localPoseDirRad(*blk, seg));
    return cad::geo::normalizeDeg360(deg);
}

bool RotateSession::isAngleLocked(RotateCopyGesture* copyGesture) const
{
    if (copyGesture && copyGesture->active()) return false;
    if (!m_connected) return false;
    if (m_base.rotationMode == cad::param::RotationMode::ArcLength)
        return !m_base.baseArcFormula.isEmpty();
    if (m_base.rotationMode == cad::param::RotationMode::ChordLength)
        return !m_base.baseChordFormula.isEmpty();
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
            a->chordLength = m_base.baseChordLength;
            a->chordLengthFormula = m_base.baseChordFormula;
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
    m_detachedFollowerAttIds.clear();
    doc->resolveAll();
    if (scene) scene->refreshAllBlockItems();
}

bool RotateSession::commit(cad::param::ParamDocument* doc, QUndoStack* undoStack)
{
    if (!doc || !undoStack) return false;

    if (m_connected) {
        if (m_shadow.active()) {
            if (m_shadow.isMounted) {
                auto* att1 = doc->findAttachment(m_shadow.att1Id);
                if (!att1) return false;
                const double curDelta = att1->followerAngle;
                if (std::abs(curDelta - m_shadow.shadowDelta0) <= cad::geo::kGeomEps)
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
                    std::abs(shNew.rotation - m_shadow.shadowTf0.rotation) > cad::geo::kGeomEps
                    || shNew.origin.distanceTo(m_shadow.shadowTf0.origin) > cad::geo::kGeomEpsLoose
                    || std::abs(blkNew.rotation - m_shadow.followerTf0.rotation) > cad::geo::kGeomEps
                    || blkNew.origin.distanceTo(m_shadow.followerTf0.origin) > cad::geo::kGeomEpsLoose;
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
        const double curChord = att->chordLength;
        const QString curChordFormula = att->chordLengthFormula;

        const bool changed = std::abs(curAngle - m_base.baseAngle) > cad::geo::kGeomEps
                          || curFormula != m_base.baseFormula
                          || curMode != m_base.rotationMode
                          || std::abs(curArc - m_base.baseArcLength) > cad::geo::kGeomEpsLoose
                          || curArcFormula != m_base.baseArcFormula
                          || std::abs(curChord - m_base.baseChordLength) > cad::geo::kGeomEpsLoose
                          || curChordFormula != m_base.baseChordFormula;
        if (!changed) return false;

        att->followerAngle = m_base.baseAngle;
        att->followerAngleFormula = m_base.baseFormula;
        att->rotationMode = m_base.rotationMode;
        att->arcLength = m_base.baseArcLength;
        att->arcLengthFormula = m_base.baseArcFormula;
        att->chordLength = m_base.baseChordLength;
        att->chordLengthFormula = m_base.baseChordFormula;
        undoStack->push(new cad::cmd::SetFollowerAngleCommand(
                doc, m_attId, curAngle, curFormula,
                curMode, curArc, curArcFormula,
                curChord, curChordFormula));
        m_base.baseAngle = curAngle;
        m_base.baseFormula = curFormula;
        m_base.rotationMode = curMode;
        m_base.baseArcLength = curArc;
        m_base.baseArcFormula = curArcFormula;
        m_base.baseChordLength = curChord;
        m_base.baseChordFormula = curChordFormula;
        return true;
    } else {
        cad::param::Block* blk = doc->findBlock(m_blockId);
        if (!blk) return false;

        const cad::param::Transform2D curTf = blk->transform;
        const QUuid curEndBlock = blk->endTargetBlockId;
        const QUuid curEndPoint = blk->endTargetPointId;
        const bool changed = std::abs(curTf.rotation - m_base.baseTf.rotation) > cad::geo::kGeomEps
                          || curTf.origin.distanceTo(m_base.baseTf.origin) > cad::geo::kGeomEpsLoose
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

        for (const QUuid& attId : m_detachedFollowerAttIds) {
            if (auto* a = doc->findAttachment(attId)) {
                if (auto* child = doc->findBlock(a->fromBlockId)) {
                    child->preservedBenchmarkAngle = a->followerAngle;
                }
                undoStack->push(new cad::cmd::RemoveAttachmentCommand(doc, attId));
            }
        }
        m_detachedFollowerAttIds.clear();

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
        return cad::geo::radToDeg(m_refWorldRad + cad::geo::kPi - cad::geo::degToRad(alpha));
    }
    // 自由段起手姿态：世界向 = 快照 rotation + 局部姿态方向（局部姿态方向在
    // 旋转下不变，可现读）。2026-09 统一：禁止现读文档姿态 —— 拖动期文档已被
    // 实时改写，现读得到「当前」姿态（黄圈把起点也一起转，用户点 1）。
    // 2026-12：局部姿态方向走 localPoseDirRad（圆 = 半径方向）。
    double baseDeg = 0.0;
    if (doc) {
        if (const auto* blk = doc->findBlock(m_blockId);
            blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            const auto* sp = blk->findPoint(seg.startPointId);
            const auto* ep = blk->findPoint(seg.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                baseDeg = cad::geo::radToDeg(m_base.baseTf.rotation)
                    + cad::geo::radToDeg(localPoseDirRad(*blk, seg));
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
    // 2026-09 拍板 D4：枢轴可为任意点。枢轴不在锚心端上时提示「自由点」，
    // 免得用户以为锚心仍在端点上。
    if (!m_connected && m_pivot.distanceTo(blk->worldPos(m_anchor.pointId)) > cad::geo::kGeomEpsLoose)
        return QStringLiteral("自由点");
    const auto* ap = blk->findPoint(m_anchor.pointId);
    if (ap) return cad::param::Serial::tag(ap->serial);
    const auto* fallback = blk->findPoint(m_anchor.isEnd ? seg.endPointId : seg.startPointId);
    return fallback ? cad::param::Serial::tag(fallback->serial) : QStringLiteral("?");
}

bool RotateSession::pivotOnEndpoint(const cad::param::ParamDocument* doc) const
{
    if (!doc) return false;
    const auto* blk = doc->findBlock(m_blockId);
    if (!blk) return false;
    for (const auto& seg : blk->segments) {
        for (const QUuid& pid : {seg.startPointId, seg.endPointId}) {
            const auto* pt = blk->findPoint(pid);
            if (pt && pt->resolved
                && m_pivot.distanceTo(blk->transform.toWorld(pt->resolvedPos)) <= cad::geo::kGeomEpsLoose)
                return true;
        }
    }
    return false;
}

} // namespace cad::tools
