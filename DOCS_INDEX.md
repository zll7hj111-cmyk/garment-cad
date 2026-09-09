# DOCS_INDEX.md —— 文档主索引

> **用途**：回答「哪个文档讲什么、何时需要看、是否过时」。
> **主加载文档（会话常驻，唯一）= `AGENTS.md`**（约 18KB / 95 行；2026-09 重写，2026-12 压缩并迁出低频参考节）；其余全部**按需查阅**——先看本索引定位，再打开对应文件。
>
> **目录约定（2026-09 整理）**：根目录只保留 9 个活跃 md；已落地设计（含 UI 原型）→ `docs/design/`；
> 领域参考资料 → `docs/reference/`；已闭环的报告 / 任务书 / 废弃方案 → `docs/archive/<年-月>/`（摘要见 `docs/archive/SUMMARY.md`）。
> 维护规则：新增 / 删除 / 归档文档时同步更新本索引；状态变化（落地 / 过时）时更新「状态」列。

---

## 一、主加载文档（会话常驻，唯一）

| 文档 | 内容 | 说明 |
|------|------|------|
| `AGENTS.md` | 经验库规则、项目简介、模块边界、架构原则、构建 / 验证命令、开发规范（压缩版）、领域决策索引、关键约束 | **唯一常驻**。2026-09 重写：订正单位体系（内部 mm / 显示 cm）、缩放常量、模块表补 `src/document`；细节一律按需查对应档案。用例数以 `ctest -N` 为准（2026-12 为 64 = 56 功能 + 8 守卫） |

## 二、根目录按需档案

| 文档 | 内容 | 查阅时机 |
|------|------|----------|
| `DECISIONS.md` | 领域建模决策全文（**25 项**，用户拍板勿翻案）：默认主题 / 面板悬浮窗 / 桥接线 / 曲线 / 辅助层 / 图层 / 角度测量隔离 / 同点堆叠捕捉 / Resolver 性能 / 连接手势半径 / 拆开保留角度 / 滑轨 / 连接卡片 / 选择交互 / 重叠消歧 / 组件 / 延长线 / 换向 / 辅助点拆开重连 / OrthoOffset / ChordLength 等 | 改动 AGENTS.md「领域建模决策」摘要对应的行为前；条目名可 grep 本文件定位 |
| `CONVENTIONS.md` | 开发规范全文（工具系统 / 卡片 / 角度 / 表单 / 性能 / 测量 / 公式引擎）+ 验证命令全文（用例清单 / 基线红名单 / 测试拆分改名对照表 / 守卫脚本） | 需要历史沿革、测试名、证据细节时；AGENTS.md 只留压缩版 |
| `TROUBLESHOOTING.md` | 踩坑经验库（第 0 组快捷键登记表 + 1 构建工具链 / 2 ElaWidgetTools / 3 Qt-UI 渲染 / 4 数据引擎 / 5 测试基线） | **修 bug 前必 grep**（AGENTS.md 经验库规则第 1 条）；改快捷键前查第 0 组登记表 |
| `ARCHIVE.md` | 2026-08 架构评审决策档案（worker 线程 / 指针句柄化 / undo 增量快照 / Rust 试点） | 源码注释出现 `P0-x/P1-x/P2-x` 或 `(ARCHITECTURE_REVIEW)` 时；「为什么不做」的不可再生知识 |
| `ARCHITECTURE.md` | 模块分层依赖图（Mermaid）+ 外部中间件清单 | 想快速看七库依赖方向 / 第三方依赖版本时 |
| `CONTRIBUTING.md` | Git 提交规范 + 代码格式化 / 静态检查命令 | 提交前 |
| `README.md` | 项目说明（一键启动 + 运行测试） | 首次上手 |

## 三、设计文档（`docs/design/`，按主题查阅）

> **阅读须知**：本区各篇是当时的设计稿 / 验收记录，其中的测试文件名（`test_commands.cpp`、`test_resolver.cpp`、`test_rotate_copy.cpp`、`test_dialog_tabs.cpp`、`test_attachment_commands.cpp`）与构建目录（`build/out`）多为当时状态。2026-12 测试拆分与构建目录统一后，**现行测试清单 / 构建命令以 `AGENTS.md`、`CONVENTIONS.md` 为准**。

