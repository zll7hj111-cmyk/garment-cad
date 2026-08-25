#include "Tool.h"

#include "ToolSmartPen.h"  // HudItem (defined in this header)
#include "canvas/CanvasScene.h"

namespace cad::tools {

HudItem* Tool::ensureHud()
{
    if (!m_hud) {
        m_hud = new HudItem();
        m_scene->addItem(m_hud);
    }
    return m_hud;
}

void Tool::releaseHud()
{
    if (m_hud) {
        if (m_scene) m_scene->removeItem(m_hud);
        delete m_hud;
        m_hud = nullptr;
    }
}

} // namespace cad::tools
