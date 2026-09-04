#pragma once

#include "geometry/Vec2.h"
#include "parametric/ParamDocument.h"  // blocksView/layers 访问 (候选构造)
#include "tools/HitTester.h"           // 统一 blockHitsAtScene (P1/M7+L2)
#include "canvas/BlockItem.h"           // BlockItem (候选名用 segmentId)
#include "canvas/HudItem.h"             // 重叠提示 pill

#include <QList>
#include <QString>
#include <QUuid>
#include <functional>

namespace cad::param { class Block; }

namespace cad::tools {

/// 重叠线段消歧器 (ToolSelect 手势提炼, 阶段 3 拆分): 同一位置多条线段时
/// 悬停提示 → 点选 → W 循环 → 右键候选菜单的统一状态机。
///
/// 三个输出通道 (回调注入, 与 ConnectGesture 同款骨架):
///   · stateFn  : 进入/退出循环上下文时切换 ToolSelect 外部状态 (Selecting);
///   · selFn    : applyPick 把命中块写进 m_selection + m_lastHitSegmentId;
///   · toastFn  : 状态栏提示文案刷新 (W 循环时第 x/y 项滚动)。
/// HUD pill / 候选快照由本控制器自持, 工具切换时 dispose() 清空。
class OverlapDisambiguationController
{
public:
    struct Candidate {
        QUuid blockId;
        QUuid segmentId;    ///< 块内最近线段 (仅端点命中时可能为空).
        QString name;       ///< 显示名: 线段名 → 块名 → 线段 serial.
        QString layerName;
        QString roleText;   ///< 轮廓线 / 内部线 / 辅助线.
        double  lengthMm = 0.0;
    };

    using StateFn = std::function<void()>;
    using SelectFn = std::function<void(const QUuid& blockId,
                                        const QUuid& segmentId)>;

    OverlapDisambiguationController(CanvasScene* scene,
                                    cad::param::ParamDocument* doc,
                                    SelectFn selFn, StateFn modeFn)
        : m_scene(scene), m_paramDoc(doc), m_selFn(std::move(selFn)),
          m_modeFn(std::move(modeFn)) {}

    ~OverlapDisambiguationController() = default;

    // ── 收集 / 构造 ──
    /// 收集拾取半径内、活动层的全部线段块候选 (场景堆叠序, 顶部优先 —
    /// 与 hitBlock 的首选一致; 完全重合的线上层/后建者在前).
    [[nodiscard]] QList<Candidate> collect(const cad::geo::Vec2& worldPos) const;
    /// 由块+段构造候选身份快照 (名称/层名/角色/长度; 随时可重取).
    [[nodiscard]] Candidate makeCandidate(const cad::param::Block& blk,
                                          const QUuid& segmentId) const;

    // ── 循环上下文 (激活后按 W 在候选间循环) ──
    void activate(const QList<Candidate>& cands, const QUuid& hitBlockId,
                  const cad::geo::Vec2& anchor);
    void deactivate();
    void cycle();   ///< W 循环到下一候选 (剔除已消失的块, 逐位回绕).
    /// 命令式选中第 @p index 个候选 (W 循环与右键「重叠候选」菜单共用入口).
    void applyPick(int index);
    /// 点名选中 (不进入循环上下文): 菜单项被点、或外部联动.
    void pick(int index);

    // ── 悬停提示 (无循环上下文时显示集群清单, 有循环上下文时锚定集群) ──
    void refreshHint(const cad::geo::Vec2& worldPos,
                     const QList<Candidate>* precomputed = nullptr);
    void showHint(const QString& text, const cad::geo::Vec2& anchor);
    void hideHint();

    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] const QList<Candidate>& candidates() const { return m_candidates; }
    [[nodiscard]] QString hintText() const { return m_hitText; }

    /// 工具切换 / 上下文销毁: 清列表 + 隐藏 HUD + 状态栏回默认。P2/L5 常驻实例下,
    /// 这是唯一可靠的清理点 (不能靠析构 — 常驻实例下析构只在程序退出时跑)。
    void dispose();

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    SelectFn m_selFn;
    StateFn m_modeFn;

    QList<Candidate> m_candidates;  ///< 激活时的候选快照 (堆叠序).
    int m_index = -1;                ///< -1 = 未激活循环上下文.
    cad::geo::Vec2 m_anchor;         ///< HUD 锚点 (用户坐标).
    HudItem* m_hint = nullptr;       ///< 重叠提示 pill (惰性创建, dispose 清空).
    QString m_hitText;               ///< 当前 HUD 文本 (同值短路).
};

} // namespace cad::tools
