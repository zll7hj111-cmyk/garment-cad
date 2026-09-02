#include "BreakCommands.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "parametric/ParamDocumentRaw.h"

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

/// BreakState 承载 redo 流水线各阶段间的中间状态（几何 → 位置 → 分配 →
/// 前段 → 后段 → 收尾）。函数间不传递裸指针：每阶段自行 re-acquire，
/// 避免 addPoint/erase 引起的指针失效（原实现靠 re-acquire 注释防御）。
struct BreakState {
    // --- 几何（阶段 1 输出） ---
    double segLenMm = 0.0;
    double localAngleDeg = 0.0;
    double worldAngleRad = 0.0;
    bool isCurve = false;
    cad::geo::Vec2 curveTanAtBreak;          // 断点处原始曲线切线（方向）
    std::vector<QUuid> frontPassIds;         // 归前段的 pass 点
    std::vector<cad::param::ParamPoint> backPassPoints;  // 归后段的 pass 点
    double backEndLocalAngle = 0.0;          // 后段终点在后段局部系的角度
    double curveFrontDist = 0.0;             // |start → 断点|（曲线真实距离）
    double curveBackDist = 0.0;              // |断点 → end|
    double curveBreakPolarAngleDeg = 0.0;    // start → 断点 的真实极角
    // 原曲线逐点冻结切线（Hobby 解 — 与画布曲线缓存同一解，逐点 in/out 分开）：
    // 手动点用存储切线；AUTO 点用 Hobby 解。打断后两半以 manual 模式复用这些
    // 切线，形状与打断前位一致（de Casteljau 子跨度再参数化的端点除外）。
    QHash<QUuid, cad::geo::Vec2> frozenTanIn;
    QHash<QUuid, cad::geo::Vec2> frozenTanOut;
    // 断点所在跨度的子跨度端点切线重写（t0 缩放，sub-parameterization）：
    //   前段最后跨度的起点 out 切线 = t0·T_out(P_j)，
    //   断点 in/out    切线 = t0·B'/(1−t0)·B'（子跨度参数化导数），
    //   后段第一个跨度终点 in 切线 = (1−t0)·T_in(P_{j+1})。
    // 为空 = 断点正好落在链点（CurveAnchor 打断）——冻结原切线即精确。
    bool hasSubSpans = false;
    QHash<QUuid, cad::geo::Vec2> subTanInOverride;
    QHash<QUuid, cad::geo::Vec2> subTanOutOverride;
    // 断点/辅助点的弧长位置（曲线辅助点按弧长分派前后段，而非 interpPercent）。
    double breakArc = 0.0;
    bool auxArcValid = false;
    QHash<QUuid, double> auxArc;
    // 断点附件参考方向变化量（打断前 interior 参考 → 打断后端点参考），
    // 用于对保留在断点上的跟随附件做跟随角补偿（零跳变）。
    double refDeltaRad = 0.0;

    // --- 位置求值（阶段 2 输出） ---
    BreakMode mode = BreakMode::Formula;
    QString origFormula;                     // 打断前的原长度公式（可能空）
    QString frontFormula;
    QString backFormula;
    double frontDistMm = 0.0;
    double backDistMm = 0.0;
    double breakAlong = 0.0;                 // 距起点的 mm 数
    QUuid polarRefId;                        // RefChain：Polar 锚点（空=冻结）
    double refOffsetMm = 0.0;
    QString refOffsetFormula;

    // --- 辅助点分配（阶段 3 输出） ---
    std::vector<QUuid> frontAuxIds;
    std::vector<cad::param::ParamPoint> backAuxPoints;

    // --- 后段构建（阶段 5 输出） ---
    cad::geo::Vec2 breakWorld;               // 断点世界坐标（后段原点）
    QUuid bpStartId, bpEndId, backSegId;
    double rotToLocal = 0.0;                 // 切线向量 原块系 → 后段系
    bool endPtKept = false;                  // 原端点仍被别的段引用（不删除）

    // --- 端点延长线 (EXTEND_LINE_DESIGN.md D8) ---
    // 原终点延长量归后段；modifyFrontBlock 先把原段 extendEnd 清零，故在
    // gatherBreakGeometry 阶段快照，buildBackBlock 继承。
    double  origExtendEndMm = 0.0;
    QString origExtendEndFormula;
};

