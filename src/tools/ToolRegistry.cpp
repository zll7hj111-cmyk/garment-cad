#include "ToolRegistry.h"

#include "ToolSelect.h"
#include "ToolSmartPen.h"
#include "ToolCurveEdit.h"
#include "ToolRotate.h"
#include "ToolBreak.h"
#include "ToolIntersection.h"
#include "ToolMeasure.h"
#include "ToolAngleMeasure.h"

namespace cad::tools {

// P3 (TOOL_SYSTEM_AUDIT): 内置工具集中注册。注册序 = 工具坞按钮序 = 菜单序
// (与旧 MainWindow specs[]/菜单创建顺序一致)。新增工具: 加 include + 一行
// registerTool<T>()。
ToolRegistry::ToolRegistry()
{
    registerTool<ToolSelect>();
    registerTool<ToolSmartPen>();
    registerTool<ToolCurveEdit>();
    registerTool<ToolRotate>();
    registerTool<ToolBreak>();
    registerTool<ToolIntersection>();
    registerTool<ToolMeasure>();
    registerTool<ToolAngleMeasure>();
}

ToolRegistry& ToolRegistry::instance()
{
    static ToolRegistry registry;
    return registry;
}

const ToolDescriptor* ToolRegistry::descriptor(ToolType id) const
{
    if (auto it = m_descriptors.find(id); it != m_descriptors.end())
        return it->second.get();
    return nullptr;
}

std::unique_ptr<Tool> ToolRegistry::create(ToolType id) const
{
    const ToolDescriptor* d = descriptor(id);
    return (d && d->factory) ? d->factory() : nullptr;
}

QString toolHintText(ToolType type)
{
    // H3 (TOOL_SYSTEM_AUDIT): 提示文本唯一出处 = 各工具 ToolDescriptor::describe()
    // (P3 迁入, 原 ToolManager.cpp switch 表删除)。未注册返回空串兜底。
    if (const ToolDescriptor* d = ToolRegistry::instance().descriptor(type))
        return d->hintText;
    return QString();
}

} // namespace cad::tools
