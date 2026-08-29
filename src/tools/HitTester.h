#pragma once

#include <QPointF>
#include <QSet>

#include <vector>

#include "canvas/BlockItem.h"
#include "canvas/CanvasScene.h"
#include "geometry/Vec2.h"
#include "parametric/ParamDocument.h"

class QGraphicsItem;

namespace cad::tools {

/// 场景点上的单个块命中（TOOL_SYSTEM_AUDIT P1/M7+L2，2026-08-29 收口）。
struct SceneBlockHit
{
    QUuid blockId;
    QUuid segmentId;   ///< 命中点落在该块的哪条线段上（无 = null）。
    const BlockItem* item = nullptr;
};

/// 一次 items() 扫描，产出 @p scenePt 上的**全部**块命中：
///   · 堆叠降序（场景 items 顺序，后建者在前）；
///   · 同块多图元（曲线子项 + 主体）按 blockId 去重；
///   · 只保留活动层的块（跨层重叠靠图层过滤区分 —— 全仓唯一规则源，
///     替代 hitBlock 两份近似实现里 layersView().activeLayer() 与
///     activeLayer() 的漂移写法，L2）。
/// 消费方一次拿到"首选块 / 全部重叠候选 / 命中段"，悬停路径不再对同一
/// 帧做 2~3 遍全场景命中（M7 的卡顿源）。
inline std::vector<SceneBlockHit> blockHitsAtScene(
    CanvasScene& scene, const cad::param::ParamDocument& doc, const QPointF& scenePt)
{
    std::vector<SceneBlockHit> out;
    const QList<QGraphicsItem*> hits = scene.items(scenePt);
    QSet<QUuid> seen;
    for (QGraphicsItem* item : hits) {
        // 曲线子项归属其宿主块 —— 上溯到 BlockItem。
        auto* bi = BlockItem::containingItem(item);
        if (!bi) continue;
        if (seen.contains(bi->blockId())) continue;
        seen.insert(bi->blockId());
        const auto* blk = doc.blocksView().byId(bi->blockId());
        if (!blk) continue;
        if (blk->layer != doc.activeLayer()) continue;
        SceneBlockHit h;
        h.blockId = bi->blockId();
        h.segmentId = bi->hitSegmentAtScene(scenePt);
        h.item = bi;
        out.push_back(h);
    }
    return out;
}

/// 空白判定：任何块图元命中即非空白（**不**做活动层过滤 —— 智能笔右键
/// 切换工具的原始语义，实体处右键保持保留）。
inline bool isBlankSpaceAtScene(CanvasScene& scene, const QPointF& scenePt)
{
    const QList<QGraphicsItem*> hits = scene.items(scenePt);
    for (QGraphicsItem* item : hits)
        if (BlockItem::containingItem(item) != nullptr)
            return false;
    return true;
}

} // namespace cad::tools
