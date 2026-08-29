#pragma once

#include <QString>   // ToolHost::setHintOverride / Tool::reportHintOverride (M5)

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QUndoStack;
class QUuid;
class CanvasScene;
class HudItem;

namespace cad::param { class ParamDocument; }

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

    /// Report the single-selection edit target (status-bar strip). Both ids
    /// null = clear. Only ToolSelect fires this.
    virtual void setEditTarget(const QUuid& blockId, const QUuid& segmentId) = 0;

    /// 覆盖状态栏操作提示 (M5, TOOL_SYSTEM_AUDIT): 工具内部有"静态元数据里
    /// 表达不了"的运行期状态时 (如智能笔的 直线/省道线 模式), 用它把当前
    /// 状态写进提示 —— 状态栏因此成为模式的常驻指示器。
    /// 传空串 = 恢复 ToolDescriptor::hintText 的默认文案。
    ///
    /// 与上面两个方法不同, 这里**不是纯虚**: 提示是可选能力, 无头宿主
    /// (单测桩) 不实现也能编译 —— 忽略提示不会让任何契约失效。
    virtual void setHintOverride(const QString& hint) { (void)hint; }
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

    /// Report the single-selection edit target through the host (no-op when
    /// not injected). Only ToolSelect uses this.
    void reportEditTarget(const QUuid& blockId, const QUuid& segmentId)
    {
        if (m_host) m_host->setEditTarget(blockId, segmentId);
    }

    /// 覆盖状态栏提示 (M5): 运行期状态变化时通知宿主; 传空串恢复默认文案。
    /// 无宿主注入时是 no-op (单测直驱工具的场合)。
    void reportHintOverride(const QString& hint)
    {
        if (m_host) m_host->setHintOverride(hint);
    }

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
