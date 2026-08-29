#pragma once

#include <QUuid>
#include <QList>
#include <QHash>
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

class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
class HudItem;

namespace cad::tools {

class ConnectGesture;
class CopyDragController;
class MarqueeGesture;

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
    // 无自定义析构: 全部清理都在 onDeactivate() (重叠提示图元 / 三个手势
    // 协作对象)。ToolManager 析构时会先 deactivate 激活工具, 保证这条路径
    // 对"退出时仍在激活的那个工具"也成立 (N5)。

    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

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
    struct OverlapCandidate {
        QUuid blockId;
        QUuid segmentId;    ///< 块内最近线段 (仅端点命中时可能为空).
        QString name;       ///< 显示名: 线段名 → 块名 → 线段 serial.
        QString layerName;
        QString roleText;   ///< 轮廓线 / 内部线 / 辅助线.
        double  lengthMm = 0.0;
    };
    /// 当前激活的候选列表 (点选集群后快照; 空 = 未激活 W 循环上下文).
    [[nodiscard]] const QList<OverlapCandidate>& overlapCandidates() const { return m_overlapCandidates; }
    /// 当前循环索引 (-1 = 未激活).
    [[nodiscard]] int overlapIndex() const { return m_overlapIndex; }
    /// 画布 HUD 的重叠提示文本 (空 = 未显示).
    [[nodiscard]] QString overlapHintText() const { return m_overlapHitText; }
    /// 命令式选中第 @p index 个候选 (W 循环与右键「重叠候选」菜单共用入口).
    void pickOverlapCandidate(int index);

private:
    // ── State transitions ──
    void setState(SelectState s);
    void clearSelectionAndIdle();

    // ── Selection mode (W toggle) ──
    void toggleSelectionMode();
    void showModeToast();   ///< Transient canvas toast showing the active mode.
    void showToast(const QString& text);  ///< Generic transient canvas toast.

    // ── Selection management (add/remove, red highlight) ──
    void toggleBlock(const QUuid& blockId);
    void syncSelectionVisual();              ///< Apply m_selection → BlockItem flags.
    void setBlockHighlight(const QUuid& blockId, bool on);

    // ── Drag (move selection, anchored at press point) ──
    void beginDrag(const cad::geo::Vec2& pos);
    void updateDrag(const cad::geo::Vec2& pos);
    void endDrag(const cad::geo::Vec2& pos);
    /// 拖拽释放时若满足“解焊后拖到新位置点”, 执行位置重挂 + 角度基准保留。
    [[nodiscard]] bool tryReattachOnDragEnd(const cad::geo::Vec2& pos);

    // ── Hit testing ──
    /// Block (segment) under worldPos via scene hit-testing.
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;

    /// Segment under worldPos (segment-level hit, active layer only).
    [[nodiscard]] QUuid hitSegmentAt(const cad::geo::Vec2& worldPos) const;

    /// Re-emit the edit target from the current selection (single → the last
    /// clicked segment, else clear).
    void notifyEditTarget();

    /// Point-level press dispatch (曲线锚点=曲线点拖动, 端点=连接拖拽): runs
    /// against the CURRENT (already selected) set and consumes the press when
    /// a point operation starts. Multi-select members connect individually —
    /// a press on ANY selected member's endpoint starts that block's
    /// connection gesture (多选成员逐块连接). Bridges fall through (they
    /// cannot connect — the caller then starts a plain drag). Used by every
    /// Idle/Selecting press (2026-09 取消确认基准), so a single gesture
    /// covers 选中+操作.
    ///
    /// 重叠点源点规则 (用户设计提案 2026-09): 单选模式下「先点选线段、再点击
    /// 重叠部位」= 连接源点恒取当前选中线段的所属点 —— 候选点先按"选中块"
    /// 过滤 (未选线段的重叠点直接排除), 多个已选块端点同点重叠才进
    /// ConfirmSource 点选候选线段确认.
    [[nodiscard]] bool tryPointOperation(const cad::geo::Vec2& pos);

    // ── Component (组件) ──
    /// 右键「创建组件」: group the current multi-selection into a rigid
    /// component (anchor = earliest selected block in document order).
    void createComponentFromSelection();

    // ── Legacy helpers ──
    void deleteSelectedBlocks();

    /// 快拆 (D 键, 用户拍板 2026-09): 解除选中线连接的位置吸附、保留角度
    /// 跟随 (angleOnly)。经 undo 栈 (SetAttachmentAngleOnlyCommand); 无连接
    /// 或已仅角度时 toast 提示; 桥线 pin 无角度语义, 跳过。
    void quickDetachSelection();

    // ── 长按/拖动判定与悬停反馈 (2026-09 取消确认基准) ──
    /// press 线身后进入待定: 移动超阈值由 mouseMove 判定进入 beginDrag
    /// (锚点 = press 位置); release 未触发 = 单击语义.
    /// @param wasSelected 两重含义 —— ①多选下 release 未拖动 = 减选;
    ///        ②M6 拖动阈值档位: 已选中的块用更宽的 kDragThresholdSelectedPx
    ///        (press 已选中 = 意图就是拖, 但误拖会静默改几何, 代价更高)。
    void beginPressPending(const cad::geo::Vec2& pos, const QUuid& blockId,
                           bool wasSelected);
    void cancelPressPending();
    /// 无按钮悬停: 已选块端点 (kConnectGrabRadius 内) = 十字 (可连接),
    /// 线身 = 抓手 (可拖动), 空白 = 箭头. 同值短路.
    void updateHoverCursor(const cad::geo::Vec2& pos);
    /// 该块是否有可捕捉端点在 worldRadius 内 (悬停端点判定).
    [[nodiscard]] bool blockHasEndpointNear(const QUuid& blockId,
                                            const cad::geo::Vec2& pos,
                                            double worldRadius) const;

