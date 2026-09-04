#include "document/commands/ReverseSegmentCommand.h"

#include <cmath>
#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AngleMeasureVariable.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── ReverseSegmentCommand (线段换向, 角度基准视角切换) ───

namespace {

/// 换向方向翻转分析 (canReverse / 快照构造 / 补偿 共用同一判定):
///   · exitDirectionAtPoint 语义: 端点 = 离体方向 (换向不变); 宿主辅助点/
///     曲线锚点 = 弦向/切向 (换向翻转 180°)。
///   · directionAtPoint 语义 (跟随侧 localDir): 端点/辅助点 = start→end 弦向
///     (换向翻转); 曲线锚点返回 0 (不翻转)。
struct AttachmentFlip {
    int  k = 0;                  ///< 方向参照翻转次数 (两次翻转相互抵消 → 偶数不补偿)
    bool needsBackfill = false;  ///< 旧档空角度基准点 → 回填旧终点吸收翻转
};

AttachmentFlip attachmentFlip(const cad::param::Block& block,
                              const QUuid& segId,
                              const QUuid& startId, const QUuid& endId,
                              const cad::param::Attachment& att)
{
    AttachmentFlip r;
    auto exitFlips = [&](const QUuid& pid) {
        if (pid.isNull() || pid == startId || pid == endId) return false;
        const auto* pt = block.findPoint(pid);
        return pt && pt->hostSegmentId == segId &&
               (pt->constraint == cad::param::PointConstraint::Interpolated ||
                pt->constraint == cad::param::PointConstraint::CurveAnchor);
    };
    auto dirAtFlips = [&](const QUuid& pid) {
        if (pid == startId || pid == endId) return true;
        const auto* pt = block.findPoint(pid);
        return pt && pt->hostSegmentId == segId &&
               pt->constraint == cad::param::PointConstraint::Interpolated;
    };

    // 跟随侧: rotation = refWorld + π − angle − localDir, localDir 翻转 → angle +180。
    if (att.fromBlockId == block.id && !att.isPin && dirAtFlips(att.fromPointId))
        ++r.k;
    // 角度基准侧: 位置宿主 (angleRefBlockId 空) 或独立角度基准为本块时,
    // 参照方向翻转 → angle +180; 旧档空基准点 (start→end) 由回填吸收。
    if (att.angleRefBlockId.isNull()) {
        if (att.toBlockId == block.id && exitFlips(att.toPointId)) ++r.k;
    } else if (att.angleRefBlockId == block.id && att.angleRefSegmentId == segId) {
        if (!att.angleRefPointId.isNull()) {
            if (exitFlips(att.angleRefPointId)) ++r.k;
        } else {
            r.needsBackfill = true;
        }
    }
    return r;
}

} // namespace

