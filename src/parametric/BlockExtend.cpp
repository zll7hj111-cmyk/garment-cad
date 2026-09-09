#include "Block.h"

#include <algorithm>
#include <cmath>

#include "parametric/ConditionEngine.h"
#include "geometry/Angle.h"
#include "geometry/Epsilon.h"

namespace cad::param {
geo::Vec2 Block::effectiveLocalPos(const QUuid& pointId) const
{
    // 按当前位置即时计算（求解 pass 内也不依赖缓存）：有效位置 = 本体 +
    // 该端点所属段的延长量 × 本体出方向。无延长/非端点 = 本体位。
    const ParamPoint* pt = findPoint(pointId);
    if (!pt) return geo::Vec2::zero();
    // 无延长（绝大多数文档的常态）：本体位即有效位，跳过逐段扫描。
    // 与旧行为等价：m_extendEval 为空时下面的循环也必然找不到匹配段。
    if (m_extendEval.isEmpty())
        return pt->resolvedPos;
    for (const auto& seg : segments) {
        auto it = m_extendEval.constFind(seg.id);
        if (it == m_extendEval.constEnd()) continue;
        const bool isStart = (seg.startPointId == pointId);
        const bool isEnd   = (seg.endPointId == pointId);
        if (!isStart && !isEnd) continue;
        const double ext = isStart ? it->startMm : it->endMm;
        if (std::abs(ext) < cad::geo::kGeomEps) continue;
        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved) return pt->resolvedPos;
        geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
        const double len = dir.length();
        if (len < cad::geo::kGeomEps) return pt->resolvedPos;  // 退化段: 无出方向
        dir = dir / len;
        // 出方向与 exitDirectionAtPoint 一致：终点 = start→end；起点 = end→start。
        if (isEnd)
            return ep->resolvedPos + dir * ext;
        return sp->resolvedPos - dir * ext;
    }
    return pt->resolvedPos;
}

double Block::segmentBaseLength(const QUuid& segmentId) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return 0.0;
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved) return 0.0;
    if (seg->isCurve()) {
        // 圆拟合段 + 圆度 0 = 真圆（D16 走解析绘制），弧长有解析解 r·θ。
        // 数值积分 spans 得到的是 4 跨三次 Bézier 近似的弧长：整圆时比 2πr 大
        // 1.4e-4 相对量（r = 100 mm 差 0.088 mm，面板 2 位小数 cm 肉眼可见），
        // 会让「弧长」行与「周长」行（2πr）自相矛盾，也会把 D15 发布的周长变量
        // 污染成非精确值（§11.6e：值 == 2πr）。圆度 ≠ 0 时形状确实不再是圆，
        // 继续用数值弧长。
        if (seg->fitKind == FitKind::Circle && std::abs(seg->tension) <= 1e-12) {
            const double r = circleRadiusMm(*seg);
            if (r > cad::geo::kGeomEps)
                return geo::degToArcMm(circleSweepDeg(*seg), r);
        }
        if (const CurveSpanEntry* entry = curveSpanEntry(seg->id);
            entry && !entry->spans.empty())
            return entry->arcLengthMm;  // exact arc length cached with the spans
    }
    return ep->resolved ? sp->resolvedPos.distanceTo(ep->resolvedPos) : 0.0;
}

double Block::segmentEffectiveLength(const QUuid& segmentId) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return 0.0;
    if (seg->isCurve())
        return segmentBaseLength(segmentId);  // 曲线不支持延长（D3）
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    return effectiveLocalPos(sp->id).distanceTo(effectiveLocalPos(ep->id));
}

double Block::segmentExtendStart(const QUuid& segmentId) const
{
    auto it = m_extendEval.constFind(segmentId);
    return it == m_extendEval.constEnd() ? 0.0 : it->startMm;
}

double Block::segmentExtendEnd(const QUuid& segmentId) const
{
    auto it = m_extendEval.constFind(segmentId);
    return it == m_extendEval.constEnd() ? 0.0 : it->endMm;
}

