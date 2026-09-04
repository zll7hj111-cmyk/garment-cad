#include "OverlapDisambiguationController.h"

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "canvas/HudItem.h"
#include "parametric/Block.h"
#include "parametric/DomainViews.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"

#include <QGraphicsView>
#include <algorithm>

namespace cad::tools {

namespace {

/// 线段角色 → 界面文案 (与 LinePropertyDialog 的角色下拉一致).
QString segmentRoleText(cad::param::SegmentRole role)
{
    using cad::param::SegmentRole;
    switch (role) {
    case SegmentRole::Outline:   return QString::fromUtf8("轮廓线");
    case SegmentRole::Internal:  return QString::fromUtf8("内部线");
    case SegmentRole::Auxiliary: return QString::fromUtf8("辅助线");
    }
    return QString::fromUtf8("轮廓线");
}

} // namespace

// ── 收集 / 构造 ──

QList<OverlapDisambiguationController::Candidate>
OverlapDisambiguationController::collect(const cad::geo::Vec2& worldPos) const
{
    QList<Candidate> out;
    if (!m_scene || !m_paramDoc) return out;

    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    // 统一命中 (P1/M7+L2): 堆叠降序 + 去重 + 活动层过滤都在 HitTester 一处。
    for (const auto& h : blockHitsAtScene(*m_scene, *m_paramDoc, scenePt)) {
        const auto* blk = m_paramDoc->blocksView().byId(h.blockId);
        if (!blk) continue;
        out.append(makeCandidate(*blk, h.segmentId));
    }
    return out;
}

OverlapDisambiguationController::Candidate
OverlapDisambiguationController::makeCandidate(const cad::param::Block& blk,
                                              const QUuid& segmentId) const
{
    Candidate c;
    c.blockId = blk.id;
    c.segmentId = segmentId;
    const cad::param::Segment* seg = nullptr;
    if (!segmentId.isNull())
        seg = blk.findSegment(segmentId);
    if (!seg && !blk.segments.empty())
        seg = &blk.segments.front();

    // 显示名: 线段名 → 块名 → 线段 serial (可读 ID, 无名的重叠也有身份).
    if (seg && !seg->name.isEmpty())        c.name = seg->name;
    else if (!blk.name.isEmpty())           c.name = blk.name;
    else if (seg && !seg->serial.isEmpty()) c.name = seg->serial;
    else                                    c.name = QString::fromUtf8("(未命名)");

    c.roleText = segmentRoleText(seg ? seg->role : cad::param::SegmentRole::Outline);
    c.layerName.clear();
    for (const auto& l : m_paramDoc->layers())
        if (l.id == blk.layer) { c.layerName = l.name; break; }

    if (seg) {
        const auto* sp = blk.findPoint(seg->startPointId);
        const auto* ep = blk.findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved)
            c.lengthMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
    }
    return c;
}

// ── 循环上下文 ──

void OverlapDisambiguationController::activate(const QList<Candidate>& cands,
                                                const QUuid& hitBlockId,
                                                const cad::geo::Vec2& anchor)
{
    if (cands.size() < 2) { deactivate(); return; }
    m_candidates = cands;
    // 与单击命中一致: 首项 = hitBlock 首选; 找不到时取 0.
    int idx = 0;
    for (int i = 0; i < m_candidates.size(); ++i)
        if (m_candidates[i].blockId == hitBlockId) { idx = i; break; }
    m_index = idx;
    m_anchor = anchor;
    applyPick(m_index);
    // 常驻循环 HUD (锚定集群, 不随光标走).
    const auto& c = m_candidates[m_index];
    showHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                 .arg(m_candidates.size())
                 .arg(m_index + 1)
                 .arg(m_candidates.size())
                 .arg(c.roleText)
                 .arg(c.name),
             m_anchor);
    // 进入循环上下文: W 的语义从"切模式"变成"循环候选", 状态栏整句要跟着换。
    m_modeFn();
}

void OverlapDisambiguationController::deactivate()
{
    m_index = -1;
    m_candidates.clear();
    hideHint();
    // 状态没变但"此刻按 W 会发生什么"变了 (从循环候选回到切模式)。
    m_modeFn();
}