// 阶段函数前置声明（后三个定义在 redo 之后，先声明供 redo 调用）。
void modifyFrontBlock(cad::param::ParamDocument& doc, const QUuid& blockId,
                      const QUuid& segId, const QUuid& auxPtId, BreakState& st,
                      const QUuid& origEndId, QUuid& publishedLinkedId,
                      QString& publishedRefName);
cad::param::Block buildBackBlock(cad::param::ParamDocument& doc,
                                 const QUuid& blockId, const QUuid& segId,
                                 const QUuid& auxPtId, BreakState& st,
                                 const QUuid& origEndId,
                                 const QString& publishedRefName);
void finalizeBreak(cad::param::ParamDocument& doc, BreakState& st,
                   const QUuid& frontBlockId, const QUuid& frontSegId,
                   const QUuid& frontAuxPtId, cad::param::Block backBlock,
                   const std::vector<cad::param::Attachment>& removedAttachments);

/// 阶段 1：解析打断几何——段长、方向角；曲线时构建 Bézier、冻结切线、
/// 按打断点分配 pass 点并计算后段端点角。返回 false = 几何不可用，redo 中止
/// （此时尚未修改模型，与旧实现的中途 return 语义一致）。
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
    // NOTE: the aux point itself may BE a pass point (CurveAnchor break) —
    // then it is part of the chain and its original in/out tangents are used.
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

    // Spans via the unified entry — same Hobby solve as the frame cache,
    // memoized (single code path; the freeze below keeps its own
    // solveHobbyTangents call because it needs per-point in/out vectors).
    auto spans = block->spansForSegment(*seg, /*skipUnresolvedPassPoints=*/true);
    if (spans.empty()) return true;

    // Freeze the tangents of the ORIGINAL curve. The running curve is built
    // with the HOBBY solve (Block::rebuildCurveCache uses AutoCurveMode::Hobby),
    // so the freeze must use the SAME solver — a C2 freeze would rebuild both
    // halves with a different shape (曲线打断不能维持形状, 用户报告 2026-10).
    // Manual points keep their stored tangents; auto points get the Hobby
    // solution (in/out are stored SEPARATELY — they differ in magnitude for
    // interior points; freezing a single value would create a visible kink).
    std::vector<cad::geo::Vec2> hobbyIn(cIds.size()), hobbyOut(cIds.size());
    std::tie(hobbyIn, hobbyOut) = cad::geo::solveHobbyTangents(
        cPts, cAuto, cTanIn, cTanOut, seg->tension);
    for (size_t k = 0; k < cIds.size(); ++k) {
        st.frozenTanIn[cIds[k]] = cAuto[k] ? hobbyIn[k] : cTanIn[k];
        st.frozenTanOut[cIds[k]] = cAuto[k] ? hobbyOut[k] : cTanOut[k];
    }

    // Project the break point onto the curve to get global parameter T.
    auto proj = cad::geo::projectPointOnCurve(auxPt->resolvedPos, spans);
    if (proj.valid) {
        st.curveTanAtBreak = cad::geo::evalCurveDerivative(spans, proj.t);
        // Use curve tangent for back block rotation.
        if (st.curveTanAtBreak.lengthSquared() > 1e-12)
            st.worldAngleRad = block->transform.rotation
                             + std::atan2(st.curveTanAtBreak.y, st.curveTanAtBreak.x);
        // 打断点参考方向（打前）：Interpolated 点 = 宿主弦向；打后 = 端点
        // 切线方向（曲线出口）。差值用于附件角度补偿（零跳变保持）。
        const double refOldRad =
            block->transform.rotation + block->exitDirectionAtPoint(auxPtId, segId);
        st.refDeltaRad = st.worldAngleRad - refOldRad;
        st.breakArc = proj.s;
        st.auxArcValid = true;

        // Arc positions of every aux point (used to split them between the
        // front and back halves — comparing chord-relative fractions of
        // different parameterizations is wrong on curves).
        for (const QUuid& aid : seg->auxPointIds) {
            if (aid == auxPtId) continue;
            const auto* ap = block->findPoint(aid);
            if (!ap || !ap->resolved) continue;
            auto apProj = cad::geo::projectPointOnCurve(ap->resolvedPos, spans);
            if (apProj.valid)
                st.auxArc.insert(aid, apProj.s);
        }

        // Subdivide the span containing the break point (de Casteljau) and
        // derive the EXACT sub-parameterized tangent handles. The split halves
        // are reparameterized to [0,1], so their end tangents are scaled by
        // t0 / (1−t0) — freezing the full-value original tangents would make
        // the spans adjacent to the break point bulge away from the original
        // curve (曲面断裂的另一半成因).
        const int spanIdx = std::clamp(
            static_cast<int>(std::floor(proj.t)), 0, static_cast<int>(spans.size()) - 1);
        const double t0 = std::clamp(proj.t - static_cast<double>(spanIdx), 0.0, 1.0);
        if (t0 > 1e-6 && t0 < 1.0 - 1e-6 && spanIdx + 1 < static_cast<int>(cIds.size())) {
            const auto [left, right] = cad::geo::subdivideBezier(spans[static_cast<size_t>(spanIdx)], t0);
            st.hasSubSpans = true;
            // Break point B: in-tangent (front side) = 3(B − L.ctrl2),
            // out-tangent (back side) = 3(R.ctrl1 − B).
            const cad::geo::Vec2 tInB = (left.p3 - left.ctrl2) * 3.0;
            const cad::geo::Vec2 tOutB = (right.ctrl1 - right.p0) * 3.0;
            st.subTanInOverride.insert(auxPtId, tInB);
            st.subTanOutOverride.insert(auxPtId, tOutB);
            const QUuid subStartId = cIds[static_cast<size_t>(spanIdx)];
            st.subTanOutOverride.insert(subStartId, (left.ctrl1 - left.p0) * 3.0);
            const QUuid subEndId = cIds[static_cast<size_t>(spanIdx) + 1];
            st.subTanInOverride.insert(subEndId, (right.p3 - right.ctrl2) * 3.0);
        }
    }

    // Distribute pass points by ARC-LENGTH position (the break point's and the
    // pass points' interpPercent live in DIFFERENT frames — pass points are
    // chord-parameterized CurveAnchors, the break point is an arc fraction —
    // comparing them directly misclassifies points before/after the break).
    // The break point itself is EXCLUDED (it becomes the shared endpoint of
    // both halves; when it is a pass point, its id is skipped below).
    const cad::geo::Vec2 backChord = endPt->resolvedPos - auxPt->resolvedPos;
    const double backChordLen = backChord.length();

    for (const auto& ppId : seg->passPointIds) {
        if (ppId == auxPtId) continue;  // the break point itself
        const auto* pp = block->findPoint(ppId);
        if (!pp || !pp->resolved) continue;
        double ppArc = -1.0;
        if (proj.valid) {
            auto ppProj = cad::geo::projectPointOnCurve(pp->resolvedPos, spans);
            if (ppProj.valid) ppArc = ppProj.s;
        }
        const bool before = proj.valid ? (ppArc <= st.breakArc)
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

/// 阶段 2：按模式求值打断位置（Formula/Freeze/RefChain 的公式与数值距离）。
/// 曲线打断随后覆盖为实际弧长距离（冻结，无公式）。
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
    // Compile into the document's own cache (P1-5) rather than the
    // context-free fallback, so the bytecode lands where the resolve passes
    // will reuse it (and is released when the document is cleared).
    ctx.cache = &doc.expressionCache();
    const auto& params = doc.parameters();
    const auto& conditioned = doc.conditions();

    if (st.mode == BreakMode::Freeze) {
        // Intersection-driven break: freeze to absolute numeric distances.
        // Project the break point's resolved position onto the segment direction.
        const cad::geo::Vec2 unit = localDir / st.segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        st.breakAlong = rel.dot(unit);
        st.breakAlong = std::clamp(st.breakAlong, 0.0, st.segLenMm);

        st.frontDistMm = st.breakAlong;
        st.backDistMm = st.segLenMm - st.breakAlong;
        // The break position is not a linear function of the segment length, so
        // the front half freezes to its numeric length. The back half keeps the
        // original formula's variable drive: back = orig − front snapshot
        // (用户要求: 原公式减去前半段 = 后半段公式, 不再退化为纯数值).
        if (!seg->lengthFormula.isEmpty()) {
            st.backFormula = QStringLiteral("(%1)-%2")
                .arg(seg->lengthFormula)
                .arg(QString::number(cad::geo::Units::mmToCm(st.breakAlong), 'g', 15));
        }
    } else if (st.mode == BreakMode::RefChain) {
        // Project the true resolved position for aux-point redistribution.
        const cad::geo::Vec2 unit = localDir / st.segLenMm;
        const cad::geo::Vec2 rel = auxPt->resolvedPos - startPt->resolvedPos;
        st.breakAlong = std::clamp(rel.dot(unit), 0.0, st.segLenMm);
        st.frontDistMm = st.breakAlong;
        st.backDistMm = st.segLenMm - st.breakAlong;

        // The parametric anchor survives only if the ref point stays on the
        // front half; otherwise the Polar reference would dangle (the ref is
        // redistributed to the back block) — fall back to a numeric freeze.
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
        // Front length is endpoint-driven (start → Polar end); no formula.
        // Back: keep the original formula's variable drive — back = orig −
        // front snapshot (用户要求: 原公式减去前半段 = 后半段公式).
        if (!seg->lengthFormula.isEmpty()) {
            st.backFormula = QStringLiteral("(%1)-%2")
                .arg(seg->lengthFormula)
                .arg(QString::number(cad::geo::Units::mmToCm(st.breakAlong), 'g', 15));
        }
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
        cad::param::ConditionEngine::evaluateLengthMm(auxPt->interpConstantFormula, params, conditioned, constantMm, &ctx);

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
            origF = QString::number(cad::geo::Units::mmToCm(st.segLenMm), 'g', 15);
        else
            origF = seg->lengthFormula;

        // Front formula: (orig)*effPercent [+constant]
        st.frontFormula = QStringLiteral("(%1)*%2").arg(origF, effPercentText);
        if (hasConstant)
            st.frontFormula += QStringLiteral("+%1").arg(constantText);

        // Back formula: (orig)*(1-effPercent) [-constant]
        st.backFormula = QStringLiteral("(%1)*(1-%2)").arg(origF, effPercentText);
        if (hasConstant)
            st.backFormula += QStringLiteral("-%1").arg(constantText);

        // Numeric distances (mm) for initial Polar values.
        st.frontDistMm = st.segLenMm * effPercent + constantMm;
        st.backDistMm = st.segLenMm - st.frontDistMm;
        st.breakAlong = st.frontDistMm;
    }

    // Curve break: the break point is offset from the chord, so freeze to its
    // ACTUAL distance/angle (the chord projection above would snap it onto the
    // chord and destroy the shape). Lengths are frozen (no formula) because the
    // break point's position on the curve is fixed.
    if (st.isCurve) {
        st.frontDistMm = st.curveFrontDist;
        st.backDistMm = st.curveBackDist;
        st.frontFormula.clear();
        st.backFormula.clear();
    }

    // World position of the break point.
    st.breakWorld = block->transform.toWorld(auxPt->resolvedPos);
}

