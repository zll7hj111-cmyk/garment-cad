#include "geometry/CurveSplitter.h"

#include <cmath>
#include <algorithm>

namespace cad::geo {

CurveSplitInfo splitCurveAtPoint(const Vec2& point, const std::vector<BezierSpan>& spans)
{
    CurveSplitInfo info;
    if (spans.empty()) return info;

    auto proj = projectPointOnCurve(point, spans);
    if (!proj.valid) return info;

    info.valid = true;
    info.t = proj.t;
    info.s = proj.s;
    info.tangentAtBreak = evalCurveDerivative(spans, proj.t);

    const int spanIdx = std::clamp(
        static_cast<int>(std::floor(proj.t)), 0, static_cast<int>(spans.size()) - 1);
    const double t0 = std::clamp(proj.t - static_cast<double>(spanIdx), 0.0, 1.0);
    info.spanIndex = spanIdx;

    if (t0 > 1e-6 && t0 < 1.0 - 1e-6) {
        const auto [left, right] = subdivideBezier(spans[static_cast<size_t>(spanIdx)], t0);
        info.hasSubSpans = true;
        info.subTan.tInBreak     = (left.p3 - left.ctrl2) * 3.0;
        info.subTan.tOutBreak    = (right.ctrl1 - right.p0) * 3.0;
        info.subTan.tOutSubStart = (left.ctrl1 - left.p0) * 3.0;
        info.subTan.tInSubEnd    = (right.p3 - right.ctrl2) * 3.0;
    }

    return info;
}

} // namespace cad::geo
