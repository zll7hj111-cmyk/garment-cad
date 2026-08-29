#pragma once

#include <QKeySequence>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "Tool.h"

namespace cad::tools {

/// Tool type identifiers for easy switching. 归属 ToolRegistry.h (P3):
/// 工具身份标识与元数据同居一处, 不再定义在 ToolManager.h。
enum class ToolType {
    Select,
    SmartPen,
    CurveEdit,
    Rotate,
    Break,
    Intersection,
    Measure,
    AngleMeasure,
};

/// 工具静态元数据 (TOOL_SYSTEM_AUDIT P3): 每个工具经 static describe()
/// 提供并登记进 ToolRegistry —— 名字/图标/快捷键/提示/工厂由工具自身携带,
/// MainWindow 与 ToolManager 遍历消费, 不再散落 MainWindow 5 处平行结构
/// 手工对齐 (H3/M4 的根源)。
struct ToolDescriptor
{
    ToolType id = ToolType::Select;
    QString displayName;    ///< 菜单显示名 (含 & 加速符), 如 "选择(&V)".
    QString iconName;       ///< Phosphor SVG 资源名 (菜单图标 / refreshToolIcons).
    QKeySequence shortcut;  ///< 空 = 无快捷键 (测量 M 让给画布长按).
    QString hintText;       ///< 状态栏操作提示 —— 唯一出处 (原 toolHintText 内容迁入).
    std::function<std::unique_ptr<Tool>()> factory;
};

/// 工具注册表 (P3): 注册序 = 工具坞按钮序 = 菜单序。ToolRegistry.cpp 构造时
/// 注册全部内置工具; 新增工具 = 加 1 对 .cpp/.h + ToolRegistry.cpp 1 行注册。
class ToolRegistry
{
public:
    ToolRegistry();

    template <typename T>
    void registerTool()
    {
        ToolDescriptor d = T::describe();
        // N7 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 先取 id 再 move —— 原写法
        // move 之后还读 d.id (标量成员 move 后值不变, 所以行为是对的, 但那
        // 是在读一个已移动对象)。重排一下零成本且不再需要读者推演 move 语义。
        const ToolType id = d.id;
        // 防重复注册: 同一 id 注册两次会把 m_order 塞进重复项 → 工具坞生成
        // 两枚同功能按钮。重复注册是调用方 bug, 直接断言暴露而不是静默去重。
        Q_ASSERT_X(!m_descriptors.contains(id), "ToolRegistry::registerTool",
                   "ToolType registered twice");
        if (m_descriptors.contains(id))
            return;
        m_descriptors.emplace(id, std::make_unique<ToolDescriptor>(std::move(d)));
        m_order.push_back(id);
    }

    /// 注册表项 (nullptr = 未注册 / 非法 id)。
    [[nodiscard]] const ToolDescriptor* descriptor(ToolType id) const;
    /// 工厂创建 (未注册 → nullptr)。
    [[nodiscard]] std::unique_ptr<Tool> create(ToolType id) const;
    /// 注册序 (与工具坞/菜单一致)。
    [[nodiscard]] const std::vector<ToolType>& order() const { return m_order; }

    static ToolRegistry& instance();

private:
    std::map<ToolType, std::unique_ptr<ToolDescriptor>> m_descriptors;
    std::vector<ToolType> m_order;
};

/// 状态栏操作提示的唯一出处 (TOOL_SYSTEM_AUDIT H3, P3 后查 ToolRegistry):
/// 8 个 ToolType 一一对应, 未注册返回空串。新增工具漏写提示会被
/// test_tool_hints 拦下。
[[nodiscard]] QString toolHintText(ToolType type);

} // namespace cad::tools