/// 阶段 3：按打断位置把辅助点分派到前后段（前段保留原 ID，后段按后段弦长
/// 重算 percent/常量）。
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

        // Curve: split by ARC-LENGTH position (the only frame the two kinds of
        // points share — chord fractions of CurveAnchors and arc fractions of
        // Interpolated points are not comparable).
        double otherAlong = 0.0;
        bool haveAlong = false;
        if (st.isCurve && st.auxArcValid) {
            const auto it = st.auxArc.constFind(auxId);
            if (it != st.auxArc.constEnd()) {
                otherAlong = it.value();
                haveAlong = true;
            } else if (otherAux->resolved && startPt->resolved && endPt->resolved) {
                // Projection failed for this point (rare) — chord fallback.
                const cad::geo::Vec2 rel = otherAux->resolvedPos - startPt->resolvedPos;
                const cad::geo::Vec2 unit = localDir / st.segLenMm;
                otherAlong = rel.x * unit.x + rel.y * unit.y;
                haveAlong = true;
            }
            if (haveAlong && otherAlong <= st.breakArc) {
                st.frontAuxIds.push_back(auxId);
            } else {
                // Move to back block: recalculate percent relative to back
                // segment (percentage-of-arc is meaningless here — the moved
                // aux point becomes an interpolated point on the BACK curve;
                // percent = relative arc share of the back half).
                cad::param::ParamPoint moved = *otherAux;
                const double relAlong = haveAlong ? (otherAlong - st.breakArc) : 0.0;
                const double backLenMm = st.curveBackDist;
                moved.interpPercent = (backLenMm > 1e-9)
                    ? std::clamp(relAlong / backLenMm, -100.0, 100.0) : 0.0;
                moved.interpPercentFormula.clear();  // numeric
                moved.interpConstant = 0.0;
                moved.interpConstantFormula.clear();
                moved.interpFromEnd = false;
                // hostSegmentId will be set to the new segment's ID below.
                st.backAuxPoints.push_back(std::move(moved));
            }
            continue;
        }

        // Compute this aux point's distance from the original start.
        if (otherAux->resolved && startPt->resolved && endPt->resolved) {
            // Project resolved position onto the segment direction.
            const cad::geo::Vec2 rel = otherAux->resolvedPos - startPt->resolvedPos;
            const cad::geo::Vec2 unit = localDir / st.segLenMm;
            otherAlong = rel.x * unit.x + rel.y * unit.y;
        }

        if (otherAlong <= st.breakAlong) {
            st.frontAuxIds.push_back(auxId);
        } else {
            // Move to back block: recalculate percent relative to back segment.
            cad::param::ParamPoint moved = *otherAux;
            const double relAlong = otherAlong - st.breakAlong;
            moved.interpPercent = (st.backDistMm > 1e-9)
                ? relAlong / st.backDistMm : 0.0;
            moved.interpPercentFormula.clear();  // numeric for V1
            moved.interpConstant = 0.0;
            moved.interpConstantFormula.clear();
            moved.interpFromEnd = false;
            // hostSegmentId will be set to the new segment's ID below.
            st.backAuxPoints.push_back(std::move(moved));
        }
    }
}

void BreakSegmentCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    auto* auxPt = block->findPoint(m_auxPointId);
    if (!seg || !auxPt) return;
    if (!canBreak(*block, *seg, *auxPt)) return;

    // Fresh state for every redo pass (undo removes the auto-published var).
    m_publishedLinkedId = QUuid();
    m_publishedRefName.clear();

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

    // --- 六阶段流水线（每阶段一个命名函数，中间状态经 BreakState 传递） ---
    // 1) 几何解析：段长/方向；曲线切线冻结与 pass 点分配
    // 2) 位置求值：按 Formula/Freeze/RefChain 模式生成前后段公式与距离
    // 3) 辅助点分派：按打断位置决定去前段还是后段
    // 4) 前段改造：断点转 Polar 端点、段属性、长度发布、曲线 pass 点/切线
    // 5) 后段构建：断点→新起点、Polar 终点、曲线中点插入保持形状
    // 6) 收尾：移除旧附件、入块、建连接（addBlock/addAttachment 触发求解）
    BreakState st;
    if (!gatherBreakGeometry(*m_doc, m_blockId, m_segmentId, m_auxPointId, st))
        return;
    evaluateBreakPosition(*m_doc, m_blockId, m_segmentId, m_auxPointId, st);
    redistributeAuxPoints(*m_doc, m_blockId, m_segmentId, m_auxPointId, st);
    modifyFrontBlock(*m_doc, m_blockId, m_segmentId, m_auxPointId, st, origEndId,
                     m_publishedLinkedId, m_publishedRefName);
    cad::param::Block backBlock = buildBackBlock(
        *m_doc, m_blockId, m_segmentId, m_auxPointId, st, origEndId,
        m_publishedRefName);
    m_newBlockId = backBlock.id;
    finalizeBreak(*m_doc, st, m_blockId, m_segmentId, m_auxPointId,
                  std::move(backBlock), m_removedAttachments);
}