| 文档 | 主题 | 状态 |
|------|------|------|
| `CONTEXT_STRIP_DESIGN.md` | 上下文属性条（ContextStrip）一期 + 二期 + 三期 | ✅ 已落地（2026-12） |
| `PANEL_REDESIGN_DESIGN.md` | 面板重设计（v3 表单 / 端点组 / 栅格化） | ✅ 已落地（2026-08-31，§3 结构全部实施） |
| `EXTEND_LINE_DESIGN.md` | 端点延长线（权威文档） | ✅ 已落地（2026-08-26 源码批次，见 git log） |
| `DETACH_SHADOW_DESIGN.md` | 拆开影子线段（拆开 = 复制隐藏影子基准线 + 可挂载新线形成跟随链；翻案「拆开保留角度」活引用语义） | ✅ 已落地（2026-09-03，头部含实现摘要与差异说明） |
| `ROTATE_REDESIGN_DESIGN.md` | 旋转工具重设计（D15 确认门 / 影子偏转 §2.6） | ✅ 已落地（2026-08-27）；选集旋转 2026-08-29 已删、**2026-09-04 重新设计回归**（MarqueeGesture 框选 + adoptSelection + RotateBlocksCommand）；§2.5/§2.6 判定表仍可复用；**2026-09 角度/gizmo 口径以 `ROTATE_ANGLE_UNIFY_DESIGN.md` 为准** |
| `ROTATE_ANGLE_UNIFY_DESIGN.md` | 旋转工具 / 角度模块统一设计（2026-09 六点反馈诊断：黄圈基准、单选 vs 框选 9 处差异、灰虚线 0°、任意枢轴刚体化、角度域 U1-U6、历史包袱清单 + S1-S5 方案 + §8 落地记录） | ✅ **已落地**（2026-09 拍板 D1-D5 后一次性实施 S1-S5；代码/测试/文档同步，全量 ctest 64/64 绿） |
| `DESIGN.md` | 打版工作台视觉设计（piece 调色板 / 字号与圆角 token / 暗色适配） | ⚠️ 部分过时：2026-08-09 视觉稿。**视觉语言与 Rules 仍有效**（字号 / 圆角已按 `src/ui/Theme.h` 订正）；默认主题已拍板为 Light，AngleHud 已随 CONTEXT_STRIP 二期删除 |
| `PRODUCT.md` | 产品定位与平台约束（用户画像 / 定位 / 能力与约束 / 产品原则） | ⚠️ 部分过时：**产品定位与原则仍有效**；已订正单位体系（内部 mm / 显示 cm）与删除影响九项计数；原始 critique 产物未入库 |
| `PERF_AUDIT_REPORT.md` | 性能审计报告（重复计算 + 可维护性）：5 项重复计算 + 4 项维护性提升，带 file:line、影响面矩阵与优先级 | ⚠️ 部分实施（2026-12）：**① 分段弧长缓存 + A `runResolvePass` 收口已实现**；②③④⑤ C/D 待实施 |
| `CIRCLE_TOOL_DESIGN.md` | 圆形工具（Bézier 拟合圆 + circleFit 切向重拟合；两枚端点 p0/p1 = Polar，半径 + 基准角度权威，整圆 a₁ = a₀ + 360°；3 个 CurveAnchor 象限锚按角度定位（§4.1.1 弦定位陷阱）；包角本期可编辑（半圆一步得）；圆度 = tension（−1 = 内接四边形，配 a₀=45° 即正方形）；不做「段数」参数（D20）；标记字段用枚举 `fitKind` 而非 bool（D21：`check_bool_flags` 阈值 5）；锯齿由解析绘制消除（D16）；打断 = 两段真圆弧不变形（§4.4、§19）；圆心+半径 / 两点直径双模式；R/D/C 三向联动；周长可发布为变量；条带圆会话；additive key 不 bump 格式版本；§13 逐问速查；§14 M1 第一步清单） | 📐 **D1-D21 全部拍板（2026-12 用户追认）并已实施**（M1/M2/M3 + §17-§26；决策已登记 `DECISIONS.md`）：M1 引擎三件套 + 最小 ToolCircle（O 键点圆心拖半径）落地（§14.1-14.4：改动表、实测修正 κ 误差 2.7253e-4·r / 零切向写回守卫 / flatLocal≈56 点、`EnumCodec.h` 抽取与「陈旧 object」误判排查）；M2 面板圆区段（R/D/C 联动、基准角、包角预设、圆度夹紧）+ D16 解析绘制 + undo 补洞（§15，含 a0/包角联合写与 `CurveItem.h` 前向声明 bug 修复）；M3 整圆求交（弦长退化守卫过零、起点 CCW 切向基准、两轮拟合、射线最近命中重挑）+ 条带圆区段（半径/基准角/包角三槽 + 「圆」徽标，§16，**2026-12 已被 §23 圆专属条带 `CircleStripBar` 取代**：六槽 圆/编号/名称/R/D/C，`src/app/CircleStripBar.{h,cpp}` + `tests/test_circle_strip.cpp`）；D14 解除圆约束（新命令 `DetachCircleCommand`，形状原地冻结、锚点转 Free、`autoTangent` 刻意不置 true，§17）；D8 曲线编辑对圆段置灰（七处守卫 + `kCircleCurveLockedHint` toast，§18）；D7 圆段打断 = 两段真圆弧（`BreakState::isCircle` + 四阶段显式圆分支，角度分割、半径镜像、前段锚点就近重排，§19）；D9 圆段作角度基准（`ConnectGestureAttach` 手写 refWorld → `effectiveAngleRefWorld`，整圆取接缝切向、弧取弦，§20，清审计 TOOL-P1-17）；D5 两点直径模式（`ToolCircle` 双模式 + W 切换 + 独立 `Session` 状态机，§21）；D15 周长发布为变量（`segmentBaseLength` 弧长解析化 r·θ 使值精确 == 2πr + 面板「发布参数」按钮，§22）；§23 一期补充：圆专属底部条带 `CircleStripBar`（六槽，取代 M3-b 三槽复用）；§24 一期补充：圆绘制会话条带（画圆时条带输半径/直径，Enter 或二次点击落圆、Esc 取消、锁定 chip）；§25 一期补充：圆心→接缝半径基准虚线 + 世界角标注（悬停/选中时显示），并修复解析圆绘制路径把世界角取负导致可见弧镜像到 X 轴另一侧（三症状：锚点在线外 / 弧身点不中 / 拖动残影，`tests/test_circle_edit.cpp`）；§26 一期补充（修订 §25）：基准线口径修订——接缝取解析起点、世界角 = a₀ + 块旋转、解析弧同基准（修「块旋转后虚线不指外圆点 / 标注恒定」），角度标注从圆角底块改为沿虚线挂载的文字（修「像一个大方块」），圆心双击开面板（`Block::circleCenterPoint`）、旋转枢轴吸附圆心（`RotateInputTracker` 扫全部可捕捉点）；§27 一期补充：圆段旋转辅助显示适配——自由块姿态角原取弦向 start→end，整圆两端点同为接缝点 ⇒ 弦向量 (0,0) ⇒ 姿态恒 0°（黄虚线压灰虚线、黄弧跨度 0、徽标恒 0），改为统一 helper `cad::tools::localPoseDirRad`（圆段 = 圆心→接缝半径方向 = 块内 a₀，与 §26 标注同源），`RotateSession`/`RotateAimSnap`/`RotateCopyGesture` 四处消费点收口；`test_rotate_copy_flow` 新增 `gizmoCircleUsesRadiusDirection`（8 passed）；`test_circle_edit` 20 passed + `test_rotate_pivot_multi` 6 passed，全量 71/71 绿；待用户实测验收 |
| `line-property-panel-designs.html` | 线条属性面板 UI 方案对比稿（HTML 交互原型，浏览器直接打开） | 📄 参考稿：配色/间距是当时视觉稿，现行 token 以 `src/ui/Theme.h` 为准 |

