#pragma once

class QGraphicsSceneMouseEvent;
class QKeyEvent;
class CanvasScene;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

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
};

} // namespace cad::tools
