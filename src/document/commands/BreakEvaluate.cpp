#include "document/commands/BreakAnalysis.h"   // 声明这两个函数

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "geometry/CurveSplitter.h"

namespace cad::cmd {

void evaluateBreakPosition(cad::param::ParamDocument& doc, const QUuid& blockId,
                           const QUuid& segId, const QUuid& auxPtId, BreakState& st)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    auto* auxPt = block ? block->findPoint(auxPtId) : nullptr;
    if (!block || !seg || !auxPt) return;

    st.mode = determineBreakMode(*block, *seg, *auxPt);
    st.origFormula = seg->lengthFormula;  // 打断前的原长度公式（可能空）

    const auto* startPt = block->findPoint(seg->startPointId);
    const auto* endPt = block->findPoint(seg->endPointId);
    if (!startPt || !endPt || !startPt->resolved || !endPt->resolved) return;
    const cad::geo::Vec2 localDir = endPt->resolvedPos - startPt->resolvedPos;

    // Formula re-evaluation uses the same environment as the Resolver.
    cad::param::EvalContext ctx;
    ctx.cache = &doc.expressionCache();
    const auto& params = doc.parameters();
    const auto& conditioned = doc.conditions();

    if (st.mode == BreakMode::Freeze) {
        // Intersection-driven break: freeze to absolute numeric distances.
        const cad::geo::Vec2 unit = localDir / st.segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        st.breakAlong = rel.dot(unit);
        st.breakAlong = std::clamp(st.breakAlong, 0.0, st.segLenMm);

        st.frontDistMm = st.breakAlong;
        st.backDistMm = st.segLenMm - st.breakAlong;
        if (!seg->lengthFormula.isEmpty()) {
            st.backFormula = QStringLiteral("(%1)-%2")
                .arg(seg->lengthFormula)
                .arg(QString::number(cad::geo::Units::mmToCm(st.breakAlong), 'g', 15));
        }
    } else if (st.mode == BreakMode::RefChain) {
        const cad::geo::Vec2 unit = localDir / st.segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        st.breakAlong = std::clamp(rel.dot(unit), 0.0, st.segLenMm);
        st.frontDistMm = st.breakAlong;
        st.backDistMm = st.segLenMm - st.breakAlong;

        const auto* refPt = block->findPoint(auxPt->interpRefPointId);
        const cad::geo::Vec2 refRel = refPt->resolvedPos - startPt->resolvedPos;
        double offsetMm = auxPt->interpConstant;
        cad::param::ConditionEngine::evaluateLengthMm(auxPt->interpConstantFormula, params, conditioned, offsetMm, &ctx);
        if (offsetMm < -1e-9 || refRel.dot(unit) > st.breakAlong + 1e-6) {
            st.polarRefId = QUuid();  // marker: no anchor, numeric freeze below
        } else {
            st.polarRefId = refPt->id;
            st.refOffsetMm = offsetMm;
            st.refOffsetFormula = auxPt->interpConstantFormula;  // cm domain
        }
        if (!seg->lengthFormula.isEmpty()) {
            st.backFormula = QStringLiteral("(%1)-%2")
                .arg(seg->lengthFormula)
                .arg(QString::number(cad::geo::Units::mmToCm(st.breakAlong), 'g', 15));
        }
    } else {
        // Standard Interpolated break: parametric formula splitting.
        double percent = auxPt->interpPercent;
        if (!auxPt->interpPercentFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                auxPt->interpPercentFormula, params, conditioned, &ctx);
            if (r.ok) percent = r.value;
        }
        double constantMm = auxPt->interpConstant;  // mm internal
        cad::param::ConditionEngine::evaluateLengthMm(auxPt->interpConstantFormula, params, conditioned, constantMm, &ctx);

        double effPercent = auxPt->interpFromEnd ? (1.0 - percent) : percent;

        QString percentText = auxPt->interpPercentFormula.isEmpty()
            ? QString::number(percent, 'g', 15)
            : auxPt->interpPercentFormula;

        QString effPercentText = auxPt->interpFromEnd
            ? QStringLiteral("(1-%1)").arg(percentText)
            : percentText;

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