    // ── 重叠线段消歧 (2026-10: 悬停提示 → 点选+W 循环 → 右键候选菜单) ──
    /// 收集拾取半径内、活动层的全部线段块候选 (场景堆叠序, 顶部优先 ——
    /// 与 hitBlock 的首选一致; 完全重合的线上层/后建者在前).
    [[nodiscard]] QList<OverlapCandidate> collectOverlapCandidates(
        const cad::geo::Vec2& worldPos) const;
    /// 由块+段构造候选身份快照 (名称/层名/角色/长度; 随时可重取).
    [[nodiscard]] OverlapCandidate makeOverlapCandidate(
        const cad::param::Block& blk, const QUuid& segmentId) const;
    /// 点选集群后激活循环上下文: 快照候选 + 选中与 hitBlock 一致的首项 + 常驻 HUD.
    void activateOverlapContext(const QList<OverlapCandidate>& cands,
                                const QUuid& hitBlockId,
                                const cad::geo::Vec2& anchor);
    /// 选中候选 @p index 并同步选择态/编辑目标 (W 循环与候选菜单共用).
    void applyOverlapPick(int index);
    /// 退出循环上下文 (清列表/索引/隐藏 HUD).
    void deactivateOverlapContext();
    /// W 循环到下一候选 (剔除已消失的块, 逐位回绕).
    void cycleOverlapCandidate();
    /// 悬停/循环 HUD 刷新: 无上下文 = 集群提示; 有上下文 = 循环状态 (锚定集群).
    /// @p precomputed: 调用方已持有同帧命中结果时直接传入 (悬停路径
    /// 单次扫描, P1/M7); 空则内部现查。
    void refreshOverlapHint(const cad::geo::Vec2& worldPos,
                            const QList<OverlapCandidate>* precomputed = nullptr);
    /// HUD 呈现 (同值短路 + 锚点; 惰性创建 pill).
    void showOverlapHint(const QString& text, const cad::geo::Vec2& anchor);
    void hideOverlapHint();

    // ── Core state ──
    SelectState m_state = SelectState::Idle;
    SelectionMode m_selectionMode = SelectionMode::Single;

    // Persistent, toggleable selection (block ids).
    QSet<QUuid> m_selection;      ///< Currently selected blocks (red + bold).

    /// Last clicked segment (segment-level edit target for the status bar).
    QUuid m_lastHitSegmentId;

    // Drag state
    cad::geo::Vec2 m_dragStartPos;             ///< Anchor (blank-space press point).
    QList<QUuid>   m_dragBlockIds;
    QHash<QUuid, cad::geo::Vec2> m_dragOrigins;
    QList<QUuid>   m_detachedAttachments;      ///< Cross-boundary attachments to break.
    /// 滑轨模式 (抽屉式滑动): attachmentId → pre-drag (slideAlongMm, slidePerpMm)
    /// snapshot for slide attachments whose FOLLOWER is inside the drag set.
    QHash<QUuid, std::pair<double, double>> m_dragOldSlideOffsets;
    SnapEngine m_snapEngine;  ///< 拖拽释放时检测“重新挂接”目标点.

    // Extracted gestures (connection + quick copy + marquee)
    ConnectGesture*    m_connectGesture = nullptr;
    CopyDragController* m_copyDrag = nullptr;
    MarqueeGesture*    m_marqueeGesture = nullptr;

    // ── Curve anchor dragging (overrides normal state when active) ──
    bool    m_anchorDragging = false;
    QUuid   m_anchorBlockId;
    QUuid   m_anchorPointId;
    cad::geo::Vec2 m_anchorOrigPos;  ///< Original freePos before drag.

    /// Check if worldPos is near a pass point of a selected curve block.
    /// Returns the point ID if found, null otherwise.
    /// Nearest curve pass-point within 10px among the SELECTED blocks; returns
    /// {blockId, pointId} so multi-select drags know which block owns the hit.
    [[nodiscard]] std::optional<std::pair<QUuid, QUuid>> hitCurveAnchor(
        const cad::geo::Vec2& worldPos, double zoom) const;
    void beginAnchorDrag(const QUuid& blockId, const QUuid& pointId);
    void updateAnchorDrag(const cad::geo::Vec2& worldPos);
    void endAnchorDrag();

    // ── Press-pending (长按拖动判定) + hover cursor state ──
    bool    m_pressPending = false;        ///< press 线身后待定 (移动>阈值→拖).
    QUuid   m_pressBlockId;                ///< press 命中的块.
    bool    m_pressWasSelected = false;    ///< press 时块已在选择集 (多选 release 减选判定).
    cad::geo::Vec2 m_pressPos;             ///< press 位置 (拖动锚点).
    Qt::CursorShape m_hoverCursor = Qt::ArrowCursor;  ///< 悬停光标 (同值短路).

    // ── 重叠线段状态 (2026-10) ──
    QList<OverlapCandidate> m_overlapCandidates;  ///< 激活时的候选快照 (堆叠序).
    int m_overlapIndex = -1;                      ///< -1 = 未激活循环上下文.
    cad::geo::Vec2 m_overlapAnchor;               ///< HUD 锚点 (用户坐标).
    /// 重叠提示 = 共享 HudItem (DarkPill, 1/zoom 补偿; TOOL_SYSTEM_AUDIT
    /// P1/M1 收口 —— 原 QGraphicsRectItem+TextItem 手搭对不补偿缩放且
    /// 每次切工具泄漏一对, P0 已加析构兜底, 现按需创建/常驻复用).
    HudItem* m_overlapHint = nullptr;
    QString m_overlapHitText;                     ///< 当前 HUD 文本 (同值短路).
};

} // namespace cad::tools
