#pragma once

#include <QObject>
#include <QMetaObject>
#include <memory>

class CanvasScene;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QUndoStack;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

class Tool;

/// Tool type identifiers for easy switching.
enum class ToolType {
    Select,
    SmartPen,
    CurveEdit,
    Rotate,
    Break,
    Intersection,
    Measure,
    AngleMeasure,
};

/// Manages tool lifecycle and dispatches input events to the active tool.
class ToolManager : public QObject
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

    /// Switch to a tool by type (creates and activates it).
    void switchTool(ToolType type);

    void setActiveTool(std::unique_ptr<Tool> tool);
    [[nodiscard]] Tool* activeTool() const { return m_activeTool.get(); }
    [[nodiscard]] ToolType activeToolType() const { return m_activeType; }

    // Event dispatch (called by CanvasView)
    void dispatchMousePress(QGraphicsSceneMouseEvent* event);
    void dispatchMouseMove(QGraphicsSceneMouseEvent* event);
    void dispatchMouseRelease(QGraphicsSceneMouseEvent* event);
    void dispatchMouseDoubleClick(QGraphicsSceneMouseEvent* event);
    void dispatchKeyPress(QKeyEvent* event);
    void dispatchKeyRelease(QKeyEvent* event);

    [[nodiscard]] CanvasScene* scene() const { return m_scene; }

signals:
    void activeToolChanged(ToolType type, const char* toolName);

    /// Select tool picked a single segment to edit in the status-bar strip
    /// (either blockId+segmentId for one segment, or both null = nothing).
    void editTargetChanged(const QUuid& blockId, const QUuid& segmentId);

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    std::unique_ptr<Tool> m_activeTool;
    ToolType m_activeType = ToolType::Select;
    QMetaObject::Connection m_activeLayerConn;  ///< Clears selection on layer switch.
};

} // namespace cad::tools