bool Block::segmentSnapWithinBase(const QUuid& segmentId, double t) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return true;
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return true;
    const double baseLen = segmentBaseLength(segmentId);
    const double effLen  = segmentEffectiveLength(segmentId);
    if (effLen < cad::geo::kGeomEps) return true;
    // t 沿有效段（SnapEngine 用有效端点投影）。距本体起点的距离 =
    // t·effLen − 起点延长量；本体范围 = [0, baseLen]。
    const double along = t * effLen - segmentExtendStart(segmentId);
    return along >= -cad::geo::kGeomEpsLoose && along <= baseLen + cad::geo::kGeomEpsLoose;
}

void Block::clampExtendLimits(const Segment& seg, double& inOutStartMm, double& inOutEndMm) const
{
    // 如果两端都是正延长（外延），无需负值限位钳制
    if (inOutStartMm >= 0.0 && inOutEndMm >= 0.0) return;

    const ParamPoint* sp = findPoint(seg.startPointId);
    const ParamPoint* ep = findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) {
        if (inOutStartMm < 0.0) inOutStartMm = 0.0;
        if (inOutEndMm < 0.0) inOutEndMm = 0.0;
        return;
    }

    const double baseLen = sp->resolvedPos.distanceTo(ep->resolvedPos);
    constexpr double kMinLineLen = 1.0; // 最小保留线长 1mm (0.1cm)，防止奇异点退化与倒挂
    if (baseLen <= kMinLineLen) {
        if (inOutStartMm < 0.0) inOutStartMm = 0.0;
        if (inOutEndMm < 0.0) inOutEndMm = 0.0;
        return;
    }

    const geo::Vec2 u = (ep->resolvedPos - sp->resolvedPos) / baseLen;

    double minAuxDist = baseLen;
    double maxAuxDist = 0.0;
    bool hasAux = false;

    auto checkPoint = [&](const ParamPoint* pt) {
        if (!pt || !pt->resolved) return;
        if (pt->id == seg.startPointId || pt->id == seg.endPointId) return;
        const double d = (pt->resolvedPos - sp->resolvedPos).dot(u);
        if (d >= -1e-4 && d <= baseLen + 1e-4) {
            minAuxDist = std::min(minAuxDist, std::max(0.0, d));
            maxAuxDist = std::max(maxAuxDist, std::min(baseLen, d));
            hasAux = true;
        }
    };

    for (const auto& auxId : seg.auxPointIds)
        checkPoint(findPoint(auxId));
    for (const auto& pt : points) {
        if (pt.hostSegmentId == seg.id)
            checkPoint(&pt);
    }

    // 1. 各自单端的最大允许向内收缩量（正值表示向内位移距离）
    // 起点向内收缩最多不能越过最近辅助点
    const double maxShrinkStart = hasAux ? minAuxDist : (baseLen - kMinLineLen);
    // 终点向内收缩最多不能越过最远辅助点
    const double maxShrinkEnd = hasAux ? (baseLen - maxAuxDist) : (baseLen - kMinLineLen);

    // 钳制起点负值
    if (inOutStartMm < 0.0) {
        const double shrink = -inOutStartMm;
        if (shrink > maxShrinkStart) {
            inOutStartMm = -maxShrinkStart;
        }
    }

    // 钳制终点负值
    if (inOutEndMm < 0.0) {
        const double shrink = -inOutEndMm;
        if (shrink > maxShrinkEnd) {
            inOutEndMm = -maxShrinkEnd;
        }
    }

    // 2. 双端收缩总和安全检查：有效总长不得低于 kMinLineLen
    const double effLen = baseLen + inOutStartMm + inOutEndMm;
    if (effLen < kMinLineLen) {
        const double deficit = kMinLineLen - effLen;
        // 如果两端都为负，按比例回调；否则只回调为负的那端
        if (inOutStartMm < 0.0 && inOutEndMm < 0.0) {
            const double totalNeg = (-inOutStartMm) + (-inOutEndMm);
            if (totalNeg > cad::geo::kGeomEps) {
                inOutStartMm += deficit * (-inOutStartMm / totalNeg);
                inOutEndMm += deficit * (-inOutEndMm / totalNeg);
            }
        } else if (inOutStartMm < 0.0) {
            inOutStartMm += deficit;
        } else if (inOutEndMm < 0.0) {
            inOutEndMm += deficit;
        }
    }
}

