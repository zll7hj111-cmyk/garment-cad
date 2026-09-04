#pragma once

#include <vector>
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"

namespace cad::geo {

/// De Casteljau 子跨度参数化切线
struct SubspanTangents {
    Vec2 tInBreak;        ///< 3(left.p3 - left.ctrl2)
    Vec2 tOutBreak;       ///< 3(right.ctrl1 - right.p0)
    Vec2 tOutSubStart;    ///< 3(left.ctrl1 - left.p0)
    Vec2 tInSubEnd;       ///< 3(right.p3 - right.ctrl2)
};

/// 曲线分割几何分析结果
struct CurveSplitInfo {
    bool valid = false;
    double t = 0.0;
    double s = 0.0;       ///< 断点处的弧长参数
    Vec2 tangentAtBreak;
    bool hasSubSpans = false;
    int spanIndex = 0;
    SubspanTangents subTan;
};

/// 投影点至曲线并计算分割信息：求断点导数；当断点不在端点时，
/// 使用 de Casteljau 细分当前 span 并导出子跨度端点导数切线。
CurveSplitInfo splitCurveAtPoint(const Vec2& point, const std::vector<BezierSpan>& spans);

} // namespace cad::geo
