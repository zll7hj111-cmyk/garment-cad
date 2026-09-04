#pragma once

#include <QUuid>
#include <QList>
#include <QSet>
#include <QRectF>
#include <QCursor>
#include <optional>
#include <utility>
#include <vector>

#include "Tool.h"
#include "ToolRegistry.h"
#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "tools/SelectState.h"
#include "tools/SnapEngine.h"
#include "tools/SelectHoverFeedback.h"
#include "tools/OverlapDisambiguationController.h"

class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
class HudItem;

namespace cad::tools {

class ConnectGesture;
class CopyDragController;
class MarqueeGesture;
class SelectDragController;
class CurveAnchorDragSession;

/// Selection tool with ETCAD-style interaction (2026-09 取消确认基准):
///   - Left-drag on empty space → marquee (intersect = select; toggle add/remove)
///   - Click a line body → select (SINGLE: replaces; MULTI: toggles); the
///     press enters a pending state — moving >5px (or >10px at the endpoint
///     grab radius) starts a DRAG immediately, release without moving is a
///     plain click. No right-click confirm needed (选中即就绪).
///   - Press an endpoint of a SELECTED line → connection gesture (drag to a
///     target, then angle HUD) — handled by ConnectGesture
///   - Hover feedback: no-button move shows 抓手 on line bodies (draggable)
///     and 十字 on a selected line's endpoints (connectable)
///   - Right-click: context menu when a selection exists, else switch to
///     SmartPen on blank space
///   - Drag release → move committed, selection cleared, back to Idle
///   - Ctrl+press on a selected segment → quick copy (快捷复制)
///   - Double-click segment → property dialog; Del → delete; Esc → clear;
///     D → 快拆 (拆开保留角度)
class ToolSelect : public Tool
{
public:
    // 无自定义析构: 全部清理都在 onDeactivate()。ToolManager 析构时会先
    // deactivate 激活工具, 保证这条路径对"退出时仍在激活的那个工具"也成立 (N5)。

    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    // ── 连接角度会话输入 (二期: 上下文属性条 → ConnectGesture) ──
    void connectAngleTextChanged(const QString& text) override;
    void connectAngleModeChanged(cad::param::RotationMode mode) override;
    void connectAngleCommitted() override;
    void connectAngleCancelled() override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override { return reinterpret_cast<const char*>(u8"选择"); }
    [[nodiscard]] SelectState state() const { return m_state; }
    [[nodiscard]] const QSet<QUuid>& selection() const { return m_selection; }

    /// Drop the current selection (used when the active canvas layer switches,
    /// since selection is scoped to the active layer).
    void clearSelectionOnLayerChange();

    /// External selection entry (面板联动): select + CONFIRM the given blocks.
    void selectBlocksExternally(const QList<QUuid>& blockIds);

    // ── 重叠线段消歧 (2026-10) ──
    /// 一个重叠候选: 拾取半径内、活动层上的一个线段块及其身份快照.
    /// 与 OverlapDisambiguationController::Candidate 同形 —— 公共 API 保留
    /// OverlaoCandidate 字段, 测试与外部消费者不变 (阶段 3 桥接)。
    using OverlapCandidate = OverlapDisambiguationController::Candidate;
    /// 当前激活的候选列表 (点选集群后快照; 空 = 未激活 W 循环上下文).
    [[nodiscard]] const QList<OverlapCandidate>& overlapCandidates() const;
    /// 当前循环索引 (-1 = 未激活).
    [[nodiscard]] int overlapIndex() const;
    /// 画布 HUD 的重叠提示文本 (空 = 未显示).
    [[nodiscard]] QString overlapHintText() const;
    /// 命令式选中第 @p index 个候选 (W 循环与右键「重叠候选」菜单共用入口).
    void pickOverlapCandidate(int index);

    /// 移动当前选择集中的全部线段到指定图层 (通过 MoveBlocksToLayerCommand).
    void moveSelectionToLayer(const QUuid& targetLayerId);

private:
    /// 取消重叠循环上下文 (右键菜单 / 点级操作 / Esc 共用)。对控制器做 no-op
    /// 转发, 未激活时无副作用。
    void deactivateOverlapContext();
    // ── State transitions ──
    void setState(SelectState s);
    void clearSelectionAndIdle();

    // ── Selection mode (W toggle) ──
    void toggleSelectionMode();
    void showToast(const QString& text);  ///< Generic transient canvas toast.

    [[nodiscard]] ModeIndicator modeIndicator() const override;
    [[nodiscard]] static ModeIndicator modeIndicatorFor(SelectionMode mode,
                                                       int overlapIndex,
                                                       int overlapCount);

    // ── Selection management (add/remove, red highlight) ──
    void toggleBlock(const QUuid& blockId);
    void syncSelectionVisual();
    void setBlockHighlight(const QUuid& blockId, bool on);

    // ── Hit testing ──
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;
    [[nodiscard]] QUuid hitSegmentAt(const cad::geo::Vec2& worldPos) const;
    void notifyEditTarget();

    [[nodiscard]] bool tryPointOperation(const cad::geo::Vec2& pos);

    // ── Component / legacy helpers ──
    void createComponentFromSelection();
    void deleteSelectedBlocks();
    void quickDetachSelection();

    // ── Core state ──
    SelectState m_state = SelectState::Idle;
    SelectionMode m_selectionMode = SelectionMode::Single;

    // Persistent, toggleable selection (block ids).
    QSet<QUuid> m_selection;

    QUuid m_lastHitSegmentId;

    // ── Extracted gesture controllers (阶段 3 拆分, onActivate 时构造) ──
    SelectDragController*                m_dragCtl = nullptr;
    CurveAnchorDragSession*              m_anchorDrag = nullptr;
    OverlapDisambiguationController*     m_overlapCtl = nullptr;
    SelectHoverFeedback                  m_hoverCtl;  // 无外部依赖, 直接持有

    ConnectGesture*    m_connectGesture = nullptr;
    CopyDragController* m_copyDrag = nullptr;
    MarqueeGesture*    m_marqueeGesture = nullptr;

    // 桥接: 供提取控制器回调 ToolSelect 的受保护方法
    friend class SelectDragController;
    friend class CurveAnchorDragSession;
    friend class OverlapDisambiguationController;
};

} // namespace cad::tools