/// 阶段 4：把原块改为前段——断点转 Polar 端点（RefChain 保留参数锚链）、
/// 段属性更新、RefChain/Freeze 自动发布前段长度（back = orig − M_front）、
/// 曲线 pass 点重参数化与切线冻结、无 pass 点时插入 Bézier 中点、删除
/// 原端点（无其他段引用时）。
void modifyFrontBlock(cad::param::ParamDocument& doc, const QUuid& blockId,
                      const QUuid& segId, const QUuid& auxPtId, BreakState& st,
                      const QUuid& origEndId, QUuid& publishedLinkedId,
                      QString& publishedRefName)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    auto* auxPt = block ? block->findPoint(auxPtId) : nullptr;
    if (!block || !seg || !auxPt) return;

    // Convert the break point to a Polar endpoint. In RefChain mode the Polar
    // anchor is the point's measurement ref point (kept on the front half with
    // its own constraint — intersections and further refs stay fully dynamic;
    // the Resolver's fixpoint resolves the resulting dependency cycle).
    auxPt = block->findPoint(auxPtId);  // re-acquire (pointer may have shifted)
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

    if (st.mode == BreakMode::RefChain && !st.polarRefId.isNull()) {
        // Keep the parametric anchor: Polar from the original ref point, offset
        // driven by the same formula (cm domain), direction along the segment.
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

    // Curve: set tangent at the break point (front endpoint's outgoing tangent
    // MUST be the exact sub-parameterized in-tangent of the split span — see
    // gatherBreakGeometry — so the front half reproduces the original curve
    // right up to the break).
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

    // Update the original segment: endpoint becomes the break point.
    seg = block->findSegment(segId);  // re-acquire
    seg->endPointId = auxPtId;
    seg->lengthFormula = st.frontFormula;
    seg->auxPointIds = st.frontAuxIds;
    // 端点延长线 (EXTEND_LINE_DESIGN.md D8): 延长量随端点归属 —— 原终点延长量
    // 归后段（buildBackBlock 继承）；前段新断点（切点）延长量恒 0；原起点
    // 延长量保留在前段（startPointId 未变）。
    seg->extendEndMm = 0.0;
    seg->extendEndFormula.clear();

    // RefChain/Freeze: publish the front length as a linked measurement so the
    // back half can compensate exactly — back = orig − M_front (用户拍板:
    // 前后段都必须有精确约束). Reuse an existing publication of this segment
    // (用户已发布过原线长度); otherwise create one (打断自动发布前段长度).
    if (st.mode == BreakMode::RefChain || st.mode == BreakMode::Freeze) {
        if (auto* lv = doc.findLinkedBySource(blockId, segId)) {
            publishedRefName = lv->refName;   // 复用已有发布
        } else {
            cad::param::LinkedVariable fresh =
                cad::param::LinkedVariable::fromSegment(*block, *seg);
            publishedRefName = fresh.refName;
            publishedLinkedId = fresh.id;
            doc.addLinked(std::move(fresh));
        }
    }

    // Curve: update front passPointIds and re-parameterize them.
    if (st.isCurve) {
        seg->passPointIds = st.frontPassIds;
        // Re-parameterize front pass points relative to the new front chord.
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
        // Freeze tangents on all front points (start + pass points + break point)
        // so the front half keeps the exact pre-break shape. Sub-span endpoints
        // (the span the break point splits) use the t0-scaled overrides.
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

        // If the front half has NO pass points left (e.g. breaking at the first
        // curve point) it would render as a straight line. Insert the exact
        // Bézier midpoint of the single remaining span as a pass point so the
        // curved shape is preserved.
        //
        // IMPORTANT: splitting one cubic span into two half-spans reparameterizes
        // each half to half the original parameter range, so every tangent
        // (which encodes the derivative) must be HALVED for the two sub-spans
        // to reproduce the original span exactly (de Casteljau).
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
                // Halve the endpoint tangents (they were frozen to full values).
                auto* spM = block->findPoint(seg->startPointId);
                auto* neM = block->findPoint(auxPtId);
                if (spM) spM->tangentOut = spM->tangentOut / 2.0;
                if (neM) neM->tangentIn = neM->tangentIn / 2.0;
                block->addPoint(std::move(midPt));
                seg = block->findSegment(segId);  // re-acquire after addPoint
                seg->passPointIds.push_back(midId);
            }
        }
        if (seg->passPointIds.empty())
            seg->type = cad::param::SegmentType::Line;

        // Remove back pass points from the original block.
        for (const auto& bp : st.backPassPoints) {
            auto& pts = block->points;
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&bp](const cad::param::ParamPoint& p) { return p.id == bp.id; }),
                pts.end());
        }
        block->rebuildPointIndex();

        // 曲线结构变了（链点重参数化/切线冻结/端点更换）但点位置可能没动 —
        // Block::resolve 的增量几何检测不会 bump epoch，惰性曲线缓存就会继续
        // 用打断前的 span 缓存（前段画出旧曲线 = 打断形状不保持的根因）。
        // 铁律见 AGENTS.md「画布缓存刷新」。
        block->touchGeometry();
    }

    // Remove the original end point if no other segment references it.
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