void OverlapDisambiguationController::cycle()
{
    if (m_index < 0 || m_candidates.isEmpty()) return;

    // 剔除已消失的块 (拖走/删除后名单失效), 实时重取身份信息.
    QList<Candidate> live;
    for (const auto& c : m_candidates) {
        const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
        if (!blk) continue;
        live.append(makeCandidate(*blk, c.segmentId));
    }
    if (live.isEmpty()) { deactivate(); return; }
    if (live.size() != m_candidates.size())
        m_index = qBound(0, m_index, live.size() - 1);
    m_candidates = live;

    m_index = (m_index + 1) % m_candidates.size();
    applyPick(m_index);
    const auto& c = m_candidates[m_index];
    showHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                 .arg(m_candidates.size())
                 .arg(m_index + 1)
                 .arg(m_candidates.size())
                 .arg(c.roleText)
                 .arg(c.name),
             m_anchor);
    // 循环后状态栏的「第 x/y 项」要跟着走 —— 这是持久层相对 HUD 的价值:
    // HUD 锚在集群上可能被遮挡, 状态栏永远在读同一份索引。
    m_modeFn();
}

void OverlapDisambiguationController::applyPick(int index)
{
    if (index < 0 || index >= m_candidates.size()) return;
    const auto& c = m_candidates[index];
    if (m_selFn) m_selFn(c.blockId, c.segmentId);
}

void OverlapDisambiguationController::pick(int index)
{
    if (index < 0 || index >= m_candidates.size()) return;
    m_index = index;
    applyPick(index);
    const auto& c = m_candidates[index];
    showHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                 .arg(m_candidates.size())
                 .arg(index + 1)
                 .arg(m_candidates.size())
                 .arg(c.roleText)
                 .arg(c.name),
             m_anchor);
}

// ── 悬停提示 ──

void OverlapDisambiguationController::refreshHint(
    const cad::geo::Vec2& worldPos, const QList<Candidate>* precomputed)
{
    if (m_index >= 0 && !m_candidates.isEmpty()) {
        // 循环上下文已存在: HUD 锚定集群位置, 不随光标移动; 同值短路由
        // showHint 内部处理 (拖帧路径零重建).
        const auto& c = m_candidates[m_index];
        showHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                     .arg(m_candidates.size())
                     .arg(m_index + 1)
                     .arg(m_candidates.size())
                     .arg(c.roleText)
                     .arg(c.name),
                 m_anchor);
        return;
    }

    const QList<Candidate> cands = precomputed ? *precomputed : collect(worldPos);
    if (cands.size() >= 2) {
        QString text = QString::fromUtf8("此处重叠 %1 条 ｜").arg(cands.size());
        for (int i = 0; i < cands.size(); ++i) {
            if (i) text += QStringLiteral("、");
            text += cands[i].name;
        }
        text += QString::fromUtf8("（点选后按 W 循环）");
        showHint(text, worldPos);
    } else {
        hideHint();
    }
}

void OverlapDisambiguationController::showHint(const QString& text,
                                                const cad::geo::Vec2& anchor)
{
    if (!m_scene) return;
    if (text == m_hitText && m_hint && m_hint->isVisible())
        return;  // 同值短路: 悬停/拖帧路径不重构 HUD

    if (!m_hint) {
        m_hint = new HudItem();
        m_hint->setLook(HudItem::Look::DarkPill);
        m_hint->setZValue(9990.0);
        m_scene->addItem(m_hint);
    }
    m_hitText = text;
    m_hint->setText(text);
    // 锚点右下 12px 屏幕常量 (HudItem 内部除以 zoom; 原实现写 12 场景单位,
    // 缩放下与光标忽远忽近 —— M1)。
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_hint->moveToPoint(anchor, view, QPointF(12.0, 12.0));
    m_hint->show();
}

void OverlapDisambiguationController::hideHint()
{
    if (m_hint) m_hint->setText(QString());
    if (m_hint) m_hint->hide();
    // 清缓存重触发: 每次都重新隐现
    m_hitText.clear();
}

void OverlapDisambiguationController::dispose()
{
    deactivate();
    delete m_hint;
    m_hint = nullptr;
}

} // namespace cad::tools
