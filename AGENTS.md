# AGENTS.md

> **主加载文档（会话常驻）**。其余文档全部按需查阅——先看 `DOCS_INDEX.md` 定位，再打开对应文件。
> 全文分布：用户拍板决策 → `DECISIONS.md`；开发规范与验证基线 → `CONVENTIONS.md`；架构评审档案 → `ARCHIVE.md`；踩坑经验 → `TROUBLESHOOTING.md`；已落地设计 → `docs/design/`。本文件只保留压缩版规则。

## 经验库规则

- **修 bug 前**：先 grep `TROUBLESHOOTING.md` 相关关键词，避免重踩已知坑；改快捷键前必查第 0 组登记表。
- **修完清单外的新 bug**：根因 + 修复验证后追加到 `TROUBLESHOOTING.md` 对应主题分组。
- **任务收尾防过时**：本任务若触碰了本文件 / `TROUBLESHOOTING.md` / `DECISIONS.md` / `CONVENTIONS.md` 中声明的事实（常量 / 命令 / 路径 / 文件 / 用例数），收尾时必须核对源码并更新对应条目；事实性条目应带源码引用（file:line）。
- **约定变更须同步**：改动了本文件「架构原则 / 领域建模决策」声明的行为时，同步修订对应条目（决策属用户拍板，翻案前先确认）。
- **文档状态维护**：新增 / 删除 / 归档文档时同步更新 `DOCS_INDEX.md`；设计文档落地后把状态标记改为「已落地」。

## 项目简介

WildWind Pattern（野风帖）——参数化服装 CAD 系统，C++23 / Qt6。参数化建模引擎（ParamPoint/Segment/Block/Attachment/FormulaVariable）+ 约束求解（Resolver + ConditionEngine）+ 交互制图工具链 + 文档持久化（DocumentSerializer，ZIP via miniz）。

## 模块边界

| 目录 | 职责 |
|------|------|
| `src/parametric` | 核心引擎：参数点、线段、Block、公式变量、条件引擎、Resolver、PerfProbe |
| `src/document` | 文档持久化：DocumentSerializer（ZIP via miniz）、DocumentFile、FormatMigration（`kFormatVersion` 唯一定义点） |
| `src/ui` | 面板与对话框 UI：变量面板、图层面板、公式卡片、条件对话框、IconHelper、Theme、线段属性对话框群——凡 QWidget 即归 `ui/` |
| `src/tools` | **只放手势与状态机**：选择、智能笔、曲线编辑、捕捉引擎、工具管理器、Connect/Marquee/RotateCopyGesture、LineFactory、SnapEngine、RotateGizmo。**本层不含任何 QWidget**；需要弹窗用 `cad::ui::` 前置声明 |
| `src/canvas` | 画布渲染：CanvasView/CanvasScene、BlockItem/CurveItem、图层、OpenGL 视口 |
| `src/geometry` | 基础几何：Vec2、单位定义、CurveMath、RayCast |
| `src/app` | 应用入口、上下文属性条（ContextStrip，见 `docs/design/CONTEXT_STRIP_DESIGN.md`） |

## 架构原则（不可破坏）

- **分层单向依赖**：上层可依赖下层，下层绝不反向依赖上层（parametric 引擎不依赖任何 UI）。依赖方向靠 `python tools/check_layering.py` 把关。需要"下层回调上层"时用**依赖倒置**：接口声明在下层，或把逻辑提到上层、通过信号对接。
- **ParamDocument 是门面**：所有公共 API 与信号签名必须保持不变；容器写路径已收口（只保留 const 访问器），模型变更只能走 ①带校验/信号的门面方法 ②`cad::param::RawModelAccess` 静默恢复通道（唯一定义在 `src/parametric/ParamDocumentRaw.h`，只有反序列化 / undo 回放 / 拖拽取消快照还原能编译通过）。禁止 `const_cast`；attachment 原位编辑唯一通道是 `findAttachment(id)` 可变重载。
- **undo 栈有上限**：`ParamDocument::kUndoStackLimit = 150`，命令带全量模型快照；写长链条测试（>150 步）注意历史会被截断。
- **可变裸指针**：`findBlock()/blockById()` 返回裸指针，任何结构性变更都可能使其失效——**一次一取，禁止跨变更持有**。
- **UI 与引擎并联观察者**：面板与画布都是 ParamDocument 信号的观察者，不直接调用 Resolver。
- **隐藏实体**：visible=false 是纯视觉属性，不影响交互（仍可悬停 / 选择 / 捕捉）。几何缓存 rebuildCache 必须包含所有实体。
- **画布缓存刷新**：只改显示/语义属性、不移动几何的命令必须显式 `block->touchGeometry()`，否则画布不刷新。`Block::geometryEpoch` 是私有字段，唯一 bump 入口 = `touchGeometry()`，禁止再写 `++geometryEpoch`。

