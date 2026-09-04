#include "document/commands/BreakExecution.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ParamDocumentRaw.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"

namespace cad::cmd {

void modifyFrontBlock(cad::param::ParamDocument& doc, const QUuid& blockId,
                      const QUuid& segId, const QUuid& auxPtId, BreakState& st,
                      const QUuid& origEndId, QUuid& publishedLinkedId,
                      QString& publishedRefName)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    auto* auxPt = block ? block->findPoint(auxPtId) : nullptr;
    if (!block || !seg || !auxPt) return;

    auxPt = block->findPoint(auxPtId);  // re-acquire
    auxPt->constraint = cad::param::PointConstraint::Polar;

    auto clearInterpFields = [](cad::param::ParamPoint* p) {
        p->angleFormula.clear();
        p->refSegmentId = QUuid();
        p->hostSegmentId = QUuid();
        p->interpPercent = 0.0;
        p->interpPercentFormula.clear();
        p->interpConstant = 0.0;
        p->interpConstantFormula.clear();
        p->interpOffsetAngle = 0.0;
        p->interpOffsetAngleFormula.clear();
        p->interpOffsetDist = 0.0;
        p->interpOffsetDistFormula.clear();
        p->interpFromEnd = false;
        p->interpRefPointId = QUuid();
        p->interAngle = 90.0;
        p->interAngleFormula.clear();
        p->interBidirectional = false;
    };

    if (st.mode == BreakMode::RefChain && !st.polarRefId.isNull()) {
        auxPt->refPointId = st.polarRefId;
        auxPt->distance = st.refOffsetMm;
        auxPt->distanceFormula = st.refOffsetFormula;
        auxPt->angle = st.localAngleDeg;
        clearInterpFields(auxPt);
        auxPt->isAuxiliary = false;
    } else {
        auxPt->refPointId = seg->startPointId;
        auxPt->distance = st.frontDistMm;
        auxPt->distanceFormula = st.frontFormula;
        auxPt->angle = st.isCurve ? st.curveBreakPolarAngleDeg : st.localAngleDeg;
        clearInterpFields(auxPt);
        auxPt->isAuxiliary = false;
    }

    if (st.isCurve) {
        auxPt->autoTangent = false;
        auxPt->tangentLocked = true;
        const auto oi = st.subTanInOverride.constFind(auxPtId);
        const auto of = st.frozenTanIn.constFind(auxPtId);
        if (oi != st.subTanInOverride.constEnd()) auxPt->tangentIn = oi.value();
        else if (of != st.frozenTanIn.constEnd()) auxPt->tangentIn = of.value();
        const auto oso = st.subTanOutOverride.constFind(auxPtId);
        const auto osf = st.frozenTanOut.constFind(auxPtId);
        if (oso != st.subTanOutOverride.constEnd()) auxPt->tangentOut = oso.value();
        else if (osf != st.frozenTanOut.constEnd()) auxPt->tangentOut = osf.value();
        else if (auxPt->tangentIn.lengthSquared() > 1e-12) auxPt->tangentOut = auxPt->tangentIn;
        else if (auxPt->tangentOut.lengthSquared() > 1e-12) auxPt->tangentIn = auxPt->tangentOut;
    }

    seg = block->findSegment(segId);  // re-acquire
    seg->endPointId = auxPtId;
    seg->lengthFormula = st.frontFormula;
    seg->auxPointIds = st.frontAuxIds;
    seg->extendEndMm = 0.0;
    seg->extendEndFormula.clear();

    if (st.mode == BreakMode::RefChain || st.mode == BreakMode::Freeze) {
        if (auto* lv = doc.findLinkedBySource(blockId, segId)) {
            publishedRefName = lv->refName;
        } else {
            cad::param::LinkedVariable fresh =
                cad::param::LinkedVariable::fromSegment(*block, *seg);
            publishedRefName = fresh.refName;
            publishedLinkedId = fresh.id;
            doc.addLinked(std::move(fresh));
        }
    }

