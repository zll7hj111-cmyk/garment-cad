#pragma once

#include <QUuid>
#include <QString>

#include "parametric/ParamDocument.h"  // ParamDocument, Block, Attachment

namespace cad::tools {

/// Cross-layer feedback helpers (跨层连接反馈, 2026-08 收口).
///
/// 合法方向契约: aux follower → working leader (单向跨层附着)。三个工具文件
/// (LineFactory / ConnectGesture / SegmentConnectionCard) 曾各复制一份
/// crossLayerToast, SegmentAuxTab / SegmentConnectionCard 各复制一份
/// crossLayerBadge —— 统一实现在此, 改文案/语义只改这一个头文件。

/// Toast text when a freshly established attachment crosses layers:
/// "已建立跨层连接（测量层→操作层1）" with the real layer names. Empty for
/// same-layer connections (or when either layer is gone).
inline QString crossLayerToast(const cad::param::ParamDocument* doc,
                               const QUuid& fromLayer, const QUuid& toLayer)
{
    if (!doc || fromLayer.isNull() || toLayer.isNull()) return QString();
    if (doc->isAuxLayer(fromLayer) == doc->isAuxLayer(toLayer)) return QString();
    auto name = [doc](const QUuid& layerId) {
        const auto* l = doc->layerById(layerId);
        return l ? l->name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(fromLayer), name(toLayer));
}

/// Block overload: derives the layer ids from the two blocks.
inline QString crossLayerToast(const cad::param::ParamDocument* doc,
                               const cad::param::Block& from,
                               const cad::param::Block& to)
{
    return crossLayerToast(doc, from.layer, to.layer);
}

/// Cross-layer badge text for an attachment whose follower and leader live on
/// different layer kinds (合法方向: aux follower → working leader): returns
/// "→ <leader 所在层名>"; empty for same-layer attachments.
inline QString crossLayerBadge(const cad::param::ParamDocument* doc,
                               const cad::param::Attachment& att)
{
    if (!doc) return QString();
    const cad::param::Block* from = doc->findBlock(att.fromBlockId);
    const cad::param::Block* to   = doc->findBlock(att.toBlockId);
    if (!from || !to) return QString();
    if (doc->isAuxBlock(*from) == doc->isAuxBlock(*to)) return QString();
    const auto* leaderLayer = doc->layerById(to->layer);
    if (!leaderLayer)
        return QString();
    return QStringLiteral("\u2192 ")  // → <层名>
         + leaderLayer->name;
}

} // namespace cad::tools
