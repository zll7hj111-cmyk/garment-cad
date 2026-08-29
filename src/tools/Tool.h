#pragma once

#include <QString>   // ToolHost::setHintOverride / Tool::reportHintOverride (M5)

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QUndoStack;
class QUuid;
class CanvasScene;
class HudItem;

namespace cad::param {
class ParamDocument;
enum class RotationMode;
}

namespace cad::tools {

/// Forward-declared (defined in ToolManager.h) — tools only pass it through
/// the host interface, they never build one.
enum class ToolType;

/// 宿主回调接口 (TOOL_SYSTEM_AUDIT P2, 2026-12): 工具不直接认识 ToolManager,
/// 需要"切工具 / 上报编辑目标"时经该接口回调 —— 与 InputDispatcher 同款依赖
/// 倒置。由 ToolManager 实现; 单测直驱工具时可不注入 (两方法均 no-op)。
class ToolHost
{
public:
    virtual ~ToolHost() = default;

    /// Request a tool switch (blank-space right-click gesture).
    virtual void requestToolSwitch(ToolType type) = 0;

    /// 覆盖状态栏操作提示 (M5, TOOL_SYSTEM_AUDIT): 工具内部有"静态元数据里
    /// 表达不了"的运行期状态时 (如智能笔的 直线/省道线 模式、旋转的确认门),
    /// 用它把当前状态写进提示 —— 状态栏因此成为模式的常驻指示器。
    /// 传空串 = 恢复 ToolDescriptor::hintText 的默认文案。
    ///
    /// 与 requestToolSwitch 不同, 这里**不是纯虚**: 提示是可选能力, 无头宿主
    /// (单测桩) 不实现也能编译 —— 忽略提示不会让任何契约失效。
    virtual void setHintOverride(const QString& hint) { (void)hint; }

    /// 上下文属性条 (CONTEXT_STRIP_DESIGN.md §2.2): 上报"当前关注的线段"。
    ///
    /// hover = 光标悬停候选 (条带只读预览); pinned = 点击锁定 (条带可编辑)。
    /// 两者 id 均为 null = 清除。**非虚** —— 与 setHintOverride 同款, 无头
    /// 单测桩不实现也能编译; 条带只是显示层, 忽略它不会让任何契约失效。
    /// 节流由条带自己负责, 工具可以每帧上报 hover。
    ///
    /// (原 setEditTarget 单选上报通道已被 pinned 取代 —— 同一个语义不该有
    ///  两条通道。)
    virtual void setHoverTarget(const QUuid& blockId, const QUuid& segmentId)
    { (void)blockId; (void)segmentId; }
    virtual void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId)
    { (void)blockId; (void)segmentId; }

    /// 连接手势角度会话 (CONTEXT_STRIP_DESIGN.md 二期): 条带进入"连接角度
    /// 编辑"模式 —— 锁定到新连接的跟随线段, 仅角度可编辑, 输入实时预览、
    /// Enter/Esc 经工具回报收尾。blockId/segmentId = 跟随线段 (条带显示),
    /// attachmentId = 正在调角度的附件, initialAngle = 保向初值 (度, 带符号
    /// 折角显示)。**四个参数全 null = 会话结束** (条带收起、恢复普通行为)。
    /// 非虚 —— 与 setHintOverride 同款, 无头单测桩不实现也能编译。
    virtual void setConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                        const QUuid& attachmentId, double initialAngle)
    { (void)blockId; (void)segmentId; (void)attachmentId; (void)initialAngle; }

    /// 连接角度输入的合法性 (二期): 公式解析失败时条带角度框给红边提示;
    /// 合法时恢复。非虚 (同上)。
    virtual void setConnectAngleValidity(bool valid) { (void)valid; }

    /// 旋转工具的锚心状态 (2026-12): 旋转会话激活时, 条带的「换向」按钮
    /// 转义为「切换锚心」—— 条带需要知道锚心在哪端 (基准读数锚心端在前)、
    /// 能否切换 (Ready 且端点无连接) 以及禁用原因。active=false 且其余全空 =
    /// 会话结束 (条带恢复普通换向语义)。非虚 (无头单测桩不实现也能编译)。
    virtual void setRotateAnchorState(bool active, bool anchorIsEnd, bool canToggle,
                                      const QString& reason)
    { (void)active; (void)anchorIsEnd; (void)canToggle; (void)reason; }
};

