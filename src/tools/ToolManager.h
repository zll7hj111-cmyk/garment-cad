#pragma once

#include <QObject>
#include <QMetaObject>
#include <map>
#include <memory>

#include "ToolRegistry.h"
#include "canvas/InputDispatcher.h"

class CanvasScene;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QUndoStack;

namespace cad::param { class ParamDocument; enum class RotationMode; }

namespace cad::tools {

class Tool;

/// Manages tool lifecycle and dispatches input events to the active tool.
///
/// Implements cad::canvas::InputDispatcher (P1-6) so the canvas can forward
/// input WITHOUT including any tools/ header: the interface is declared in the
/// canvas layer and implemented here, inverting what used to be an upward
/// canvas → tools dependency. Implements cad::tools::ToolHost (P2) so tools
/// can request switches / report edit targets without knowing the manager.
///
/// Lifecycle (TOOL_SYSTEM_AUDIT P2): every tool instance is created once and
/// kept alive (m_tools). Switching only deactivates the old / activates the
/// new one; re-clicking the CURRENT tool is a no-op (L5) — sessions survive
/// an accidental double-click instead of being rebuilt and cleared.
class ToolManager : public QObject, public cad::canvas::InputDispatcher, public ToolHost
{
    Q_OBJECT

public:
    explicit ToolManager(CanvasScene* scene, QObject* parent = nullptr);
    ~ToolManager() override;

    /// Inject the document / undo stack. The default tool is activated in the
    /// constructor (before injection), so these re-bind the active tool —
    /// otherwise it would keep operating on null pointers until the user
    /// switches tools manually.
    void setParamDocument(cad::param::ParamDocument* paramDoc);
    void setUndoStack(QUndoStack* stack);

    /// Switch to a tool by type. Same-type re-click is a no-op (L5).
    void switchTool(ToolType type);

    [[nodiscard]] Tool* activeTool() const { return m_activeTool; }
    [[nodiscard]] ToolType activeToolType() const { return m_activeType; }

    // cad::tools::ToolHost
    void requestToolSwitch(ToolType type) override;
    void setHintOverride(const QString& hint) override;
    void setHoverTarget(const QUuid& blockId, const QUuid& segmentId) override;
    void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId) override;
    void setConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                const QUuid& attachmentId, double initialAngle) override;
    void setConnectAngleValidity(bool valid) override;
    void setRotateAnchorState(bool active, bool anchorIsEnd, bool canToggle,
                              const QString& reason) override;

    // ── 连接角度会话输入转发 (二期): MainWindow 接条带信号后调用, 转给激活
    //    工具 (选择工具 → ConnectGesture)。无激活工具时 no-op。 ──
    void forwardConnectAngleText(const QString& text);
    void forwardConnectAngleMode(cad::param::RotationMode mode);
    void forwardConnectAngleCommit();
    void forwardConnectAngleCancel();

    // ── 旋转会话换向转发 (2026-12): 条带换向点击 (旋转会话内) → 激活工具
    //    (ToolRotate 切锚心)。无激活工具时 no-op。 ──
    void forwardReverseRequest(const QUuid& blockId, const QUuid& segmentId);

    // Event dispatch (called by CanvasView through InputDispatcher)
    void dispatchMousePress(QGraphicsSceneMouseEvent* event) override;
    void dispatchMouseMove(QGraphicsSceneMouseEvent* event) override;
    void dispatchMouseRelease(QGraphicsSceneMouseEvent* event) override;
    void dispatchMouseDoubleClick(QGraphicsSceneMouseEvent* event) override;
    void dispatchKeyPress(QKeyEvent* event) override;
    void dispatchKeyRelease(QKeyEvent* event) override;

    [[nodiscard]] CanvasScene* scene() const { return m_scene; }

signals:
    void activeToolChanged(ToolType type, const char* toolName);

    /// 活动工具的运行期提示覆盖 (M5): 非空 = 用这段文案替掉
    /// ToolDescriptor::hintText; 空 = 恢复默认。切换工具时宿主须自行清掉
    /// (见 MainWindow::onToolChanged) —— 覆盖不跨工具继承。
    void hintOverrideChanged(const QString& hint);

    /// 上下文属性条焦点 (CONTEXT_STRIP_DESIGN.md): 工具经 ToolHost 上报后
    /// 转发成真实信号。hover = 悬停候选 (只读预览), pinned = 锁定 (可编辑);
    /// 两者 id 均为 null = 清除。
    void hoverTargetChanged(const QUuid& blockId, const QUuid& segmentId);
    void pinnedTargetChanged(const QUuid& blockId, const QUuid& segmentId);

    /// 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期): 非空 attachmentId =
    /// 会话开始 (条带进入连接角度编辑), 全 null = 会话结束。宿主编排
    /// ContextStrip::beginConnectAngleSession / endConnectAngleSession。
    void connectAngleSessionChanged(const QUuid& blockId, const QUuid& segmentId,
                                    const QUuid& attachmentId, double initialAngle);
    /// 连接角度输入的合法性 (红边提示)。
    void connectAngleValidityChanged(bool valid);

    /// 旋转工具锚心状态 (2026-12): 见 ToolHost::setRotateAnchorState。宿主
    /// 编排 ContextStrip::setRotateAnchorState (基准读数锚心端在前 + 换向
    /// 按钮转义为切锚心)。
    void rotateAnchorStateChanged(bool active, bool anchorIsEnd, bool canToggle,
                                  const QString& reason);

private:
    /// Create the tool instance on first use, then keep it (P2/L5).
    Tool* ensureTool(ToolType type);
    /// Bind the current context (scene/doc/undo/host) and activate @p tool.
    void activateTool(Tool& tool);

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    std::map<ToolType, std::unique_ptr<Tool>> m_tools;  ///< 常驻实例池 (P2/L5).
    Tool* m_activeTool = nullptr;  ///< 指向 m_tools 中的当前激活实例.
    ToolType m_activeType = ToolType::Select;
    QMetaObject::Connection m_activeLayerConn;  ///< Clears selection on layer switch.
};

} // namespace cad::tools
