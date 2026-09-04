# AGENTS.md

## 经验库使用规则

- **文档定位先看索引**：`DOCS_INDEX.md` 是文档主索引。**主加载文档 = 本文件**；其余全部按需查阅——先看索引定位，再打开对应文件。
- **修 bug 前**：先 grep `TROUBLESHOOTING.md` 相关关键词，避免重踩已知坑。改快捷键前必查第 0 组登记表。
- **修完清单外的新 bug**：根因 + 修复验证后追加到 `TROUBLESHOOTING.md` 对应主题分组。
- **任务收尾防过时**：本任务若触碰了 AGENTS.md/TROUBLESHOOTING.md/DECISIONS.md/CONVENTIONS.md 中声明的事实（常量/命令/路径/文件/用例数），收尾时必须核对源码并更新对应条目；事实性条目应带源码引用（file:line）。
- **约定变更须同步**：改动了 AGENTS.md「架构原则/领域建模决策」声明的行为时，同步修订对应条目（决策属用户拍板，翻案前先确认）。**决策全文在 `DECISIONS.md`，规范/验证全文在 `CONVENTIONS.md`，评审档案在 `ARCHIVE.md`**——AGENTS.md 只留压缩版，改档案先改对应文件再同步摘要。
- **文档状态维护**：新增/删除/归档文档时同步更新 `DOCS_INDEX.md`；设计文档落地后把状态标记改为「已落地」。

## 项目简介

WildWind Pattern（野风帖）——参数化服装 CAD 系统，C++23 / Qt6。参数化建模引擎（ParamPoint/Segment/Block/Attachment/FormulaVariable）+ 约束求解（Resolver + ConditionEngine）+ 交互制图工具链 + 文档持久化（DocumentSerializer，ZIP via miniz）。

## 模块边界

| 目录 | 职责 |
|------|------|
| `src/parametric` | 核心引擎：参数点、线段、Block、公式变量、条件引擎、Resolver、PerfProbe |
| `src/ui` | 面板与对话框 UI：变量面板、图层面板、公式卡片、条件对话框、IconHelper、Theme、线段属性对话框群——凡 QWidget 即归 `ui/` |
| `src/tools` | **只放手势与状态机**：选择、智能笔、曲线编辑、捕捉引擎、工具管理器、Connect/Marquee/RotateCopyGesture、LineFactory、SnapEngine、RotateGizmo。**本层不含任何 QWidget**；需要弹窗用 `cad::ui::` 前置声明 |
| `src/canvas` | 画布渲染：CanvasView/CanvasScene、BlockItem/CurveItem、图层、OpenGL 视口 |
| `src/geometry` | 基础几何：Vec2、单位定义、CurveMath |
| `src/app` | 应用入口、上下文属性条（ContextStrip，见 CONTEXT_STRIP_DESIGN.md） |

## 架构原则（不可破坏）

- **分层单向依赖**：上层可依赖下层，下层绝不反向依赖上层（parametric 引擎不依赖任何 UI）。依赖方向靠 `python tools/check_layering.py` 把关。需要"下层回调上层"时用**依赖倒置**：接口声明在下层，或把逻辑提到上层、通过信号对接。
- **ParamDocument 是门面**：所有公共 API 与信号签名必须保持不变；容器写路径已收口（只保留 const 访问器），模型变更只能走 ①带校验/信号的门面方法 ②`cad::param::RawModelAccess` 静默恢复通道（唯一 friend，只有反序列化/undo 回放/拖拽取消快照还原能编译通过）。禁止 `const_cast`；attachment 原位编辑唯一通道是 `findAttachment(id)` 可变重载。
- **undo 栈有上限**：`ParamDocument::kUndoStackLimit = 150`，命令带全量模型快照；写长链条测试（>150 步）注意历史会被截断。
- **可变裸指针**：`findBlock()/blockById()` 返回裸指针，任何结构性变更都可能使其失效——**一次一取，禁止跨变更持有**。
- **UI 与引擎并联观察者**：面板与画布都是 ParamDocument 信号的观察者，不直接调用 Resolver。
- **隐藏实体**：visible=false 是纯视觉属性，不影响交互（仍可悬停/选择/捕捉）。几何缓存 rebuildCache 必须包含所有实体。
- **画布缓存刷新**：只改显示/语义属性、不移动几何的命令必须显式 `block->touchGeometry()`，否则画布不刷新。`Block::geometryEpoch` 是私有字段，唯一 bump 入口 = `touchGeometry()`，禁止再写 `++geometryEpoch`。

