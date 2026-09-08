#include "Resolver.h"

#include <cmath>

#include "Block.h"
#include "Attachment.h"

namespace cad::param {

// ────────────────────────────────────────────────────────────────────────────
// 滑轨投影快照 (用户拍板 2026-08): 跟随线当前 from-point 的世界位置投影到
// 基准线局部系 (x = 基准线在吸附点的延长方向, y = 垂直)。激活/重定向滑轨
// 模式时锁定轴坐标从此处快照。
// ────────────────────────────────────────────────────────────────────────────
std::pair<double, double> computeSlideOffsets(const Block& from,
                                              const Attachment& att,
                                              const Block& to)
{
    const ParamPoint* toPt = to.findPoint(att.toPointId);
    const ParamPoint* fromPt = from.findPoint(att.fromPointId);
    if (!toPt || !toPt->resolved || !fromPt || !fromPt->resolved)
        return {0.0, 0.0};

    // Leader-local frame at the anchor (same reference as applyAttachment).
    const double railAngle = to.transform.rotation
                           + to.exitDirectionAtPoint(att.toPointId, att.toSegmentId);
    const geo::Vec2 alongDir(std::cos(railAngle), std::sin(railAngle));
    const geo::Vec2 perpDir(-alongDir.y, alongDir.x);

    const geo::Vec2 fromPtWorldCur = from.worldPos(att.fromPointId);
    const geo::Vec2 rel = fromPtWorldCur - to.worldPos(att.toPointId);
    const double s = rel.x * alongDir.x + rel.y * alongDir.y;
    const double t = rel.x * perpDir.x + rel.y * perpDir.y;
    return {s, t};
}

} // namespace cad::param
