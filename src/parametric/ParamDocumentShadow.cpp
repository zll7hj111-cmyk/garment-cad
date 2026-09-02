#include "ParamDocument.h"

#include <algorithm>
#include <cmath>

#include <QDebug>

#include "parametric/Resolver.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/FollowerAngle.h"
#include "parametric/PerfProbe.h"

namespace cad::param {

// ─── 影子基准门面 (拆开影子线段, DETACH_SHADOW_DESIGN.md; 2026-xx 拆分) ────
// 拆开 = 复制本体 exit 线段为隐藏冻结克隆 (影子 Block) + Att2 原地换代指向
// 影子 (angleOnly)。挂新宿主 = 影子作为 follower 挂上去 (Att1, Δ 反算保向)
// 形成 L3→影子→L2 标准双连接链 —— R1 去耦合 / R2 保偏移 / R3 链式随动全部
// 由"连接链"天然获得 (影子是普通块, Resolver 零新增)。生命周期状态机 ①–⑦
// 见设计稿 §6; 级联善后 (⑤⑥⑦) 在 ParamDocumentBlocks::removeBlock。

namespace {

/// Att2 查找: 以影子为基准 (to-block) 的非 pin 连接 (每个影子至多一条)。
Attachment* findAtt2OfShadow(std::vector<Attachment>& atts, const QUuid& shadowId)
{
    for (auto& a : atts)
        if (!a.isPin && a.fromComponentId.isNull() && a.toBlockId == shadowId)
            return &a;
    return nullptr;
}

const Attachment* findAtt2OfShadow(const std::vector<Attachment>& atts,
                                   const QUuid& shadowId)
{
    for (const auto& a : atts)
        if (!a.isPin && a.fromComponentId.isNull() && a.toBlockId == shadowId)
            return &a;
    return nullptr;
}

/// Att1 查找: 影子作为跟随线 (from-block) 的非 pin 连接 (森林不变式)。
Attachment* findAtt1OfShadow(std::vector<Attachment>& atts, const QUuid& shadowId)
{
    for (auto& a : atts)
        if (!a.isPin && a.fromBlockId == shadowId)
            return &a;
    return nullptr;
}

const Attachment* findAtt1OfShadow(const std::vector<Attachment>& atts,
                                   const QUuid& shadowId)
{
    for (const auto& a : atts)
        if (!a.isPin && a.fromBlockId == shadowId)
            return &a;
    return nullptr;
}

} // namespace

QUuid ParamDocument::detachWithShadow(const QUuid& attId)
{
    Attachment* att = findAttachment(attId);
    if (!att || att->isPin) return QUuid();
    if (att->angleOnly) return QUuid();  // 已是拆开态 (旧式 angleOnly): 不重复换代
    // ④ 再拆开 (基准已是影子): 释放挂载 = 结构复位, 影子冻结当前方向。
    if (const Block* cur = blockById(att->toBlockId); cur && cur->isShadow)
        return releaseShadowToDetached(cur->id) ? cur->id : QUuid();

    Block shadow;
    Attachment newAtt;
    if (!buildShadowDetach(attId, shadow, newAtt))
        return QUuid();  // 降级场景: 调用方走旧 angleOnly 行为 (无影子)

    const QUuid shadowId = addBlock(std::move(shadow));  // blockAdded+resolve+信号
    if (Attachment* a = findAttachment(attId))
        *a = newAtt;  // Att2 原地换代 (findAttachment 授权通道)
    resolveAll();
    emit structureChanged();
    return shadowId;
}

bool ParamDocument::mountShadowTo(const QUuid& shadowId, const QUuid& toBlockId,
                                  const QUuid& toPointId, const QUuid& toSegmentId)
{
    Attachment att1;
    if (!buildShadowMount(shadowId, toBlockId, toPointId, toSegmentId, att1))
        return false;
    // 预检镜像 addAttachment 的全部门槛 (校验先行, 防 Att2 翻转后拒绝留半态)。
    const Block* fromBlk = blockById(att1.fromBlockId);
    const Block* toBlk = blockById(att1.toBlockId);
    if (!fromBlk || !toBlk) return false;
    // 已挂载 (面板二次重定向 = 影子换宿主): 旧 Att1 参与校验时排除, 通过后
    // 原子替换 (删旧插新, Δ 按新宿主重新反算)。id 先行拷贝 —— erase 会使
    // 指向 m_attachments 的指针失效。
    Attachment* existingAtt1 = findAtt1OfShadow(m_attachments, shadowId);
    const QUuid oldAtt1Id = existingAtt1 ? existingAtt1->id : QUuid();
    {
        std::vector<Attachment> others;
        others.reserve(m_attachments.size());
        for (const auto& a : m_attachments)
            if (a.id != oldAtt1Id)
                others.push_back(a);
        if (checkAttachment(others, att1) != AttachmentIssue::Ok) return false;
    }
    const bool crossLayer = isAuxBlock(*fromBlk) && !isAuxBlock(*toBlk);
    if (!isAuxBlock(*fromBlk) && isAuxBlock(*toBlk))
        return false;  // 跨层单向: working 不得被 aux 驱动
    if (crossLayer && wouldCreateMeasureValueCycle(att1))
        return false;

    if (!oldAtt1Id.isNull()) {
        m_attachments.erase(
            std::remove_if(m_attachments.begin(), m_attachments.end(),
                [&oldAtt1Id](const Attachment& a) { return a.id == oldAtt1Id; }),
            m_attachments.end());
    }
    // Att2 恢复位置钉点并重新焊接 (链条第二环; R3 由连接链传导)。
    if (Attachment* a2 = findAtt2OfShadow(m_attachments, shadowId)) {
        a2->angleOnly = false;
        a2->isLocked = true;
        a2->slideMode = SlideMode::None;
    }
    att1.isLocked = true;  // 新建连接默认焊接 (与 addAttachment 同约定)
    m_attachments.push_back(std::move(att1));
    if (crossLayer)
        ++m_crossLayerCount;
    m_followersDirty = true;
    resolveAll();
    emit structureChanged();
    return true;
}

bool ParamDocument::releaseShadowToDetached(const QUuid& shadowId)
{
    const Block* shadow = blockById(shadowId);
    if (!shadow || !shadow->isShadow) return false;
    bool hadAtt1 = false;
    for (auto it = m_attachments.begin(); it != m_attachments.end(); ) {
        if (!it->isPin && it->fromBlockId == shadowId) {
            it = m_attachments.erase(it);
            hadAtt1 = true;
        } else {
            ++it;
        }
    }
    if (!hadAtt1)
        return true;  // 未挂载: 已是拆开态 (幂等)
    // Att2 回 angleOnly (影子保持当前解算姿态 —— 冻结当前方向, 不跳变;
    // 跟随线世界方向 = 影子基准 + offset, 影子不动则跟随线不动)。
    for (auto& a : m_attachments) {
        if (!a.isPin && a.toBlockId == shadowId) {
            a.angleOnly = true;
            a.isLocked = false;
            a.slideMode = SlideMode::None;
        }
    }
    recountCrossLayerAttachments();
    m_followersDirty = true;
    resolveAll();
    emit structureChanged();
    return true;
}

bool ParamDocument::reattachShadowToMaster(const QUuid& attId,
                                           const QUuid& explicitToPoint,
                                           const QUuid& explicitToSegment)
{
    Attachment* att = findAttachment(attId);
    if (!att || att->isPin) return false;
    const Block* shadow = blockById(att->toBlockId);
    if (!shadow || !shadow->isShadow) return false;
    Attachment restored;
    if (!buildShadowReconnect(attId, restored, explicitToPoint, explicitToSegment))
        return false;
    const QUuid shadowId = att->toBlockId;
    *att = restored;        // Att2 → 本体 (活引用恢复, offset 原样)
    removeBlock(shadowId);  // 引用清理顺带删除 Att1 (若挂载) + 信号/重解
    resolveAll();
    emit structureChanged();
    return true;
}

bool ParamDocument::removeShadow(const QUuid& shadowId)
{
    const Block* shadow = blockById(shadowId);
    if (!shadow || !shadow->isShadow) return false;
    // Att2 移除 → 跟随线失去角度基准转纯自由线; Att1 (若挂载) 一并移除;
    // 最后删影子块 (removeBlock 自带信号/级联/重解)。
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&shadowId](const Attachment& a) {
                return !a.isPin
                    && (a.toBlockId == shadowId || a.fromBlockId == shadowId);
            }),
        m_attachments.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;
    removeBlock(shadowId);
    return true;
}

