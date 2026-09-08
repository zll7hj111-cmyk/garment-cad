#pragma once

#include <QString>
#include <QUuid>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"

namespace cad::ui {

/// 格式化线段所在 Block 的连接拓扑摘要提示 (如 "已连接 L1·腰围线 · 独立角", "桥接线 L2", "终点指向 L3" 等).
/// 当未连接或参数无效时返回空字符串.
inline QString formatConnectionHint(const cad::param::ParamDocument* doc, const QUuid& blockId)
{
    if (!doc || blockId.isNull()) return {};
    const auto* block = doc->findBlock(blockId);
    if (!block) return {};

    QString connHint;
    const cad::param::Attachment* att = nullptr;
    for (const auto& a : doc->attachments()) {
        if (a.isPin) continue;
        if (a.fromBlockId == blockId) { att = &a; break; }
    }
    if (att) {
        // 连接分区状态提示 (已连接 L#·名; 仅角度/滑轨等子态由卡内 badge 细化)。
        connHint = QString::fromUtf8("已连接");
        if (const auto* leader = doc->findBlock(att->toBlockId)) {
            if (const auto* lseg = leader->findSegment(att->toSegmentId)) {
                connHint += QStringLiteral(" ")
                    + cad::param::Serial::tag(lseg->serial);
                if (!lseg->name.isEmpty())
                    connHint += QStringLiteral("·") + lseg->name;
            }
        }
        // 连接子状态 (两维独立四态): 双拆开 = 自由; 独立角 =
        // 有连接线·无基准线; 仅角度 = 无连接线·有基准线。
        if (att->angleIndependent && att->angleOnly)
            connHint += QString::fromUtf8(" · 自由");
        else if (att->angleIndependent)
            connHint += QString::fromUtf8(" · 独立角");
        else if (att->angleOnly)
            connHint += QString::fromUtf8(" · 仅角度");
    }
    // 终点指向 (终点连接, 每端完整连接): 双端连接 = 桥接线 (起点
    // Attachment + 终点 endTarget); 仅终点指向 = 自由线带指向。
    if (!block->endTargetPointId.isNull()) {
        connHint = att ? QString::fromUtf8("桥接线")
                       : QString::fromUtf8("终点指向");
        if (const auto* tb = doc->findBlock(block->endTargetBlockId)) {
            const QUuid ts = tb->exitSegmentAtPoint(block->endTargetPointId);
            if (const auto* tsg = tb->findSegment(ts)) {
                QString t = cad::param::Serial::tag(tsg->serial);
                if (!tsg->name.isEmpty())
                    t += QStringLiteral("·") + tsg->name;
                connHint += QStringLiteral(" ") + t;
            }
        }
    }
    return connHint;
}

} // namespace cad::ui