## 构建命令

```bash
tools\build.bat WildWindPattern      # 【日常首选·极速】增量构建主程序（~0.3s，跳过全部测试的链接风暴）
tools\build.bat <test_name>          # 【单测首选·极速】增量构建单个测试 target（如 tools\build.bat test_select_wkey）
tools\build.bat reconfig             # 强制重跑 CMake Configure（日常增删修改代码无需运行，Ninja 自动感知）
tools\build.bat reldeb               # 【收尾验收专用】构建全量所有目标（主程序 + 全部 57 个测试可执行文件重新链接）
tools\build.bat release              # 纯 Release（产物 build/out-rel，无调试符号）
tools\build.bat debug                # 纯 Debug（产物 build/out，未优化慢速，常规开发跑测禁止使用）
```

- **【铁律·极速迭代】日常严禁无脑全量构建**：改完业务代码只验证编译或跑主程序 → `tools\build.bat WildWindPattern`；调试单测 → 精准指定该测试 target（如 `tools\build.bat test_select_wkey`）。无参数 `tools\build.bat` 会触发全部测试重新链接（含 25 个 `/INCREMENTAL:NO` 全量胖重链），CPU / 磁盘 I/O 拥塞 30~60 秒。
- **【全局统一构建目录】`build/out-reldeb`**（RelWithDebInfo：`/O2` + 完整 PDB，杜绝 Debug 拖拽卡顿）。`tools\build.bat` 默认走它；`debug`/`release` 分支写入的 `build/out`、`build/out-rel` 仅供特殊排查，日常禁用。
- **构建 / 跑测环境**：Ninja 生成器需要 MSVC 环境，一律走 `tools\build.bat`（内部按需 vcvars64）或 Developer PowerShell。长日志建议 `*> x.log` 落盘再读，避免截断。
- **模块静态库**：源文件按模块目录归入 7 个静态库，分层单向依赖 `gcad_geometry` → `gcad_parametric` → `gcad_canvas`/`gcad_document` → `gcad_ui` → `gcad_tools` → `gcad_app`；主程序与全部测试通过链接库获得源码。
- **PDB 治理**：25 个全链测试在 `CMakeLists.txt:999-1010` 强制 `/INCREMENTAL:NO`——**勿删这些标志**（详情见 CONVENTIONS.md）。
- `compile_commands.json` 由 Ninja 自动导出到 `build/out-reldeb`，clangd 直接使用；增删源文件后只需重跑构建脚本。

## 验证命令

```bash
ctest -C RelWithDebInfo -R <test名>   # 【日常首选·秒级】只跑受影响的特定单测（如 ctest -C RelWithDebInfo -R test_select_wkey）
ctest -C RelWithDebInfo               # 【收尾验收专用】全量 61 个用例（54 功能测试 + 7 守卫，耗时 1~2 分钟）
ctest -N                              # 列出当前实际注册的全部用例名（测试拆分/改名后以此为准）
```