    if (st.isCurve) {
        seg->passPointIds = st.frontPassIds;
        const auto* newEnd = block->findPoint(auxPtId);
        const auto* sp = block->findPoint(seg->startPointId);
        if (sp && newEnd && sp->resolved && newEnd->resolved) {
            const cad::geo::Vec2 fChord = newEnd->resolvedPos - sp->resolvedPos;
            const double fLen = fChord.length();
            if (fLen > 1e-9) {
                const cad::geo::Vec2 fUnit = fChord / fLen;
                const cad::geo::Vec2 fNormal{-fUnit.y, fUnit.x};
                for (const auto& ppId : st.frontPassIds) {
                    auto* pp = block->findPoint(ppId);
                    if (!pp || !pp->resolved) continue;
                    const cad::geo::Vec2 rel = pp->resolvedPos - sp->resolvedPos;
                    pp->interpPercent = rel.dot(fUnit) / fLen;
                    pp->interpOffsetDist = rel.dot(fNormal);
                    pp->interpPercentFormula.clear();
                    pp->interpOffsetDistFormula.clear();
                }
            }
        }
        auto frozenTanFor = [&](const QUuid& pid, bool in) -> cad::geo::Vec2 {
            const auto& ov = in ? st.subTanInOverride : st.subTanOutOverride;
            const auto& fr = in ? st.frozenTanIn : st.frozenTanOut;
            const auto io = ov.constFind(pid);
            if (io != ov.constEnd()) return io.value();
            const auto fi = fr.constFind(pid);
            if (fi != fr.constEnd()) return fi.value();
            return cad::geo::Vec2();
        };
        auto freezeTan = [&](const QUuid& pid) {
            auto* p = block->findPoint(pid);
            if (!p) return;
            p->autoTangent = false;
            const cad::geo::Vec2 tI = frozenTanFor(pid, true);
            const cad::geo::Vec2 tO = frozenTanFor(pid, false);
            if (tI.lengthSquared() > 1e-12) p->tangentIn = tI;
            if (tO.lengthSquared() > 1e-12) p->tangentOut = tO;
            if (p->tangentIn.lengthSquared() < 1e-12 && p->tangentOut.lengthSquared() > 1e-12)
                p->tangentIn = p->tangentOut;
            if (p->tangentOut.lengthSquared() < 1e-12 && p->tangentIn.lengthSquared() > 1e-12)
                p->tangentOut = p->tangentIn;
            p->tangentLocked = true;
        };
        freezeTan(seg->startPointId);
        for (const auto& ppId : st.frontPassIds) freezeTan(ppId);
        freezeTan(auxPtId);

        if (st.frontPassIds.empty() && sp && newEnd && sp->resolved && newEnd->resolved) {
            cad::geo::BezierSpan fSpan;
            fSpan.p0 = sp->resolvedPos;
            fSpan.ctrl1 = sp->resolvedPos + sp->tangentOut / 3.0;
            fSpan.ctrl2 = newEnd->resolvedPos - newEnd->tangentIn / 3.0;
            fSpan.p3 = newEnd->resolvedPos;
            const cad::geo::Vec2 midPos = cad::geo::evalBezier(fSpan, 0.5);
            const cad::geo::Vec2 midTan = cad::geo::evalBezierDerivative(fSpan, 0.5) / 2.0;
            const cad::geo::Vec2 mChord = newEnd->resolvedPos - sp->resolvedPos;
            const double mLen = mChord.length();
            if (mLen > 1e-9) {
                const cad::geo::Vec2 mUnit = mChord / mLen;
                const cad::geo::Vec2 mNormal{-mUnit.y, mUnit.x};
                const cad::geo::Vec2 mRel = midPos - sp->resolvedPos;
                cad::param::ParamPoint midPt;
                midPt.constraint = cad::param::PointConstraint::CurveAnchor;
                midPt.hostSegmentId = seg->id;
                midPt.interpPercent = mRel.dot(mUnit) / mLen;
                midPt.interpOffsetDist = mRel.dot(mNormal);
                midPt.autoTangent = false;
                midPt.tangentIn = midTan;
                midPt.tangentOut = midTan;
                midPt.tangentLocked = true;
                midPt.serial = doc.newPointSerial();
                const QUuid midId = midPt.id;
                auto* spM = block->findPoint(seg->startPointId);
                auto* neM = block->findPoint(auxPtId);
                if (spM) spM->tangentOut = spM->tangentOut / 2.0;
                if (neM) neM->tangentIn = neM->tangentIn / 2.0;
                block->addPoint(std::move(midPt));
                seg = block->findSegment(segId);
                seg->passPointIds.push_back(midId);
            }
        }
        if (seg->passPointIds.empty())
            seg->type = cad::param::SegmentType::Line;

        for (const auto& bp : st.backPassPoints) {
            auto& pts = block->points;
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&bp](const cad::param::ParamPoint& p) { return p.id == bp.id; }),
                pts.end());
        }
        block->rebuildPointIndex();
        block->touchGeometry();
    }

    bool endPtUsedElsewhere = false;
    for (const auto& s : block->segments) {
        if (s.id == segId) continue;
        if (s.startPointId == origEndId || s.endPointId == origEndId) {
            endPtUsedElsewhere = true;
            break;
        }
    }
    st.endPtKept = endPtUsedElsewhere;
    if (!endPtUsedElsewhere) {
        auto& pts = block->points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [&origEndId](const cad::param::ParamPoint& p) {
                return p.id == origEndId;
            }),
            pts.end());
        block->rebuildPointIndex();
    }
}

} // namespace cad::cmd
