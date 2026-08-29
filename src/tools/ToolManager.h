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

namespace cad::param { class ParamDocument; }

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
    void setEditTarget(const QUuid& blockId, const QUuid& segmentId) override;
    void setHintOverride(const QString& hint) override;

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

    /// Select tool picked a single segment to edit in the status-bar strip
    /// (either blockId+segmentId for one segment, or both null = nothing).
    void editTargetChanged(const QUuid& blockId, const QUuid& segmentId);

    /// 活动工具的运行期提示覆盖 (M5): 非空 = 用这段文案替掉
    /// ToolDescriptor::hintText; 空 = 恢复默认。切换工具时宿主须自行清掉
    /// (见 MainWindow::onToolChanged) —— 覆盖不跨工具继承。
    void hintOverrideChanged(const QString& hint);

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