- **【铁律·按影响面跑测】日常严禁无过滤 `ctest`**：全量含真实 GUI 事件循环模拟与全像素渲染扫描，且反向倒逼全量重链；单步修改 / 单点 bug 修复必须 `-R <名>`，全量仅在任务彻底完工、收尾交付前的最后一轮执行 1 次。
- **按影响面选测试**：纯 geometry → `test_curve` + `test_expression`；parametric 引擎 → `test_resolver_*` + `test_block_commands` + `test_attachment_*` + `test_variable_layer_commands` + `test_reverse_segment_commands` + `test_serializer` + `test_migration` + `test_ortho_offset`；序列化 → `test_serializer` + `test_migration`；工具/UI → `test_select_wkey` / `test_rotate_copy_*` / `test_context_strip` / `test_dialog_tabs_*` 等；跨模块大改 / 收尾 → 全量 ctest。
- **回归基线（判"是不是我引入的红"先看这里）**：既有基线红 = 0。已知非回归项（环境漂移红、GUI 时序抖动）与**测试拆分改名对照表**见 `CONVENTIONS.md` 验证命令区——`test_rotate_copy` 已拆为 `test_rotate_copy_semantics/_shadow/_endtarget`，`test_dialog_tabs` 已拆为 `test_dialog_tabs_switch/_angle_conn/_aux`，`test_resolver` 已拆为 `test_resolver_points/_attachment/_curve_arc/_diag_misc`，`test_attachment_commands` 已拆为 `test_attachment_shadow/_slide/_angle`；判红前先 `ctest -N` 确认用例名。
- 不进 ctest 需手动跑：`test_realdoc_perf`、`test_realdoc_full`（env `GCAD_DOC`）、`test_nav_smoke`。
- **守卫脚本已进 ctest（2026-09）**：七守卫（`check_layering` 分层 / `check_hardcoded_colors` 颜色 / `check_test_fixtures` 夹具 / `check_file_size` 行数 / `check_header_classification` 头文件 / `check_bool_flags` 状态机 / `check_test_split` 测试体量）——全量 ctest 即覆盖，无需单独跑；单跑：`ctest -R check_`。CI（GitHub Actions）已接入，push/PR 自动全量 ctest。代码拆分执行《文件拆分实操规则 v2.0》（**外部参考，未入库**；本仓以 `tools/check_file_size.py` + `redline_exceptions.json` 为准），存量超标申报于 `redline_exceptions.json`，基线只许缩短不许扩增。
- **GUI 测试等待**：禁止固定 `QTest::qWait(N)` 占位；用 `tests/TestHelpers.h` 三件套 waitUntil/grabStable/settle（断言"值变了"→waitUntil；"没变"→settle）。
- **磁盘格式版本与迁移**：`kFormatVersion` 唯一定义点在 `src/document/FormatMigration.h`；改格式 = bump 常量 + 写 migrateVNToVN+1 + registry() 加一行；链路有缺口拒绝加载。回归：`test_migration` + `test_serializer`。
- **性能探针**：`src/parametric/PerfProbe.h`，运行时 `GCAD_PROFILE=1` 启用，按逻辑帧统计并打印到 stderr（每 120 帧一行）。
- **新增 .cpp**：加入其模块库源列表（七库之一）；测试 target 源列表只放 `tests/*.cpp`，切勿加项目头文件（AUTOMOC 重复定义 LNK2005）。

## 环境要求

| 依赖 | 要求 |
|------|------|
| Qt | 6.x（推荐 6.5+），组件：Widgets、Svg、Test、OpenGLWidgets |
| 编译器 | MSVC 2022（Visual Studio 17，x64，`/std:c++latest` + `/permissive-` + `/FS`） |
| CMake | ≥ 3.25 |
| C++ 标准 | C++23（`CMAKE_CXX_STANDARD_REQUIRED ON`） |

Qt 安装与 QT_DIR 配置见环境方式一（Qt 官方安装器，QT_DIR 指向 msvc2022_64）/ 方式二（vcpkg manifest + toolchain file），两种方式无需同时使用。

## 依赖管理

- FetchContent 内嵌于 CMakeLists.txt：miniz 3.0.2 / ElaWidgetTools（GIT_TAG `aa1856b8`，`third_party/elawidgettools_qt69_patch.cmake` 6 个 Part 幂等，LNK4217 正常）/ spdlog v1.17.0 / Tracy v0.14.0（TRACY_ON_DEMAND=ON）。
- Qt6 组件：Widgets Svg OpenGLWidgets Test；AUTOMOC/AUTORCC/AUTOUIC 已启用。
- 图标：Phosphor Icons SVG（MIT），resources/icons/ + icons.qrc（前缀 `:/icons/`），统一经 `src/ui/IconHelper.h`。

## 开发规范

> 全文与历史沿革见 `CONVENTIONS.md`——按需查阅。

