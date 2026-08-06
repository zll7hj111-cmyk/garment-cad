#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include "Tool.h"
#include "ToolSelect.h"
#include "ToolSmartPen.h"
#include "ToolCurveEdit.h"
#include "ToolRotate.h"
#include "ToolBreak.h"
#include "ToolIntersection.h"
#include "ToolMeasure.h"
#include "ToolAngleMeasure.h"
#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"

namespace cad::tools {

ToolManager::ToolManager(CanvasScene* scene, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
{
    // Default tool: Select
    switchTool(ToolType::Select);
}

ToolManager::~ToolManager() = default;

void ToolManager::setParamDocument(cad::param::ParamDocument* paramDoc)
{
    m_paramDoc = paramDoc;

    // Switching the active layer drops any selection (selection is scoped to
    // the active layer). Re-bind for the (possibly new) document.
    disconnect(m_activeLayerConn);
    if (m_paramDoc) {
        m_activeLayerConn = connect(m_paramDoc,
            &cad::param::ParamDocument::activeLayerChanged, this, [this](int) {
                if (auto* ts = dynamic_cast<ToolSelect*>(m_activeTool.get()))
                    ts->clearSelectionOnLayerChange();
            });
    }

    // Re-activate the current tool so it picks up the freshly injected
    // document (the default Select tool was activated with a null one).
    if (m_activeTool && m_scene) {
        m_activeTool->deactivate();
        m_activeTool->setUndoStack(m_undoStack);
        m_activeTool->activate(*m_scene, m_paramDoc);
    }
}

void ToolManager::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    if (m_activeTool)
        m_activeTool->setUndoStack(stack);
}

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
    case ToolType::CurveEdit:
        setActiveTool(std::make_unique<ToolCurveEdit>());
        break;
    case ToolType::Rotate:
        setActiveTool(std::make_unique<ToolRotate>());
        break;
    case ToolType::Break:
        setActiveTool(std::make_unique<ToolBreak>());
        break;
    case ToolType::Intersection:
        setActiveTool(std::make_unique<ToolIntersection>());
        break;
    case ToolType::Measure:
        setActiveTool(std::make_unique<ToolMeasure>());
        break;
    case ToolType::AngleMeasure:
        setActiveTool(std::make_unique<ToolAngleMeasure>());
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
        // Blank-space right-click switch request (智能笔 ↔ 选择). The tool
        // never knows the manager — it only fires the injected callback.
        m_activeTool->setToolSwitchRequest(
            [this](ToolType t) { switchTool(t); });
        m_activeTool->setUndoStack(m_undoStack);
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
