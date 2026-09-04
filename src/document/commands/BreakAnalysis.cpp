#include "document/commands/BreakAnalysis.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "geometry/CurveSplitter.h"

namespace cad::cmd {

bool canBreak(const cad::param::Block& block,
              const cad::param::Segment& seg,
              const cad::param::ParamPoint& pt)
{
    // Straight lines and curves supported.
    if (seg.type != cad::param::SegmentType::Line &&
        seg.type != cad::param::SegmentType::Bezier)
        return false;
    // Bridges are passive-length; breaking is meaningless.
    if (block.isBridge)
        return false;

    if (pt.constraint == cad::param::PointConstraint::Intersection) {
        // Intersection point: must target this segment.
        return pt.hostSegmentId == seg.id;
    }

    // Curve anchor point (曲线点): must belong to this segment.
    if (pt.constraint == cad::param::PointConstraint::CurveAnchor) {
        return pt.hostSegmentId == seg.id;
    }

    if (pt.constraint == cad::param::PointConstraint::Interpolated) {
        if (pt.hostSegmentId != seg.id)
            return false;
        // Must have no perpendicular offset (point lies exactly on the segment).
        if (std::abs(pt.interpOffsetDist) > 1e-9)
            return false;
        if (!pt.interpOffsetDistFormula.isEmpty())
            return false;
        return true;
    }

    return false;
}

BreakMode determineBreakMode(const cad::param::Block& block,
                             const cad::param::Segment& seg,
                             const cad::param::ParamPoint& pt)
{
    // Direct intersection point: trigonometric position, not a linear function
    // of the segment length.
    if (pt.constraint == cad::param::PointConstraint::Intersection)
        return BreakMode::Freeze;

    if (pt.constraint == cad::param::PointConstraint::Interpolated) {
        // Measured from another point on the same segment: keep the parametric
        // chain by turning the break point into a Polar point anchored to its
        // ref point (which stays on the front half and keeps its own
        // constraint — intersections and further refs stay fully dynamic).
        // Valid only for straight segments with a pure-constant offset; the
        // offset evaluation and front-half check happen in redo().
        if (!pt.interpRefPointId.isNull()) {
            const auto* ref = block.findPoint(pt.interpRefPointId);
            if (seg.type == cad::param::SegmentType::Line
                && ref && ref->isAuxiliary
                && !pt.interpFromEnd
                && pt.interpPercentFormula.isEmpty()
                && std::abs(pt.interpPercent) < 1e-9)
                return BreakMode::RefChain;
            // Unsafe offset shape: the position is not expressible as a
            // linear split — freeze.
            return BreakMode::Freeze;
        }
        // Constant offset driven by a formula (e.g. dart width): the split
        // stays linear, but the numeric distance must be re-evaluated from the
        // formula (the cached interpConstant may be stale). Formula mode does
        // the re-evaluation.
    }

    return BreakMode::Formula;
}

bool gatherBreakGeometry(cad::param::ParamDocument& doc, const QUuid& blockId,
                         const QUuid& segId, const QUuid& auxPtId, BreakState& st)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    auto* auxPt = block ? block->findPoint(auxPtId) : nullptr;
    if (!block || !seg || !auxPt) return false;

    // 快照原终点延长量（modifyFrontBlock 会清零原段 extendEnd，后段需继承）。
    st.origExtendEndMm = seg->extendEndMm;
    st.origExtendEndFormula = seg->extendEndFormula;

    // --- Gather resolved geometry ---
    const auto* startPt = block->findPoint(seg->startPointId);
    const auto* endPt = block->findPoint(seg->endPointId);
    if (!startPt || !endPt || !startPt->resolved || !endPt->resolved) return false;

    st.segLenMm = startPt->resolvedPos.distanceTo(endPt->resolvedPos);
    if (st.segLenMm < 1e-9) return false;  // degenerate segment

    // Local direction angle of the original segment (degrees).
    const cad::geo::Vec2 localDir = endPt->resolvedPos - startPt->resolvedPos;
    st.localAngleDeg = std::atan2(localDir.y, localDir.x) * 180.0 / M_PI;

    // World direction for the new block's rotation.
    st.worldAngleRad = block->transform.rotation
                     + std::atan2(localDir.y, localDir.x);

    // --- Curve-specific: build Bézier, find break tangent & pass-point split ---
    st.isCurve = seg->isCurve();
    if (!st.isCurve) {
        // 打断点参考方向（打前）：interior 辅助点/交点的出口 = 宿主 start→end
        // 方向（exitDirectionAtPoint 语义）；打后 = 新端点的出口方向
        // （曲线 = 断点切线）。两者之差用于对保留在断点的附件做角度补偿。
        const double refOldRad =
            block->transform.rotation + block->exitDirectionAtPoint(auxPtId, segId);
        st.refDeltaRad = st.worldAngleRad - refOldRad;
        return true;
    }

