# AGENTS.md

## 经验库使用规则

- **修 bug 前**：先 grep `TROUBLESHOOTING.md`（踩坑经验库，按需查阅，不注入会话）相关关键词，避免重踩已知坑。改快捷键前必查第 0 组登记表。
- **修完清单外的新 bug**：根因 + 修复验证后追加到 `TROUBLESHOOTING.md` 对应主题分组。
- **任务收尾防过时**：本任务若触碰了 AGENTS.md/TROUBLESHOOTING.md 中声明的事实（常量/命令/路径/文件/用例数），收尾时必须核对源码并更新对应条目；事实性条目应带源码引用（file:line），改动源码后同步修订。
- **约定变更须同步**：改动了 AGENTS.md「架构原则/领域建模决策」声明的行为时，同步修订对应条目（决策属用户拍板，翻案前先确认）。

## 项目简介

WildWind Pattern（野风帖）——参数化服装 CAD 系统，基于 C++23 / Qt6 构建。通过参数化建模引擎驱动服装纸样的自动生成与联动更新。核心能力：参数化建模（ParamPoint/Segment/Block/Attachment/FormulaVariable）、智能约束求解（Resolver + ConditionEngine）、交互式制图工具链（智能笔/打断/交点/曲线编辑/旋转）、UI 组件化（VariablePanel/LayerPanel/SidePanel）、文档持久化（DocumentSerializer，ZIP 压缩 via miniz 3.0.2）。

## 模块边界

| 目录 | 职责 |
|------|------|
| `src/parametric` | 核心引擎：参数点、线段、Block、组、公式变量、条件引擎、Resolver、PerfProbe |
| `src/ui` | 面板 UI：变量面板、图层面板、公式卡片、条件对话框、IconHelper、Theme |
| `src/tools` | 交互工具：选择、智能笔、曲线编辑、捕捉引擎、工具管理器 |
| `src/canvas` | 画布渲染：CanvasView/CanvasScene、BlockItem/CurveItem、图层、OpenGL 视口 |
| `src/geometry` | 基础几何：Vec2、单位定义、CurveMath |
| `src/app` | 应用入口、状态栏组件（SegmentEditBar/预输入条）与演示数据 |

## 架构原则（不可破坏）

- **分层单向依赖**：上层可依赖下层，下层绝不反向依赖上层（parametric 引擎不依赖任何 UI）。可测试性靠此保证。
- **ParamDocument 是门面**：所有公共 API 与信号签名必须保持不变；业务逻辑封装在内部子域类中。容器写路径已收口：blocks/layers/variables/formulas 等只保留 const 访问器，模型变更只能走 ①带校验/信号的门面方法 ②显式命名的静默恢复 API（`*Raw`，仅限反序列化与 undo 回放）。禁止 `const_cast`；attachment 原位编辑唯一通道是 `findAttachment(id)` 可变重载。
- **组零限制**：组只是"方便选中"的选择标签，不干预任何几何/连接/求解行为。组模型 = Block 不携带 groupId 字段，由 ParamDocument 的 `m_blockGroup`（blockId→groupId）集中维护；**Resolver 零改动**，组逻辑只在 ParamDocument 与命令层实现。
- **UI 与引擎并联观察者**：面板与画布都是 ParamDocument 信号的观察者，不直接调用 Resolver（被封装在文档内部）。
- **隐藏实体**：visible=false 是纯视觉属性，不影响交互（仍可悬停/选择/捕捉）。几何缓存 rebuildCache 必须包含所有实体，渲染 paint 独立按可见性控制。
- **画布缓存刷新**：只改显示/语义属性、不移动几何的命令必须显式 `++block->geometryEpoch`，否则 rebuildCache 不触发、画布不刷新。

## 构建命令

```bash
tools\build.bat               # 一键：vcvars64 + configure + build（Ninja Debug，binaryDir=build/out）
tools\build.bat release       # Release（binaryDir=build/out-rel）
```