- **文件编码**：新建 / 编辑含中文的源文件必须保持 UTF-8 with BOM（MSVC 未施加全局 `/utf-8`，无 BOM 会误解析中文；编辑后用脚本核验首三字节，见 TROUBLESHOOTING 第 2 组）。
- **工具模式切换统一用 W 键，禁用 Tab**；切换后 `event->accept()` 并刷新预览 / HUD。
- **交互函数必须显式接入事件流**（mouseMove/mousePress/keyPress），只实现不调用 = 功能静默失效。
- **新增工具 5 步**：①ToolType 枚举加值 ②实现 onActivate/onDeactivate + name() ③实现 `static ToolDescriptor describe()`（缺 = test_tool_hints 红）④ToolRegistry.cpp 加 `registerTool<T>()` ⑤MainWindow.cpp `toolDockIcon()` 补图标（漏补 = 兜底箭头 + 警告不崩）。运行期提示覆盖走 `Tool::reportHintOverride`。
- **工具公共能力层三件套**：HUD 标签一律 `HudItem`；命中测试一律 `HitTester.h`（活动层过滤唯一规则源）；临时图元登记 `ManagedItems`。禁止手搭 QGraphicsRectItem+TextItem / 复制 hitBlock 循环 / 手写 removeItem+delete。
- **工具生命周期**：Tool::activate/deactivate 非虚，派生只实现 onActivate/onDeactivate 虚钩子；上下文一次注入 ToolContext；实例常驻（重复点击当前工具 = no-op）；onActivate 必须复位会话状态。
- **连接角度会话**：条带是纯输入面，连接语义全在 ConnectGesture；会话内条带绝不 push 命令；°/⌒ 切换 = 数值几何保持换算 + 公式原样搬移（不乘系数）；° /⌒ 按钮必须原生 QPushButton + chipButtonStyle + QButtonGroup 互斥；输入锁定只认真桥线 `block->isBridge`。
- **约束类型分派点登记表**（ParamPoint.h）：改 PointConstraint 枚举必须逐层同步 **13 处**（登记表见 `src/parametric/ParamPoint.h:37-51`，全文见 CONVENTIONS.md）；序列化映射已表驱动（DocumentSerializer.cpp 四组枚举表）。
- **删除影响报告**：新删善后分支必须同步更新 `deleteImpactReport` 与测试（九项计数，2026-09 下线省道线 dartLinesDegraded 后收敛）。
- **卡片抽取范式**：子卡片持 doc 指针 + 目标 id，setTarget/refresh 双入口，模型变更经 `changed(ChangeKind)` 信号回报。
- **卡片基类 CardBase**：五张虚拟列表卡片继承 CardBase；改卡片骨架先改 CardBase 再改派生；indexLabel 固定 objectName（varIndex/cardIndex/linkedIndex/measureIndex/angleIndex）是测试契约勿改。
- **角度工具收口**：存储域归一化 `normalizeDeg360`/`normalizeDeg180`、显示格式化 `formatDegValue`/`formatDegTrimmed`、弧长↔角度换算 `arcMmToDeg`/`degToArcMm`、双模切换 `followerModeSwitchValues`——统一在 `src/geometry/Angle.h`、`src/geometry/Units.h`、`src/parametric/FollowerAngle.h`，改角度约定只改这几处。
- **跨层连接反馈**：`crossLayerToast`/`crossLayerBadge` 统一在 `src/ui/LayerFeedback.h`（cad::ui），改文案只改这一个头文件。
- **表单骨架 + 圆角纪律**：共享骨架在 `src/ui/FormScaffold.h`（makeFormGroupHeader/applyFormGrid/makeFormButtonBar/makeFormTitleBar）；新增 chrome 圆角勿超 4px（RadiusBadge 恒 4px）；输入框与数值微调框统一为无底边横杠纯胶囊输入框（ElaWidgetTools 补丁收口，全包围高亮边框）。
- **卡片竖线色/字号**：卡片左竖线 = 类型色（变量 piece1/公式 piece2/测量 piece3/关联 piece4），由 `CardBase::setAccentRole` 驱动；字号阶梯 FontXs 10/FontSm 11/FontMd 12/FontBase 13/FontLg 15/FontXl 18（`src/ui/Theme.h`）。
- **Qt 性能**：每帧同步槽里禁止 setStyleSheet；setText 同值短路；批量操作禁用布局避免 O(N²)。
- **性能断言抗噪声**：Debug 性能波动 30%+，断言用宽松边界（≤2.0x），勿用严格排序。
- **测量工作流**：测量发布 MeasureVariable（refName M_xxx，cm 域）；W 键循环 距离/水平/垂直；水平/垂直轴重合时第二击拒绝；"烘焙到操作层"=复制非移动；跨层附着单向；角度测量严格禁止跨图层（方案 A），基于交点与光标点选位置推导射线方向（flipA/flipB 持久化稳定）。
- **公式引擎符号集**：`+ - * / ^`、一元 ±、括号归一化、小写函数与任意名字变量（含中文）；单参 `cos/sin/tan/sqrt/abs/atan/asin/acos/floor/ceil/round`（可裸参）、双参 `atan2(y,x)/pow/min/max`（必须括号+逗号）；三角函数参数与结果均为度制；`^` 右结合优先于一元负号；无 `√` 用 `sqrt(...)`；域错误返回错误而非 NaN。

