#pragma once

#include <QUuid>
#include "document/commands/BreakState.h"

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
class ParamPoint;
}

namespace cad::cmd {

/// Precondition check: can the segment be broken at this auxiliary point?
bool canBreak(const cad::param::Block& block,
              const cad::param::Segment& seg,
              const cad::param::ParamPoint& pt);

BreakMode determineBreakMode(const cad::param::Block& block,
                             const cad::param::Segment& seg,
                             const cad::param::ParamPoint& auxPt);

/// 阶段 1：解析打断几何——段长、方向角；曲线时构建 Bézier、冻结切线、
/// 按打断点分配 pass 点并计算后段端点角。返回 false = 几何不可用，redo 中止。
bool gatherBreakGeometry(cad::param::ParamDocument& doc, const QUuid& blockId,
                         const QUuid& segId, const QUuid& auxPtId, BreakState& st);

/// 阶段 2：按模式求值打断位置（Formula/Freeze/RefChain 的公式与数值距离）。
void evaluateBreakPosition(cad::param::ParamDocument& doc, const QUuid& blockId,
                           const QUuid& segId, const QUuid& auxPtId, BreakState& st);

/// 阶段 3：按打断位置把辅助点分派到前后段。
void redistributeAuxPoints(cad::param::ParamDocument& doc, const QUuid& blockId,
                           const QUuid& segId, const QUuid& auxPtId, BreakState& st);

} // namespace cad::cmd
