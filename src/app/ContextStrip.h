#pragma once

#include <QWidget>
#include <QUuid>

#include "document/commands/BlockCommands.h"   // SegmentEditBarCommand::State

class ElaText;
class ElaLineEdit;
class ElaPushButton;
class QTimer;
class QUndoStack;

namespace cad::param {
class ParamDocument;
struct Attachment;
enum class RotationMode;
}

namespace cad::app {

/// 上下文属性条焦点三态 (CONTEXT_STRIP_DESIGN.md §2.1)。
enum class StripFocus {
    Empty,   ///< 无焦点: 条带隐藏.
    Hover,   ///< 悬停预览: 字段只读, 虚线描边.
    Pinned,  ///< 已锁定: 字段可编辑, accent 实线描边.
};

/// 画布下方常驻的"当前关注线段"属性带 (CONTEXT_STRIP_DESIGN.md)。
///
/// 单一焦点来源: 工具经 ToolHost 上报 hover / pinned 目标, 条带只负责显示与
/// 编辑。取代原来的 SegmentEditBar + SmartPenPreInputBar 两条互斥 bar, 并让
/// 旋转工具不再需要浮动 AngleHud (一期)。
///
///   Empty ──悬停──> Hover ──点击──> Pinned
///     ↑               │              │
///     └───移出────────┘   Esc/删除───┘
///
/// **Pinned 优先于 Hover**: 锁定后鼠标划过别的线不抢显示 —— 否则点中一条准备
/// 改角度、手一抖内容就被覆盖。智能笔从不锁定 (点击 = 画线), 永远由 hover 驱动。
class ContextStrip : public QWidget
{
    Q_OBJECT

public:
    explicit ContextStrip(cad::param::ParamDocument* paramDoc, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack);
    /// 焦点回落到画布的落点 (Enter 走完最后一个字段 / Esc 解除锁定时用)。
    void setCanvasView(QWidget* canvasView);

    // ── 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期) ──
    /// 进入连接手势的角度编辑会话: 锁定到新连接的跟随线段, 仅角度可编辑,
    /// 击键/单位切换/Enter/Esc 经信号回传宿主 (→ 工具 → ConnectGesture)。
    /// attachmentId 为空 = 会话结束 (条带收起、焦点回画布)。
    void beginConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                  const QUuid& attachmentId, double initialAngle);
    /// 退出连接角度会话 (收尾/取消后由宿主调用)。
    void endConnectAngleSession();
    /// 输入合法性 (公式解析失败 → 角度框红边; 合法恢复)。
    void setConnectAngleValid(bool valid);
    /// 会话是否激活 (测试/宿主判据)。
    [[nodiscard]] bool connectSession() const { return m_connectSession; }

    // ── 旋转工具锚心 (2026-12): 换向按钮在旋转会话内 = 切换锚心 ──
    /// 旋转工具上报锚心状态: active = 旋转会话激活 (条带基准读数显示锚心端在
    /// 前); anchorIsEnd = 锚在终点; canToggle = 当前可切换锚心 (Ready 且端点
    /// 无连接); reason = 禁用原因。全 false + 空串 = 会话结束。
    void setRotateAnchorState(bool active, bool anchorIsEnd, bool canToggle,
                              const QString& reason);

    // ── 焦点上报 (MainWindow 经 ToolHost 转发) ──
    /// 悬停候选: 节流 80ms 后生效; 任一输入框聚焦中直接忽略 (焦点保护)。
    /// 两个 id 均为 null = 移出。Pinned 态下不改变显示。
    void setHoverTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 锁定焦点 (两个 id 均为 null = 解除锁定)。
    void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId,
                         bool grabFocus = false);
    /// 悬停移出 (Pinned 态下 no-op —— 不抢显示)。
    void clearHover();
    /// 解除锁定: 退回 hover 候选 (有) 或 Empty。
    void clearPinned();
    /// 创建后编辑: 锁定并记住 undo 起点 —— Esc = 撤销创建 (原 SegmentEditBar 语义)。
    void pinCreatedLine(const QUuid& blockId, const QUuid& segmentId,
                        bool grabFocus = true);
    /// 画线过程中的只读读数 (0,0 = 收起)。显示的是"正在画的那条线"。
    void showStrokePreview(double lenCm, double angleDeg);
    void hideBar();
    /// 创建后 Esc 的收口: 回退 undo 栈到创建点 —— 连同创建命令与之后的
    /// 条带编辑一起撤销(线消失), 然后隐藏条带。由宿主接 cancelRequested
    /// 后调用 (撤销栈归宿主所有)。
    void cancelCreation();

    [[nodiscard]] StripFocus focusState() const { return m_focus; }
    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] QUuid segmentId() const { return m_segmentId; }
    /// 字段当前是否只读 (Hover / 画线预览 = true)。
    [[nodiscard]] bool readOnly() const;

    // ── 控件访问 (测试与宿主) ──
    [[nodiscard]] ElaLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] ElaLineEdit* lengthEdit() const { return m_lenEdit; }
    [[nodiscard]] ElaLineEdit* angleEdit() const { return m_angleEdit; }
    [[nodiscard]] ElaPushButton* reverseButton() const { return m_btnReverse; }
    [[nodiscard]] ElaPushButton* unitAngleButton() const { return m_btnUnitAngle; }
    [[nodiscard]] ElaPushButton* unitArcButton() const { return m_btnUnitArc; }
    /// 状态徽标文本 (跟随 L5 / 自由 / 曲线 / 桥线)。
    [[nodiscard]] QString badgeText() const;
    /// 角度基准读数 (P1 → P2)。
    [[nodiscard]] QString basisText() const;