## 构建命令

```bash
tools\build.bat               # 一键：vcvars64 + configure + build（Ninja Debug，binaryDir=build/out）
tools\build.bat release       # Release（binaryDir=build/out-rel）
```

- **Ninja 生成器需要 MSVC 环境**：必须走 `tools\build.bat`（内部先 call vcvars64.bat）或 Developer PowerShell。
- **构建/跑测的宿主限制**：Bash 工具禁止调用 cmd.exe；构建/ctest 一律走 PowerShell 工具的 `& .\tools\build.bat` / `ctest`；PowerShell 不回显 stdout，日志必须 `*>` 落盘（Out-File 默认 UTF-16）再用 `iconv -f UTF-16LE -t UTF-8 x.log` 转码读取。
- **模块静态库**：源文件按模块目录归入 7 个静态库，分层单向依赖 `gcad_geometry` → `gcad_parametric` → `gcad_canvas`/`gcad_document` → `gcad_ui` → `gcad_tools` → `gcad_app`；主程序与全部测试通过链接库获得源码。
- **PDB 治理**：11 个全链测试在 CMakeLists 末尾强制 `/INCREMENTAL:NO`——**勿删这些标志**（详情见 CONVENTIONS.md）。
- compile_commands.json 由 Ninja 自动导出到 `build/out`，clangd 直接使用；增删源文件后只需重跑构建脚本。

## 验证命令

```bash
cd build/out
ctest
```

