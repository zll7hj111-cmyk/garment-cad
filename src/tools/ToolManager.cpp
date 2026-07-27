#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include "Tool.h"
#include "ToolSelect.h"
#include "ToolSmartPen.h"
#include "canvas/CanvasScene.h"

namespace cad::tools {

ToolManager::ToolManager(CanvasScene* scene, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
{
    // Default tool: Select
    switchTool(ToolType::Select);
}

ToolManager::~ToolManager() = default;

void ToolManager::switchTool(ToolType type)
{
    m_activeType = type;

    switch (type) {
    case ToolType::Select:
        setActiveTool(std::make_unique<ToolSelect>());
        break;
    case ToolType::SmartPen:
        setActiveTool(std::make_unique<ToolSmartPen>());
        break;
    }
}

void ToolManager::setActiveTool(std::unique_ptr<Tool> tool)
{
    if (m_activeTool) {
        m_activeTool->deactivate();
    }

    m_activeTool = std::move(tool);

    if (m_activeTool && m_scene) {
        m_activeTool->activate(*m_scene, m_paramDoc);
        emit activeToolChanged(m_activeType, m_activeTool->name());
    }
}

void ToolManager::dispatchMousePress(QGraphicsSceneMouseEvent* event)
{
    if (m_activeTool) {
        m_activeTool->mousePress(event);
    }
}

void ToolManager::dispatchMouseMove(QGraphicsSceneMouseEvent* event)
{
    if (m_activeTool) {
        m_activeTool->mouseMove(event);
    }
}

void ToolManager::dispatchMouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (m_activeTool) {
        m_activeTool->mouseRelease(event);
    }
}

void ToolManager::dispatchMouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (m_activeTool) {
        m_activeTool->mouseDoubleClick(event);
    }
}

void ToolManager::dispatchKeyPress(QKeyEvent* event)
{
    if (m_activeTool) {
        m_activeTool->keyPress(event);
    }
}

void ToolManager::dispatchKeyRelease(QKeyEvent* event)
{
    if (m_activeTool) {
        m_activeTool->keyRelease(event);
    }
}

} // namespace cad::tools
