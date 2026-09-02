---
kind: frontend_style
name: Qt6 原生样式与主题系统
category: frontend_style
scope:
    - '**'
source_files:
    - src/canvas/CanvasStyle.h
    - src/canvas/CanvasStyle.cpp
    - src/ui/IconHelper.h
    - src/ui/IconHelper.cpp
    - src/tools/LinePropertyDialog.cpp
    - resources/icons.qrc
---

该仓库为 C++/Qt6 桌面 CAD 应用，前端样式完全基于 Qt Widgets 原生能力实现，未使用 CSS、QSS 文件或外部 UI 框架。样式体系由以下核心部分组成：

**1. 设计令牌与主题（CanvasStyle）**
- `src/canvas/CanvasStyle.h/.cpp` 定义了集中式的设计令牌表，包含颜色、线宽、点半径、标签颜色等所有画布视觉参数。
- 通过 `EntityState`（Normal/Hover/Selected/GroupHighlight）状态机驱动参数变化，提供 `lineColor()`、`pointColor()`、`labelColor()` 等查询接口。
- 支持三套主题：`lightTheme()`（默认浅色）、`darkTheme()`（深色背景与高对比度配色）、`printTheme()`（打印模式，禁用动画与强调效果）。
- 角色默认值（RoleDefaults）按 SegmentRole（Outline/Internal/Auxiliary）区分轮廓线、内部线、辅助线的粗细与虚实。

**2. SVG 图标系统（IconHelper）**
- `src/ui/IconHelper.h/.cpp` 提供 Phosphor SVG 图标的动态着色加载，通过替换 `currentColor` 实现主题化。
- 支持单色图标、双色状态图标（normal/active），以及多尺寸 HiDPI 适配（16/20/24/32/48/64px）。
- 图标资源通过 Qt Resource System（`resources/icons.qrc`）嵌入，路径格式如 `:/icons/pen.svg`。

**3. 内联样式约定**
- 对话框与控件样式采用 Qt 内联 `setStyleSheet()` 字符串，如 `LinePropertyDialog` 中的卡片边框、圆角、背景色等。
- 字体使用 `QFont::setPixelSize()` 直接设置像素大小（10-11px），未建立统一的字体令牌。
- HTML 富文本用于复杂布局（如连接关系卡片），配合 `Qt::RichText` 渲染。

**4. 交互反馈规范**
- 悬停：线条加粗（+0.8px）、颜色混合 hoverTint（Figma 风格保留用户底色）、点半径放大 1.5 倍。
- 选中：统一使用 `m_selectColor`（红色系），线宽增加 +0.6px。
- 分组高亮：半透明蓝色覆盖（alpha=130）。
- 动画过渡：默认 150ms，打印模式下禁用。

**约束与限制**
- 无全局样式文件，每个组件自行管理样式字符串。
- 颜色硬编码在 CanvasStyle 成员变量中，未使用外部调色板或配置文件。
- 未实现响应式设计，所有尺寸以像素为单位固定。