bool ReverseSegmentCommand::canReverse(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const QUuid& segmentId,
                                       QString* reason)
{
    auto fail = [reason](const QString& r) {
        if (reason) *reason = r;
        return false;
    };
    if (!doc) return fail(QStringLiteral("文档为空"));
    const auto* block = doc->findBlock(blockId);
    if (!block) return fail(QStringLiteral("线段不存在"));
    const auto* seg = block->findSegment(segmentId);
    if (!seg) return fail(QStringLiteral("线段不存在"));
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep || sp->id == ep->id)
        return fail(QString::fromUtf8("端点缺失或重合"));
    if (block->isBridge)
        return fail(QString::fromUtf8("桥接线两端被动, 无换向意义"));
    if (block->isDart())
        return fail(QString::fromUtf8("省道线由约束算出, 不可换向"));
    if (!block->endTargetPointId.isNull())
        return fail(QString::fromUtf8("终点指向在驱动方向, 先清除指向再换向"));

    // 连接 (v2 放开): 跟随角 +180·k 补偿 / 旧档角度基准回填, 世界姿态不变。
    // 仍拒绝: 滑轨 (基准线局部系快照会镜像)、需补偿的弧长模式 (πr 不可
    // 参数化表达, 冻结常数会在段长变化后失真)。
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != blockId &&
            !(att.angleRefBlockId == blockId && att.angleRefSegmentId == segmentId))
            continue;
        if (att.fromBlockId == blockId &&
            att.slideMode != cad::param::SlideMode::None)
            return fail(QString::fromUtf8("滑轨模式连接暂不支持换向"));
        const auto flip = attachmentFlip(*block, segmentId,
                                         seg->startPointId, seg->endPointId, att);
        if (att.rotationMode == cad::param::RotationMode::ArcLength && (flip.k % 2) != 0)
            return fail(QString::fromUtf8("弧长模式连接换向需改写弧长, 暂不支持"));
    }

    // 端点被块内其他线段共享 → 换向会重写共享点的驱动约束。
    for (const auto& other : block->segments) {
        if (other.id == segmentId) continue;
        if (other.startPointId == sp->id || other.startPointId == ep->id ||
            other.endPointId == sp->id || other.endPointId == ep->id)
            return fail(QString::fromUtf8("端点被其他线段共享, 暂不支持换向"));
    }

    // 标准驱动结构: 恰一端 Polar(ref=另一端, 无基准段), 另一端 Free。
    auto isPolarTo = [](const cad::param::ParamPoint* p, const QUuid& other) {
        return p->constraint == cad::param::PointConstraint::Polar &&
               p->refPointId == other && p->refSegmentId.isNull();
    };
    const bool standardStructure =
        (isPolarTo(ep, sp->id) && sp->constraint == cad::param::PointConstraint::Free) ||
        (isPolarTo(sp, ep->id) && ep->constraint == cad::param::PointConstraint::Free);
    if (!standardStructure)
        return fail(QString::fromUtf8("两端不是锚点+驱动的标准结构, 暂不支持换向"));

    // 角度测量以段 start→end 为基准方向, 换向会改变测量值 (v2 仍拒绝)。
    for (const auto& am : doc->angleMeasures())
        if ((am.blockA == blockId && am.segmentA == segmentId) ||
            (am.blockB == blockId && am.segmentB == segmentId))
            return fail(QString::fromUtf8("角度测量引用了本线, 换向会改变测量值"));
    return true;
}

