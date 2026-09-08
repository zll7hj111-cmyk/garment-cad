#include "Block.h"

#include <cmath>

namespace cad::param {
bool Block::freezeSegmentGeometry()
{
    if (segments.empty()) return false;
    const Segment& seg = segments.front();
    ParamPoint* pStart = findPoint(seg.startPointId);
    ParamPoint* pEnd   = findPoint(seg.endPointId);
    if (!pStart || !pEnd || !pStart->resolved || !pEnd->resolved) return false;

    const geo::Vec2 w1 = transform.toWorld(pStart->resolvedPos);
    const geo::Vec2 w2 = transform.toWorld(pEnd->resolvedPos);
    const geo::Vec2 d  = w2 - w1;
    const double len   = d.length();
    const double dir   = std::atan2(d.y, d.x);

    transform.origin   = w1;
    transform.rotation = dir;
    pStart->constraint = PointConstraint::Free;
    pStart->freePos    = geo::Vec2::zero();
    pEnd->constraint  = PointConstraint::Polar;
    pEnd->refPointId  = pStart->id;
    pEnd->refSegmentId = QUuid();
    pEnd->distance    = len;              // own length attribute (mm)
    pEnd->distanceFormula.clear();
    pEnd->angle       = 0.0;
    pEnd->angleFormula.clear();

    // Sync the resolved cache with the frozen local construction (stale
    // stretched positions would corrupt directionAtPoint for callers that
    // back-solve angles before the next resolve pass).
    pStart->resolvedPos = geo::Vec2::zero();
    pStart->resolved    = true;
    pEnd->resolvedPos   = geo::Vec2(len, 0.0);
    pEnd->resolved      = true;
    return true;
}

Block Block::cloneShadowOf(const Block& master, const QUuid& segmentId,
                           const QUuid& anchorPointId,
                           QUuid* outAnchorId, QUuid* outSegmentId)
{
    if (outAnchorId)  *outAnchorId = QUuid();
    if (outSegmentId) *outSegmentId = QUuid();

    const Segment* seg = master.findSegment(segmentId);
    if (!seg || seg->isCurve()) return Block{};
    const ParamPoint* sp = master.findPoint(seg->startPointId);
    const ParamPoint* ep = master.findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return Block{};

    // 冻结克隆 (R7): 两点 + 一段, 全部固化为普通数值 —— Free 点拷贝本体
    // resolved 位置 (resolved 缓存同步置位, 下一次 resolve 前方向查询即有效),
    // 清空一切公式/延长量, transform/layer 逐位拷贝 (拆开瞬间姿态, R1)。
    Block shadow;
    shadow.isShadow = true;
    shadow.shadowMasterBlockId = master.id;
    shadow.name = master.name;
    shadow.transform = master.transform;
    shadow.layer = master.layer;

    ParamPoint s = *sp;
    s.id = QUuid::createUuid();
    s.constraint = PointConstraint::Free;
    s.freePos = sp->resolvedPos;   // Free 分支 resolve 取 freePos —— 冻结位置必须双写
    s.resolvedPos = sp->resolvedPos;
    s.distanceFormula.clear();
    s.angleFormula.clear();
    s.isAuxiliary = false;
    s.visible = false;
    s.selectable = false;

    ParamPoint e = *ep;
    e.id = QUuid::createUuid();
    e.constraint = PointConstraint::Free;
    e.freePos = ep->resolvedPos;
    e.resolvedPos = ep->resolvedPos;
    e.distanceFormula.clear();
    e.angleFormula.clear();
    e.isAuxiliary = false;
    e.visible = false;
    e.selectable = false;

    Segment ss;
    ss.id = QUuid::createUuid();
    ss.name = seg->name;
    ss.startPointId = s.id;
    ss.endPointId = e.id;
    ss.lengthFormula.clear();
    ss.extendStartMm = 0.0;
    ss.extendStartFormula.clear();
    ss.extendEndMm = 0.0;
    ss.extendEndFormula.clear();
    ss.visible = false;
    ss.showName = false;
    ss.showLength = false;

    shadow.addPoint(s);
    shadow.addPoint(e);
    shadow.addSegment(ss);

    // 锚点角色 1:1 映射: 本体锚是段起点 → 影子锚 = 影子段起点 (出方向
    // end→start 不变), 终点同理 —— Att2 的 offset (R2) 语义逐位保持。
    const bool anchorWasStart = (anchorPointId == seg->startPointId);
    if (outAnchorId)
        *outAnchorId = anchorWasStart ? ss.startPointId : ss.endPointId;
    if (outSegmentId)
        *outSegmentId = ss.id;
    return shadow;
}


} // namespace cad::param