Block* ParamDocument::findShadowOfMaster(const QUuid& masterBlockId)
{
    for (auto& b : m_blocks)
        if (b.isShadow && b.shadowMasterBlockId == masterBlockId)
            return &b;
    return nullptr;
}

const Block* ParamDocument::findShadowOfMaster(const QUuid& masterBlockId) const
{
    for (const auto& b : m_blocks)
        if (b.isShadow && b.shadowMasterBlockId == masterBlockId)
            return &b;
    return nullptr;
}

bool ParamDocument::buildShadowDetach(const QUuid& attId, Block& outShadow,
                                      Attachment& outNewAtt) const
{
    outShadow = Block{};
    outNewAtt = Attachment{};
    const Attachment* att = findAttachment(attId);
    if (!att || att->isPin || !att->fromComponentId.isNull()) return false;
    const Block* master = blockById(att->toBlockId);
    if (!master || master->isShadow) return false;  // 影子基准走 ④ 释放路由
    // 降级门 (计划 L2-2.1): 桥线/省道/组件成员/多段块/曲线段 → 旧 angleOnly。
    if (master->isBridge || master->isDart()) return false;
    if (master->segments.size() != 1) return false;
    if (componentOfBlock(master->id)) return false;
    const Segment* seg = master->findSegment(att->toSegmentId);
    if (!seg)
        seg = master->findSegment(master->exitSegmentAtPoint(att->toPointId));
    if (!seg || seg->isCurve()) return false;

    QUuid anchorId, segId;
    Block shadow = Block::cloneShadowOf(*master, seg->id, att->toPointId,
                                        &anchorId, &segId);
    if (!shadow.isShadow) return false;  // 端点缺失/未解析

    // Att2 原地换代 (verbatim 快照语义): 基准 → 影子, offset 原样保留 (R2)。
    outNewAtt = *att;
    outNewAtt.toBlockId = shadow.id;
    outNewAtt.toPointId = anchorId;
    outNewAtt.toSegmentId = segId;
    outNewAtt.angleOnly = true;
    outNewAtt.isLocked = false;          // 位置自由 ↔ 焊接互斥
    outNewAtt.slideMode = SlideMode::None;
    outShadow = std::move(shadow);
    return true;
}

