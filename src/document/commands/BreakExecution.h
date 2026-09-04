#pragma once

#include <QUuid>
#include <QString>
#include <vector>

#include "document/commands/BreakState.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// 阶段 4：把原块改为前段——断点转 Polar 端点（RefChain 保留参数锚链）、
/// 段属性更新、RefChain/Freeze 自动发布前段长度（back = orig − M_front）、
/// 曲线 pass 点重参数化与切线冻结、无 pass 点时插入 Bézier 中点、删除
/// 原端点（无其他段引用时）。
void modifyFrontBlock(cad::param::ParamDocument& doc, const QUuid& blockId,
                      const QUuid& segId, const QUuid& auxPtId, BreakState& st,
                      const QUuid& origEndId, QUuid& publishedLinkedId,
                      QString& publishedRefName);

/// 阶段 5：构建后段块——断点作为新起点（局部原点），Polar 终点（距离公式
/// 为“原总长 − 前段发布”或阶段 2 求值结果），曲线切线旋转进后段局部系、
/// pass 点切线冻结，无 pass 点时插入 Bézier 中点保持形状（de Casteljau
/// 半参数化，切线减半）。
cad::param::Block buildBackBlock(cad::param::ParamDocument& doc,
                                 const QUuid& blockId, const QUuid& segId,
                                 const QUuid& auxPtId, BreakState& st,
                                 const QUuid& origEndId,
                                 const QString& publishedRefName);

/// 阶段 6：收尾——移除以断点/原端点为目标的附件（快照后统一重建）、加入
/// 后段块、重新建立幸存连接、建立后段→前段连接（followerAngle 补偿曲线
/// 切线偏角，保证断点无折角）。addBlock/addAttachment 内部已触发 resolveAll。
void finalizeBreak(cad::param::ParamDocument& doc, BreakState& st,
                   const QUuid& frontBlockId, const QUuid& frontSegId,
                   const QUuid& frontAuxPtId, cad::param::Block backBlock,
                   const std::vector<cad::param::Attachment>& removedAttachments);

} // namespace cad::cmd