- **Ninja 生成器需要 MSVC 环境**：普通 PowerShell 直接跑 `cmake` 找不到 cl.exe 会失败，必须走 `tools\build.bat`（内部先 call vcvars64.bat）或 Developer PowerShell。
- compile_commands.json 由 Ninja 生成器自动导出到 `build/out`（`CMAKE_EXPORT_COMPILE_COMMANDS`），clangd 直接使用并热重载——增删源文件/改 CMakeLists 后只需重跑构建脚本，**无需手动刷 db**。
- clangd 安装于 `C:\Users\Administrator\AppData\Local\Programs\clangd\clangd_22.1.6\bin\clangd.exe`，opencode 的 LSP 配置指向 `build\out`（见 opencode.json）。
- PowerShell 5.1 下 `cmd /c ""...&&..."` 嵌套引号会失败（报 'C:\Program' 不是内部或外部命令），改走临时 .bat（先 call vcvars64.bat 再 cmake）。
- 如需换回 VS 生成器：改 CMakePresets.json 后删 `build/out/CMakeCache.txt`+`CMakeFiles` 及 `_deps/*-subbuild` 缓存（`_deps/*-src` 保留即可离线复用依赖源码）。

## 验证命令

```bash
cd build/out
ctest
```

- default preset 的 binaryDir 是 `build/out`（单配置 Debug，无需 `-C`）；release 是 `build/out-rel`。
- ctest 共 28 个用例（test_expression/resolver/serializer/p612_colinearity/group/duplicate/break/intersection/commands/curve/triangle_unfold/perf/aux_layer/smartpen_aux/segment_edit_bar/group_guards/tool_intersection/select_wkey/rotate_copy/dialog_tabs/canvas_perf/hold_show/log_probe + test_avatar_core/solver/render/view/panel）。**test_canvas_perf 的 singleCurveFrame 曾为 Debug 段错误存量问题**（疑似环境/软渲染），当前 Ninja Debug 下通过；复现时用基线对照法排查，勿误判回归（详见 `TROUBLESHOOTING.md` 第 5 组）。
- 不进 ctest 需手动跑：test_realdoc_perf、test_realdoc_full（env `GCAD_DOC=<.gcad路径>`，可叠加 `GCAD_PROFILE=1`）、test_nav_smoke（ElaWindow 导航栈冒烟，无 env）。
- 性能探针：src/parametric/PerfProbe.h，`GCAD_PROFILE=1` 启用；GUI 点击链路遥测日志固定路径 `e:\garment-cad\gcad_click_log.txt`。
- 新增 .cpp 必须检查所有引用该文件的 target 源列表（CMake 无自动扫描，漏加会 LNK2019）。

## 环境要求

| 依赖 | 要求 |
|------|------|
| Qt | 6.x（推荐 6.5+），组件：Widgets、Svg、Test、OpenGLWidgets |
| 编译器 | MSVC 2022（Visual Studio 17，x64，`/std:c++latest` + `/permissive-` + `/FS`） |
| CMake | ≥ 3.25 |
| C++ 标准 | C++23（`CMAKE_CXX_STANDARD_REQUIRED ON`） |

Qt 安装与 QT_DIR 配置见环境方式一（Qt 官方安装器，QT_DIR 指向 msvc2022_64）/方式二（vcpkg manifest + toolchain file），两种方式无需同时使用。

## 依赖管理