bool ParamDocument::buildShadowReconnect(const QUuid& attId, Attachment& outRestored,
                                         const QUuid& explicitToPoint,
                                         const QUuid& explicitToSegment) const
{
    outRestored = Attachment{};
    const Attachment* att = findAttachment(attId);
    if (!att || att->isPin) return false;
    const Block* shadow = blockById(att->toBlockId);
    if (!shadow || !shadow->isShadow) return false;
    const Block* master = blockById(shadow->shadowMasterBlockId);
    if (!master) return false;

    outRestored = *att;
    outRestored.toBlockId = master->id;
    if (!explicitToPoint.isNull()) {
        // 挂载路由显式落点: 用户拖回本体时钉在拖到的点上。
        outRestored.toPointId = explicitToPoint;
        outRestored.toSegmentId = explicitToSegment.isNull()
            ? master->exitSegmentAtPoint(explicitToPoint) : explicitToSegment;
    } else {
        // 面板重连: 复原拆开前锚点 —— 影子锚的角色 1:1 映射回本体段端点
        // (拆开时本体必为单段, 见 buildShadowDetach 降级门; 锚是段起点则
        // 复原到本体段起点, 出方向语义不变)。
        const Segment* sseg = shadow->findSegment(att->toSegmentId);
        if (master->segments.size() != 1 || !sseg) return false;
        const bool anchorWasStart = (att->toPointId == sseg->startPointId);
        outRestored.toPointId = anchorWasStart ? master->segments.front().startPointId
                                               : master->segments.front().endPointId;
        outRestored.toSegmentId = master->segments.front().id;
    }
    outRestored.angleOnly = false;
    outRestored.isLocked = true;   // 挂回本体 = 活引用恢复 + 重新焊接 (⑤)
    outRestored.slideMode = SlideMode::None;
    return true;
}

bool ParamDocument::buildShadowMount(const QUuid& shadowId, const QUuid& toBlockId,
                                     const QUuid& toPointId, const QUuid& toSegmentId,
                                     Attachment& outAtt1) const
{
    outAtt1 = Attachment{};
    const Block* shadow = blockById(shadowId);
    const Block* toBlk = blockById(toBlockId);
    if (!shadow || !shadow->isShadow || !toBlk || toBlk->isShadow) return false;
    const Attachment* att2 = findAtt2OfShadow(m_attachments, shadowId);
    if (!att2) return false;
    // 注: 已挂载 (存在 Att1) 不在此拒绝 —— 面板二次重定向 = 影子换宿主,
    // 由 mountShadowTo 原子替换旧 Att1; 挂载手势路由只出现在拆开态 (无 Att1)。
    // 桥接线只有辅助点可作基准 (与 addAttachment 同规则)。
    if (toBlk->isBridge) {
        const ParamPoint* tp = toBlk->findPoint(toPointId);
        if (!tp || !tp->isAuxiliary) return false;
    }

    outAtt1 = Attachment{};
    outAtt1.fromBlockId = shadowId;
    outAtt1.fromPointId = att2->toPointId;  // 影子锚点 p2.1 = 链式枢轴 (L3 p5 = p2.1 = L2 p3)
    outAtt1.toBlockId = toBlockId;
    outAtt1.toPointId = toPointId;
    outAtt1.toSegmentId = toSegmentId.isNull()
        ? toBlk->exitSegmentAtPoint(toPointId) : toSegmentId;
    // Δ 反算保向 (rotation = refWorld + π − angle − localDir 的逆): 挂载瞬间
    // 影子世界方向不变 → 跟随线 (offset 相对影子) 同样零跳变。
    const double refWorld = toBlk->transform.rotation
        + toBlk->exitDirectionAtPoint(toPointId, outAtt1.toSegmentId);
    const double localDir = shadow->directionAtPoint(outAtt1.fromPointId);
    outAtt1.followerAngle = backSolveFollowerAngle(
        shadow->transform.rotation, localDir, refWorld);
    return true;
}

} // namespace cad::param