    // Actual distances / angle of the break point (preserve its offset).
    st.curveFrontDist = startPt->resolvedPos.distanceTo(auxPt->resolvedPos);
    st.curveBackDist = auxPt->resolvedPos.distanceTo(endPt->resolvedPos);
    const cad::geo::Vec2 sb = auxPt->resolvedPos - startPt->resolvedPos;
    st.curveBreakPolarAngleDeg = std::atan2(sb.y, sb.x) * 180.0 / M_PI;
    // Build the full curve (all points: start + passPoints + end), keeping
    // the point IDs in parallel so we can freeze each point's tangent.
    std::vector<QUuid> cIds;
    std::vector<cad::geo::Vec2> cPts;
    std::vector<cad::geo::Vec2> cTanIn, cTanOut;
    std::vector<bool> cAuto;
    auto addCurvePt = [&](const cad::param::ParamPoint* p) {
        cIds.push_back(p->id);
        cPts.push_back(p->resolvedPos);
        cTanIn.push_back(p->tangentIn);
        cTanOut.push_back(p->tangentOut);
        cAuto.push_back(p->autoTangent);
    };
    addCurvePt(startPt);
    for (const auto& ppId : seg->passPointIds) {
        const auto* pp = block->findPoint(ppId);
        if (!pp || !pp->resolved) continue;
        addCurvePt(pp);
    }
    addCurvePt(endPt);

    auto spans = block->spansForSegment(*seg, /*skipUnresolvedPassPoints=*/true);
    if (spans.empty()) return true;

    // Freeze original curve tangents using Hobby solver.
    std::vector<cad::geo::Vec2> hobbyIn(cIds.size()), hobbyOut(cIds.size());
    std::tie(hobbyIn, hobbyOut) = cad::geo::solveHobbyTangents(
        cPts, cAuto, cTanIn, cTanOut, seg->tension);
    for (size_t k = 0; k < cIds.size(); ++k) {
        st.frozenTanIn[cIds[k]] = cAuto[k] ? hobbyIn[k] : cTanIn[k];
        st.frozenTanOut[cIds[k]] = cAuto[k] ? hobbyOut[k] : cTanOut[k];
    }

    // Split curve at point via CurveSplitter.
    auto split = cad::geo::splitCurveAtPoint(auxPt->resolvedPos, spans);
    if (split.valid) {
        st.curveTanAtBreak = split.tangentAtBreak;
        if (st.curveTanAtBreak.lengthSquared() > 1e-12)
            st.worldAngleRad = block->transform.rotation
                             + std::atan2(st.curveTanAtBreak.y, st.curveTanAtBreak.x);

        const double refOldRad =
            block->transform.rotation + block->exitDirectionAtPoint(auxPtId, segId);
        st.refDeltaRad = st.worldAngleRad - refOldRad;
        st.breakArc = split.s;
        st.auxArcValid = true;

        for (const QUuid& aid : seg->auxPointIds) {
            if (aid == auxPtId) continue;
            const auto* ap = block->findPoint(aid);
            if (!ap || !ap->resolved) continue;
            auto apProj = cad::geo::projectPointOnCurve(ap->resolvedPos, spans);
            if (apProj.valid)
                st.auxArc.insert(aid, apProj.s);
        }

        if (split.hasSubSpans && split.spanIndex + 1 < static_cast<int>(cIds.size())) {
            st.hasSubSpans = true;
            st.subTanInOverride.insert(auxPtId, split.subTan.tInBreak);
            st.subTanOutOverride.insert(auxPtId, split.subTan.tOutBreak);
            const QUuid subStartId = cIds[static_cast<size_t>(split.spanIndex)];
            st.subTanOutOverride.insert(subStartId, split.subTan.tOutSubStart);
            const QUuid subEndId = cIds[static_cast<size_t>(split.spanIndex) + 1];
            st.subTanInOverride.insert(subEndId, split.subTan.tInSubEnd);
        }
    }

    const cad::geo::Vec2 backChord = endPt->resolvedPos - auxPt->resolvedPos;
    const double backChordLen = backChord.length();

    for (const auto& ppId : seg->passPointIds) {
        if (ppId == auxPtId) continue;  // the break point itself
        const auto* pp = block->findPoint(ppId);
        if (!pp || !pp->resolved) continue;
        double ppArc = -1.0;
        if (split.valid) {
            auto ppProj = cad::geo::projectPointOnCurve(pp->resolvedPos, spans);
            if (ppProj.valid) ppArc = ppProj.s;
        }
        const bool before = split.valid ? (ppArc <= st.breakArc)
                                        : (pp->interpPercent <= auxPt->interpPercent);
        if (before) {
            st.frontPassIds.push_back(ppId);
        } else {
            cad::param::ParamPoint moved = *pp;
            if (backChordLen > 1e-9) {
                const cad::geo::Vec2 bUnit = backChord / backChordLen;
                const cad::geo::Vec2 bNormal{-bUnit.y, bUnit.x};
                const cad::geo::Vec2 rel = pp->resolvedPos - auxPt->resolvedPos;
                moved.interpPercent = rel.dot(bUnit) / backChordLen;
                moved.interpOffsetDist = rel.dot(bNormal);
            } else {
                moved.interpPercent = 0.0;
                moved.interpOffsetDist = 0.0;
            }
            moved.interpPercentFormula.clear();
            moved.interpOffsetDistFormula.clear();
            moved.hostSegmentId = QUuid(); // set later
            st.backPassPoints.push_back(std::move(moved));
        }
    }

    // Compute back endpoint angle in back-local coords.
    if (backChordLen > 1e-9 && st.curveTanAtBreak.lengthSquared() > 1e-12) {
        const cad::geo::Vec2 localRel = endPt->resolvedPos - auxPt->resolvedPos;
        const double localRelAngle = std::atan2(localRel.y, localRel.x);
        const double relAngleFromTan = localRelAngle
            - std::atan2(st.curveTanAtBreak.y, st.curveTanAtBreak.x);
        st.backEndLocalAngle = relAngleFromTan * 180.0 / M_PI;
    }
    return true;
}

} // namespace cad::cmd