void Block::evaluateExtendValues(const QHash<QString, double>& params,
                                 const QHash<QString, QList<Condition>>& conditioned,
                                 EvalContext* ctx)
{
    // 求值各段延长量（数值 mm / 公式 cm 域；支持正向延长与负向收缩）。
    // 在 resolve 头部求值，供求解 pass 期间的 effectiveLocalPos 初步感知。
    // 精确几何限位钳制在点解算完毕后的 applyEffectivePositions() 中执行。
    QHash<QUuid, ExtendEval> extendEval;
    for (const auto& seg : segments) {
        ExtendEval e;
        e.startMm = seg.extendStartMm;
        ConditionEngine::evaluateLengthMm(seg.extendStartFormula, params, conditioned, e.startMm, ctx);
        e.endMm = seg.extendEndMm;
        ConditionEngine::evaluateLengthMm(seg.extendEndFormula, params, conditioned, e.endMm, ctx);
        if (std::abs(e.startMm) > cad::geo::kGeomEps || std::abs(e.endMm) > cad::geo::kGeomEps)
            extendEval.insert(seg.id, e);
    }
    m_extendEval = std::move(extendEval);
}

void Block::applyEffectivePositions()
{
    // 无延长（包括公式求值为 0）：清空缓存；上帧有尾巴（刚被清零）→ 可视几何
    // 变化，显式 +epoch 触发重绘。
    if (m_extendEval.isEmpty()) {
        if (!m_effectiveLocal.isEmpty())
            touchGeometry();
        m_effectiveLocal.clear();
        return;
    }

    // 本帧端点与辅助点已全部解算完成，执行辅助点安全限位钳制
    for (const auto& seg : segments) {
        auto it = m_extendEval.find(seg.id);
        if (it != m_extendEval.end()) {
            clampExtendLimits(seg, it.value().startMm, it.value().endMm);
        }
    }

    // 本体位置入缓存，再叠加端点延长（出方向 = 本体方向，与 exitDirectionAtPoint
    // 的"延长方向"语义一致：终点 = start→end，起点 = end→start）。
    QHash<QUuid, geo::Vec2> eff;
    eff.reserve(points.size());
    for (const auto& pt : points)
        eff.insert(pt.id, pt.resolvedPos);

    for (const auto& seg : segments) {
        auto it = m_extendEval.constFind(seg.id);
        if (it == m_extendEval.constEnd()) continue;
        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
        geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;  // start→end (本体)
        const double len = dir.length();
        if (len < cad::geo::kGeomEps) continue;  // 退化线段：无出方向，跳过
        const geo::Vec2 u = dir / len;

        if (std::abs(it->startMm) > cad::geo::kGeomEps)
            eff[sp->id] = sp->resolvedPos - u * it->startMm;  // 起点往起点外（正延负缩）
        if (std::abs(it->endMm) > cad::geo::kGeomEps)
            eff[ep->id] = ep->resolvedPos + u * it->endMm;    // 终点往终点外（正延负缩）
    }

    // 可视几何变化检测：本体不动但尾巴变了 → 显式 +epoch（画布重绘铁律）。
    // 与上帧有效缓存比较，稳定后每帧零开销。
    bool moved = false;
    if (m_effectiveLocal.isEmpty()) {
        // 上帧无延长（缓存为空，有效位置等同于本体 resolvedPos）：
        // 只要本帧有任何端点产生了实际外移，即视为几何变化。
        for (const auto& pt : points) {
            auto it = eff.constFind(pt.id);
            if (it != eff.constEnd() && it.value().distanceSquaredTo(pt.resolvedPos) > cad::geo::kGeomEpsLoose) {
                moved = true;
                break;
            }
        }
    } else {
        moved = (m_effectiveLocal.size() != eff.size());
        if (!moved) {
            for (auto cit = eff.constBegin(); cit != eff.constEnd(); ++cit) {
                auto prev = m_effectiveLocal.constFind(cit.key());
                if (prev == m_effectiveLocal.constEnd() ||
                    prev->distanceSquaredTo(cit.value()) > cad::geo::kGeomEpsLoose) {
                    moved = true;
                    break;
                }
            }
        }
    }
    if (moved)
        touchGeometry();
    m_effectiveLocal = std::move(eff);
}


} // namespace cad::param