signals:
    /// Esc 由"创建后锁定"触发: 宿主撤销创建命令 (删线)。
    void cancelRequested();

    // ── 旋转会话换向 (2026-12): 旋转工具激活时, 换向 = 切换锚心 ──
    /// 换向按钮点击 (仅旋转会话内发出): 宿主转交激活工具 (ToolRotate 切锚心)。
    /// 普通 (非旋转) 会话的换向仍由条带直接推 ReverseSegmentCommand。
    void reverseRequested(const QUuid& blockId, const QUuid& segmentId);

    // ── 连接角度会话输入 (二期: 条带 → 宿主 → 工具 → ConnectGesture) ──
    /// 角度输入框击键 (全文, 实时预览)。
    void connectAngleTextChanged(const QString& text);
    /// ° / ⌒ 单位切换。
    void connectAngleModeChanged(cad::param::RotationMode mode);
    /// Enter: 确认角度并收尾 (finalize)。
    void connectAngleCommitted();
    /// Esc: 保留连接、角度回退初值并收尾。
    void connectAngleCancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void applyName();
    void applyLength();
    void applyAngle();
    /// 从模型回填全部字段; 聚焦中的字段跳过 (焦点保护, 防输入被打断)。
    void refreshFields();
    /// 回填与焦点无关的外围件: 徽标 / 基准 / 单位段 / 换向资格 / 描边 / 提示。
    void refreshChrome();
    void setReadOnlyFields(bool readOnly);
    void commitState(cad::cmd::SegmentEditBarCommand::State st);
    [[nodiscard]] cad::cmd::SegmentEditBarCommand::State snapshotState() const;
    /// 驱动本段角度的跟随连接 (匹配规则同属性对话框: 挂在本段起点或终点)。
    [[nodiscard]] const cad::param::Attachment* findEditAttachment() const;
    /// 弧长模式的显示值 = 带符号折角弧长 (cm): 多圈弧长先落到 ±180° 侧再
    /// 换算 (2026-08 v3 定稿, 同旧旋转 HUD currentModeValue)。
    [[nodiscard]] QString foldedArcDisplay(const cad::param::Attachment* att) const;
    /// 节流到期: 真正应用待定的悬停候选。
    void flushHover();
    [[nodiscard]] bool inputHasFocus() const;
    void onUnitToggled(bool wantArc);
    void onReverseClicked();
    void returnFocusToCanvas();

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QWidget*    m_canvasView = nullptr;

    StripFocus m_focus = StripFocus::Empty;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_hoverBlock;    ///< 待定悬停候选 (节流窗口内)。
    QUuid m_hoverSegment;
    bool  m_creationPinned = false;  ///< 由创建触发的锁定 (Esc = 撤销创建)。
    bool  m_strokePreview = false;   ///< 画线中读数 (非线段焦点)。
    bool  m_connectSession = false;  ///< 连接角度会话激活 (二期, 角度直写附件).
    QUuid m_connectAttId;            ///< 会话正在调角度的附件.
    double m_connectInitialAngle = 0.0;  ///< 保向初值 (Esc 回退用, 宿主侧持).
    int   m_editStartIndex = 0;      ///< 创建命令所在 undo 位置 (cancelCreation 回到此)。

    // ── 旋转工具锚心会话 (2026-12): 换向按钮转义为"切换锚心" ──
    bool    m_rotateSession = false;    ///< 旋转会话激活 (条带被旋转工具锁定).
    bool    m_rotateAnchorIsEnd = false;///< 锚心在终点 (基准读数锚心端在前).
    bool    m_rotateCanToggle = false;  ///< 当前可切换锚心.
    QString m_rotateReason;             ///< 锚心切换禁用原因 (tooltip).

    ElaText*       m_idLabel = nullptr;
    ElaLineEdit*   m_nameEdit = nullptr;
    ElaLineEdit*   m_lenEdit = nullptr;
    ElaLineEdit*   m_angleEdit = nullptr;
    ElaPushButton* m_btnUnitAngle = nullptr;
    ElaPushButton* m_btnUnitArc = nullptr;
    ElaPushButton* m_btnReverse = nullptr;
    ElaPushButton* m_btnBasis = nullptr;
    ElaText*       m_badge = nullptr;
    ElaText*       m_hint = nullptr;
    QTimer*        m_debounce = nullptr;
    QTimer*        m_hoverTimer = nullptr;
};

} // namespace cad::app
