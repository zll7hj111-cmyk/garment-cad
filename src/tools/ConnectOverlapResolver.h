#pragma once

#include <QUuid>
#include <QPointer>

#include <optional>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "tools/SnapEngine.h"
#include "tools/ConnectConfirm.h"  // ConfirmCandidate (共享定义)

class QGraphicsEllipseItem;
class QGraphicsPathItem;
class CanvasScene;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// 连接手势的重叠消歧与视觉反馈 (阶段 3 拆分): ConfirmTarget / ConfirmSource /
/// AngleInput 三个状态所用的候选收集、候选线高亮、源端点标记、吸附环与源光晕
/// 等全部画布图元。与 ConnectGesture 同生同灭 —— 本控制器只管理「候选数据与
/// 对应图元」的收集/创建/销毁, 不涉及 attachment 校验/提交/状态机迁移。
/// ConnectGesture 析构时调用 dispose()。
class ConnectOverlapResolver
{
public:
    using Vec2 = cad::geo::Vec2;

    ConnectOverlapResolver(CanvasScene* scene,
                           cad::param::ParamDocument* doc)
        : m_scene(scene), m_paramDoc(doc) {}
    ~ConnectOverlapResolver() { dispose(); }

    void dispose();

    // ── 候选收集 ──
    /// Leader segments whose endpoint lies on the connection spot (overlap disambiguation).
    [[nodiscard]] std::vector<ConfirmCandidate> collectConfirmCandidates(
        const Vec2& connWorldPos,
        const QUuid& fromBlockId) const;
    /// 组件级重选候选: leader segments whose endpoints ALSO stack on the
    /// connection spot — excluding current leader and the component's own members.
    [[nodiscard]] std::vector<ConfirmCandidate> collectComponentSwitchCandidates(
        const Vec2& connWorldPos,
        const QUuid& curBlockId, const QUuid& curSegId,
        const QUuid& fromBlockId, const QUuid& componentId) const;

    // ── ConfirmTarget 高亮 (鼠标悬停在某个候选线段上的加粗路径) ──
    /// 由线段命中 (findSegmentSnap) 在 @p candidates 里找匹配候选;
    /// 命中 → 更新并显示高亮, 未命中 → 隐藏。
    void updateHighlightAt(const Vec2& pos,
                           const std::vector<ConfirmCandidate>& candidates);
    void removeConfirmHighlight();

    // ── ConfirmSource 源端口标记 ──
    void setSourcePortMarker(const ConfirmCandidate& cand);
    void removeSourcePortMarker();
    [[nodiscard]] bool hasSourcePortMarker() const
    { return m_sourcePortMarker != nullptr; }

    // ── 吸附环 + 源光晕 ──
    void showConnectMarker(const Vec2& worldPos);
    void removeConnectMarker();
    [[nodiscard]] bool hasConnectMarker() const
    { return m_connectMarker != nullptr; }
    void updateConnectHalo(const Vec2& fromPointWorld);
    void removeConnectHalo();
    [[nodiscard]] bool hasConnectHalo() const
    { return m_connectHalo != nullptr; }

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    SnapEngine m_snapEngine;  ///< findSegmentSnap (候选线命中) — 每帧全表扫描.

    QGraphicsEllipseItem* m_connectMarker = nullptr;
    QGraphicsEllipseItem* m_connectHalo   = nullptr;
    QGraphicsPathItem* m_confirmHighlight = nullptr;
    QGraphicsEllipseItem* m_sourcePortMarker = nullptr;
};

} // namespace cad::tools