- FetchContent 内嵌于 CMakeLists.txt（无 vcpkg.json/conanfile）：miniz `https://github.com/richgel999/miniz.git` Tag `3.0.2` 浅克隆。
- **ElaWidgetTools**（`https://github.com/Liniyous/ElaWidgetTools.git`，GIT_TAG `aa1856b8f8589ba0a82e50c0ac01e289eb3ff2c4`，GIT_SHALLOW）：主项目 CMakeLists.txt 用 `PATCH_COMMAND` 执行 `third_party/elawidgettools_qt69_patch.cmake`（共 4 个 Part：Qt 6.9 编译兼容 / 弹窗按钮同步 emit 修 UAF + 隐藏空文本中间按钮 / ElaMenu 移除 400ms 动画 / ElaAppBar 默认关闭路径尊重 close() 被拒，均幂等）。库为静态库，主程序以 dllimport 语义链接产生 LNK4217 警告属正常。重建流程与链接坑详见 `TROUBLESHOOTING.md` 第 2 组。
- **spdlog**（`https://github.com/gabime/spdlog.git`，GIT_TAG `v1.17.0`，GIT_SHALLOW，MIT）：编译版静态库，自带 fmt；链接 `spdlog::spdlog`，头文件 `spdlog/spdlog.h`。Qt sink 可用（`spdlog/sinks/qt_sinks.h`）。
- **Tracy Profiler**（`https://github.com/wolfpld/tracy.git`，GIT_TAG `v0.14.0`，GIT_SHALLOW，BSD-3）：客户端库 `Tracy::TracyClient`，头文件 `tracy/Tracy.hpp`，探针 `ZoneScoped`；`TRACY_ON_DEMAND=ON`（无 profiler 连接时零收集），发布版可用 `-DTRACY_ENABLE=OFF` 整体关闭；GUI 为独立工具（GitHub releases 的 windows-x64 预编译 zip）。
- Qt6 组件：Widgets Svg OpenGLWidgets Test；AUTOMOC/AUTORCC/AUTOUIC 已启用。
- 图标：Phosphor Icons SVG（MIT），存放 resources/icons/ + icons.qrc（前缀 `:/icons/`），统一经 `src/ui/IconHelper.h`（iconByName 着色 / icon2State 双状态 / appIcon）。新图标从 phosphor-icons/core assets/regular/ 下载并在 qrc 登记。

## 开发规范

- **文件编码**：所有新建源文件（.h/.cpp/.qrc 等）必须 UTF-8 with BOM，否则 MSVC 解析失败（C2001/C2061）。Write 工具创建的文件无 BOM，需补。
- **工具模式切换统一用 W 键，禁用 Tab**（Tab 是 Qt 焦点导航键，打断交互）。W 语义按工具区分，切换后 `event->accept()` 并刷新预览/HUD。
- **交互函数必须显式接入事件流**（mouseMove/mousePress/keyPress），只实现不调用 = 功能静默失效；用单元测试直接驱动事件流验证。
- **约束类型分派点登记表**（ParamPoint.h）：新增/修改 PointConstraint 枚举必须逐层同步 12 处（数据定义、Block::resolve exhaustive switch、resolveInterpolatedPoints、曲线宿主判定、Resolver 交点 step、blockReferences 脏传播、degradeOrphanedIntersections、BlockItem 渲染、ToolSelect/ToolCurveEdit 交互、BlockCommands 降级恢复、DocumentSerializer 成对字符串映射、Duplicate）。遗漏 blockReferences 或序列化映射不会编译报错，只会静默损坏。
- **删除影响报告**：新删善后分支必须同步更新 `deleteImpactReport` 与测试（八项计数：attachmentsRemoved/bridgesReleased/intersectionsFrozen/linkedFrozen/linkedVarsRemoved/measureVarsRemoved/angleVarsRemoved/formulasBroken）。
- **卡片抽取范式**：子卡片持 doc 指针 + 目标 id，setTarget/refresh 双入口，模型变更经 `changed(ChangeKind)` 信号回报对话框，对话框只做联动刷新。
- **Qt 性能**：每帧同步槽里禁止 setStyleSheet（re-polish 数毫秒级），只在离散状态翻转时调用；setText 做同值短路；批量操作禁用布局避免 O(N²)。
- **性能断言抗噪声**：Debug 构建性能波动 30%+，断言用宽松边界（如 ≤2.0x），勿用严格排序；验证算法优化要构造最坏场景（逆序深链）而非常见微差。
- **测量工作流**：测量任意两点发布 MeasureVariable（refName `M_xxx`，cm 域）；"烘焙到操作层"=复制非移动（lengthFormula 活链接）；跨层附着单向（仅辅助层 follower、工作层 leader）；值循环预检只在 addAttachment 入口拦截。

## 领域建模决策（用户拍板，勿翻案）