/// 工具内部"模式"的显示描述 (2026-08-29: W 键模式切换的持久化显示标识)。
///
/// W 是各工具统一的模式切换 leader key (不用 Tab —— Tab 是焦点导航键),
/// 但**它的语义随工具状态变化**: 选择工具在重叠候选激活时 W = 循环候选、
/// 智能笔在 Drawing 态同理, 都不是"切模式"。所以本结构必须由 **{mode,
/// state}** 共同决定 —— 只跟 mode 走会在这些场合给出「W 切单选/多选」
/// 这类**错误指引**。
///
/// 三处消费方构成分层显示, 各管一段、互不替代:
///   · hint()    → 状态栏 L1 (持久): 现在是什么 + 此刻按 W 会发生什么
///   · modeName  → 画布角标 L2 (持久, 仅非默认态): 下一次点击的语义
///   · toast     → 瞬时 L3 (1.4s): 刚刚变成什么了
/// 三层**不可互相替代**: 持久层答不了"三态循环跳到第几个", 瞬时层答不了
/// "现在是哪个模式" (1.4 秒后就消失了)。modeName 被 hint() 与角标共用,
/// 两处的模式名不可能漂移。
struct ModeIndicator
{
    QString modeName;  ///< 模式短名 ("多选" / "水平"); 角标与方括号共用
    QString detail;    ///< 当前模式的操作说明
    QString wAction;   ///< 此刻按 W 会发生什么; 空 = 该上下文 W 无效
    QString toast;     ///< 切换瞬间的画布提示; 空 = 不发 toast

    /// true = 默认态 → **画布角标不显示**。默认态不需要提示, 需要提示的是
    /// "我切到了非常驻态", 所以角标只在非常驻态占像素 (默认零成本)。
    /// 注意与"该模式没有角标"区分: 画布上已有更详细常驻 HUD 的场合
    /// (如选择工具的重叠候选循环) 也置 true —— 再挂一个角标是重复打扰。
    bool isDefault = false;

    /// 状态栏整句: "选择[多选]：detail | wAction"。
    /// detail 与 wAction 都空 → 返回空串 (= 恢复 ToolDescriptor::hintText)。
    [[nodiscard]] QString hint(const char* toolName) const;
};

/// 激活时注入的工具上下文 (TOOL_SYSTEM_AUDIT P2): 替代旧的三处散落 setter
/// (setUndoStack / setToolSwitchRequest / setEditTargetCallback), 一次绑定
/// 全部外部依赖。ToolManager 每次激活时构造; 单测可只填 scene + paramDoc。
struct ToolContext
{
    CanvasScene* scene = nullptr;
    cad::param::ParamDocument* paramDoc = nullptr;
    QUndoStack* undoStack = nullptr;
    ToolHost* host = nullptr;
};

/// Abstract base class for all interactive tools.
class Tool
{
public:
    virtual ~Tool() = default;

    /// 非虚生命周期入口 (TOOL_SYSTEM_AUDIT H4/P2): 绑定上下文 → onActivate 钩子。
    /// 派生类禁止覆盖 activate/deactivate —— 只实现 onActivate/onDeactivate,
    /// "必须调基类"由编译器保证 (旧 5 个工具遮蔽 m_scene/m_paramDoc 且忘记
    /// 调基类的根源; 基类上下文曾是摆设)。
    void activate(const ToolContext& ctx);

    /// 便捷重载: 只绑定 scene/doc, undo/host 保持既有值 (单测直驱工具用;
    /// 与旧 Tool::activate 只碰 scene/doc 的语义一致)。
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc);

    /// 非虚: onDeactivate() → releaseHud() → 清空上下文, 顺序唯一。
    void deactivate();

    /// Set the undo stack for command-based tools. Live-update path used by
    /// ToolManager::setUndoStack; activation-time binding goes through
    /// ToolContext.undoStack.
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// Mouse events in scene coordinates.
    virtual void mousePress(QGraphicsSceneMouseEvent* event) = 0;
    virtual void mouseMove(QGraphicsSceneMouseEvent* event) = 0;
    virtual void mouseRelease(QGraphicsSceneMouseEvent* event) = 0;
    virtual void mouseDoubleClick(QGraphicsSceneMouseEvent* event) { (void)event; }

    /// Keyboard events.
    virtual void keyPress(QKeyEvent* event) { (void)event; }
    virtual void keyRelease(QKeyEvent* event) { (void)event; }

    // ── 连接手势角度会话输入 (二期, 上下文属性条 → 工具) ──
    // 条带是纯输入面, 连接语义 (预览/换算/收尾) 全部留在工具的会话里。
    // 默认 no-op; 选择工具 (ToolSelect) 转交 ConnectGesture。MainWindow →
    // ToolManager::forward* → 本组虚函数。
    /// 角度输入框击键 (全文; 实时预览, 每键一报)。
    virtual void connectAngleTextChanged(const QString& text) { (void)text; }
    /// ° / ⌒ 单位切换。
    virtual void connectAngleModeChanged(cad::param::RotationMode mode) { (void)mode; }
    /// Enter: 确认角度并收尾 (finalize)。
    virtual void connectAngleCommitted() {}
    /// Esc: 保留连接、角度回退初值并收尾 (与旧 HUD 的 Esc 同语义)。
    virtual void connectAngleCancelled() {}

    // ── 条带「换向」点击 (2026-12, 旋转会话内) ──
    /// 旋转工具激活时, 条带换向按钮 = 切换锚心 (起点 ↔ 终点)。ContextStrip
    /// 仅在旋转会话内转发此请求 (普通换向仍由条带直接 push 命令)。
    /// 默认 no-op —— 只有 ToolRotate 覆盖。MainWindow → ToolManager::
    /// forwardReverseRequest → 本虚函数。
    virtual void onReverseRequested(const QUuid& blockId, const QUuid& segmentId)
    { (void)blockId; (void)segmentId; }

    /// Tool display name.
    [[nodiscard]] virtual const char* name() const = 0;