## 四、归档区（`docs/archive/`，只读参考）

> **归档原则**：已下线功能 / 已废弃方案 / 已完成任务书 / 过时自动生成知识库，一律入档不销毁。
> 每份化石的 3-5 句结论摘要见 `docs/archive/SUMMARY.md`。

| 目录 | 内容 | 说明 |
|------|------|------|
| `2026-07/repowiki/` | 自动生成知识库（68 篇，项目概览 / 核心架构 / API 参考 / 工具与画布系统 / 测试指南） | ⚠️ 严重过时（2026-07 生成，未反映 08 起全部重构），**以源码为准** |
| `2026-08/` | `CONNECTION_REDESIGN_DESIGN.md`（连接卡方案被否决）、`COMPONENT_ROTATE_DESIGN.md`（整组旋转模态已删）、`CLEANUP_REFACTOR_DESIGN.md`（收口任务书已收尾）、`TOOL_SYSTEM_AUDIT.md`（工具链路审查已修复）、`doc_archive_20260829/`（ARCHITECTURE_REVIEW 原始档案 + P2 交接指令，自 build/ 迁入） | 已闭环 / 已废弃 |
| `2026-09/` | `CURVE_P3_DESIGN.md`（曲线 P3 任务书，2026-08-28）、`DART_LINE_REMOVAL_REPORT.md`（省道线彻底下线工程报告）、一致性/重复实现审计全案（正式版 `AUDIT_consistency_duplication.md` + `audit_dryrun.md` 中间稿 + `audit_backup_before_78.md` 备份 + `ledger_7.8_*.md` 逐条核销台账，2026-09 生成 / 2026-12 清账，自 build/ 与 docs/ 迁入） | 已落地 |
| `2026-12/` | `DETOUR_AUDIT_REPORT.md`（拐弯路径审计，10 候选全部闭环） | 已落地 |
| `plans/` | `DETACH_SHADOW_PLAN.md`、`FILE_SPLIT_PLAN_V2.md`、`FILE_SPLIT_PLAN_V3.md`、`plan-sess_577d56c5-4238-4245-abf5-5705b4540ae4.md` | 一次性计划书 / 验收口径档案 |
| `SUMMARY.md` | 归档化石摘要（本区唯一活跃文档） | 回顾历史方案先看这里 |