ReverseSegmentCommand::ReverseSegmentCommand(cad::param::ParamDocument* doc,
                                             const QUuid& blockId,
                                             const QUuid& segmentId,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
{
    setText(QString::fromUtf8("线段换向"));
    const auto* block = doc->findBlock(blockId);
    const auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return;

    auto capture = [](const cad::param::ParamPoint* p, PointSnapshot& s) {
        s.constraint     = p->constraint;
        s.freePos        = p->freePos;
        s.refPointId     = p->refPointId;
        s.distance       = p->distance;
        s.angle          = p->angle;
        s.refSegmentId   = p->refSegmentId;
        s.distanceFormula = p->distanceFormula;
        s.angleFormula   = p->angleFormula;
        s.interpFromEnd  = p->interpFromEnd;
        s.tangentIn      = p->tangentIn;
        s.tangentOut     = p->tangentOut;
        s.autoTangent    = p->autoTangent;
    };
    m_oldStartId = seg->startPointId;
    m_oldEndId   = seg->endPointId;
    if (const auto* p = block->findPoint(m_oldStartId)) capture(p, m_oldStart);
    if (const auto* p = block->findPoint(m_oldEndId))   capture(p, m_oldEnd);
    m_extendStartMm      = seg->extendStartMm;
    m_extendStartFormula = seg->extendStartFormula;
    m_extendEndMm        = seg->extendEndMm;
    m_extendEndFormula   = seg->extendEndFormula;
    for (const auto& pt : block->points)
        if (pt.constraint == cad::param::PointConstraint::Interpolated &&
            pt.hostSegmentId == segmentId)
            m_auxFromEnd.emplace_back(pt.id, pt.interpFromEnd);
    m_passPoints = seg->passPointIds;
    // v2 补偿快照: 基准消费者 (+180) / 曲线锚点 (冻结同解切线) / 连接。
    // 曲线: Hobby 自动切线求解不保证换序对称 (实测换向后弧长漂移), 与打断
    // 冻结同范式 —— 换向前从曲线缓存捕获每点的"同解"有效切线 (缓存 spans
    // 就是当前渲染几何), 换向后镜像存储并冻结 autoTangent=false, 形状精确不变。
    if (seg->isCurve()) {
        const auto* entry = block->curveSpanEntry(segmentId);
        const std::vector<cad::geo::Vec2>* anchorTanIn = nullptr;
        const std::vector<cad::geo::Vec2>* anchorTanOut = nullptr;
        cad::geo::Vec2 endTanIn{0.0, 0.0}, startTanOut{0.0, 0.0};
        bool haveCache = false;
        if (entry && entry->spans.size() == seg->passPointIds.size() + 1) {
            // 有效切线还原: ctrl1 = P0 + out/3 → out = 3(ctrl1 − P0);
            // ctrl2 = P1 − in/3 → in = 3(P1 − ctrl2)。
            startTanOut = (entry->spans.front().ctrl1 - entry->spans.front().p0) * 3.0;
            endTanIn    = (entry->spans.back().p3 - entry->spans.back().ctrl2) * 3.0;
            // 内部锚点 (n 个 span 有 n-1 个拼接点 k=1..n-1): 拼接点 k =
            // 过点 k-1 (passPointIds 序)。槽位约定: m_innerEff* [k-1] =
            // 过点 k-1 的有效切线 —— in 取左 span 终点、out 取右 span 起点。
            const size_t n = entry->spans.size();
            m_innerEffIn.assign(n - 1, cad::geo::Vec2{0.0, 0.0});
            m_innerEffOut.assign(n - 1, cad::geo::Vec2{0.0, 0.0});
            for (size_t k = 1; k < n; ++k) {
                m_innerEffIn[k - 1] =
                    (entry->spans[k - 1].p3 - entry->spans[k - 1].ctrl2) * 3.0;
                m_innerEffOut[k - 1] =
                    (entry->spans[k].ctrl1 - entry->spans[k].p0) * 3.0;
            }
            anchorTanIn = &entry->anchors;  // 仅标志缓存可用 (anchors 未用)
            haveCache = true;
        }
        Q_UNUSED(anchorTanIn);
        Q_UNUSED(anchorTanOut);
        m_curveCacheValid = haveCache;
        m_effStartTangentOut = startTanOut;
        m_effEndTangentIn = endTanIn;
    }
    for (const auto& pt : block->points) {
        if (pt.refSegmentId == segmentId ||
            (pt.constraint == cad::param::PointConstraint::Intersection &&
             pt.hostSegmentId == segmentId && !pt.interUseWorldAngle &&
             !pt.interBidirectional && pt.interAimPointId.isNull())) {
            m_consumers.push_back({pt.id, pt.angle, pt.angleFormula,
                                   pt.interAngle, pt.interAngleFormula});
        }
        if (pt.constraint == cad::param::PointConstraint::CurveAnchor &&
            pt.hostSegmentId == segmentId) {
            CurveAnchorSnapshot s;
            s.pointId = pt.id;
            s.autoTangent = pt.autoTangent;
            s.tangentIn = pt.tangentIn;
            s.tangentOut = pt.tangentOut;
            s.interpPercent = pt.interpPercent;
            s.interpOffsetDist = pt.interpOffsetDist;
            // 同解有效切线 (按过点在 passPointIds 中的位置取缓存还原值)。
            auto it = std::find(seg->passPointIds.begin(), seg->passPointIds.end(), pt.id);
            if (m_curveCacheValid && it != seg->passPointIds.end()) {
                const size_t idx = static_cast<size_t>(it - seg->passPointIds.begin());
                s.effTangentIn = m_innerEffIn[idx];
                s.effTangentOut = m_innerEffOut[idx];
            }
            m_curveAnchors.push_back(std::move(s));
        }
    }
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        // 本块是 follower (跟随侧 localDir 翻转) 或 本段是其独立角度基准
        // (旧档空基准点回填) 都要快照。
        if (att.fromBlockId != blockId &&
            !(att.angleRefBlockId == blockId && att.angleRefSegmentId == segmentId))
            continue;
        const auto flip = attachmentFlip(*block, segmentId,
                                         m_oldStartId, m_oldEndId, att);
        const bool compensate = (flip.k % 2) != 0 && !att.angleIndependent;
        if (!compensate && !flip.needsBackfill) continue;
        AttachmentSnapshot s;
        s.attId = att.id;
        s.compensateAngle = compensate;
        s.backfill = flip.needsBackfill;
        s.followerAngle = att.followerAngle;
        s.followerAngleFormula = att.followerAngleFormula;
        s.angleRefPointId = att.angleRefPointId;
        m_attComp.push_back(std::move(s));
    }
}

