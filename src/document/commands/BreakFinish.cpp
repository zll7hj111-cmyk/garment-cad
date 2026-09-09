#include "document/commands/BreakExecution.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/FollowerAngle.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ParamDocumentRaw.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "geometry/Epsilon.h"

namespace cad::cmd {

namespace {

/// 圆段后块（CIRCLE_TOOL_DESIGN.md D7）：与原地坐标系同向（只平移），圆心
/// Free、起/终点绕圆心 Polar（半径 = 原半径），3 个象限锚点重新四等分后段包角。
cad::param::Block buildCircleBackBlock(cad::param::ParamDocument& doc,
                                       const cad::param::Block& block,
                                       const cad::param::Segment& seg,
                                       BreakState& st)
{
    cad::param::Block backBlock;
    backBlock.layer = block.layer;
    backBlock.transform.origin = st.breakWorld;
    backBlock.transform.rotation = block.transform.rotation;

    const auto* center = block.findPoint(st.circleCenterId);
    if (!center || !center->resolved) return backBlock;
    const cad::geo::Vec2 centerLocal =
        backBlock.transform.toLocal(block.transform.toWorld(center->resolvedPos));

    cad::param::ParamPoint cp;
    cp.constraint = cad::param::PointConstraint::Free;
    cp.freePos = centerLocal;
    cp.serial = doc.newPointSerial();
    const QUuid centerId = cp.id;

    // 后段起点 = 断点，局部坐标 (0,0) ⇒ 绕圆心的 Polar 角由圆心反向推得。
    const cad::geo::Vec2 toSplit = cad::geo::Vec2::zero() - centerLocal;
    const double startAngleDeg = cad::geo::radToDeg(std::atan2(toSplit.y, toSplit.x));

    cad::param::ParamPoint bpStart;
    bpStart.constraint = cad::param::PointConstraint::Polar;
    bpStart.refPointId = centerId;
    bpStart.distance = st.circleRadiusMm;
    bpStart.distanceFormula = st.circleRadiusFormula;
    bpStart.angle = startAngleDeg;
    bpStart.visible = false;
    bpStart.selectable = false;
    bpStart.serial = doc.newPointSerial();
    st.bpStartId = bpStart.id;

    cad::param::ParamPoint bpEnd;
    bpEnd.constraint = cad::param::PointConstraint::Polar;
    bpEnd.refPointId = centerId;
    bpEnd.distance = st.circleRadiusMm;
    bpEnd.distanceFormula = st.circleRadiusFormula;
    bpEnd.angle = startAngleDeg + st.circleBackSweepRawDeg;
    if (const auto* origEnd = block.findPoint(seg.endPointId)) {
        bpEnd.visible = origEnd->visible;
        bpEnd.selectable = origEnd->selectable;
    }
    bpEnd.serial = doc.newPointSerial();
    st.bpEndId = bpEnd.id;

    backBlock.addPoint(std::move(cp));
    backBlock.addPoint(std::move(bpStart));
    backBlock.addPoint(std::move(bpEnd));

    cad::param::Segment backSeg;
    backSeg.startPointId = st.bpStartId;
    backSeg.endPointId = st.bpEndId;
    backSeg.type = cad::param::SegmentType::Bezier;
    backSeg.fitKind = cad::param::FitKind::Circle;
    backSeg.role = seg.role;
    backSeg.lineStyle = seg.lineStyle;
    backSeg.color = seg.color;
    backSeg.weight = seg.weight;
    backSeg.visible = seg.visible;
    backSeg.showName = false;
    backSeg.showLength = seg.showLength;
    backSeg.constructAngle = 0.0;
    backSeg.serial = doc.newLineSerial();
    backSeg.tension = seg.tension;
    backSeg.extendStartMm = 0.0;
    backSeg.extendStartFormula.clear();
    backSeg.extendEndMm = st.origExtendEndMm;
    backSeg.extendEndFormula = st.origExtendEndFormula;
    st.backSegId = backSeg.id;

    for (int k = 0; k < 3; ++k) {
        cad::param::ParamPoint anchor;
        anchor.constraint = cad::param::PointConstraint::CurveAnchor;
        anchor.hostSegmentId = st.backSegId;
        anchor.interpPercent = 0.25 * (k + 1);
        anchor.interpOffsetDist = 0.0;
        anchor.serial = doc.newPointSerial();
        const QUuid anchorId = anchor.id;
        backSeg.passPointIds.push_back(anchorId);
        backBlock.addPoint(std::move(anchor));
    }
    backBlock.addSegment(std::move(backSeg));
    return backBlock;
}

}  // namespace

cad::param::Block buildBackBlock(cad::param::ParamDocument& doc,
                                 const QUuid& blockId, const QUuid& segId,
                                 const QUuid& auxPtId, BreakState& st,
                                 const QUuid& origEndId,
                                 const QString& publishedRefName)
{
    cad::param::Block backBlock;
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    if (!block || !seg) return backBlock;

    backBlock.layer = block->layer;
    backBlock.transform.origin = st.breakWorld;
    backBlock.transform.rotation = st.worldAngleRad;

    st.rotToLocal = block->transform.rotation - st.worldAngleRad;

    if (st.isCircle)
        return buildCircleBackBlock(doc, *block, *seg, st);

    cad::param::ParamPoint bpStart;
    bpStart.constraint = cad::param::PointConstraint::Free;
    bpStart.freePos = cad::geo::Vec2::zero();
    bpStart.serial = doc.newPointSerial();
    st.bpStartId = bpStart.id;

    {
        auto ovIt = st.subTanOutOverride.constFind(auxPtId);
        auto frIt = st.frozenTanOut.constFind(auxPtId);
        if (ovIt != st.subTanOutOverride.constEnd() || frIt != st.frozenTanOut.constEnd()) {
            const cad::geo::Vec2 t = (ovIt != st.subTanOutOverride.constEnd())
                ? ovIt.value() : frIt.value();
            const cad::geo::Vec2 tanLocal = t.rotated(st.rotToLocal);
            bpStart.autoTangent = false;
            bpStart.tangentIn = tanLocal;
            bpStart.tangentOut = tanLocal;
            bpStart.tangentLocked = true;
        }
    }
    auto backLocalTan = [&](const QUuid& pid, bool in) -> cad::geo::Vec2 {
        const auto& ov = in ? st.subTanInOverride : st.subTanOutOverride;
        const auto& fr = in ? st.frozenTanIn : st.frozenTanOut;
        const auto io = ov.constFind(pid);
        if (io != ov.constEnd()) return io.value().rotated(st.rotToLocal);
        const auto fi = fr.constFind(pid);
        if (fi != fr.constEnd()) return fi.value().rotated(st.rotToLocal);
        return cad::geo::Vec2();
    };

    if (!publishedRefName.isEmpty()) {
        const QString totalCm = st.origFormula.isEmpty()
            ? QString::number(cad::geo::Units::mmToCm(st.segLenMm), 'g', 15)
            : QStringLiteral("(%1)").arg(st.origFormula);
        st.backFormula = QStringLiteral("%1-%2").arg(totalCm, publishedRefName);
    }
    cad::param::ParamPoint bpEnd;
    bpEnd.constraint = cad::param::PointConstraint::Polar;
    bpEnd.refPointId = st.bpStartId;
    bpEnd.distance = st.backDistMm;
    bpEnd.distanceFormula = st.backFormula;
    bpEnd.angle = st.isCurve ? st.backEndLocalAngle : 0.0;
    bpEnd.serial = doc.newPointSerial();
    st.bpEndId = bpEnd.id;

    if (st.isCurve) {
        if (st.frozenTanIn.contains(origEndId)) {
            const cad::geo::Vec2 tanLocal = backLocalTan(origEndId, true);
            bpEnd.autoTangent = false;
            bpEnd.tangentIn = tanLocal;
            bpEnd.tangentOut = tanLocal;
            bpEnd.tangentLocked = true;
        } else if (st.frozenTanOut.contains(origEndId)) {
            const cad::geo::Vec2 tanLocal = backLocalTan(origEndId, false);
            bpEnd.autoTangent = false;
            bpEnd.tangentIn = tanLocal;
            bpEnd.tangentOut = tanLocal;
            bpEnd.tangentLocked = true;
        }
    }

    backBlock.addPoint(std::move(bpStart));
    backBlock.addPoint(std::move(bpEnd));

    cad::param::Segment backSeg;
    backSeg.startPointId = st.bpStartId;
    backSeg.endPointId = st.bpEndId;
    backSeg.type = seg->type;
    backSeg.role = seg->role;
    backSeg.lengthFormula = st.backFormula;
    backSeg.lineStyle = seg->lineStyle;
    backSeg.color = seg->color;
    backSeg.weight = seg->weight;
    backSeg.visible = seg->visible;
    backSeg.showName = false;
    backSeg.showLength = seg->showLength;
    backSeg.constructAngle = 0.0;
    backSeg.serial = doc.newLineSerial();
    backSeg.tension = seg->tension;

    backSeg.extendStartMm = 0.0;
    backSeg.extendStartFormula.clear();
    backSeg.extendEndMm = st.origExtendEndMm;
    backSeg.extendEndFormula = st.origExtendEndFormula;
    st.backSegId = backSeg.id;

    if (st.isCurve && !st.backPassPoints.empty()) {
        backSeg.type = cad::param::SegmentType::Bezier;
        for (auto& pp : st.backPassPoints) {
            pp.hostSegmentId = st.backSegId;
            const cad::geo::Vec2 tI = backLocalTan(pp.id, true);
            const cad::geo::Vec2 tO = backLocalTan(pp.id, false);
            if (tI.lengthSquared() > cad::geo::kGeomEpsTight || tO.lengthSquared() > cad::geo::kGeomEpsTight) {
                pp.autoTangent = false;
                pp.tangentIn = tI.lengthSquared() > cad::geo::kGeomEpsTight ? tI : tO;
                pp.tangentOut = tO.lengthSquared() > cad::geo::kGeomEpsTight ? tO : tI;
                pp.tangentLocked = true;
            }
            backSeg.passPointIds.push_back(pp.id);
            backBlock.addPoint(std::move(pp));
        }
    }

    if (st.isCurve && st.backPassPoints.empty()) {
        backBlock.resolve(doc.parameters());
        const auto* bs = backBlock.findPoint(st.bpStartId);
        const auto* be = backBlock.findPoint(st.bpEndId);
        if (bs && be && bs->resolved && be->resolved) {
            cad::geo::BezierSpan bSpan;
            bSpan.p0 = bs->resolvedPos;
            bSpan.ctrl1 = bs->resolvedPos + bs->tangentOut / 3.0;
            bSpan.ctrl2 = be->resolvedPos - be->tangentIn / 3.0;
            bSpan.p3 = be->resolvedPos;
            const cad::geo::Vec2 midPos = cad::geo::evalBezier(bSpan, 0.5);
            const cad::geo::Vec2 midTan = cad::geo::evalBezierDerivative(bSpan, 0.5) / 2.0;
            const cad::geo::Vec2 mChord = be->resolvedPos - bs->resolvedPos;
            const double mLen = mChord.length();
            if (mLen > cad::geo::kGeomEps) {
                const cad::geo::Vec2 mUnit = mChord / mLen;
                const cad::geo::Vec2 mNormal{-mUnit.y, mUnit.x};
                const cad::geo::Vec2 mRel = midPos - bs->resolvedPos;
                cad::param::ParamPoint midPt;
                midPt.constraint = cad::param::PointConstraint::CurveAnchor;
                midPt.hostSegmentId = st.backSegId;
                midPt.interpPercent = mRel.dot(mUnit) / mLen;
                midPt.interpOffsetDist = mRel.dot(mNormal);
                midPt.autoTangent = false;
                midPt.tangentIn = midTan;
                midPt.tangentOut = midTan;
                midPt.tangentLocked = true;
                midPt.serial = doc.newPointSerial();
                const QUuid midId = midPt.id;
                auto* bsM = backBlock.findPoint(st.bpStartId);
                auto* beM = backBlock.findPoint(st.bpEndId);
                if (bsM) bsM->tangentOut = bsM->tangentOut / 2.0;
                if (beM) beM->tangentIn = beM->tangentIn / 2.0;
                backBlock.addPoint(std::move(midPt));
                backSeg.passPointIds.push_back(midId);
                backSeg.type = cad::param::SegmentType::Bezier;
            }
        }
    }

    for (auto& aux : st.backAuxPoints) {
        aux.hostSegmentId = st.backSegId;
        backSeg.auxPointIds.push_back(aux.id);
        backBlock.addPoint(std::move(aux));
    }

    backBlock.addSegment(std::move(backSeg));
    return backBlock;
}

void finalizeBreak(cad::param::ParamDocument& doc, BreakState& st,
                   const QUuid& frontBlockId, const QUuid& frontSegId,
                   const QUuid& frontAuxPtId, cad::param::Block backBlock,
                   const std::vector<cad::param::Attachment>& removedAttachments)
{
    QList<QUuid> staleIds;
    staleIds.reserve(static_cast<qsizetype>(removedAttachments.size()));
    for (const auto& att : removedAttachments)
        staleIds.push_back(att.id);
    doc.removeAttachments(staleIds);

    const QUuid newBlockId = backBlock.id;
    const QUuid bpStartId = st.bpStartId;
    doc.addBlock(std::move(backBlock));

    std::vector<cad::param::Attachment> kept;
    kept.reserve(removedAttachments.size());
    for (cad::param::Attachment att : removedAttachments) {
        if (att.toPointId == frontAuxPtId) {
            if (std::abs(st.refDeltaRad) > cad::geo::kGeomEps) {
                if (att.rotationMode == cad::param::RotationMode::ArcLength) {
                    if (att.arcLengthFormula.isEmpty()) {
                        if (const auto* fb = doc.findBlock(att.fromBlockId)) {
                            const double radius =
                                fb->segmentLengthAtPoint(att.fromPointId);
                            if (radius > cad::geo::kGeomEps)
                                att.arcLength += st.refDeltaRad * radius;
                        }
                    }
                } else if (att.rotationMode == cad::param::RotationMode::ChordLength) {
                    if (att.chordLengthFormula.isEmpty()) {
                        if (const auto* fb = doc.findBlock(att.fromBlockId)) {
                            const double radius =
                                fb->segmentLengthAtPoint(att.fromPointId);
                            if (radius > cad::geo::kGeomEps) {
                                double deg = cad::geo::chordMmToDeg(att.chordLength, radius);
                                deg = cad::geo::normalizeDeg180(deg + cad::geo::radToDeg(st.refDeltaRad));
                                att.chordLength = cad::geo::degToChordMm(deg, radius);
                            }
                        }
                    }
                } else if (att.followerAngleFormula.isEmpty()) {
                    double ang = att.followerAngle + cad::geo::radToDeg(st.refDeltaRad);
                    ang = cad::geo::normalizeDeg360(ang);
                    att.followerAngle = ang;
                }
            }
            kept.push_back(std::move(att));
        } else if (st.endPtKept) {
            kept.push_back(std::move(att));
        } else {
            att.toBlockId = newBlockId;
            att.toPointId = st.bpEndId;
            att.toSegmentId = st.backSegId;
            kept.push_back(std::move(att));
        }
    }
    cad::param::RawModelAccess::addAttachmentsRaw(doc, kept);

    cad::param::Attachment att;
    att.fromBlockId = newBlockId;
    att.fromPointId = bpStartId;
    att.toBlockId = frontBlockId;
    att.toPointId = frontAuxPtId;
    att.toSegmentId = frontSegId;
    const double refWorld = cad::param::effectiveAngleRefWorld(&doc, att);
    const auto* backBlk = doc.findBlock(newBlockId);
    const double localDir = backBlk ? backBlk->directionAtPoint(bpStartId) : 0.0;
    const double rot = backBlk ? backBlk->transform.rotation : 0.0;
    att.followerAngle = cad::param::backSolveFollowerAngle(rot, localDir, refWorld);
    doc.addAttachment(std::move(att));
}

} // namespace cad::cmd