## 五、领域参考（`docs/reference/`）

| 文件 | 内容 | 说明 |
|------|------|------|
| `garment-formulas.txt` | 服装打版公式集（领宽 / 袖笼高 / 肩长 / 胸宽 / 省道与褶量 / 肩部修正角等 27 条） | 领域数据笔记，无源码引用；公式引擎相关推导时参考 |

## 六、仓库内技能与记忆（不属文档体系，勿当项目文档引用）

| 路径 | 说明 |
|------|------|
| `.agents/skills/qt-anthropic-design/SKILL.md` | 设计流程技能定义（自包含，不引用项目文档） |
| `.reasonix/skills/{arch-optimize,make-ui-not-ai}/SKILL.md` | 工具链技能定义 |
| `.workbuddy/memory/` | 工作记忆存档（含 2026-08-31 连接考古笔记，部分过时，以源码为准） |

---

## 查阅路径速查

| 想查什么 | 去哪 |
|----------|------|
| 构建 / 测试命令、模块边界、架构原则 | `AGENTS.md` |
| 模块分层依赖图、外部中间件 | `ARCHITECTURE.md` |
| 某领域行为为什么这么设计（用户拍板） | `DECISIONS.md`（grep 条目名） |
| 开发规范细节 / 历史沿革、测试用例清单 / 基线 | `CONVENTIONS.md` |
| 「为什么不做 X」的架构评审结论 | `ARCHIVE.md` |
| 踩过的坑、快捷键登记 | `TROUBLESHOOTING.md`（grep 关键词） |
| 某功能的设计细节 / 权威文档 | 本索引第三节 `docs/design/` |
| 历史方案 / 已删功能的设计 | `docs/archive/`（摘要先看 `SUMMARY.md`） |
| 已闭环报告 / 拐弯路径审计 | `docs/archive/2026-09/AUDIT_consistency_duplication.md`（正式版）、`docs/archive/2026-12/DETOUR_AUDIT_REPORT.md` |
| 服装打版公式（领宽 / 省道 / 肩部修正等） | `docs/reference/garment-formulas.txt` |
| 面板 UI 原型（交互/视觉参考） | `docs/design/line-property-panel-designs.html`（浏览器打开） |
