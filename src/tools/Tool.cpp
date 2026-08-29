#include "Tool.h"

#include "canvas/CanvasScene.h"
#include "canvas/HudItem.h"

namespace cad::tools {

void Tool::activate(const ToolContext& ctx)
{
    m_scene = ctx.scene;
    m_paramDoc = ctx.paramDoc;
    m_undoStack = ctx.undoStack;
    m_host = ctx.host;
    onActivate(*m_scene, m_paramDoc);
}

void Tool::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    // 便捷重载: 只重绑 scene/doc, 保留既有 undo/host (与旧 Tool::activate
    // 只碰 m_scene/m_paramDoc 的语义一致; ToolManager 走上面的全量入口)。
    ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = paramDoc;
    ctx.undoStack = m_undoStack;
    ctx.host = m_host;
    activate(ctx);
}

void Tool::deactivate()
{
    onDeactivate();
    releaseHud();
    m_scene = nullptr;
    m_paramDoc = nullptr;
    m_host = nullptr;
}

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