- **默认主题**：默认使用白色（亮色）模式（`Theme::apply(Light)`，src/main.cpp）；暗色仅经 视图 → 暗色主题（Ctrl+D）显式切换，不做持久化默认翻转。
- **面板悬浮窗（原变量悬浮窗，2026-08 重构）**：变量/图层/组 三个面板统一为 Qt::Tool 悬浮窗口（侧边栏样式：长竖条、宽 ≤480、初始高按屏幕可用区 ≤660、贴主窗口右缘）；窗口内顶部为分类大标签 变量/图层/组（ElaTabBar，与主窗口后三个标签一一对应）。「变量」大标签页头部两行：变量/公式/关联/测量 四个子标签独占整行自动铺满（无滚动箭头、不裁剪），计数与操作按钮在第二行。主窗口顶部标签 画布/变量/图层/组 中，后三个是悬浮窗开关：点击显示窗口并切到对应大标签页，再点当前分类 = 隐藏；悬浮窗打开时对应按钮保持选中高亮 + 强调色圆点指示（`syncPanelTabs`，MainWindow.cpp），X 关闭/隐藏后主标签回画布；悬浮窗内切换大标签 → 主窗口按钮跟随高亮。主窗口页面堆栈只剩画布页。用户拖动后不再自动归位，X 关闭 = 隐藏。主窗口标签条与悬浮窗大标签的同步入口：`onPageTabChanged` / `syncPanelTabs` / `eventFilter(Hide)`；悬浮窗大标签 `currentChanged` 必须同时切 `m_panelStack` 内容页 + 同步主窗口按钮高亮（漏接 stack 会导致内容永远停在变量页）。**卡片行区分（2026-08 用户拍板，最终方案）**：左侧竖线统一**蓝/橙按行交替**——偶数行蓝 `#2F6FED`、奇数行橙 `#F59E0B`，四页（变量/公式/关联/测量）一致；卡片构造函数 `alternate` 参数（`row % 2` / 公式页 `localIndex % 2`）存为 `m_alternate` 驱动竖线颜色。背景斑马纹方案已废弃删除（VirtualCardList 无 paintEvent 覆盖、无 stripe token——曾尝试 `ThemeTokens::stripe` 亮 #E3E8EF/暗 #333B45，用户反馈与底色区分度不够且不美观，两次迭代后放弃）；类型色条（蓝=变量/紫=公式/橄榄=关联/陶土=测量）亦被取消（各页签内类型本就单一）。**卡片列表底色 = 画布纸色 `ThemeTokens::canvasBg`**（2026-08 用户要求，亮 #F6F3EC / 暗 #14181E，VariablePanel.cpp buildListPage + applyTheme、MeasureTab.cpp 同步）；**悬停删除按钮防跳动**：五张卡片（VariableCard/FormulaCard/LinkedCard/MeasureCard/AngleMeasureCard）的悬停显隐删除按钮都配同尺寸透明占位 `m_deleteBtnSlot` 互斥换显隐——布局空间恒定，按钮出现不引起行宽挤压/行高变化（否则 VirtualCardList 重测整列下移）。引用名输入框（2026-08 用户要求）：测量结果对话框「引用名」行改为可编辑输入框、初始为空（不显示自动生成的 M_xxx，留空保留自动生成名；输入即转大写）；变量/关联/测量/角度卡片上的引用名 chip 不再显示灰色占位文字「引用名」，无值时纯空框（tooltip 仍提示双击编辑）。**chip 静态态 = 输入框样式（2026-08 用户拍板，最终方案）**：所有 CopyChip（名称/引用名/公式名）**空闲时即显示输入框**——背景 `@surface`（hover `@surface2`）+ 1px 圆角描边，**由 `CopyChip::paintEvent` 自绘**（**坑：ElaText 不渲染 QSS 边框**——它自带内联表 `#ElaText{background-color:transparent}` 且文本路径不画 PE_Widget，QSS 的 background 能渲染（曾有"空白蓝色小方块"为证）但 border 完全无效，两次 QSS 尝试用户均反馈无差别）。**描边用专用中灰色**（亮 `#9AA4B2` / 暗 `#4E5866`，2026-08 三次迭代：`@border`→`@borderStrong`→专用色——前两版在画布纸色底上肉眼不可见，用户两次反馈"完全没有生效"；像素探针 chipBox/cardBorderPx/lightCardBorderPx 验证渲染），空值/占位态也是完整空输入框、不"空一块"；`m_label->setMinimumHeight(16)` 防塌成细条；编辑态覆盖层 (ElaLineEdit) 与静态框同尺寸同形制，输入结束收起后描边仍在；hover 由 chip 自绘（label 的 Enter/Leave 事件经 eventFilter 转发 setHovered）。引用名 chip 无值时纯空框（tooltip 仍提示双击编辑）。
- **智能笔预输入**：切到智能笔时状态栏显示一次性预输入条（名称/长度 cm/角度 °）；长度+角度齐全 → 一击成线（起点即吸附点）；仅长度/角度 → 预览锁该维度、第二击定另一维；无效表达式 toast 后忽略。角度语义与 HUD 一致：自由起点 = 绝对世界角，吸附起点 = 跟随折角（闭合基准）。数值或公式均可；内容被创建使用后清空，回到空白预输入态。
- **桥接线范式**：自由线 + 长度公式引用测量变量 M_xxx + 可选端点跟随（起点跟随宿主 / 终点指向 endTarget，LinePropertyDialog 开关）；创建后与普通线段无异，无 isBridge/pin 标志。终点指向 = Block 的 endTargetBlockId/endTargetPointId/endTargetOffset 字段，Resolver Step 7 驱动（容差 5° 吸附，琥珀环高亮）。
- **曲线系统**：分段三次 Bézier + 过点锚点；AUTO 点 C2 曲率连续（solveC2Tangents Thomas 算法，tension 不作用于 C2 解）；MANUAL 点（autoTangent=false）存储切线为已知边界；智能笔只画直线，曲线编辑全部在 ToolCurveEdit（快捷键 C）；共线不等长切线手柄（tangentLocked=true 时跟随转向保长度）。桥接线禁止加曲线点。打断需贝塞尔细分（de Casteljau）。
- **辅助计算层**：全局唯一、固定在图层列表最底部（index 0）、不可删除。层间契约：唯一出口 = 发布的测量标量；工作层对辅助层不可见、不可捕捉、不可附着。非激活时完全隐身，求解照常。
- **图层系统**：图层是纯粹"选择/显示过滤器"，求解器/测量/附着零改动。非活动层灰显（#9E9E9E+40%、不显示标注、不可选不可悬停）但可捕捉；最终可见性 = block.visible && layer.visible；新线归活动层；切换活动层自动清空选择。LayerPanel 的 refresh 必须 QueuedConnection（否则 use-after-free）；QTreeWidget 设了 stylesheet 必须显式定义 ::indicator。
- **Resolver 性能**：settle 收敛 = BFS 拓扑一趟（森林不变式保证无环）；拖拽帧内增量求解（collectAffected BFS + affectedOnly + resolveForDrag 忽略跨选择集附件）；公式求值 = Kahn 拓扑序单趟；Step5 桥接仅在 bridgesMoved 时重收敛、Step7 仅在 rotated 时收敛。
- **隐藏/灰显与交互**：隐藏实体仍可交互；灰显层可捕捉不可选。
- **连接手势**：抓取/吸附/光环/目标环四半径由单一常量 `kConnectSnapRadius` 统一驱动（当前值 7.5px，WYSIWYG 铁律）。

## 关键约束

- **单位体系**：内部计算使用厘米（cm），界面显示使用毫米（mm）。
- **Block 刚体模型**：Block 是刚体变换单元，内部点相对位置固定，整体支持平移/旋转。
- **画布缩放**：ZOOM_MIN=0.2（20%）/ ZOOM_MAX=10.0（1000%）/ SCENE_BOUND=±10,000mm（浮点精度安全）——常量定义于 src/canvas/CanvasView.h:74-77。
- **Git 远程**：origin = https://github.com/zll7hj111-cmyk/garment-cad.git，主分支 main；本地身份 林林 <2274789227@qq.com>。
