#pragma once

#include <QObject>
#include <memory>

class CanvasScene;
class QGraphicsSceneMouseEvent;
class QKeyEvent;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

class Tool;

/// Tool type identifiers for easy switching.
enum class ToolType {
    Select,
    SmartPen,
};

/// Manages tool lifecycle and dispatches input events to the active tool.
class ToolManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolManager(CanvasScene* scene, QObject* parent = nullptr);
    ~ToolManager() override;

    void setParamDocument(cad::param::ParamDocument* paramDoc) { m_paramDoc = paramDoc; }

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

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    std::unique_ptr<Tool> m_activeTool;
    ToolType m_activeType = ToolType::Select;
};

} // namespace cad::tools
