# WildWind Pattern（野风帖）

参数化服装 CAD 系统 —— 基于 C++23 / Qt6 的参数化服装纸样自动生成与联动更新。

设计师在画布上绘制受约束的线段，公式变量、测量、条件与图层驱动模型自动求解与联动更新。核心目标是：**让打版师可以不打断思路地完成整件纸样的绘制与调整**，同时让学习者无需手册也能看懂每个工具在做什么。

## 功能特性

- **参数化建模引擎**：参数点（ParamPoint）/ 线段（Segment）/ 纸样块（Block）/ 附着关系（Attachment）/ 公式变量（FormulaVariable），几何由参数与公式驱动，编辑即联动重解。
- **智能约束求解**：Resolver + ConditionEngine，BFS 拓扑一趟收敛、拖拽帧内增量求解，公式求值按 Kahn 拓扑序单趟完成。
- **交互式制图工具链**：智能笔、打断、交点、旋转复制、曲线编辑（分段三次 Bézier + C2 曲率连续）、捕捉引擎、桥接线跟随。
- **变量 / 公式 / 测量系统**：变量面板、公式卡片、测量与角度测量（发布 M_xxx 测量变量）、条件对话框、引用名、跨块依赖自动传播。
- **图层与组件**：辅助层（非激活完全隐身、求解照常）、工作层、组件级整体移动（componentClosure）。
- **文档持久化**：DocumentSerializer，ZIP 压缩（miniz 3.0.2），带格式版本迁移（FormatMigration）与损坏兜底。
- **UI**：Qt6 Widgets + Graphics View + OpenGL 视口，ElaWidgetTools 主题化界面，变量/图层/组件悬浮面板，上下文属性条（ContextStrip）。

## 技术栈

| 依赖 | 说明 |
|------|------|
| 语言 / 标准 | C++23（MSVC 2022，`/std:c++latest` + `/permissive-`） |
| 框架 | Qt 6.x（Widgets、Svg、Test、OpenGLWidgets） |
| 构建 | CMake ≥ 3.25 + Ninja |
| 三方库 | miniz 3.0.2、ElaWidgetTools、spdlog v1.17.0、Tracy Profiler v0.14.0 |

## 构建

```bat
tools\build.bat          :: 一键：vcvars64 + configure + build（Ninja Debug，binaryDir=build/out）
tools\build.bat release  :: Release（binaryDir=build/out-rel）
```

构建脚本内部会先调用 vcvars64.bat，普通 PowerShell 直接跑 cmake 找不到 cl.exe 时会失败，请走脚本或 Developer PowerShell。

## 测试

```bat
cd build\out
ctest
```

共 31 个用例，覆盖解析器、序列化、工具交互（打断/交点/测量/旋转复制/曲线编辑/选择）、图层、组件、格式迁移等。分层单向依赖由 `python tools/check_layering.py` 静态检查把关；测试档禁止引用仓库外路径，由 `python tools/check_test_fixtures.py` 守卫。

## 目录结构

| 目录 | 职责 |
|------|------|
| `src/parametric` | 核心引擎：参数点、线段、Block、公式变量、条件引擎、Resolver |
| `src/ui` | 面板与对话框 UI、线段属性对话框群、CardBase 卡片族 |
| `src/tools` | 手势与工具状态机：选择、智能笔、曲线编辑、捕捉引擎、旋转等 |
| `src/canvas` | 画布渲染：CanvasView/CanvasScene、BlockItem/CurveItem、图层、OpenGL 视口 |
| `src/geometry` | 基础几何：Vec2、单位定义、CurveMath |
| `src/app` | 应用入口、上下文属性条（ContextStrip）、MainWindow |

## 许可证

[MIT](./LICENSE)