## 领域建模决策（用户拍板，勿翻案）

> **全文在 `DECISIONS.md`——按需 grep 查阅，不注入会话。** 改动了本区声明的行为时，同步修订 `DECISIONS.md` 对应条目（决策属用户拍板，翻案前先确认）。
>
> **拆开影子基准**：拆开 = 复制隐藏影子块（`Block::isShadow`）作为角度基准——本体旋转不再影响跟随线（R1）、offset 含公式原样保留（R2）、影子挂新宿主链式随动（R3，L3→影子→L2 双连接链，零新增 Resolver 逻辑）；挂回本体 = 删影子 + 活引用。权威设计 `docs/design/DETACH_SHADOW_DESIGN.md`。（2026-09 补充拍板：新建挂载默认焊接锁定跟随；重连缓存只缓存上次连接对象，从新宿主拆开后重连回到新宿主而非旧本体；辅助点挂载拆开语义 = 彻底释放连接 RemoveAttachmentCommand，使线段转为自由线段可随时重新吸附/连接；右键菜单全图元穿透提供就地拆开；D 键快拆与面板辅助点 Tab 均彻底释放数据。）
>
> **线段正交拐角偏置（OrthoOffset，2026-09 拍板）**：沿主轴前进基准长折 90° 左右偏置直连端点（YX 局部直角坐标系模型，FMA 纯浮点乘加 $O(1)$ 求解保 1200 FPS；屏幕系向左法向 $(\sin\theta, -\cos\theta)$ 纠偏左右按钮；首次开启锁死当前几何基准角与基准长，主长度框与 ContextStrip 回填基准长，斜长独立标签解耦防膨胀；ContextStrip 保持 OrthoOffset 约束禁退化；输入数值自动切方向，左右切换基准角恒定）；画布中心参考虚线（showOrthoAxis）与「👁 基准轴」开关；在 `src/ui/LineOrthoOffsetCard.h/.cpp` + `LineGeometrySection.cpp` 面板无缝集成；经用户拍板取消镜像线段与复杂开度逻辑，保持单线轻量稳健。详见 `DECISIONS.md`。
>
> **端点连接跟随角度直线弦长/开度模式（ChordLength，2026-09 拍板）**：端点跟随体系（Attachment）新增「直线弦长 / 开度模式」（`RotationMode::ChordLength`，界面图标 `[↔]`），专用于服装制图省道展开、褶裥开度等场景。支持直接输入开度物理直线距离（如 `3.0 cm` 或公式 `D_dart`），以 $O(1)$ 几何反算展开角 $\theta = 2\arcsin\left(\frac{C}{2R}\right)$（超限平滑钳制 $180^\circ$ 直行绝不 NaN）；三模零跳变几何换算（角度 $\leftrightarrow$ 弧长 $\leftrightarrow$ 弦长/开度）；ContextStrip 扩充为 `[°] [⌒] [↔]` 三键互斥胶囊组，SegmentAngleCard 支持循环切换；持久化向前向下完全兼容。详见 `DECISIONS.md`。

## 关键约束

- **单位体系**：内部计算与存储统一毫米（mm）；界面显示与公式消费默认厘米（cm）——权威定义 `src/geometry/Units.h:29-31`，长度格式化走 `Units::formatLength`（输出 cm）。测量变量（MeasureVariable）以 cm 域发布给公式，缓存值以 mm 存储（`src/parametric/MeasureVariable.h:23,44`）。
- **Block 刚体模型**：Block 是刚体变换单元，内部点相对位置固定，整体支持平移/旋转。
- **画布缩放**：ZOOM_MIN=0.2（20%）/ ZOOM_MAX=10.0（1000%）/ SCENE_BOUND=±10,000mm（浮点精度安全）——常量定义于 `src/canvas/CanvasView.h:100-103`。
- **Git 远程**：origin = https://github.com/zll7hj111-cmyk/garment-cad.git，主分支 main；本地身份 林林 <2274789227@qq.com>。

## 架构决策记录（2026-08 全量评审档案）

> 全文在 `ARCHIVE.md`——按需查阅。源码注释里的 `P0-x/P1-x/P2-x` 编号与 `(ARCHITECTURE_REVIEW)` 字样均指该档案。