        QString origF = seg->lengthFormula.isEmpty()
            ? QString::number(cad::geo::Units::mmToCm(st.segLenMm), 'g', 15)
            : seg->lengthFormula;

        st.frontFormula = QStringLiteral("(%1)*%2").arg(origF, effPercentText);
        if (hasConstant)
            st.frontFormula += QStringLiteral("+%1").arg(constantText);

        st.backFormula = QStringLiteral("(%1)*(1-%2)").arg(origF, effPercentText);
        if (hasConstant)
            st.backFormula += QStringLiteral("-%1").arg(constantText);

        st.frontDistMm = st.segLenMm * effPercent + constantMm;
        st.backDistMm = st.segLenMm - st.frontDistMm;
        st.breakAlong = st.frontDistMm;
    }

    if (st.isCurve) {
        st.frontDistMm = st.curveFrontDist;
        st.backDistMm = st.curveBackDist;
        st.frontFormula.clear();
        st.backFormula.clear();
    }

    st.breakWorld = block->transform.toWorld(auxPt->resolvedPos);
}

void redistributeAuxPoints(cad::param::ParamDocument& doc, const QUuid& blockId,
                           const QUuid& segId, const QUuid& auxPtId, BreakState& st)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    if (!block || !seg) return;

    const auto* startPt = block->findPoint(seg->startPointId);
    const auto* endPt = block->findPoint(seg->endPointId);
    if (!startPt || !endPt || !startPt->resolved || !endPt->resolved) return;
    const cad::geo::Vec2 localDir = endPt->resolvedPos - startPt->resolvedPos;

    for (const QUuid& auxId : seg->auxPointIds) {
        if (auxId == auxPtId) continue;  // the break point itself
        const auto* otherAux = block->findPoint(auxId);
        if (!otherAux) continue;

        double otherAlong = 0.0;
        bool haveAlong = false;
        if (st.isCurve && st.auxArcValid) {
            const auto it = st.auxArc.constFind(auxId);
            if (it != st.auxArc.constEnd()) {
                otherAlong = it.value();
                haveAlong = true;
            } else if (otherAux->resolved && startPt->resolved && endPt->resolved) {
                const cad::geo::Vec2 rel = otherAux->resolvedPos - startPt->resolvedPos;
                const cad::geo::Vec2 unit = localDir / st.segLenMm;
                otherAlong = rel.x * unit.x + rel.y * unit.y;
                haveAlong = true;
            }
            if (haveAlong && otherAlong <= st.breakArc) {
                st.frontAuxIds.push_back(auxId);
            } else {
                cad::param::ParamPoint moved = *otherAux;
                const double relAlong = haveAlong ? (otherAlong - st.breakArc) : 0.0;
                const double backLenMm = st.curveBackDist;
                moved.interpPercent = (backLenMm > 1e-9)
                    ? std::clamp(relAlong / backLenMm, -100.0, 100.0) : 0.0;
                moved.interpPercentFormula.clear();  // numeric
                moved.interpConstant = 0.0;
                moved.interpConstantFormula.clear();
                moved.interpFromEnd = false;
                st.backAuxPoints.push_back(std::move(moved));
            }
            continue;
        }

        if (otherAux->resolved && startPt->resolved && endPt->resolved) {
            const cad::geo::Vec2 rel = otherAux->resolvedPos - startPt->resolvedPos;
            const cad::geo::Vec2 unit = localDir / st.segLenMm;
            otherAlong = rel.x * unit.x + rel.y * unit.y;
        }

        if (otherAlong <= st.breakAlong) {
            st.frontAuxIds.push_back(auxId);
        } else {
            cad::param::ParamPoint moved = *otherAux;
            const double relAlong = otherAlong - st.breakAlong;
            moved.interpPercent = (st.backDistMm > 1e-9)
                ? relAlong / st.backDistMm : 0.0;
            moved.interpPercentFormula.clear();  // numeric for V1
            moved.interpConstant = 0.0;
            moved.interpConstantFormula.clear();
            moved.interpFromEnd = false;
            st.backAuxPoints.push_back(std::move(moved));
        }
    }
}

} // namespace cad::cmd
