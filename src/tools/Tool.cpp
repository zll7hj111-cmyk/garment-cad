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
    // 角标归场景所有, 不随工具销毁 —— 切工具时必须显式撤下, 否则上一个
    // 工具的「多选」会挂在画布上不走 (m_scene 下面一行就空了)。
    if (m_scene) m_scene->setModeBadge(QString());
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

QString ModeIndicator::hint(const char* toolName) const
{
    // 无模式可言 (detail 与 wAction 都空) → 空串, 宿主据此恢复
    // ToolDescriptor::hintText 的默认文案 (无内部模式的工具走这条)。
    if (detail.isEmpty() && wAction.isEmpty())
        return QString();

    QString text = QString::fromUtf8(toolName);
    if (!modeName.isEmpty())
        text += QStringLiteral("[") + modeName + QStringLiteral("]");
    text += QStringLiteral("：") + detail;
    if (!wAction.isEmpty())
        text += QStringLiteral(" | ") + wAction;
    return text;
}

void Tool::refreshModeIndicator()
{
    const ModeIndicator mi = modeIndicator();
    reportHintOverride(mi.hint(name()));
    // L2 画布角标: 默认态不显示 (传空串 = 撤下), 只有切到非常驻态才占像素。
    // 状态栏要看一眼底部, 角标钉在视口左上角 —— 就在视线里。
    if (m_scene) m_scene->setModeBadge(mi.isDefault ? QString() : mi.modeName);
}

void Tool::announceModeChange()
{
    const ModeIndicator mi = modeIndicator();
    if (m_scene && !mi.toast.isEmpty())
        m_scene->showToast(mi.toast);
    // 走同一出口重算持久层 (modeIndicator() 是纯查询, 调两次无副作用):
    // 保证 toast 与状态栏永远来自同一份状态快照。
    refreshModeIndicator();
}

} // namespace cad::tools
