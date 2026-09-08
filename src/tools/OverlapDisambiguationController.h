#pragma once

#include "geometry/Vec2.h"
#include "parametric/ParamDocument.h"  // blocksView/layers 访问 (候选构造)
#include "tools/HitTester.h"           // 统一 blockHitsAtScene (P1/M7+L2)
#include "canvas/BlockItem.h"           // BlockItem (候选名用 segmentId)
#include "canvas/OverlapBatteryHud.h"   // 电池芯片组 HUD (任务 4)
#include "canvas/ManagedItems.h"        // 图元生命周期管理

#include <QList>
#include <QString>
#include <QUuid>
#include <functional>

namespace cad::param { class Block; class ParamPoint; }

namespace cad::tools {

/// 重叠线段与点消歧器 (ToolSelect 手势提炼, 阶段 3 拆分 / 任务 4 增强):
/// 同一位置多个点或多条线段时，引线电池组/胶囊芯片卡片 HUD + 常规 W 循环/右键菜单状态机。
///
/// 输出通道:
///   · stateFn    : 进入/退出循环上下文时切换 ToolSelect 外部状态;
///   · selFn      : applyPick 把选中的线段写入选择集;
///   · pointSelFn : applyPick 把选中的点 (放置点/端点) 写入选择集;
///   · toastFn    : 状态栏提示刷新。
class OverlapDisambiguationController
{
public:
    struct Candidate {
        enum class Kind { Point, Segment };
        Kind kind = Kind::Segment;
        QUuid blockId;
        QUuid pointId;      ///< 当为点重叠时的点 ID
        QUuid segmentId;    ///< 块内最近线段 (仅端点命中时可能为空).
        int serial = 0;     ///< 点/线标号
        QString title;      ///< 标号徽章: 如 "P1", "L1"
        QString name;       ///< 显示名: 点名/线段名 → 块名 → serial
        QString blockName;  ///< 所属图块
        QString layerName;
        QString roleText;   ///< 轮廓线 / 内部线 / 辅助线 / 放置点 / 端点
        double  lengthMm = 0.0;
        bool isPlaced = false;
        bool isAuxiliary = false;
        bool isCurveAnchor = false;
    };

    using StateFn = std::function<void()>;
    using SelectFn = std::function<void(const QUuid& blockId,
                                        const QUuid& segmentId)>;
    using PointSelectFn = std::function<void(const QUuid& blockId,
                                             const QUuid& pointId)>;

    OverlapDisambiguationController(CanvasScene* scene,
                                    cad::param::ParamDocument* doc,
                                    SelectFn selFn, StateFn modeFn,
                                    PointSelectFn pointSelFn = nullptr)
        : m_scene(scene), m_paramDoc(doc), m_selFn(std::move(selFn)),
          m_modeFn(std::move(modeFn)), m_pointSelFn(std::move(pointSelFn)) {}

    ~OverlapDisambiguationController() = default;

    // ── 收集 / 构造 ──
    /// 收集拾取半径内、活动层的全部线段候选 (场景堆叠序, 顶部优先).
    [[nodiscard]] QList<Candidate> collect(const cad::geo::Vec2& worldPos) const;
    /// 收集拾取半径内、活动层的全部重叠点候选 (相同位置的多点聚类).
    [[nodiscard]] QList<Candidate> collectPoints(const cad::geo::Vec2& worldPos, double zoom) const;

    /// 由块+段构造候选身份快照 (线段).
    [[nodiscard]] Candidate makeCandidate(const cad::param::Block& blk,
                                          const QUuid& segmentId) const;
    /// 由块+点构造候选身份快照 (点/放置点).
    [[nodiscard]] Candidate makePointCandidate(const cad::param::Block& blk,
                                               const cad::param::ParamPoint& pt) const;

    // ── 循环上下文 (激活后按 W 在候选间循环) ──
    void activate(const QList<Candidate>& cands, const QUuid& hitBlockId,
                  const cad::geo::Vec2& anchor);
    void deactivate();
    void cycle();   ///< W 循环到下一候选 (剔除已消失的块, 逐位回绕).
    /// 命令式选中第 @p index 个候选 (W 循环与右键「重叠候选」菜单共用入口).
    void applyPick(int index);
    /// 点名选中 (不进入循环上下文): 菜单项被点、或外部联动.
    void pick(int index);

    // ── 电池组 HUD (引线 + 胶囊芯片卡片) ──
    void showBattery(const cad::geo::Vec2& worldPos, const QList<Candidate>& cands,
                     cad::canvas::OverlapBatteryHud::DisplayMode mode);
    void hideBattery();
    [[nodiscard]] bool hasBattery() const;
    [[nodiscard]] cad::canvas::OverlapBatteryHud::DisplayMode batteryMode() const;
    void setBatteryMode(cad::canvas::OverlapBatteryHud::DisplayMode mode);
    [[nodiscard]] int hitBatteryCandidateAt(const QPointF& scenePos, double zoom) const;
    [[nodiscard]] int hitBatteryCandidateAtWorld(const cad::geo::Vec2& worldPos, double zoom) const;
    [[nodiscard]] bool hitBatteryBadgeAt(const QPointF& scenePos, double zoom) const;
    void setBatteryHoveredIndex(int index);
    [[nodiscard]] int batteryHoveredIndex() const;
    void setBatterySelectedIndex(int index);
    [[nodiscard]] int batterySelectedIndex() const;
    [[nodiscard]] int batteryCandidateCount() const;
    [[nodiscard]] Candidate batteryCandidateAt(int index) const;
    [[nodiscard]] const cad::canvas::OverlapBatteryHud* batteryHud() const { return m_batteryHud; }
    [[nodiscard]] cad::geo::Vec2 batteryAnchor() const { return m_batteryAnchor; }

    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] const QList<Candidate>& candidates() const { return m_candidates; }
    [[nodiscard]] QString hintText() const { return QString(); }

    // ── 重叠电池点击簿记与热键处理 ──
    void recordClickedOverlap(const cad::geo::Vec2& pos, double zoom,
                              const std::function<void(const QString&)>& toastFn = nullptr);
    bool handleSpaceOrAltKey();
    bool handleWOrBKey(const cad::geo::Vec2& cursorPos, double zoom);
    [[nodiscard]] bool hasClickedOverlap() const { return !m_clickedCands.isEmpty(); }
    [[nodiscard]] const QList<Candidate>& clickedCandidates() const { return m_clickedCands; }
    [[nodiscard]] cad::geo::Vec2 clickedOverlapPos() const { return m_clickedPos; }
    void clearClickedOverlap() { m_clickedCands.clear(); }

    /// 工具切换 / 上下文销毁: 清列表 + 隐藏 HUD + 状态栏回默认.
    void dispose();

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    SelectFn m_selFn;
    StateFn m_modeFn;
    PointSelectFn m_pointSelFn;

    QList<Candidate> m_candidates;  ///< 激活时的候选快照 (堆叠序).
    int m_index = -1;                ///< -1 = 未激活循环上下文.
    cad::geo::Vec2 m_anchor;         ///< HUD 锚点 (用户坐标).

    cad::canvas::OverlapBatteryHud* m_batteryHud = nullptr;
    ManagedItems m_managed;
    QList<Candidate> m_batteryCandidates;
    cad::geo::Vec2 m_batteryAnchor;

    cad::geo::Vec2 m_clickedPos;
    QList<Candidate> m_clickedCands;
};

} // namespace cad::tools
