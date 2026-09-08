#pragma once

#include <QSet>
#include <QUuid>
#include <vector>

#include "parametric/Block.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// 删除级联的「依赖快照」公共尾段 (2026-12 审计 U3 收口):
/// 把 sourced linked 变量 / 其固化的消费者块 / 引用级联集的测量变量**追加**到 out*
/// (不清空 —— 调用方持有 undo 快照成员向量)。语义与既有两份逐字实现一致:
/// 消费者块去重, linked/measure 不去重 (同一测量可被多个级联块引用)。
void collectCascadeDependents(const cad::param::ParamDocument& doc,
                              const QSet<QUuid>& cascade,
                              std::vector<cad::param::LinkedVariable>& outLinked,
                              std::vector<cad::param::Block>& outBakedConsumers,
                              std::vector<cad::param::MeasureVariable>& outMeasures);

} // namespace cad::cmd