/// 阶段 5：构建后段块——断点作为新起点（局部原点），Polar 终点（距离公式
/// 为“原总长 − 前段发布”或阶段 2 求值结果），曲线切线旋转进后段局部系、
/// pass 点切线冻结，无 pass 点时插入 Bézier 中点保持形状（de Casteljau
/// 半参数化，切线减半）。
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

    backBlock.layer = block->layer;  // 打断继承原层（与复制一致：打断/复制继承原层）
    backBlock.transform.origin = st.breakWorld;
    backBlock.transform.rotation = st.worldAngleRad;

    // Rotation to convert tangent vectors from the ORIGINAL block's local
    // frame into the back block's local frame (they differ by the block
    // rotation delta). Tangents are direction+magnitude, so only rotate.
    st.rotToLocal = block->transform.rotation - st.worldAngleRad;

    // Start point at local origin (coincides with break point).
    cad::param::ParamPoint bpStart;
    bpStart.constraint = cad::param::PointConstraint::Free;
    bpStart.freePos = cad::geo::Vec2::zero();
    bpStart.serial = doc.newPointSerial();
    st.bpStartId = bpStart.id;
    // Curve: inherit tangent at break point (rotate into back-local frame).
    // The OUT tangent is the sub-parameterized right-half handle at B
    // (= 3(R.ctrl1 − B) when the span is split; the original Hobby out-tangent
    // when the break point is itself a chain point) — either way it makes the
    // back half's first span reproduce the original curve exactly.
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

    // End point: Polar from origin. For curves the angle may differ from 0
    // because the block rotation follows the curve tangent, not the chord.
    // Back formula: with a published front measurement the back = total − M
    // (exact conservation, front moves dynamically); otherwise fall back to
    // the numeric front snapshot computed in the mode branch above. Applied
    // BEFORE bpEnd is built — the back length is driven by the endpoint's
    // Polar distanceFormula, so both must carry the same expression.
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
    // Curve: freeze the original end point's tangent (rotated to back-local).
    // The END point uses its IN tangent (the handle before it); the OUT tangent
    // is unused but stored for consistency.
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

    // Segment inheriting visual properties from the original.
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
    // 端点延长线 (EXTEND_LINE_DESIGN.md D8): 后段继承原终点延长量; 新断点
    // （后段起点 = 切点）延长量恒 0。
    backSeg.extendStartMm = 0.0;
    backSeg.extendStartFormula.clear();
    backSeg.extendEndMm = st.origExtendEndMm;
    backSeg.extendEndFormula = st.origExtendEndFormula;
    st.backSegId = backSeg.id;

    // Curve: add pass points to the back segment, freezing each one's tangent
    // (rotated into back-local) so the back half keeps the pre-break shape.
    if (st.isCurve && !st.backPassPoints.empty()) {
        backSeg.type = cad::param::SegmentType::Bezier;
        for (auto& pp : st.backPassPoints) {
            pp.hostSegmentId = st.backSegId;
            const cad::geo::Vec2 tI = backLocalTan(pp.id, true);
            const cad::geo::Vec2 tO = backLocalTan(pp.id, false);
            if (tI.lengthSquared() > 1e-12 || tO.lengthSquared() > 1e-12) {
                pp.autoTangent = false;
                pp.tangentIn = tI.lengthSquared() > 1e-12 ? tI : tO;
                pp.tangentOut = tO.lengthSquared() > 1e-12 ? tO : tI;
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
    if (st.isCurve && st.backPassPoints.empty()) {
        backBlock.resolve(doc.parameters());  // resolve bpStart/bpEnd positions
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
            if (mLen > 1e-9) {
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
                // Halve the endpoint tangents (they were frozen to full values).
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

    // Add moved auxiliary points to the back block.
    for (auto& aux : st.backAuxPoints) {
        aux.hostSegmentId = st.backSegId;
        backSeg.auxPointIds.push_back(aux.id);
        backBlock.addPoint(std::move(aux));
    }

    backBlock.addSegment(std::move(backSeg));
    return backBlock;
}

/// 阶段 6：收尾——移除以断点/原端点为目标的附件（快照后统一重建）、加入
/// 后段块、重新建立幸存连接、建立后段→前段连接（followerAngle 补偿曲线
/// 切线偏角，保证断点无折角）。addBlock/addAttachment 内部已触发 resolveAll。
///
/// 连接保留规则（用户报告 2026-10: 断点上的端点/组件连接被打断会丢失/跳变）：
///   · 目标 = 断点：断点仍是前段端点（同 id 同段）→ 附件保留；打断后参考
///     方向由 interior 弦向变为端点切线，对跟随角做补偿，世界朝向零跳变。
///   · 目标 = 原端点：原端点被删时（无其他段引用）→ 重指到后段终点
///     （原线尾部），跟随线仍然跟随；原端点幸存时原样保留。
void finalizeBreak(cad::param::ParamDocument& doc, BreakState& st,
                   const QUuid& frontBlockId, const QUuid& frontSegId,
                   const QUuid& frontAuxPtId, cad::param::Block backBlock,
                   const std::vector<cad::param::Attachment>& removedAttachments)
{
    // --- Remove stale attachments (snapshot already taken by the caller) ---
    QList<QUuid> staleIds;
    staleIds.reserve(static_cast<qsizetype>(removedAttachments.size()));
    for (const auto& att : removedAttachments)
        staleIds.push_back(att.id);
    doc.removeAttachments(staleIds);

    // --- Add the new block ---
    const QUuid newBlockId = backBlock.id;
    const QUuid bpStartId = st.bpStartId;
    doc.addBlock(std::move(backBlock));

    // --- Re-establish the connections that survived the break ---
    std::vector<cad::param::Attachment> kept;
    kept.reserve(removedAttachments.size());
    for (cad::param::Attachment att : removedAttachments) {
        if (att.toPointId == frontAuxPtId) {
            // 断点保留：补偿跟随角，使跟随线世界朝向与打断前完全一致
            // （参考方向从 interior 弦向变为端点切线/段向）。
            if (std::abs(st.refDeltaRad) > 1e-9) {
                if (att.rotationMode == cad::param::RotationMode::ArcLength) {
                    if (att.arcLengthFormula.isEmpty()) {
                        if (const auto* fb = doc.findBlock(att.fromBlockId)) {
                            const double radius =
                                fb->segmentLengthAtPoint(att.fromPointId);
                            if (radius > 1e-9)
                                att.arcLength += st.refDeltaRad * radius;
                        }
                    }
                } else if (att.followerAngleFormula.isEmpty()) {
                    double ang = att.followerAngle + st.refDeltaRad * 180.0 / M_PI;
                    ang = cad::geo::normalizeDeg360(ang);
                    att.followerAngle = ang;
                }
            }
            kept.push_back(std::move(att));
        } else if (st.endPtKept) {
            kept.push_back(std::move(att));   // 原端点幸存：连接原样保留
        } else {
            // 原端点被删：重指到后段终点（原线的尾部），跟随关系不丢失。
            att.toBlockId = newBlockId;
            att.toPointId = st.bpEndId;
            att.toSegmentId = st.backSegId;
            kept.push_back(std::move(att));
        }
    }
    cad::param::RawModelAccess::addAttachmentsRaw(doc, kept);

    // --- Create attachment: back block → front block at break point ---
    cad::param::Attachment att;
    att.fromBlockId = newBlockId;
    att.fromPointId = bpStartId;
    att.toBlockId = frontBlockId;
    att.toPointId = frontAuxPtId;
    att.toSegmentId = frontSegId;  // front segment (inherited original ID)
    // For a curve break the back block was initialized with its X-axis along
    // the curve tangent (worldAngleRad) so the two halves join smoothly. The
    // Resolver re-rotates the follower by (refDirection + π − angle − localDir);
    // refDirection is the front curve's exit tangent and localDir is the back
    // chord direction, so compensate with the chord-vs-tangent angle to keep
    // the back block at its tangent-aligned rotation (no kink at the break).
    // 闭合基准（用户拍板 2026-08）：0° = 折叠重叠、180° = 直行延续，故
    // 直线分支存 180°，曲线分支把旧基准局部角翻转为 180° − 局部角。
    att.followerAngle = st.isCurve ? (180.0 - st.backEndLocalAngle) : 180.0;
    doc.addAttachment(std::move(att));
}

void BreakSegmentCommand::undo()
{
    // Remove the back block (also removes its attachments — including the
    // re-pointed copies that now target the back block's end point).
    m_doc->removeBlock(m_newBlockId);

    // Restore the original block from snapshot.
    if (auto* block = m_doc->findBlock(m_blockId))
        *block = m_origBlockSnapshot;

    // Restore attachments verbatim: drop the live (compensated / re-pointed)
    // copies first, then re-add the pre-break originals (keeps isLocked).
    QList<QUuid> liveIds;
    for (const auto& att : m_doc->attachments()) {
        const bool snapped = std::any_of(
            m_removedAttachments.begin(), m_removedAttachments.end(),
            [&att](const cad::param::Attachment& o) { return o.id == att.id; });
        if (snapped) liveIds.push_back(att.id);
    }
    m_doc->removeAttachments(liveIds);
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_removedAttachments);

    // Remove the auto-published front-length variable (a variable the user
    // published themselves stays untouched).
    if (!m_publishedLinkedId.isNull())
        m_doc->removeLinked(m_publishedLinkedId);

    m_doc->resolveAll();
}

} // namespace cad::cmd