- **按影响面选测试（2026-12 拍板）**：纯 geometry → test_curve + test_expression；parametric 引擎 → test_resolver + test_block_commands + test_attachment_commands + test_variable_layer_commands + test_reverse_segment_commands + test_serializer + test_migration；序列化 → test_serializer + test_migration；工具/UI → 对应 test_select_wkey / test_rotate_copy / test_context_strip / test_dialog_tabs 等；跨模块大改/收尾 → 全量 ctest。单测：`ctest -R <名>`。
- **回归基线（判"是不是我引入的红"先看这里）**：既有基线红 = 0（2026-09-02 修复两条：test_serializer::bridgeAuxPointSnappableAndAttachable + test_component::dragComponentLeaderCurveFollowStable，根因见 TROUBLESHOOTING 第 5 组）；环境漂移红 = test_intersection_update 全部 + test_extend::savedDocFormulaStartExtendRenders（依赖活档 E:/3.gcad，非代码回归）；**test_hold_show 已根治（2026-09-04：空输出是 Qt 6.11.1 debug 全局现象，真失败 = renderNonWhitePixels 隔行/隔列采样在 100% DPI 把长度标签测小，改全像素扫描 → 9 用例全绿 + 全量 41/41 通过，详戳 TROUBLESHOOTING 第 5 组）**；GUI 时序抖动 = test_dialog_tabs::switchBackAfterTyping / test_aux_layer / test_rotate_copy / test_select_wkey / test_component::junctionComponentConnectGesture + componentConnectOverlapSwitchTarget（整批偶发失败，单跑即过）。用例清单与详情见 CONVENTIONS.md。
- 不进 ctest 需手动跑：test_realdoc_perf、test_realdoc_full（env `GCAD_DOC`）、test_nav_smoke。
- **守卫脚本已进 ctest（2026-09）**：七守卫（`check_layering` 分层 / `check_hardcoded_colors` 颜色 / `check_test_fixtures` 夹具 / `check_file_size` 行数 / `check_header_classification` 头文件 / `check_bool_flags` 状态机 / `check_test_split` 测试体量）——全量 ctest 即覆盖，无需再手动单独跑；单跑：`ctest -R check_`。CI（GitHub Actions）已接入，push/PR 自动全量 ctest。代码拆分执行《文件拆分实操规则 v2.0》（`E:/e/file_split_rules_v2_final.md`），存量超标申报于 `redline_exceptions.json`，基线只许缩短不许扩增。
- **GUI 测试等待**：禁止固定 `QTest::qWait(N)` 占位；用 TestHelpers.h 三件套 waitUntil/grabStable/settle（断言"值变了"→waitUntil；"没变"→settle）。
- **磁盘格式版本与迁移**：kFormatVersion 唯一定义点在 src/document/FormatMigration.h；改格式 = bump 常量 + 写 migrateVNToVN+1 + registry() 加一行；链路有缺口拒绝加载。回归：test_migration + test_serializer。
- 性能探针：PerfProbe.h，`GCAD_PROFILE=1`；点击链路日志 `e:\garment-cad\gcad_click_log.txt`。
- **新增 .cpp**：加入其模块库源列表（七库之一）；测试 target 源列表只放 tests/*.cpp，切勿加项目头文件（AUTOMOC 重复定义 LNK2005）。

## 环境要求

| 依赖 | 要求 |
|------|------|
| Qt | 6.x（推荐 6.5+），组件：Widgets、Svg、Test、OpenGLWidgets |
| 编译器 | MSVC 2022（Visual Studio 17，x64，`/std:c++latest` + `/permissive-` + `/FS`） |
| CMake | ≥ 3.25 |
| C++ 标准 | C++23（`CMAKE_CXX_STANDARD_REQUIRED ON`） |

Qt 安装与 QT_DIR 配置见环境方式一（Qt 官方安装器，QT_DIR 指向 msvc2022_64）/方式二（vcpkg manifest + toolchain file），两种方式无需同时使用。

## 依赖管理

- FetchContent 内嵌于 CMakeLists.txt：miniz 3.0.2 / ElaWidgetTools（GIT_TAG aa1856b8，PATCH_COMMAND 5 个 Part 幂等，LNK4217 正常）/ spdlog v1.17.0 / Tracy v0.14.0（TRACY_ON_DEMAND=ON）。
- Qt6 组件：Widgets Svg OpenGLWidgets Test；AUTOMOC/AUTORCC/AUTOUIC 已启用。
- 图标：Phosphor Icons SVG（MIT），resources/icons/ + icons.qrc（前缀 `:/icons/`），统一经 `src/ui/IconHelper.h`。

## 开发规范

> 全文与历史沿革见 `CONVENTIONS.md`——按需查阅。

- **文件编码**：新建源文件必须 UTF-8 with BOM（Write/Edit 工具写出的无 BOM，含中文文件写完必须补 BOM）。
- **工具模式切换统一用 W 键，禁用 Tab**；切换后 `event->accept()` 并刷新预览/HUD。
- **交互函数必须显式接入事件流**（mouseMove/mousePress/keyPress），只实现不调用 = 功能静默失效。
- **新增工具 5 步**：①ToolType 枚举加值 ②实现 onActivate/onDeactivate + name() ③实现 `static ToolDescriptor describe()`（缺 = test_tool_hints 红）④ToolRegistry.cpp 加 `registerTool<T>()` ⑤MainWindow.cpp `toolDockIcon()` 补图标（漏补 = 兜底箭头 + 警告不崩）。运行期提示覆盖走 `Tool::reportHintOverride`。
- **工具公共能力层三件套**：HUD 标签一律 `HudItem`；命中测试一律 `HitTester.h`（活动层过滤唯一规则源）；临时图元登记 `ManagedItems`。禁止手搭 QGraphicsRectItem+TextItem / 复制 hitBlock 循环 / 手写 removeItem+delete。
- **工具生命周期**：Tool::activate/deactivate 非虚，派生只实现 onActivate/onDeactivate 虚钩子；上下文一次注入 ToolContext；实例常驻（重复点击当前工具 = no-op）；onActivate 必须复位会话状态。
- **连接角度会话**：条带是纯输入面，连接语义全在 ConnectGesture；会话内条带绝不 push 命令；°/⌒ 切换 = 数值几何保持换算 + 公式原样搬移（不乘系数）；° /⌒ 按钮必须原生 QPushButton + chipButtonStyle + QButtonGroup 互斥；输入锁定只认真桥线 `block->isBridge`。
- **约束类型分派点登记表**（ParamPoint.h）：改 PointConstraint 枚举必须逐层同步 12 处（清单见 CONVENTIONS.md）；序列化映射已表驱动（DocumentSerializer.cpp 四组枚举表）。
- **删除影响报告**：新删善后分支必须同步更新 `deleteImpactReport` 与测试（十项计数）。
- **卡片抽取范式**：子卡片持 doc 指针 + 目标 id，setTarget/refresh 双入口，模型变更经 `changed(ChangeKind)` 信号回报。
- **卡片基类 CardBase**：五张虚拟列表卡片继承 CardBase；改卡片骨架先改 CardBase 再改派生；indexLabel 固定 objectName（varIndex/cardIndex/linkedIndex/measureIndex/angleIndex）是测试契约勿改。
- **角度工具收口**：存储域归一化 `normalizeDeg360`/`normalizeDeg180`、显示格式化 `formatDegValue`/`formatDegTrimmed`、弧长↔角度换算 `arcMmToDeg`/`degToArcMm`、双模切换 `followerModeSwitchValues`——统一在 `src/geometry/Angle.h`/`Units.h`/`FollowerAngle.h`，改角度约定只改这几处。
- **跨层连接反馈**：`crossLayerToast`/`crossLayerBadge` 统一在 `src/ui/LayerFeedback.h`（cad::ui），改文案只改这一个头文件。
- **表单骨架 + 圆角纪律**：共享骨架在 `src/ui/FormScaffold.h`（makeFormGroupHeader/applyFormGrid/makeFormButtonBar/makeFormTitleBar）；新增 chrome 圆角勿超 4px（RadiusBadge 恒 4px）。
- **卡片竖线色/字号**：卡片左竖线 = 类型色（变量 piece1/公式 piece2/测量 piece3/关联 piece4），由 `CardBase::setAccentRole` 驱动；字号阶梯 FontXs 10/FontSm 11/FontMd 12/FontBase 13/FontLg 15/FontXl 18。
- **Qt 性能**：每帧同步槽里禁止 setStyleSheet；setText 同值短路；批量操作禁用布局避免 O(N²)。
- **性能断言抗噪声**：Debug 性能波动 30%+，断言用宽松边界（≤2.0x），勿用严格排序。
- **测量工作流**：测量发布 MeasureVariable（refName M_xxx，cm 域）；W 键循环 距离/水平/垂直；水平/垂直轴重合时第二击拒绝；"烘焙到操作层"=复制非移动；跨层附着单向。
- **公式引擎符号集**：`+ - * / ^`、一元 ±、括号归一化、小写函数与任意名字变量（含中文）；单参 `cos/sin/tan/sqrt/abs/atan/asin/acos/floor/ceil/round`（可裸参）、双参 `atan2(y,x)/pow/min/max`（必须括号+逗号）；三角函数参数与结果均为度制；`^` 右结合优先于一元负号；无 `√` 用 `sqrt(...)`；域错误返回错误而非 NaN。
## 领域建模决策（用户拍板，勿翻案）

> **全文在 `DECISIONS.md`（第四轮迁出，2026-09-01）——按需 grep 查阅，不注入会话。**
> 改动了本档案声明的行为时，同步修订 `DECISIONS.md` 对应条目（决策属用户拍板，翻案前先确认）。
>
> **拆开影子基准（2026-xx 拍板，翻案「拆开保留角度=活引用」）**：拆开 = 复制隐藏影子块（`Block::isShadow`）作为角度基准——本体旋转不再影响跟随线（R1）、offset 含公式原样保留（R2）、影子挂新宿主链式随动（R3，L3→影子→L2 双连接链，零新增 Resolver 逻辑）；挂回本体 = 删影子 + 活引用。权威设计 `DETACH_SHADOW_DESIGN.md`，摘要见 `DECISIONS.md`「拆开影子基准（2026-xx）」条目。

## 关键约束

- **单位体系**：内部计算使用厘米（cm），界面显示使用毫米（mm）。
- **Block 刚体模型**：Block 是刚体变换单元，内部点相对位置固定，整体支持平移/旋转。
- **画布缩放**：ZOOM_MIN=0.2（20%）/ ZOOM_MAX=10.0（1000%）/ SCENE_BOUND=±10,000mm（浮点精度安全）——常量定义于 src/canvas/CanvasView.h:74-77。
- **Git 远程**：origin = https://github.com/zll7hj111-cmyk/garment-cad.git，主分支 main；本地身份 林林 <2274789227@qq.com>。

## 架构决策记录（2026-08 全量评审档案）

> 全文在 `ARCHIVE.md`——按需查阅。源码注释里的 `P0-x/P1-x/P2-x` 编号与 `(ARCHITECTURE_REVIEW)` 字样均指该档案。

