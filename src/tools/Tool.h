#pragma once

#include <functional>

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QUndoStack;
class CanvasScene;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// Forward-declared (defined in ToolManager.h) — tools only pass it through
/// the switch-request callback, they never build one.
enum class ToolType;

/// Abstract base class for all interactive tools.
class Tool
{
public:
    virtual ~Tool() = default;

    /// Called when the tool becomes active.
    virtual void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) {
        (void)scene; (void)paramDoc;
    }

    /// Called when the tool is deactivated.
    virtual void deactivate() {}

    /// Set the undo stack for command-based tools.
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// Inject the tool-switch request (blank-space right-click gesture).
    /// Injected by ToolManager on activation; tools fall back to a no-op
    /// when unset (e.g. unit tests that drive the tool directly).
    void setToolSwitchRequest(std::function<void(ToolType)> request)
    {
        m_switchRequest = std::move(request);
    }

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
    /// Fire the switch request (no-op when not injected by ToolManager).
    void requestToolSwitch(ToolType type)
    {
        if (m_switchRequest) m_switchRequest(type);
    }

    QUndoStack* m_undoStack = nullptr;

private:
    std::function<void(ToolType)> m_switchRequest;
};

} // namespace cad::tools
