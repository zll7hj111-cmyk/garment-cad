#include "BreakCommands.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"

namespace cad::cmd {

namespace {

/// Check whether a point can serve as a break point:
/// - Interpolated with no offset, on a Line segment, not on a bridge block.
/// - Intersection point on the segment (hostSegmentId matches).
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

/// How the break distances are materialized on the two resulting segments.
enum class BreakMode {
    Formula,   // linear percent/constant split — the length formulas survive
    Freeze,    // numeric distances, no formulas (intersection-driven positions)
    RefChain,  // the break point's ref chain is converted into a Polar anchor
               // chain — every hop keeps its offset (formula included)
};

/// Pick the break materialization mode for a given break point.
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

} // namespace

BreakSegmentCommand::BreakSegmentCommand(cad::param::ParamDocument* doc,
                                         const QUuid& blockId,
                                         const QUuid& segmentId,
                                         const QUuid& auxPointId,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_auxPointId(auxPointId)
{
    setText(QStringLiteral("打断线段"));

    // Validate preconditions at construction time.
    const auto* block = doc->findBlock(blockId);
    const auto* seg = block ? block->findSegment(segmentId) : nullptr;
    const auto* pt = block ? block->findPoint(auxPointId) : nullptr;
    if (block && seg && pt)
        m_valid = canBreak(*block, *seg, *pt);
}

void BreakSegmentCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    auto* auxPt = block->findPoint(m_auxPointId);
    if (!seg || !auxPt) return;
    if (!canBreak(*block, *seg, *auxPt)) return;

    // --- Snapshot for undo ---
    m_origBlockSnapshot = *block;

    // Save attachments that will be removed (those targeting the aux point
    // or the original end point on this block).
    m_removedAttachments.clear();
    const QUuid origEndId = seg->endPointId;
    for (const auto& att : m_doc->attachments()) {
        if (att.toBlockId == m_blockId
            && (att.toPointId == m_auxPointId || att.toPointId == origEndId)) {
            m_removedAttachments.push_back(att);
        }
    }

    // --- Gather resolved geometry ---
    const auto* startPt = block->findPoint(seg->startPointId);
    const auto* endPt = block->findPoint(seg->endPointId);
    if (!startPt || !endPt || !startPt->resolved || !endPt->resolved) return;

    const double segLenMm = startPt->resolvedPos.distanceTo(endPt->resolvedPos);
    if (segLenMm < 1e-9) return;  // degenerate segment

    // Local direction angle of the original segment (degrees).
    const cad::geo::Vec2 localDir = endPt->resolvedPos - startPt->resolvedPos;
    const double localAngleDeg = std::atan2(localDir.y, localDir.x) * 180.0 / M_PI;

    // World direction for the new block's rotation.
    double worldAngleRad = block->transform.rotation
                         + std::atan2(localDir.y, localDir.x);

    // --- Curve-specific: build Bézier, find break tangent & pass-point split ---
    const bool isCurve = seg->isCurve();
    cad::geo::Vec2 curveTanAtBreak;  // tangent (derivative) at the break point
    std::vector<QUuid> frontPassIds; // pass points going to front segment
    std::vector<cad::param::ParamPoint> backPassPoints; // pass points for back block
    double backEndLocalAngle = 0.0;  // angle of back endpoint in back-local coords
    // Actual (non-projected) distances & polar angle for the break point. A
    // curve break point is OFFSET from the chord, so projecting it onto the
    // chord would snap it back onto the chord and destroy the shape — we must
    // keep its true position.
    double curveFrontDist = 0.0;          // |start → breakPoint|
    double curveBackDist = 0.0;           // |breakPoint → end|
    double curveBreakPolarAngleDeg = 0.0; // true polar angle start → breakPoint
    // Frozen tangents (pointId → tangent) captured from the ORIGINAL curve's C2
    // solve, so both halves keep the exact pre-break shape (no re-solve drift).
    QHash<QUuid, cad::geo::Vec2> frozenTangents;

    if (isCurve) {
        // Actual distances / angle of the break point (preserve its offset).
        curveFrontDist = startPt->resolvedPos.distanceTo(auxPt->resolvedPos);
        curveBackDist = auxPt->resolvedPos.distanceTo(endPt->resolvedPos);
        const cad::geo::Vec2 sb = auxPt->resolvedPos - startPt->resolvedPos;
        curveBreakPolarAngleDeg = std::atan2(sb.y, sb.x) * 180.0 / M_PI;
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

        auto spans = cad::geo::buildBezierSpans(cPts, cTanIn, cTanOut, cAuto, seg->tension,
                                                cad::geo::AutoCurveMode::Hobby);
        if (!spans.empty()) {
            // Solve the C2 tangents of the ORIGINAL curve and freeze them for
            // every point. After the break both halves reuse these tangents
            // (manual mode), so the shape is bit-identical to before the break.
            const std::vector<cad::geo::Vec2> c2 =
                cad::geo::solveC2Tangents(cPts, cAuto, cTanIn, cTanOut);
            for (size_t k = 0; k < cIds.size(); ++k) {
                // Manual points keep their own stored tangents; auto points get
                // the solved value.
                frozenTangents[cIds[k]] = cAuto[k] ? c2[k]
                    : cad::geo::Vec2(cTanOut[k]);  // manual: use its out-tangent
            }

            // Project the break point onto the curve to get global parameter T.
            auto proj = cad::geo::projectPointOnCurve(auxPt->resolvedPos, spans);
            if (proj.valid) {
                curveTanAtBreak = cad::geo::evalCurveDerivative(spans, proj.t);
                // Use curve tangent for back block rotation.
                if (curveTanAtBreak.lengthSquared() > 1e-12)
                    worldAngleRad = block->transform.rotation
                                  + std::atan2(curveTanAtBreak.y, curveTanAtBreak.x);
            }

            // Distribute pass points: those before the break go to front,
            // those after go to back. The break point itself is EXCLUDED (it
            // becomes the shared endpoint of both halves).
            const double breakPercent = auxPt->interpPercent;
            const cad::geo::Vec2 backChord = endPt->resolvedPos - auxPt->resolvedPos;
            const double backChordLen = backChord.length();

            for (const auto& ppId : seg->passPointIds) {
                if (ppId == m_auxPointId) continue;  // the break point itself
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                if (pp->interpPercent <= breakPercent) {
                    frontPassIds.push_back(ppId);
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
                    backPassPoints.push_back(std::move(moved));
                }
            }

            // Compute back endpoint angle in back-local coords.
            if (backChordLen > 1e-9 && curveTanAtBreak.lengthSquared() > 1e-12) {
                const cad::geo::Vec2 localRel = endPt->resolvedPos - auxPt->resolvedPos;
                const double localRelAngle = std::atan2(localRel.y, localRel.x);
                const double relAngleFromTan = localRelAngle
                    - std::atan2(curveTanAtBreak.y, curveTanAtBreak.x);
                backEndLocalAngle = relAngleFromTan * 180.0 / M_PI;
            }
        }
    }

    // --- Evaluate break position ---
    const BreakMode mode = determineBreakMode(*block, *seg, *auxPt);

    QString frontFormula;
    QString backFormula;
    double frontDistMm = 0.0;
    double backDistMm = 0.0;
    double breakAlong = 0.0;  // mm from start

    // RefChain anchor: the Polar break point keeps its parametric ref point.
    QUuid polarRefId;
    double refOffsetMm = 0.0;  // numeric offset from the anchor (mm)
    QString refOffsetFormula;  // offset formula (cm domain), may be empty

    // Formula re-evaluation uses the same environment as the Resolver.
    cad::param::EvalContext ctx;
    const auto& params = m_doc->parameters();
    const auto& conditioned = m_doc->conditions();

    if (mode == BreakMode::Freeze) {
        // Intersection-driven break: freeze to absolute numeric distances.
        // Project the break point's resolved position onto the segment direction.
        const cad::geo::Vec2 unit = localDir / segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        breakAlong = rel.dot(unit);
        breakAlong = std::clamp(breakAlong, 0.0, segLenMm);

        frontDistMm = breakAlong;
        backDistMm = segLenMm - breakAlong;
        // No formulas — leave frontFormula / backFormula empty.
    } else if (mode == BreakMode::RefChain) {
        // Project the true resolved position for aux-point redistribution.
        const cad::geo::Vec2 unit = localDir / segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        breakAlong = std::clamp(rel.dot(unit), 0.0, segLenMm);
        frontDistMm = breakAlong;
        backDistMm = segLenMm - breakAlong;

        // The parametric anchor survives only if the ref point stays on the
        // front half; otherwise the Polar reference would dangle (the ref is
        // redistributed to the back block) — fall back to a numeric freeze.
        const auto* refPt = block->findPoint(auxPt->interpRefPointId);
        const cad::geo::Vec2 refRel = refPt->resolvedPos - startPt->resolvedPos;
        double offsetMm = auxPt->interpConstant;
        if (!auxPt->interpConstantFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                auxPt->interpConstantFormula, params, conditioned, &ctx);
            if (r.ok) offsetMm = cad::geo::Units::cmToMm(r.value);
        }
        if (offsetMm < -1e-9 || refRel.dot(unit) > breakAlong + 1e-6) {
            polarRefId = QUuid();  // marker: no anchor, numeric freeze below
        } else {
            polarRefId = refPt->id;
            refOffsetMm = offsetMm;
            refOffsetFormula = auxPt->interpConstantFormula;  // cm domain
        }
        // Front length is endpoint-driven (start → Polar end); no formula.
    } else {
        // Standard Interpolated break: parametric formula splitting.
        // Percent/constant are re-evaluated from formulas so the numeric
        // distances agree with the Resolver (cached fields may be stale).
        double percent = auxPt->interpPercent;
        if (!auxPt->interpPercentFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                auxPt->interpPercentFormula, params, conditioned, &ctx);
            if (r.ok) percent = r.value;
        }
        double constantMm = auxPt->interpConstant;  // mm internal
        if (!auxPt->interpConstantFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                auxPt->interpConstantFormula, params, conditioned, &ctx);
            if (r.ok) constantMm = cad::geo::Units::cmToMm(r.value);
        }

        // Effective percent from start (accounting for interpFromEnd).
        double effPercent = auxPt->interpFromEnd ? (1.0 - percent) : percent;

        // --- Build formula texts ---
        QString percentText;
        if (auxPt->interpPercentFormula.isEmpty())
            percentText = QString::number(percent, 'g', 15);
        else
            percentText = auxPt->interpPercentFormula;

        QString effPercentText;
        if (auxPt->interpFromEnd)
            effPercentText = QStringLiteral("(1-%1)").arg(percentText);
        else
            effPercentText = percentText;

        QString constantText;
        bool hasConstant = false;
        if (auxPt->interpConstantFormula.isEmpty()) {
            if (std::abs(constantMm) > 1e-9) {
                hasConstant = true;
                constantText = QString::number(
                    cad::geo::Units::mmToCm(constantMm), 'g', 15);
            }
        } else {
            hasConstant = true;
            constantText = auxPt->interpConstantFormula;
        }

        // Original formula (or numeric fallback in cm).
        QString origF;
        if (seg->lengthFormula.isEmpty())
            origF = QString::number(cad::geo::Units::mmToCm(segLenMm), 'g', 15);
        else
            origF = seg->lengthFormula;

        // Front formula: (orig)*effPercent [+constant]
        frontFormula = QStringLiteral("(%1)*%2").arg(origF, effPercentText);
        if (hasConstant)
            frontFormula += QStringLiteral("+%1").arg(constantText);

        // Back formula: (orig)*(1-effPercent) [-constant]
        backFormula = QStringLiteral("(%1)*(1-%2)").arg(origF, effPercentText);
        if (hasConstant)
            backFormula += QStringLiteral("-%1").arg(constantText);

        // Numeric distances (mm) for initial Polar values.
        frontDistMm = segLenMm * effPercent + constantMm;
        backDistMm = segLenMm - frontDistMm;
        breakAlong = frontDistMm;
    }

    // Curve break: the break point is offset from the chord, so freeze to its
    // ACTUAL distance/angle (the chord projection above would snap it onto the
    // chord and destroy the shape). Lengths are frozen (no formula) because the
    // break point's position on the curve is fixed.
    if (isCurve) {
        frontDistMm = curveFrontDist;
        backDistMm = curveBackDist;
        frontFormula.clear();
        backFormula.clear();
    }

    // World position of the break point.
    const cad::geo::Vec2 breakWorld = block->transform.toWorld(auxPt->resolvedPos);

    // --- Redistribute auxiliary points ---
    // Determine which aux points go to the back block.
    std::vector<QUuid> frontAuxIds;
    std::vector<cad::param::ParamPoint> backAuxPoints;

    for (const QUuid& auxId : seg->auxPointIds) {
        if (auxId == m_auxPointId) continue;  // the break point itself
        const auto* otherAux = block->findPoint(auxId);
        if (!otherAux) continue;

        // Compute this aux point's distance from the original start.
        double otherAlong = 0.0;
        if (otherAux->resolved && startPt->resolved && endPt->resolved) {
            // Project resolved position onto the segment direction.
            const cad::geo::Vec2 rel = otherAux->resolvedPos - startPt->resolvedPos;
            const cad::geo::Vec2 unit = localDir / segLenMm;
            otherAlong = rel.x * unit.x + rel.y * unit.y;
        }

        if (otherAlong <= breakAlong) {
            frontAuxIds.push_back(auxId);
        } else {
            // Move to back block: recalculate percent relative to back segment.
            cad::param::ParamPoint moved = *otherAux;
            const double relAlong = otherAlong - breakAlong;
            moved.interpPercent = (backDistMm > 1e-9)
                ? relAlong / backDistMm : 0.0;
            moved.interpPercentFormula.clear();  // numeric for V1
            moved.interpConstant = 0.0;
            moved.interpConstantFormula.clear();
            moved.interpFromEnd = false;
            // hostSegmentId will be set to the new segment's ID below.
            backAuxPoints.push_back(std::move(moved));
        }
    }

    // --- Modify original block (front segment) ---

    // Convert the break point to a Polar endpoint. In RefChain mode the Polar
    // anchor is the point's measurement ref point (kept on the front half with
    // its own constraint — intersections and further refs stay fully dynamic;
    // the Resolver's fixpoint resolves the resulting dependency cycle).
    auxPt = block->findPoint(m_auxPointId);  // re-acquire (pointer may have shifted)
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
        // Clear Intersection fields (in case the point was an Intersection).
        p->interAngle = 90.0;
        p->interAngleFormula.clear();
        p->interBidirectional = false;
    };

    if (mode == BreakMode::RefChain && !polarRefId.isNull()) {
        // Keep the parametric anchor: Polar from the original ref point, offset
        // driven by the same formula (cm domain), direction along the segment.
        auxPt->refPointId = polarRefId;
        auxPt->distance = refOffsetMm;
        auxPt->distanceFormula = refOffsetFormula;
        auxPt->angle = localAngleDeg;
        clearInterpFields(auxPt);
        auxPt->isAuxiliary = false;
    } else {
        auxPt->refPointId = seg->startPointId;
        auxPt->distance = frontDistMm;
        auxPt->distanceFormula = frontFormula;
        auxPt->angle = isCurve ? curveBreakPolarAngleDeg : localAngleDeg;
        clearInterpFields(auxPt);
        auxPt->isAuxiliary = false;
    }

    // Curve: set tangent at the break point (front endpoint's outgoing tangent
    // = curve derivative at break, so the shape is preserved).
    if (isCurve && curveTanAtBreak.lengthSquared() > 1e-12) {
        auxPt->autoTangent = false;
        auxPt->tangentIn = curveTanAtBreak;   // incoming (from the front span)
        auxPt->tangentOut = curveTanAtBreak;  // outgoing (continues into back)
        auxPt->tangentLocked = true;
    }

    // Update the original segment: endpoint becomes the break point.
    seg = block->findSegment(m_segmentId);  // re-acquire
    seg->endPointId = m_auxPointId;
    seg->lengthFormula = frontFormula;
    seg->auxPointIds = frontAuxIds;

    // Curve: update front passPointIds and re-parameterize them.
    if (isCurve) {
        seg->passPointIds = frontPassIds;
        // Re-parameterize front pass points relative to the new front chord.
        const auto* newEnd = block->findPoint(m_auxPointId);
        const auto* sp = block->findPoint(seg->startPointId);
        if (sp && newEnd && sp->resolved && newEnd->resolved) {
            const cad::geo::Vec2 fChord = newEnd->resolvedPos - sp->resolvedPos;
            const double fLen = fChord.length();
            if (fLen > 1e-9) {
                const cad::geo::Vec2 fUnit = fChord / fLen;
                const cad::geo::Vec2 fNormal{-fUnit.y, fUnit.x};
                for (const auto& ppId : frontPassIds) {
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
        // Freeze tangents on all front points (start + pass points + break point)
        // so the front half keeps the exact pre-break shape.
        auto freezeTan = [&](const QUuid& pid) {
            auto it = frozenTangents.constFind(pid);
            if (it == frozenTangents.constEnd()) return;
            auto* p = block->findPoint(pid);
            if (!p) return;
            p->autoTangent = false;
            p->tangentIn = it.value();
            p->tangentOut = it.value();
            p->tangentLocked = true;
        };
        freezeTan(seg->startPointId);
        for (const auto& ppId : frontPassIds) freezeTan(ppId);
        freezeTan(m_auxPointId);

        // If the front half has NO pass points left (e.g. breaking at the first
        // curve point) it would render as a straight line. Insert the exact
        // Bézier midpoint of the single remaining span as a pass point so the
        // curved shape is preserved.
        //
        // IMPORTANT: splitting one cubic span into two half-spans reparameterizes
        // each half to half the original parameter range, so every tangent
        // (which encodes the derivative) must be HALVED for the two sub-spans
        // to reproduce the original span exactly (de Casteljau).
        if (frontPassIds.empty() && sp && newEnd && sp->resolved && newEnd->resolved) {
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
                midPt.serial = m_doc->newPointSerial();
                const QUuid midId = midPt.id;
                // Halve the endpoint tangents (they were frozen to full values).
                auto* spM = block->findPoint(seg->startPointId);
                auto* neM = block->findPoint(m_auxPointId);
                if (spM) spM->tangentOut = spM->tangentOut / 2.0;
                if (neM) neM->tangentIn = neM->tangentIn / 2.0;
                block->addPoint(std::move(midPt));
                seg = block->findSegment(m_segmentId);  // re-acquire after addPoint
                seg->passPointIds.push_back(midId);
            }
        }
        if (seg->passPointIds.empty())
            seg->type = cad::param::SegmentType::Line;

        // Remove back pass points from the original block.
        for (const auto& bp : backPassPoints) {
            auto& pts = block->points;
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&bp](const cad::param::ParamPoint& p) { return p.id == bp.id; }),
                pts.end());
        }
        block->rebuildPointIndex();
    }

    // Remove the original end point if no other segment references it.
    bool endPtUsedElsewhere = false;
    for (const auto& s : block->segments) {
        if (s.id == m_segmentId) continue;
        if (s.startPointId == origEndId || s.endPointId == origEndId) {
            endPtUsedElsewhere = true;
            break;
        }
    }
    if (!endPtUsedElsewhere) {
        auto& pts = block->points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [&origEndId](const cad::param::ParamPoint& p) {
                return p.id == origEndId;
            }),
            pts.end());
        block->rebuildPointIndex();
    }

    // --- Create new block (back segment) ---
    cad::param::Block backBlock;
    backBlock.layer = block->layer;  // 打断继承原层（与复制一致：打断/复制继承原层）
    backBlock.transform.origin = breakWorld;
    backBlock.transform.rotation = worldAngleRad;

    // Rotation to convert tangent vectors from the ORIGINAL block's local
    // frame into the back block's local frame (they differ by the block
    // rotation delta). Tangents are direction+magnitude, so only rotate.
    const double rotToLocal = block->transform.rotation - worldAngleRad;

    // Start point at local origin (coincides with break point).
    cad::param::ParamPoint bpStart;
    bpStart.constraint = cad::param::PointConstraint::Free;
    bpStart.freePos = cad::geo::Vec2::zero();
    bpStart.serial = m_doc->newPointSerial();
    const QUuid bpStartId = bpStart.id;
    // Curve: inherit tangent at break point (rotate into back-local frame).
    if (isCurve && curveTanAtBreak.lengthSquared() > 1e-12) {
        const cad::geo::Vec2 tanLocal = curveTanAtBreak.rotated(rotToLocal);
        bpStart.autoTangent = false;
        bpStart.tangentIn = tanLocal;
        bpStart.tangentOut = tanLocal;
        bpStart.tangentLocked = true;
    }

    // End point: Polar from origin. For curves the angle may differ from 0
    // because the block rotation follows the curve tangent, not the chord.
    cad::param::ParamPoint bpEnd;
    bpEnd.constraint = cad::param::PointConstraint::Polar;
    bpEnd.refPointId = bpStartId;
    bpEnd.distance = backDistMm;
    bpEnd.distanceFormula = backFormula;
    bpEnd.angle = isCurve ? backEndLocalAngle : 0.0;
    bpEnd.serial = m_doc->newPointSerial();
    const QUuid bpEndId = bpEnd.id;
    // Curve: freeze the original end point's tangent (rotated to back-local).
    if (isCurve) {
        auto it = frozenTangents.constFind(origEndId);
        if (it != frozenTangents.constEnd()) {
            const cad::geo::Vec2 tanLocal = it.value().rotated(rotToLocal);
            bpEnd.autoTangent = false;
            bpEnd.tangentIn = tanLocal;
            bpEnd.tangentOut = tanLocal;
            bpEnd.tangentLocked = true;
        }
    }

    backBlock.addPoint(std::move(bpStart));
    backBlock.addPoint(std::move(bpEnd));

    // Segment inheriting visual properties from the original.
    cad::param::Segment backSeg;
    backSeg.startPointId = bpStartId;
    backSeg.endPointId = bpEndId;
    backSeg.type = seg->type;
    backSeg.role = seg->role;
    backSeg.lengthFormula = backFormula;
    backSeg.lineStyle = seg->lineStyle;
    backSeg.color = seg->color;
    backSeg.weight = seg->weight;
    backSeg.visible = seg->visible;
    backSeg.showName = false;
    backSeg.showLength = seg->showLength;
    backSeg.constructAngle = 0.0;
    backSeg.serial = m_doc->newLineSerial();
    backSeg.tension = seg->tension;
    const QUuid backSegId = backSeg.id;

    // Curve: add pass points to the back segment, freezing each one's tangent
    // (rotated into back-local) so the back half keeps the pre-break shape.
    if (isCurve && !backPassPoints.empty()) {
        backSeg.type = cad::param::SegmentType::Bezier;
        for (auto& pp : backPassPoints) {
            pp.hostSegmentId = backSegId;
            auto it = frozenTangents.constFind(pp.id);
            if (it != frozenTangents.constEnd()) {
                const cad::geo::Vec2 tanLocal = it.value().rotated(rotToLocal);
                pp.autoTangent = false;
                pp.tangentIn = tanLocal;
                pp.tangentOut = tanLocal;
                pp.tangentLocked = true;
            }
            backSeg.passPointIds.push_back(pp.id);
            backBlock.addPoint(std::move(pp));
        }
    }

    // Curve: if the back half has NO pass points it would render as a straight
    // line. Insert the exact Bézier midpoint (in back-local coords) as a pass
    // point so the curved shape is preserved. Tangents are HALVED because the
    // single span is reparameterized into two half-spans (de Casteljau).
    if (isCurve && backPassPoints.empty()) {
        backBlock.resolve(m_doc->parameters());  // resolve bpStart/bpEnd positions
        const auto* bs = backBlock.findPoint(bpStartId);
        const auto* be = backBlock.findPoint(bpEndId);
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
            if (mLen > 1e-9) {
                const cad::geo::Vec2 mUnit = mChord / mLen;
                const cad::geo::Vec2 mNormal{-mUnit.y, mUnit.x};
                const cad::geo::Vec2 mRel = midPos - bs->resolvedPos;
                cad::param::ParamPoint midPt;
                midPt.constraint = cad::param::PointConstraint::CurveAnchor;
                midPt.hostSegmentId = backSegId;
                midPt.interpPercent = mRel.dot(mUnit) / mLen;
                midPt.interpOffsetDist = mRel.dot(mNormal);
                midPt.autoTangent = false;
                midPt.tangentIn = midTan;
                midPt.tangentOut = midTan;
                midPt.tangentLocked = true;
                midPt.serial = m_doc->newPointSerial();
                const QUuid midId = midPt.id;
                // Halve the endpoint tangents (they were frozen to full values).
                auto* bsM = backBlock.findPoint(bpStartId);
                auto* beM = backBlock.findPoint(bpEndId);
                if (bsM) bsM->tangentOut = bsM->tangentOut / 2.0;
                if (beM) beM->tangentIn = beM->tangentIn / 2.0;
                backBlock.addPoint(std::move(midPt));
                backSeg.passPointIds.push_back(midId);
                backSeg.type = cad::param::SegmentType::Bezier;
            }
        }
    }

    // Add moved auxiliary points to the back block.
    for (auto& aux : backAuxPoints) {
        aux.hostSegmentId = backSegId;
        backSeg.auxPointIds.push_back(aux.id);
        backBlock.addPoint(std::move(aux));
    }

    backBlock.addSegment(std::move(backSeg));
    m_newBlockId = backBlock.id;

    // --- Remove stale attachments ---
    for (const auto& att : m_removedAttachments)
        m_doc->removeAttachment(att.id);

    // --- Add the new block ---
    m_doc->addBlock(std::move(backBlock));

    // --- Create attachment: back block → front block at break point ---
    cad::param::Attachment att;
    att.fromBlockId = m_newBlockId;
    att.fromPointId = bpStartId;
    att.toBlockId = m_blockId;
    att.toPointId = m_auxPointId;
    att.toSegmentId = m_segmentId;  // front segment (inherited original ID)
    // For a curve break the back block was initialized with its X-axis along
    // the curve tangent (worldAngleRad) so the two halves join smoothly. The
    // Resolver re-rotates the follower by (refDirection + followerAngle - localDir);
    // refDirection is the front curve's exit tangent and localDir is the back
    // chord direction, so compensate with the chord-vs-tangent angle to keep
    // the back block at its tangent-aligned rotation (no kink at the break).
    att.followerAngle = isCurve ? backEndLocalAngle : 0.0;
    m_doc->addAttachment(std::move(att));

    // addBlock + addAttachment already trigger resolveAll().
}

void BreakSegmentCommand::undo()
{
    // Remove the back block (also removes its attachments).
    m_doc->removeBlock(m_newBlockId);

    // Restore the original block from snapshot.
    if (auto* block = m_doc->findBlock(m_blockId))
        *block = m_origBlockSnapshot;

    // Restore removed attachments.
    for (const auto& att : m_removedAttachments)
        m_doc->addAttachment(att);

    m_doc->resolveAll();
}

} // namespace cad::cmd