protected:
    /// 派生工具的状态重置 / 协作对象重建入口。activate 已把上下文绑定到
    /// m_scene / m_paramDoc / m_undoStack / m_host 之后才调用 —— 钩子内
    /// 可直接使用这些成员 (旧实现里靠手工绑定/遮蔽才能用)。
    virtual void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) { (void)scene; (void)paramDoc; }

    /// 派生工具的清理入口。先于 releaseHud / 上下文清空调用, m_scene 仍有效
    /// (与旧"清理完再调基类"的约定同序, 由编译器保证)。
    virtual void onDeactivate() {}

    /// Fire the switch request through the host (no-op when not injected).
    void requestToolSwitch(ToolType type)
    {
        if (m_host) m_host->requestToolSwitch(type);
    }

    /// 上报悬停候选 (上下文属性条): 两个 id 均 null = 光标移出所有线段。
    /// 无宿主注入时是 no-op (单测直驱工具的场合)。
    void reportHoverTarget(const QUuid& blockId, const QUuid& segmentId)
    {
        if (m_host) m_host->setHoverTarget(blockId, segmentId);
    }
    /// 上报锁定焦点 (上下文属性条): 点击选中 / 创建完成; 均 null = 解除锁定。
    void reportPinnedTarget(const QUuid& blockId, const QUuid& segmentId)
    {
        if (m_host) m_host->setPinnedTarget(blockId, segmentId);
    }

    /// 上报连接角度会话 (二期): 四个参数全 null = 会话结束。
    void reportConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                   const QUuid& attachmentId, double initialAngle)
    {
        if (m_host) m_host->setConnectAngleSession(blockId, segmentId,
                                                   attachmentId, initialAngle);
    }
    /// 上报连接角度输入的合法性 (二期, 红边提示用)。
    void reportConnectAngleValidity(bool valid)
    {
        if (m_host) m_host->setConnectAngleValidity(valid);
    }

    /// 覆盖状态栏提示 (M5): 运行期状态变化时通知宿主; 传空串恢复默认文案。
    /// 无宿主注入时是 no-op (单测直驱工具的场合)。
    void reportHintOverride(const QString& hint)
    {
        if (m_host) m_host->setHintOverride(hint);
    }

    /// 当前模式的显示描述 —— 由 **{mode, state}** 共同决定, 理由见
    /// ModeIndicator 注释 (W 的语义随状态变化)。无内部模式的工具 (打断等)
    /// 不覆盖: 基类返回空 → hint() 得空串 → 状态栏走静态默认文案。
    [[nodiscard]] virtual ModeIndicator modeIndicator() const { return {}; }

    /// 刷新持久层显示 (L1 状态栏; L2 画布角标 P2 落地后一并刷新)。
    /// 模式**或工具状态**发生变化后调用 —— 只改状态没改模式的场合
    /// (进入/退出重叠候选上下文) 文案同样要跟着变。
    void refreshModeIndicator();

    /// 切换瞬间 (L3): 发瞬时 toast, 再刷新持久层。W 键处理走这个 ——
    /// 持久层答不了"三态循环刚跳到第几个", 这一下必须靠 toast。
    void announceModeChange();

    /// Lazily create (and add to the scene) the persistent cursor HUD.
    /// Safe to call repeatedly — the HUD is created once.
    HudItem* ensureHud();

    /// Remove and delete the HUD from the scene (idempotent).
    void releaseHud();

    QUndoStack* m_undoStack = nullptr;

    /// Bound context (set by activate; cleared by deactivate).
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    ToolHost* m_host = nullptr;
    HudItem* m_hud = nullptr;  ///< Persistent cursor-following HUD label (canvas/HudItem.h, global ns).
};

} // namespace cad::tools
