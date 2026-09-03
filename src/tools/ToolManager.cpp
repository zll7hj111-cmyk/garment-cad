#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include "Tool.h"
#include "ToolSelect.h"   // dynamic_cast: 活动层切换清选集
#include "ToolRotate.h"   // dynamic_cast: 继承选择集
#include "canvas/CanvasScene.h"
#include "parametric/Attachment.h"   // RotationMode (forwardConnectAngleMode)
#include "parametric/ParamDocument.h"

namespace cad::tools {

ToolManager::ToolManager(CanvasScene* scene, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
{
    // Default tool: Select
    switchTool(ToolType::Select);
}

ToolManager::~ToolManager()
{
    // N5 配套 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 常驻实例池意味着工具的
    // 清理点从"析构"搬到了 deactivate —— 但**当前激活的那个工具**在关闭时
    // 从未被 deactivate 过 (只有切换才 deactivate)。没有这一步, 退出时
    // 激活工具的 onDeactivate 不跑: ToolSelect 的重叠提示图元、三个手势
    // 协作对象都会随进程退出漏掉 (平时无害, 但会让泄漏检测工具报警, 也让
    // "清理写在 onDeactivate 就够了"这个约定不成立)。
    //
    // 安全前提: ToolManager 是 MainWindow 的子对象, 且创建晚于 CanvasScene,
    // 子对象反序析构 ⇒ 此处 m_scene 仍然存活。
    //
    // 2026-12 追加: **析构期禁止再向外界发信号**。deactivate 链路可能 emit
    // hintOverrideChanged (ToolSelect::onDeactivate → deactivateOverlapContext
    // → refreshModeIndicator → reportHintOverride → setHintOverride), 而本
    // 对象是 MainWindow 的子对象, 反序析构时 MainWindow 已过 ~MainWindow
    // 函数体 —— 信号会落在半销毁的接收者上, Qt 6.11 Debug 构建直接断言
    // (assertObjectType: "class destructor may have already run",
    // qobjectdefs_impl.h:107)。先断开全部出向连接再清理工具: 接收者名单
    // 随 MainWindow 的连接表走, 这里断开 = 关闭期所有 emit 变 no-op。
    disconnect(this, nullptr, nullptr, nullptr);
    if (m_activeTool)
        m_activeTool->deactivate();
    m_activeTool = nullptr;
}

void ToolManager::setParamDocument(cad::param::ParamDocument* paramDoc)
{
    m_paramDoc = paramDoc;

    // Switching the active layer drops any selection (selection is scoped to
    // the active layer). Re-bind for the (possibly new) document.
    disconnect(m_activeLayerConn);
    if (m_paramDoc) {
        m_activeLayerConn = connect(m_paramDoc,
            &cad::param::ParamDocument::activeLayerChanged, this, [this](const QUuid&) {
                if (auto* ts = dynamic_cast<ToolSelect*>(m_activeTool))
                    ts->clearSelectionOnLayerChange();
            });
    }

    // Re-activate the current tool so it picks up the freshly injected
    // document (the default Select tool was activated with a null one).
    if (m_activeTool && m_scene) {
        m_activeTool->deactivate();
        activateTool(*m_activeTool);
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
    // L5 (TOOL_SYSTEM_AUDIT): 重复点击当前工具 = no-op —— 旧实现无条件
    // 销毁重建, 连按两次 R 会清空旋转目标与 HUD 内容。
    if (m_activeType == type && m_activeTool)
        return;

    QSet<QUuid> adoptedSelection;
    if (m_activeType == ToolType::Select && type == ToolType::Rotate) {
        if (auto* selTool = dynamic_cast<ToolSelect*>(m_activeTool)) {
            adoptedSelection = selTool->selection();
        }
    }

    if (m_activeTool)
        m_activeTool->deactivate();

    Tool* next = ensureTool(type);
    m_activeType = type;
    m_activeTool = next;
    activateTool(*next);

    if (!adoptedSelection.isEmpty()) {
        if (auto* rotTool = dynamic_cast<ToolRotate*>(next)) {
            rotTool->adoptSelection(adoptedSelection);
        }
    }

    emit activeToolChanged(m_activeType, next->name());
}

Tool* ToolManager::ensureTool(ToolType type)
{
    if (auto it = m_tools.find(type); it != m_tools.end())
        return it->second.get();

    // P3 (TOOL_SYSTEM_AUDIT): 工厂移入 ToolRegistry —— 工具自持工厂,
    // 不再在 ToolManager 里逐类型 switch。
    std::unique_ptr<Tool> created = ToolRegistry::instance().create(type);
    if (!created)
        return nullptr;

    Tool* raw = created.get();
    m_tools.emplace(type, std::move(created));
    return raw;
}

void ToolManager::activateTool(Tool& tool)
{
    // 全量上下文注入 (TOOL_SYSTEM_AUDIT P2): scene/doc/undo/host 一次绑定,
    // 替代旧的三处散落 setter (setUndoStack / setToolSwitchRequest /
    // setEditTargetCallback)。
    ToolContext ctx;
    ctx.scene = m_scene;
    ctx.paramDoc = m_paramDoc;
    ctx.undoStack = m_undoStack;
    ctx.host = this;
    tool.activate(ctx);
}

void ToolManager::requestToolSwitch(ToolType type)
{
    switchTool(type);
}

void ToolManager::setHintOverride(const QString& hint)
{
    // M5: 工具的运行期状态 (智能笔 直线/省道线) → 转发给宿主覆盖状态栏文案。
    emit hintOverrideChanged(hint);
}

void ToolManager::setHoverTarget(const QUuid& blockId, const QUuid& segmentId)
{
    // 上下文属性条: 悬停候选 → 条带只读预览 (节流在条带侧)。
    emit hoverTargetChanged(blockId, segmentId);
}

void ToolManager::setPinnedTarget(const QUuid& blockId, const QUuid& segmentId)
{
    // 上下文属性条: 锁定焦点 → 条带可编辑 (均 null = 解除锁定)。
    emit pinnedTargetChanged(blockId, segmentId);
}

void ToolManager::setConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                         const QUuid& attachmentId, double initialAngle)
{
    // 上下文属性条 (二期): 连接手势角度会话开始/结束 (全 null = 结束)。
    emit connectAngleSessionChanged(blockId, segmentId, attachmentId, initialAngle);
}

void ToolManager::setConnectAngleValidity(bool valid)
{
    emit connectAngleValidityChanged(valid);
}

void ToolManager::setRotateAnchorState(bool active, bool anchorIsEnd,
                                       bool canToggle, const QString& reason)
{
    // 上下文属性条 (2026-12): 旋转工具锚心状态 → 条带基准读数锚心端在前 +
    // 换向按钮转义为切锚心。
    emit rotateAnchorStateChanged(active, anchorIsEnd, canToggle, reason);
}

void ToolManager::forwardConnectAngleText(const QString& text)
{
    if (m_activeTool) m_activeTool->connectAngleTextChanged(text);
}

void ToolManager::forwardConnectAngleMode(cad::param::RotationMode mode)
{
    if (m_activeTool) m_activeTool->connectAngleModeChanged(mode);
}

void ToolManager::forwardConnectAngleCommit()
{
    if (m_activeTool) m_activeTool->connectAngleCommitted();
}

void ToolManager::forwardConnectAngleCancel()
{
    if (m_activeTool) m_activeTool->connectAngleCancelled();
}

void ToolManager::forwardReverseRequest(const QUuid& blockId, const QUuid& segmentId)
{
    // 旋转会话换向 (2026-12): 条带换向点击 → 激活工具 (ToolRotate 切锚心)。
    if (m_activeTool) m_activeTool->onReverseRequested(blockId, segmentId);
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
