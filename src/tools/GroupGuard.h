#pragma once

#include <QString>
#include <QUuid>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Group.h"

namespace cad::tools {

/// Group protection guard (组成员保护): when @p blockId belongs to a user
/// group, show a toast naming the group and return true — the caller must
/// abort the structural operation (打断 / 创建交点 / 曲线点 / 辅助点…).
/// Delete is intentionally NOT covered by this guard (see Group.h).
/// @p actionText is the blocked verb (e.g. 打断 / 创建交点).
inline bool guardGroupedBlock(CanvasScene* scene, cad::param::ParamDocument* doc,
                              const QUuid& blockId, const QString& actionText)
{
    if (!scene || !doc) return false;
    const QUuid gid = doc->groupOfBlock(blockId);
    if (gid.isNull()) return false;
    QString label;
    if (const cad::param::Group* g = doc->findGroup(gid))
        label = g->name.isEmpty() ? g->serial : g->name;
    // 该线段属于组 %1，请先解散组再%2
    scene->showToast(QString::fromUtf8("\xe8\xaf\xa5\xe7\xba\xbf\xe6\xae\xb5"
                                       "\xe5\xb1\x9e\xe4\xba\x8e\xe7\xbb\x84 ")
                     + label
                     + QString::fromUtf8("\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88"
                                         "\xe8\xa7\xa3\xe6\x95\xa3\xe7\xbb\x84"
                                         "\xe5\x86\x8d")
                     + actionText);
    return true;
}

} // namespace cad::tools