void ReverseSegmentCommand::applyState(bool reversed)
{
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    auto* sp = block ? block->findPoint(m_oldStartId) : nullptr;
    auto* ep = block ? block->findPoint(m_oldEndId) : nullptr;
    if (!block || !seg || !sp || !ep) return;

    auto restore = [](cad::param::ParamPoint* p, const PointSnapshot& s) {
        p->constraint      = s.constraint;
        p->freePos         = s.freePos;
        p->refPointId      = s.refPointId;
        p->distance        = s.distance;
        p->angle           = s.angle;
        p->refSegmentId    = s.refSegmentId;
        p->distanceFormula = s.distanceFormula;
        p->angleFormula    = s.angleFormula;
        p->interpFromEnd   = s.interpFromEnd;
        p->tangentIn       = s.tangentIn;
        p->tangentOut      = s.tangentOut;
    };
    // v2: 角度数值 +180 / 公式包一层 (formula + 180), 保世界方向。
    auto bumpAngle = [](double& angle, QString& formula) {
        if (formula.isEmpty()) {
            angle = cad::geo::normalizeDeg360(angle + 180.0);
        } else {
            formula = QStringLiteral("(%1)+180").arg(formula);
        }
    };

    if (!reversed) {
        seg->startPointId = m_oldStartId;
        seg->endPointId   = m_oldEndId;
        seg->passPointIds = m_passPoints;
        seg->extendStartMm      = m_extendStartMm;
        seg->extendStartFormula = m_extendStartFormula;
        seg->extendEndMm        = m_extendEndMm;
        seg->extendEndFormula   = m_extendEndFormula;
        restore(sp, m_oldStart);
        restore(ep, m_oldEnd);
        sp->autoTangent = m_oldStart.autoTangent;
        ep->autoTangent = m_oldEnd.autoTangent;
        for (const auto& [auxId, fromEnd] : m_auxFromEnd)
            if (auto* aux = block->findPoint(auxId)) aux->interpFromEnd = fromEnd;
        for (const auto& c : m_consumers) {
            if (auto* pt = block->findPoint(c.pointId)) {
                if (pt->refSegmentId == m_segmentId) {
                    pt->angle = c.angle;
                    pt->angleFormula = c.angleFormula;
                } else {
                    pt->interAngle = c.interAngle;
                    pt->interAngleFormula = c.interAngleFormula;
                }
            }
        }
        for (const auto& ca : m_curveAnchors) {
            if (auto* pt = block->findPoint(ca.pointId)) {
                pt->autoTangent = ca.autoTangent;   // 恢复冻结前标志
                pt->tangentIn = ca.tangentIn;
                pt->tangentOut = ca.tangentOut;
                pt->interpPercent = ca.interpPercent;
                pt->interpOffsetDist = ca.interpOffsetDist;
            }
        }
        for (const auto& ac : m_attComp) {
            auto* att = m_doc->findAttachment(ac.attId);
            if (!att) continue;
            att->followerAngle = ac.followerAngle;
            att->followerAngleFormula = ac.followerAngleFormula;
            att->angleRefPointId = ac.angleRefPointId;
        }
    } else {
        // 驱动端快照 (canReverse 保证恰一端是 Polar-ref-另一端)。
        const bool drivenWasEnd =
            m_oldEnd.constraint == cad::param::PointConstraint::Polar &&
            m_oldEnd.refPointId == m_oldStartId;
        const PointSnapshot& driven = drivenWasEnd ? m_oldEnd : m_oldStart;

        seg->startPointId = m_oldEndId;    // 新起点 = 旧终点
        seg->endPointId   = m_oldStartId;  // 新终点 = 旧起点 (驱动端)
        if (seg->isCurve()) {
            // 曲线保形 (打断冻结同范式): Hobby 自动切线求解不保证换序对称,
            // 用换向前捕获的同解有效切线镜像后冻结为手动 → 形状精确不变。
            auto& pp = seg->passPointIds;
            std::reverse(pp.begin(), pp.end());
            if (m_curveCacheValid) {
                for (const auto& ca : m_curveAnchors) {
                    if (auto* pt = block->findPoint(ca.pointId)) {
                        pt->autoTangent = false;   // 冻结 (undo 恢复原标志)
                        pt->tangentIn  = {-ca.effTangentOut.x, -ca.effTangentOut.y};
                        pt->tangentOut = {-ca.effTangentIn.x,  -ca.effTangentIn.y};
                        pt->interpPercent    = 1.0 - ca.interpPercent;
                        pt->interpOffsetDist = -ca.interpOffsetDist;  // 左侧 → 右侧
                    }
                }
                ep->autoTangent = false;
                // 新起点 (= 旧终点): 其 out 驱动新首段 = 旧末段反向,
                // 故 out = −旧终点 in。同理新终点 in = −旧起点 out。
                // 坑: Hobby 求解器对手动点按 atan2(tangentOut) 统一取方向、
                // 按 |tangentIn| 取长度 —— 端点 out 必须是与 in 同向的非零
                // 向量, 写零向量会让方向塌成 +x (形状漂移)。
                ep->tangentOut = {-m_effEndTangentIn.x, -m_effEndTangentIn.y};
                ep->tangentIn  = {0.0, 0.0};
                sp->autoTangent = false;
                sp->tangentIn  = {-m_effStartTangentOut.x, -m_effStartTangentOut.y};
                sp->tangentOut = sp->tangentIn;   // 同向非零 (方向源)
            } else {
                // 缓存冷/不一致: 退回存储切线镜像 (手动曲线保形, 自动曲线可能微漂)。
                for (const auto& ca : m_curveAnchors) {
                    if (auto* pt = block->findPoint(ca.pointId)) {
                        pt->tangentIn  = {-ca.tangentOut.x, -ca.tangentOut.y};
                        pt->tangentOut = {-ca.tangentIn.x,  -ca.tangentIn.y};
                        pt->interpPercent    = 1.0 - ca.interpPercent;
                        pt->interpOffsetDist = -ca.interpOffsetDist;
                    }
                }
            }
        }
        seg->extendStartMm      = m_extendEndMm;       // 物理尾巴不动:
        seg->extendStartFormula = m_extendEndFormula;  // 延长量随端点角色互换
        seg->extendEndMm        = m_extendStartMm;
        seg->extendEndFormula   = m_extendStartFormula;

        // 新终点 (旧起点) 接管驱动: 角度 = 原角 + 180 (换向视角补偿),
        // 距离/距离公式原样转移 (|AB| = |BA|)。
        sp->constraint      = cad::param::PointConstraint::Polar;
        sp->refPointId      = m_oldEndId;
        sp->refSegmentId    = QUuid();
        sp->distance        = driven.distance;
        sp->distanceFormula = driven.distanceFormula;
        sp->angle           = cad::geo::normalizeDeg360(driven.angle + 180.0);
        sp->angleFormula.clear();
        // 新起点 (旧终点) 落为自由锚点, 停在原求解位置。
        // (旧终点本就是锚点时 freePos 已正确, 不动; 旧终点是驱动点时
        //  求解位置 = 锚点 freePos + dist·dir(angle), 换算落位。)
        ep->constraint = cad::param::PointConstraint::Free;
        if (drivenWasEnd) {
            ep->freePos = m_oldStart.freePos +
                cad::geo::Vec2{driven.distance * std::cos(driven.angle * M_PI / 180.0),
                               driven.distance * std::sin(driven.angle * M_PI / 180.0)};
        }
        // 曲线切线已在上方曲线块冻结 (缓存同解镜像), 直线切线恒零无需处理。

        // 宿主辅助点: 起终点互换 + fromEnd 翻转 = 求解坐标系不变, 位置零漂移。
        for (const auto& [auxId, fromEnd] : m_auxFromEnd)
            if (auto* aux = block->findPoint(auxId)) aux->interpFromEnd = !fromEnd;

        // v2 补偿: 基准段/相对交点消费者 +180 保世界方向; 连接跟随角 +180·k、
        // 旧档空角度基准点回填旧终点 (出方向 = 原 start→end 基准)。
        for (const auto& c : m_consumers) {
            auto* pt = block->findPoint(c.pointId);
            if (!pt) continue;
            if (pt->refSegmentId == m_segmentId) {
                bumpAngle(pt->angle, pt->angleFormula);
            } else {
                bumpAngle(pt->interAngle, pt->interAngleFormula);
            }
        }
        for (const auto& ac : m_attComp) {
            auto* att = m_doc->findAttachment(ac.attId);
            if (!att) continue;
            if (ac.compensateAngle) {
                if (att->followerAngleFormula.isEmpty()) {
                    att->followerAngle = cad::geo::normalizeDeg360(
                        ac.followerAngle + 180.0);
                } else {
                    att->followerAngleFormula =
                        QStringLiteral("(%1)+180").arg(ac.followerAngleFormula);
                }
            }
            if (ac.backfill) att->angleRefPointId = m_oldEndId;
        }
    }

    block->touchGeometry();
    m_doc->resolveAll();
}

void ReverseSegmentCommand::redo()
{
    applyState(true);
}

void ReverseSegmentCommand::undo()
{
    applyState(false);
}

} // namespace cad::cmd
