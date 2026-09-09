# WildWind Pattern 架构评估报告

评估日期：2026-08-28 | 范围：src/ 全量（~53k 行）+ CMakeLists.txt + tests/ 组织方式
方法：三路并行代码勘察（引擎 / 交互层 / 持久化与 undo），所有结论附 file:line 证据。

---

## ⚡ 执行状态（续接入口 —— 打开本报告先看这里）

**最后更新：2026-12（**P0 三项 + P1 六项 + P2 六项全部完成**；**ctest 基线 27/29**（仅剩 2 个确定性红：test_serializer::bridgeAuxPointSnappableAndAttachable、test_component::dragComponentLeaderCurveFollowStable，与 P0/P1 收口时完全同一组）**

> **2026-12 本轮完成的 P2 四项**：P2-4 UI 组件归位（33 个文件 tools/→ui/，命名空间 `cad::tools`→`cad::ui`）、P2-6 求解开销量化（新增 `test_resolve_scale`，结论：**不需要 worker 线程**）、P2-1 版本迁移框架（新增 `document/FormatMigration.*` + `test_migration` 11 例，把序列化器里的字段嗅探变成具名的 v0→v1 步骤）、P2-3 条件等待再迁移一批（含 `settle()` 新助手）。详见下方各条目的 ✅ 段落。

| 项 | 状态 | 完成证据 / 备注 |
|----|------|----------------|
| **P0-1** 双栈收口 | ✅ 已完成 | `src/app/MainWindow.cpp:102` `m_undoStack = m_paramDoc->undoStack()`（非拥有别名，原 `new QUndoStack` 已删）；`src/parametric/ParamDocumentLifecycle.cpp` `clear()` 末尾同步 `m_undoStack->clear(); setClean()`（:54-57）；`ParamDocument.h` clear() 注释已更新 |
| **P0-2** 命令 ID 撞号 | ✅ 已完成 | 新建 `src/document/commands/CommandIds.h` 集中枚举（MoveBlock=1001/SetFollowerAngle=1002/RotateBlock=1003/**SetVariableValue=1004**）；4 处 `id()` 改枚举；4 处 `mergeWith`（MoveBlock/RotateBlock/SetFollowerAngle/SetVariableValue）`static_cast` → `dynamic_cast` + 判空兜底 |
| **P0-3** 绕过 undo 的直写路径 | ✅ 已完成 | SegmentAnchorTab 四 lambda + 释放跟随 → `SetCurveTangentCommand` / 新建 `ReleaseCurveFollowCommand`；LinePropertyDialog `onAccepted` 推新建 `SetLinePropertiesCommand`（live-apply 会话可整体撤销）；CanvasView(3)/SegmentExtendCard(1)/LayerPanel(5) 的 "undoStack 为 null 时静默降级直改" else 分支删净、统一走文档栈命令（LayerPanel 注入栈成员随删） |
| **P1-4** 收敛「魔法 4 轮」 | ✅ 已完成 | `Resolver.h` 新增 `constexpr int kMaxSettleRounds = 4`（单一定义点）；**10 处**硬编码 4 全部引用（Resolver.cpp 交点不动点/Step7 aim/Step8 dart；ParamDocumentResolver.cpp Phase 1 aux、Phase 2 working、2.5、3、4 + 组件跟随内外层）；全部循环加收敛标志，**预算耗尽即报 NotConverged**（新增 anonymous-ns helper `reportNotConverged()` 做同 pass 去重）。测试 test_resolver 31/31（新增 aimRingExhaustingBudgetReportsNotConverged + 对照 oneWayAimReportsNoDiagnostics） |
| **P1-5** 表达式缓存实例化 | ✅ 已完成 | 新增 `ExpressionCache` 类（实例而非进程级 static）：`ParamDocument` 持有一只、经 `Resolver::resolveAll` 新参数 → `EvalContext::cache` → `cacheFor(ctx)` 贯穿全部求解路径，`clear()` 随文档释放；无上下文调用方走 `ExpressionEvaluator::defaultCache()`（**thread_local**，非共享可变全局）。**引用稳定性**：deque 分代（满 8192 开新一代、旧代只退休不释放），彻底消除旧 static "整体 clear 悬空全部已发引用"的隐患。测试 test_expression 32/32（新增 cacheIsInstanceScoped / cacheReferencesSurviveGrowthGuard / documentOwnsCompileCache） |
| **P1-1** geometryEpoch 收口 | ✅ 已完成 | `Block::geometryEpoch` 私有化为 `m_geometryEpoch`，只读 `geometryEpoch()` / 唯一 bump 入口 `touchGeometry()`；**26 处**手写 `++geometryEpoch`（engine/commands/tools/canvas/tests 四层）+ Block 内部 4 处全部改走入口（`grep -rn touchGeometry src/` = 全部刷新点）。测试 test_resolver 33/33、test_commands 46/46、test_extend 仅剩 3.gcad 基线红 |
| **P1-2** 门面瘦身 | ✅ 已完成（含按域分组前半段，B1~B3） | 14 个 `*Raw` 静默恢复 API 从公共头**私有化**，唯一通道 = `cad::param::RawModelAccess`（`parametric/ParamDocumentRaw.h`，ParamDocument 的唯一 friend）；35 处调用点改为 `RawModelAccess::xxxRaw(doc, ...)`，信任在调用点可见可 grep。门面顶层方法 −14 +1。**按域分组前半段（2026-08-29 B1~B3 完成）**：126 个公共方法（19 域）收敛到 **6 个只读窄接口视图**（`BlockView.h` / `AttachmentsView.h` / `ComponentsView.h` / `MeasurementsView.h` / `LayersView.h` / `VariablesView.h`，均 header-only、无状态持 `const ParamDocument*`、门面尾 include），累计迁移 **134 处调用点**（B1 27 / B2 44 / B3 63）；写路径与 mutable find*（编辑通道）全部留在门面；DocumentLifecycle（求解/undo/序列化/序号）**评估后不成视图**（门面级生命周期操作，包装只加间接层）；旧接口收敛审计 = **零删除**（视图委托门面方法，门面方法永远是视图的实现基底；`layerEffectivelyVisible` 全仓零调用已标记未来清理候选） |
| **P1-3** 可变裸指针护栏 | ✅（短期）+ 中期项经 C1 审计结案 | blocks 结构性变更（add/remove/clear）统一 `bumpStructureEpoch()`；新增可观察 `structureEpoch()` + debug 版 `blockPointerInRange(p)`（release 编译为 true，零成本）；访问器注释与 AGENTS.md 写入「一次一取、禁止跨变更持有」约定。测试 test_resolver `structureEpochTracksBlockMutations` / `blockPointerMayLeaveStorageAfterGrowth`。**中期句柄化（2026-08-29 C1 审计结案：不做）**：全仓 361+619 处 findBlock/blockById 审计——失效面仅 4 个 bump 点（附件/求解/图层/变量操作均不失效）、成员持有 0、违规 0（唯一跨变更位点 RotateCopyGesture::convert 已手工重查+注释）、range-for/lambda/延迟变异均 0；唯一推理密集位点 = SegmentConnectionCardConn:240（依赖"命令 redo 不结构变异"隐式不变量）。generation 设计被修正（QUuid 唯一不回收 → 无 ABA，句柄 = id + 哈希校验即可）。**护栏判定足够**；若未来出现指针类 bug，B 类视图 `byId()` 已预留句柄承接点可低成本重启 |
| **P1-6** 分层设防 | ✅ 已完成 | ①`src/document/DeleteImpactConfirm.*` **迁入 `src/ui/`**（本就编入 `gcad_ui`，目录与库归属对齐）；②新增 `tools/check_layering.py`（扫描 7 个模块目录的 include，报告向上层取依赖，exit 1），**当前全仓干净**；③CanvasView → tools 反向依赖拆除：新增 `canvas/InputDispatcher.h`（依赖倒置，ToolManager 实现之）+ `SegmentHit` 信号，右键菜单/QuickAuxDialog/切工具/选中/toast 全部上移 app 层（`MainWindow::onSegmentContextMenu`），`setToolManager` → `setInputDispatcher(InputDispatcher*)`（64 处调用点，ToolManager 隐式上转型零改动） |
| **P2-2** 测试依赖外部活档 | ✅ 已完成 | **清除了全部"仓库外基线"**：test_intersection_update 5 个用例改用 `CrossLayerDoc` 合成跨层档（含 Phase 2.5/3/4 镜像两种拓扑）→ 10/10；test_extend 删除真实档探针、公式起点延长改为合成档 → 17/17；test_p612_colinearity 由"加载 build/out/Debug/1.gcad（常年 QSKIP 空跑）"改为合成借用点档 → 3/3；test_serializer 删除写死 UUID 的用户档诊断用例；test_nav_smoke 的截图输出从 `e:/garment-cad/build/…` 改到临时目录。**新增 `tools/check_test_fixtures.py`**：扫描 tests/ 的绝对路径/构建产物引用，发现即 exit 1（当前干净）。**ctest 基线由 23/27 提升到 25/27**（仅剩 2 个确定性红：test_serializer::bridgeAuxPointSnappableAndAttachable、test_component::dragComponentLeaderCurveFollowStable） |
| **P2-5** undo 上限 | ✅ 已完成 | `ParamDocument::kUndoStackLimit = 150`，构造时 `setUndoLimit()`（命令带全量模型快照，删除命令还带级联子图，无上限即无界内存增长）。回归 test_commands 47/47（新增 `undoStackLimitIsBounded`：超限后 count 封顶、undo/redo 仍可用） |
| **P2-3** GUI 固定等待 | 🟡 增量推进 | 工具已就位：`waitUntil(pred, 5s)` / `grabStable(widget)` / **新增 `settle()`**（断言「什么都没发生」时用——没有可等的状态，只能排空事件循环）。本轮**根治了唯一的登记抖动点** `test_dialog_tabs::switchBackAfterTyping`（根因：输入值走 debounce 定时器应用，断言却在 `mouseClick()` 返回后立刻跑；负载下定时器还没触发，健康功能被判失败）+ test_segment_edit_bar 4 处（改 waitUntil）+ 1 处负向断言改用 settle。**全仓 `QTest::qWait(` 调用 150 处（A 类收官，2026-08-29）**：A1~A6 六批后**零未判定点位**——65 处 `qWait(80)`（规则 3 设计保留）、85 处逐点位判定注释（同步链路保留 / 手动跑档保留 / 中间态无谓词保留）、条件等待 waitUntil 22 + settle 2 + grabStable 2。**已勘察、不建议机械迁移**：76 处 `qWaitForWindowExposed`、53 处 sendMouse 式 `sendEvent(view.viewport())`+`qWait(20)` 对，前者是**过度等待**（qWait 不会提前返回，80ms 一定等满，负载下不会更短）而非「等不够」，换成瞬时条件反而会暴露被掩盖的竞态；后者需要逐个识别「工具状态机吃掉了这个事件」的可观测判据。剩余项按文件分批、每批配条件，属长期增量。**2026-08-29 A1 批次（test_rotate_copy.cpp 1~844 行，modeSwitchKeepsFormula 之前，区间 18 处 qWait）**：逐处判定收口——①sendMouse lambda 8 处 + sendConfirm `qWait(10)` 1 处判定为**同步链路、保留+判定注释**（Qt 6.11 下 `QTEST_QPA_MOUSE_HANDLING` 未定义 → `QTest::mouseClick(QWidget)`/`sendEvent` 均走 `qApp->notify` 同步投递；ToolRotate/AngleHud 链路无定时器/排队连接，断言不依赖异步工作——后续批次照此办理，独立点位才逐个判 waitUntil/settle）；②1 处 Esc→`settle()`（escCancelsCopy，负向断言）；③8 处 `qWaitForWindowExposed`+`qWait(80)` 原样保留。单跑耗时持平（基线 13.65s → 13.4~13.9s），43 用例全 PASS。**同日 A2 批次（864~1363 行，modeSwitchKeepsFormula → endTargetRotateCopyUndoRedoKeepsOriginalAim 之前，区间 15 处）**：6 处 sendMouse 对按 A1 同步链路结论保留+注释（2 处 12 空格缩进变体，嵌套作用域内）、9 处 `qWait(80)` 原样、区间内 3 处 waitUntil 为前批已迁移；无其他时序敏感点（无 processEvents/grab/exec/QTimer）。基线 14.66s → 15.2/14.8/15.3s（噪声内），43 用例全 PASS。**同日 A3 批次（1376 行起至文件尾，区间 47 处）**：23 对 sendMouse 按 A1 结论保留+注释、23 处 `qWait(80)` 原样、**1 处 `qWait(30)` → `waitUntil`**（arcLengthModeOverflowNormalized：HUD 输入 270 → 断言存储 followerAngle 变 270，"值变了"→ 谓词 = 存储断言本身；已核实 AngleHud textChanged 直接回调同步应用、无 debounce，转换后语义不变且抗异步化改动）。**test_rotate_copy.cpp qWait 收官**：全文件 78 处调用全部有归属——40 处 `qWait(80)`（规则 3 设计保留）、37 处 sendMouse `qWait(20)` + 1 处 `qWait(10)`（全部带判定注释）、条件等待 4 处 waitUntil + 1 处 settle，**零未判定点位**。单跑 14.0/13.9/13.7s（快于 A2 轮），43 用例全 PASS。**同日 A4 批次（test_select_wkey.cpp 1~926 行，10 用例，区间 28 处）**：10 处 `qWait(80)` 原样、18 处 sendEvent 相邻保留+注释（本文件是 `\r\r\n` 换行——全程 python 处理并逐行保持原格式，Edit 工具不可用）、**2 处 → `waitUntil`**（登记过的偶发用例 endpointClickAfterConfirmKeepsSelectionOperable：①覆盖物计数归零 ②拖拽后坐标断言——覆盖物已核实为 removeItem+立即 delete（同步），两处转换均"同步时立即返回、负载下锁住判据"）。基线 9.09s → 9.2~9.8s（噪声内），22 用例全 PASS。**同日 A5 批次（969 行 bodyDragMovesLine 起至文件尾，区间 22 处）**：9 处 `qWait(80)` 原样、13 处保留+注释（4 对标准 sendMouse + 4 个 click lambda + 2 个按键事件 + HUD Esc qWait(30) + view.scale 后 1 处 + 悬停 move lambda 1 处——逐判均为同步链路，**0 处转换**）。**test_select_wkey.cpp qWait 收官**：全文件 50 处调用全部有归属——19 处 `qWait(80)`（规则 3）、31 处全部带判定注释、2 处 waitUntil（A4 偶发用例转换），**零未判定点位**。基线 9.53s → 9.3/9.0/9.2s，22 用例全 PASS。**同日 A6 批次（其余 10 文件收官）**：test_component 8 注释 / test_dialog_tabs **2 转换**（grabStable 替代 150ms 等首帧 + waitUntil 端点双微卡布局落定，文件 qWait 清零）/ test_aux_layer 2 注释 / test_segment_edit_bar **2 转换**（waitUntil 名称写入模型 + undo 恢复构建角）+1 保留 / test_tool_intersection 2 注释 / test_curve_edit 1 注释 / test_realdoc_full 保留+注释（手动跑档不自动验证）/ test_canvas_perf **1 转换**（grabStable 替代 300ms GL 首帧 + 补 TestHelpers include）/ test_smartpen_aux 保留+注释（`\r\r\n` 文件）/ test_hold_show 本就 0 处。**A 类（qWait 条件化）全部收官：全仓 150 处调用零未判定** |
| **P2-4** UI 组件归位 | ✅ 已完成 | **33 个文件 `tools/` → `ui/`**：LinePropertyDialog(1306 行)/Segment{Anchor,Aux,Aim,Angle,Ref,Extend}Card/SegmentConnectionCard(+Build/Conn/Dart/Refresh 4 个切片 TU)/AngleHud/AuxPointForm/IntersectionForm/PointRefEdit/QuickAuxDialog/MeasureResultDialog + 依赖的 `LayerFeedback.h`；命名空间同步 `cad::tools` → `cad::ui`（44 处限定引用 + tools 层 4 处前置声明改为 `namespace cad::ui { class X; }`）；CMake 源列表 33 项 `gcad_tools` → `gcad_ui`，并给 `gcad_ui` 补 `gcad_canvas` 链接（这些控件要访问 CanvasScene/BlockItem 做实时刷新）。**`tools/` 现在一个 QWidget 都没有**，只剩手势（Connect/Marquee/RotateCopy）、状态机、SnapEngine、LineFactory、RotateGizmo、LeaderCandidatePicker。`check_layering.py` 干净，构建全绿，ctest 基线不变 |
| **P2-1** 版本迁移框架 | ✅ 已完成 | 新增 `src/document/FormatMigration.h/.cpp`：`kFormatVersion` 单一定义点 + `MigrationStep` 注册表 + `migrate(fromVersion, root, warnings, error)` 逐级前进；**缺口即拒绝**（缺失 vN→vN+1 步骤时报错而不是退回字段嗅探——这是旧实现静默损坏的入口）。首条链路 `migrateV0ToV1`（"layer-refs"）把散在 `DocumentSerializer::deserialize` 里的历史知识搬了出来：v0 没有辅助层、块按整数下标引用图层 → 插入辅助层并把下标重写成稳定 id。序列化器只留**模型不变量**（辅助层缺失时补一个——这是 `Layer.h` 声明的模型约束，不是历史）与**损坏兜底**。新增 `tests/test_migration.cpp` **11/11**（v0 下标位移 / 无 layers 数组 / 已有辅助层不位移 / v1 原样不动 / 版本过高拒绝 / 链路完整性 / 无 layer 字段留给序列化器 / v1 归档往返），该历史路径此前**零测试覆盖** |
| **P2-6** 单线程求解 | ✅ 已量化（结论：不做 worker 线程） | 新增 `tests/test_resolve_scale.cpp`，用既有 PerfProbe 分档实测（每档中位数，60fps 预算 16.67ms）：20 块 → 拖拽 105µs / 全量 167µs；100 块 → 486µs / 1091µs；400 块 → 2035µs / 9124µs；800 块 → 4039µs / 31495µs。**结论**：①**交互路径（拖拽）近似线性**，实测指数 ≈1.0，真实衣片（远小于 100 块）只占预算 1~4%，800 块时仍只占 24% —— 拖拽永远不需要 worker；②**全量 resolve 才是超线性的那条**（指数 ≈1.5~1.8，800 块 31ms ≈ 掉 2 帧），但它只由离散动作（改变量）触发，不是每帧；③真要优化，目标是 `r.settle`（占 `r.total` 50~60%）与 `resolve.aux`（大档位主导），**不是换线程**——模型是一个可变 QObject 图，上线程要么整体加锁要么整份快照，代价远大于省下的 30ms。**按报告要求"先量化再决定"，据此结案为"不做"** |
| **Rust 试点** | ⬜ 未开始 | 前置 = P0（✅ 已完成）+ P1-3；唯一高收益候选 = **ExpressionEvaluator**（实测 .cpp 651 + .h 144 ≈ 795 行，test_expression **32** 用例全绿；**静态缓存隐患已由 P1-5 在 C++ 内解决**，Rust 化对它的收益降为纯语言偏好，性价比已低于当初评估） |
| **B1** 门面按域分组：审计 + blocks 试点 | ✅ 试点完成（结论：**方案可行，建议继续 B2**） | 审计：ParamDocument 公共 API **19 域 126 方法**，src 调用点 1196（blocks 422 / 附着 172 / 求解 116 / 图层 132）；tests 2179。方案 = 按域归并为 7 个只读窄接口视图（Blocks/Components/Attachments/Variables[变量+公式+组]/Measurements[关联+测量+角度]/Layers[图层+脏标记]/DocumentLifecycle[求解+undo+序列化]），**写路径留在门面**。落地：新增 `src/parametric/BlockView.h`（`BlocksView`：all/byId/epoch/impactOf，无状态持 `const ParamDocument*`，头部注释即域契约；门面新增 `blocksView()` 访问器 + 头尾 include，旧 API 零改动）；迁移 **27 处 const 位点**（ToolSelect 11 / CanvasScene 4 / SegmentAngleCard 6 / ComponentCommands 6，按文件整组避免半迁移）。**战略价值**：C 类句柄化落地时 `byId()` 原位换成句柄语义、调用点不再动。构建绿、ctest 27/29（基线两项）、两 check OK。B2 清单见下 |
| **B2** 门面分组：附着 / 组件 / 测量 域 | ✅ 已完成（**44 处调用点迁移**，VariablesView 缓建） | 新增 3 个只读视图（同 B1 模式：无状态持 `const ParamDocument*`、门面访问器 + 前置声明 + 尾部 include、CMake 登记）：**`AttachmentsView.h`**（all/byId/lockedClosure/bridgesPinnedTo；mutable findAttachment 留门面——它是唯一授权的原位编辑通道，必须保持"编辑"可 grep）迁移 **12 处**（ConnectGesture 3 / ToolSelect 3+1 / ToolRotate 3 / bridgesPinnedTo 3）；**`ComponentsView.h`**（all/byId/ofBlock/boundingBoxOf/boundingBoxOfBlocks/closure/memberOwningPoint）迁移 **30 处**（ComponentCommands 6 / ConnectGesture 9 / ToolRotate 6 / ToolSelect 3 / ComponentTab 4 / SegmentExtendCard 1 / CanvasScene 1）；**`MeasurementsView.h`**（linkedVars/measureVars/angleMeasures/measureByOwner）迁移 **2 处**（MainWindow/DocumentCommands 的烘焙入口）。**VariablesView 缓建**：变量域读面只有 3 个 const 容器，findVariable/findFormula/findFormulaGroup 在门面与 VariableStore 均无 const 重载（ui 的 10 处只读调用是经 mutable 访问器隐式转 const）——做 byId 需连 store 加 const 重载，属门面扩面决策，单独记入 B3 候选。容器读（attachments()/components()/linkedVars() 等 57+ 处）**有意不迁移**：方法名本身已表域，视图化是噪声；视图的 all() 保留为域契约。构建绿、ctest 27/29（基线两项）、两 check OK |
| **B3** 门面分组：图层域 + 变量域 + 收敛审计 | ✅ 已完成（**63 处调用点迁移**，B 类收官） | **`LayersView.h`**（方法名与门面 1:1，仅 layers→all / layerById→byId；含 hasCrossLayerAttachments 查询）迁移 **53 处**（LayerPanel 27 / MainWindow 15 / SnapEngine 7 / LayerCommands 7 / ToolSelect·LineFactory·BlockItem 各 4 / LayerFeedback.h 4 / DocumentSerializer 5 / DocumentCommands 2 / MeasureTab 1）；**变量域 const 重载落地**：VariableStore + 门面各加 3 个 const find 重载（findVariable/findFormula/findFormulaGroup，消除了 ui 经 mutable 访问器伪读的模式），新增 **`VariablesView.h`**（all/byId/formulas/formulaById/groups/groupById）迁移 **10 处**（VariablePanel 7 / FormulaTabModel 2 / DocumentSerializer 1）。**收敛审计（零删除）**：视图委托门面公共方法 → 门面方法永远是视图的实现基底，不存在"零调用者旧方法"；`componentClosure` 看似零外部调用但门面内部（ParamDocumentResolver:194 求解管线）有无点号调用——统计正则的盲区教训；`layerEffectivelyVisible` 全仓零调用（实现已退化为 layerVisible 直接委托）→ **保留并标记未来清理候选**（渲染灰显契约文档，BlockItem 尚未采用，删契约不如留待采用）。**DocumentLifecycle（求解/undo/序列化/序号）评估后不成视图**：门面级生命周期操作，包装只加间接层。B 类总账：6 视图 / 134 处迁移 / 0 API 破坏 / 0 删除。构建绿、ctest 27/29（基线两项）、两 check OK |

**验证记录（P0 + P1-4 完成后实测）**：Debug 全目标构建通过；`ctest` **23/27 通过**，4 红均为既有基线/环境漂移、非本次回归：
- test_serializer::bridgeAuxPointSnappableAndAttachable（既有基线红）
- test_component::dragComponentLeaderCurveFollowStable（31.4798mm，既有基线红）
- test_intersection_update 5 例 + test_extend::savedDocFormulaStartExtendRenders（依赖 E:/3.gcad 活档内容漂移）
- undo 敏感用例全绿：test_commands **46/46**、test_rotate_copy **43/43**、test_dialog_tabs **9/9**、test_select_wkey **22/22**
- **P1-4 专项**：test_resolver **31/31**（含 2 个新用例）；**真实活档零误报**——e:/1.gcad、e:/2.gcad、e:/3.gcad 全量 resolve 后 NotConverged 均为 0（1.gcad 仅 1 条既有的 Dangling 诊断，与本次无关）；诊断敏感用例（test_rotate_copy 21 处 `diagnostics().empty()`、test_aux_layer 3 处）全绿
- **P1-5 专项**：test_expression **32/32**（含 3 个新用例）；活档探针再次确认 NotConverged=0，且文档缓存确被使用（1.gcad 39 条 / 3.gcad 21 条字节码，均 1 代）
- **P1-1/P1-2/P1-3/P1-6 专项**（每项改完都跑全量）：test_resolver **33/33**、test_commands **46/46**、test_expression 32/32、test_serializer 21/1skipped（仅基线红）、`python tools/check_layering.py` = 干净；ctest 始终 23/27 = 同一组 4 红
- **P2 专项（2026-12）**：`python tools/check_test_fixtures.py` = 干净；test_intersection_update **10/10**（原 5 红）、test_extend **17/17**（原 1 红）、test_p612_colinearity **3/3**（原长年空跑 QSKIP）、test_commands **47/47**；**ctest 确定性基线 25/27**。GUI 抖动（负载相关、单跑全绿）在整批 ctest 下仍偶发，红集合每次不同，见 P2-3 条目的实测说明

**验证记录（P2 四项完成后实测，2026-12）**：Debug 全目标构建通过；`tools/check_layering.py` = 干净；`tools/check_test_fixtures.py` = 干净；**ctest 29 个用例 → 27 通过 / 2 失败，失败集合与 P0/P1 收口时完全一致**（test_serializer::bridgeAuxPointSnappableAndAttachable、test_component::dragComponentLeaderCurveFollowStable 31.4798mm）。
- **P2-4 专项**：33 文件 `git mv` + 命名空间改名后一次构建通过（唯一需手改的是 4 处前置声明与 `crossLayerToast/Badge` 的 `cad::ui::` 限定）；`grep -rn '#include "tools/' src/` 归零；ctest 仍 25/27（当时的基线）。
- **P2-6 专项**：`test_resolve_scale` 2/2（含 aux 冻结在 80 块/层的证明）。**两次独立跑的对比本身就是结论的一部分**：空闲机 800 块拖拽 4039µs，构建刚结束时（机器忙）6597µs —— 差 63%，所以该测试只做正确性断言、绝不断言耗时（否则必 flaky）。
- **P2-1 专项**：`test_migration` **11/11**；`test_serializer` 21/1 与改动前完全同组（唯一红是既有基线 bridgeAuxPoint）。**中途踩到的真问题**：把「缺辅助层就补一个」也当成历史搬走是错的 —— 它是 `Layer.h` 声明的**模型不变量**（每文档恰好一个辅助层，index 0），`test_serializer::degradedValuesProduceWarnings` 立刻红了；正确切分是「不变量留在序列化器，下标位移这一条历史搬进迁移」。
- **P2-3 专项**：修 `switchBackAfterTyping` 后，整批 ctest 连续两轮的 GUI 红集合从「每轮换一批」收敛为**零**（第 3 轮出现过 test_select_wkey/test_curve_edit，单跑全绿，属既有负载抖动，非本次改动）。

**经验沉淀**：P0 三项 + P1-4 + P1-5 的根因/做法/教训已写入 TROUBLESHOOTING.md 第 4 组末尾（P0 =「undo 双栈分裂 + 命令 ID 撞号 + 绕过 undo 的直写路径」；P1-4 =「求解收敛『魔法 4 轮』收口 + 未收敛可观测化」；P1-5 =「表达式编译缓存实例化」）。估算：P0 三项 ≈ 1 个专注工作日；P1+P2 全量 ≈ 15~20 个 P0（约 3.5~4 周单人），其中 P1-2/P1-6/worker 线程计划占一半以上，建议单独立项。

---

## 一、总体结论

**分层骨架是健康的**：geometry → parametric → canvas/document → ui → tools → app 七库单向依赖、ParamDocument 信号驱动观察者、值语义实体 + QUuid 引用、序列化降级加载策略——这些都是教科书级的正确决策，执行度也高。

**但存在 3 个 P0 级真实缺陷（undo 子系统）和一批"约定代偿类型"的系统性脆弱**。当前最大的架构风险不是结构，而是：不变量靠注释和登记表维系，编译器不设防。

---

## 二、P0 —— 需要尽快修复的真实缺陷

### 1. 双 QUndoStack 分裂 + 文档栈跨文档污染
- `ParamDocument` 自持一个 undo 栈（ParamDocument.cpp:26, ParamDocument.h:552），`MainWindow` 又建一个（MainWindow.cpp:97）。
- CanvasView/ToolSelect/各 Card 往**文档栈**推命令（CanvasView.cpp:503, 593；ToolSelect.cpp:92；SegmentConnectionCardConn.cpp:249-461 共 12 处），但菜单撤销动作与 isClean 脏标记只绑 **MainWindow 栈**（MainWindow.cpp:218-222, 1340）。
- 后果：文档栈里的命令 **Ctrl+Z 撤不掉、不参与脏标记**；且 `ParamDocument::clear()` 注释明示 "Does NOT clear undo stack"（ParamDocument.h:459），打开/新建只清 MainWindow 栈（MainWindow.cpp:1377, 1407）——**旧文档的命令快照会在新文档里 undo 复活，跨文档污染**。
- 建议：收口为单一栈（文档持有，MainWindow 只做 UI 绑定），`clear()` 时同步清栈。

> ✅ **已完成（2026-12）**：MainWindow 删除自建栈，`m_undoStack` 改为文档栈的非拥有别名（`MainWindow.cpp:102`），所有 setUndoStack 注入 / 菜单 Ctrl+Z / isClean 脏标记 / cleanChanged 统一绑定同一只栈；`ParamDocument::clear()` 末尾同步 `m_undoStack->clear(); setClean()`（ParamDocumentLifecycle.cpp:54-57，反序列化路径 DocumentSerializer.cpp:819 一并受益）。原 defunct 的注入栈成员（LayerPanel）随 P0-3 已删。

### 2. mergeWith 命令 ID 撞号 = 未定义行为
- `RotateBlockCommand::id() = 1003`（BlockCommands.h:96）与 `SetVariableValueCommand::id() = 1003`（VariableCommands.h:56）**撞号**。
- `mergeWith` 仅按 id 判型后 `static_cast`（BlockCommands.cpp:319, VariableCommands.cpp:68）——栈顶类型不匹配时把 SetVariableValueCommand 强转为 RotateBlockCommand 读字段，**UB**。
- 建议：命令 id 改集中枚举（单一定义点），mergeWith 内加 `dynamic_cast` 或 debug 断言兜底。

> ✅ **已完成（2026-12）**：新建 `src/document/commands/CommandIds.h` 集中枚举 `cad::cmd::CommandId`（MoveBlock=1001 / SetFollowerAngle=1002 / RotateBlock=1003 / SetVariableValue=**1004**，撞号者重编号）；4 处 `id()` 改用 `static_cast<int>(CommandId::X)`；四个 mergeWith（MoveBlock/RotateBlock/SetFollowerAngle/SetVariableValue）的 `static_cast` 全部 `dynamic_cast` + 判空返 false 兜底。CommandIds.h 已注册进 CMakeLists gcad_document 列表。

### 3. 存在完全绕过 undo 的模型写路径
- **SegmentAnchorTab.cpp:150-231**：四个 lambda 直写 `pt->tangentIn/Out` + `++geometryEpoch` + `resolveAll()`——**锚点切线编辑完全不可撤销**。
- LinePropertyDialog.cpp:942-987 live-apply 直写 seg 字段，靠快照回滚兜底；SegmentExtendCard.cpp:267-277 / LayerPanel.cpp:840-867 / CanvasView.cpp:503-514 存在 "undoStack 为 null 时静默降级直改" 双写路径。
- 建议：模型写路径统一收口——要么走命令，要么编译期拿不到可变引用（结合下文 P1-3 的封装改造）。

> ✅ **已完成（2026-12）**：①SegmentAnchorTab 四 lambda（切线模式/重置/锁定/spin）统一改走 `SetCurveTangentCommand`（新增 `pushTangent` 提交 helper，no-op 短路防污染撤销链），「释放跟随」改走新建 `ReleaseCurveFollowCommand`；②LinePropertyDialog `onAccepted` 将「打开快照 → 确认状态」差异推成一步新建 `SetLinePropertiesCommand`（仅非创建态、有差异才 push，undo 恢复快照 / redo 重放确认状态），live-apply 会话从此可整体撤销；③P0-1 后文档栈恒非空 → CanvasView(3 处)/SegmentExtendCard(1 处)/LayerPanel(5 处) 删除 "undoStack 为 null 时静默降级直改" 的 else 分支统一走 `m_doc->undoStack()->push`，LayerPanel 注入 m_undoStack 成员及其 setUndoStack 注入调用随之删除。

---

## 三、P1 —— 系统性脆弱（约定代偿类型）

> **状态（2026-12）：全部未开始 ⬜**。推荐开工顺序见顶部执行状态表与第六节；各项规模/耗时估算见会话笔记「P1-P2 工期估算」。

### 1. geometryEpoch：20+ 处手工 bump 的脏标记
裸公有字段 `++geometryEpoch` 散点横跨三层（Block.cpp:120/636/1133；Resolver.cpp:166/370/640；BlockCommands.cpp 8 处；BreakCommands.cpp:906；SegmentAnchorTab.cpp:159-224 4 处），失效判定（BlockItem.cpp:354-356）与 bump 点无封装关联，漏一处 = 画布脏缓存。
**建议**：收敛为 `Block::touchGeometry()` 私有字段 + 唯一入口；长期看应让"语义属性变更"自动派生 epoch（写路径收口后自然成立）。

> ✅ **已完成（2026-12）**：`Block::geometryEpoch` → 私有 `m_geometryEpoch`，对外只留 `geometryEpoch()`（只读）与 `touchGeometry()`（唯一 bump 入口）。**30 处**手写 `++geometryEpoch` 全部改走入口——外部 26 处（Resolver 3 / ParamDocumentResolver 1 / MeasurementStore 1 / BreakCommands 1 / BlockCommands 14 / ToolCurveEdit 2 / test_commands 4）+ Block 内部 4 处（内部也统一走 `touchGeometry()`，`m_geometryEpoch` 只在该函数体内被写）。收益不止是封装：**刷新点现在可 grep**（`grep -rn touchGeometry src/`），漏 bump 从"靠记忆"变成"可审计"。AGENTS.md 三条铁律（画布缓存刷新 / 曲线增删点 / 延长尾巴）同步改为 `touchGeometry()` 表述。回归：test_resolver 33/33、test_commands 46/46、test_extend 仅剩 3.gcad 基线红，ctest 23/27 不变。

### 2. ParamDocument 门面名不副实
693 行头文件、~130 个 public 方法、19 个信号、横跨 12 个域（ParamDocument.h:48-507）。所谓子域拆分（ParamDocumentBlocks/Attachments/Resolver.cpp）只是同一类的成员函数切块，不是真子对象。真正的 PIMPL 子域只有 LayerRegistry/VariableStore/MeasurementStore（ParamDocument.h:560-562）。
**建议**：不必激进拆类，但应按域分组 public 接口（如 `doc->blocks().xxx()` 窄接口对象），把 14 个 `*Raw` API（ParamDocument.h:466-502）从公共头移入仅 serializer/commands 可见的友元通道。

> ✅ **已完成（2026-12，取建议的后半段）**：14 个 `*Raw` 静默恢复 API 全部**私有化**，唯一通道是 `cad::param::RawModelAccess`（新增 `src/parametric/ParamDocumentRaw.h`，ParamDocument 的唯一 friend，14 个静态转发函数）。调用形如 `cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, atts)`：**不 include 该头就编译不过**，且调用点的"信任"可 grep。35 处调用点已改（DocumentSerializer 12 / commands 17 / ToolRotate 5 / CopyDragController 1 / VariableStore 1），门面顶层方法 −14 +0。实际调用方比报告预估的多两层：除 serializer/commands 外，**tools 层的拖拽取消快照还原**（ToolRotate/CopyDragController）同样依赖 verbatim 恢复，通道对其开放（这是真实需求，不是漏网）。**未做**建议的前半段（按域分组 `doc->blocks().xxx()` 窄接口）——那是更大的门面重构，留作独立项目。

### 3. 可变裸指针逃逸通道
`findBlock/blockById` 返回 vector 内部可变裸指针（ParamDocument.h:80,127），vector 扩容即悬空，有效期无任何保护；attachment 原位编辑靠注释约定（ParamDocument.h:234-238）。
**建议**：短期内给可变访问器加 debug 版"世代计数"断言；中期考虑句柄（id + generation）替代裸指针。

> ✅ **短期项已完成（2026-12）**：blocks 的结构性变更（addBlock / removeBlock / addBlockRaw / clear）统一 `bumpStructureEpoch()`，新增可观察的 `structureEpoch()`（缓存失效可key）+ debug 版 `blockPointerInRange(p)`（校验指针是否仍落在当前 `m_blocks` 存储区内；release 下编译为 `return true`，零成本零行为变化）。访问器注释与 AGENTS.md 写入约定：**一次一取，禁止跨结构性变更持有 `Block*`**。测试 test_resolver `structureEpochTracksBlockMutations`（add/remove/clear 递增，resolve 与纯属性编辑不递增）+ `blockPointerMayLeaveStorageAfterGrowth`。
> ⬜ **中期项（句柄化）未做**：本次勘察发现代码库其实已普遍遵守"变更后重新 findBlock"的写法（ToolRotate/BreakCommands 逐段重取），所以真句柄（id+generation）的收益主要在"让编译器替我们守住这条纪律"，属独立项目，勿以为已解决。

### 4. 求解收敛靠"魔法 4 轮"
跨块交点/端点指向/测量回灌均为硬编码 4 轮定点迭代（Resolver.cpp:403, 492；ParamDocumentResolver.cpp:310, 345），收敛靠希望而非证明，且 4 这个数散落 4 处无常量统一。块内循环引用无检测、点静默保持 unresolved（Block.cpp:204-267）。
**建议**：提取 `kMaxSettleRounds` 常量；迭代后统一走 `NotConverged` 诊断（已有雏形 Resolver.cpp:318-326），把"静默未解"升级为可观测状态。

> ✅ **已完成（2026-12）**：`Resolver.h` 新增 `constexpr int kMaxSettleRounds = 4`（注释写明"禁止在循环里重打字面量"），7 处硬编码全部引用（比报告登记的 4 处更多——实测另有 Step 8 dart、Phase 4、组件跟随内外层）。**语义约定（勿反）**：所有循环都是"本轮无进展即 break"，故**预算耗尽 ≡ 最后一轮仍在动 ≡ 未达不动点**；除 Phase 3 外原先全部静默，现统一报 `NotConverged`（`runIntersectionFixpoint` 加 `bool* budgetExhausted` 出参供 Step 6d/7b 上报；aim/dart/Phase2.5/Phase4/组件跟随各加收敛标志；ParamDocument 侧经新增 `reportNotConverged()` helper 去重，防多相位重复计数）。回归：test_resolver 31/31 + 三份真实活档 NotConverged=0（无误报），ctest 仍为 23/27 基线。

### 5. 进程级静态表达式缓存
ExpressionEvaluator 的编译缓存是函数级 `static QHash + deque`（ExpressionEvaluator.cpp:67-80）：非线程安全，8192 条满时整体 clear 使已发引用悬空，靠注释约定 "callers must re-fetch"（ExpressionEvaluator.h:72-76）。
**建议**：缓存实例化（挂到 ParamDocument 或显式上下文对象），消除全局状态——这也是为未来多线程求解铺路的前提。

> ✅ **已完成（2026-12）**：新增 `ExpressionCache` 类（`ExpressionEvaluator.h`），owner 显式持有：①`ParamDocument` 持有一只（`m_exprCache`），经 `Resolver::resolveAll(..., ExpressionCache*)` 新参数 → `EvalContext::cache` → 统一决策点 `cacheFor(ctx)` 供 `ConditionEngine::evaluate` 取用，11 处 Phase 调用点全部注入；`clear()` 随文档生命周期释放；②无文档上下文的调用方（UI 校验 / 一次性命令求值 / 测试）走 `ExpressionEvaluator::defaultCache()`，改为 **thread_local** —— 不再是"共享可变全局"，多线程求解不会在缓存上竞争；③**引用稳定性**：存储为 `std::deque` 分代，满 8192 条开新一代（旧代只退休、不释放），彻底消除旧实现"整体 clear 使全部已发引用悬空"的 UB 隐患；`BreakCommands` 的一一次性求值也改用文档缓存。回归：test_expression **32/32**、ctest 仍为 23/27 基线、活档探针 NotConverged=0。

### 6. 分层在 include 层面不设防
所有库 `target_include_directories(... PUBLIC ${GCAD_SRC})`——任何文件都能 include 任何层的头。**已发生实例**：CanvasView.cpp:28-31 include `tools/ToolManager.h/ToolRotate.h/QuickAuxDialog.h`（canvas→tools 反向依赖），app 层逻辑下沉画布（右键菜单"发布参数/添加辅助点"在 CanvasView.cpp:441-530 直接 new 对话框 exec）。
**建议**：每库 include dir 收窄到本模块目录 + 显式声明的下层；CanvasView 的对话框/菜单逻辑上移到 app 层（通过信号回调解耦）。

> ✅ **已完成（2026-12，取可行的一半 + 换一种设防方式）**：①**app 层逻辑上移**——CanvasView 只做命中测试并 `emit segmentContextMenuRequested(SegmentHit)`（块/段/投影参数 t/全局坐标），菜单构建、QuickAuxDialog、AddLinkedCommand/AddAuxPointCommand/BakeMeasureCopyCommand、切工具+选中+toast 全部搬进 `MainWindow::onSegmentContextMenu`；旋转工具"右键=确认反悔"的抑制也从 canvas 里 `dynamic_cast<ToolRotate*>` 改为 app 安装的 `ContextMenuGuard` 谓词。②**canvas→tools 的 include 彻底切断**——新增 `canvas/InputDispatcher.h`（依赖倒置：接口声明在下层，ToolManager 实现之），`setToolManager(ToolManager*)` 改为 `setInputDispatcher(InputDispatcher*)`，64 处调用点靠隐式上转型零改动。③**设防改用静态检查**：新增 `tools/check_layering.py`（扫描 7 个模块目录的 include，凡指向更上层即报错 exit 1），因为"收窄 include dir"在当前 `#include "parametric/X.h"` 全路径写法下无法做到不破坏构建。脚本当前报告**全仓干净**（最后一条违规 `document/DeleteImpactConfirm.cpp → ui/ElaMsgBox.h` 已通过把该 UI 对话框 `git mv` 到 `src/ui/` 消除——它本来就编在 `gcad_ui`，这次让目录与库归属对齐）。

---

## 四、P2 —— 演进性负债

> **状态（2026-12）：六项全部收口 ✅✅🟡✅✅✅** —— P2-2 ✅、P2-5 ✅（上一轮）、P2-1 ✅、P2-4 ✅、P2-6 ✅（已量化并结案为「不做」）、P2-3 🟡（工具完备、剩余 233 处按文件分批的长期增量）。详见顶部执行状态表。

| 问题 | 证据 | 建议 |
|------|------|------|
| ~~序列化无版本迁移框架~~ | ~~kFormatVersion=1 只有上限拒绝，"迁移"靠字段嗅探~~ | ✅ **已完成（2026-12 P2-1）**：新增 `src/document/FormatMigration.h/.cpp`（`kFormatVersion` 单一定义点 + `MigrationStep` 注册表 + `migrate()` 逐级前进，缺口即拒绝），首条链路 `migrateV0ToV1`（"layer-refs"）承载 v0「无辅助层 + 整数下标引用图层」的历史；`DocumentSerializer::deserialize` 只留模型不变量与损坏兜底。新增 `tests/test_migration.cpp` 11/11 —— 该历史路径此前零覆盖。加 v2 只需：bump 常量 + 写 `migrateV1ToV2` + 往 `registry()` 追加一行 |
| ~~测试依赖外部活档~~ | ~~E:/3.gcad（test_intersection_update.cpp:80 等 6 处）、E:/存档/1.gcad（test_serializer.cpp:1243），QSKIP 兜底~~ | ✅ **已完成（2026-12 P2-2）**：没有"固化活档进仓库"——用户明确指出 3.gcad 是**真实的制图纸样、不是测试文档**，把用户数据搬进 fixtures/ 只是把漂移搬进仓库。改为**用模型 API 合成等价拓扑**：`CrossLayerDoc`（跨层交点，Phase 2.5/3/4 双向）、合成借用点档（P612）、合成公式延长档。附带 `tools/check_test_fixtures.py` 守卫（禁止 tests/ 引用仓库外路径与构建产物），test_nav_smoke 的截图也改写到临时目录。详见顶部执行状态表 P2-2 |
| GUI 测试大量 qWait(20~80) | test_dialog_tabs.cpp:120-152 等；全仓 249 处 | 🟡 **增量推进（2026-12 P2-3）**：共享工具三件套 `waitUntil` / `grabStable` / **`settle()`**。本轮根治了登记在案的唯一抖动点 `test_dialog_tabs::switchBackAfterTyping`（根因：debounce 定时器 + 断言跑得太早），再迁移 test_segment_edit_bar 5 处。**剩余 156 处 `QTest::qWait(` 调用**（2026-08-29 复核：test_rotate_copy 79、test_select_wkey 50、其余 27）已勘察：76 处 `qWaitForWindowExposed` 后的 `qWait(80)`（属**过度等待**，`QTest::qWait` 不提前返回，负载下不会更短——换成瞬时条件只会暴露被掩盖的竞态），53 处 sendMouse 式 `sendEvent`+`qWait(20)` 对（**已判定为同步链路统一保留**，test_rotate_copy 1~844 行批次逐点位加判定注释，见顶部 P2-3 A1 批次）。**不建议机械迁移**，按文件分批做 |
| ~~UI 组件住错层~~ | ~~LinePropertyDialog(1306行)/Segment*Card/AngleHud 全是 QWidget 却在 tools/~~ | ✅ **已完成（2026-12 P2-4）**：33 个文件迁入 `ui/`，命名空间同步 `cad::tools`→`cad::ui`，CMake 33 项源改挂 `gcad_ui`（并补 `gcad_canvas` 链接）。**`tools/` 不再含任何 QWidget**。详见顶部 P2-4 条目 |
| ~~undo 无上限 + 全量 Memento~~ | ~~未见 setUndoLimit；删除命令快照级联子图（BlockCommands.cpp:53-93）~~ | ✅ **已完成（2026-12 P2-5）**：`ParamDocument::kUndoStackLimit = 150` + 构造时 `setUndoLimit()`。增量快照（真正的治本方案）未做，属独立项目 |
| ~~单线程求解的 UI 阻塞风险~~ | ~~全 src 无线程；resolveForDrag 每帧跑在 GUI 线程；全量 resolve 最多 5 遍 scope 解~~ | ✅ **已量化并结案（2026-12 P2-6）**：`tests/test_resolve_scale.cpp` 实测 20/100/400/800 块 → 拖拽 105/486/2035/4039µs（**线性**，800 块仍只占 60fps 预算 24%）；全量 167/1091/9124/31495µs（**超线性**，但只由离散动作触发）。**结论：不做 worker 线程**——收益（至多省一次 31ms 的离散卡顿）远小于代价（可变 QObject 模型图要么加锁要么整份快照）。详见顶部 P2-6 条目 |

---

## 五、哪些模块适合 Rust 重构？

> **状态（2026-12）：未开始 ⬜**。前置条件中的 P0 已完成 ✅；候选排序与实测规模已更新（ExpressionEvaluator ~795 行 / geometry ~1.8k 行 / DocumentSerializer ~988 行），详见顶部执行状态表。

**先泼冷水：上面 P0/P1 的问题没有一个是语言问题。** 双 undo 栈、ID 撞号、绕过命令系统——Rust 不会自动修好它们，反而混合语言会抬高维护成本。Rust 只适合替代**纯计算、零 Qt 依赖、边界清晰**的叶子模块。

### 适合（按性价比排序）

| 模块 | 规模 | 适配度 | 理由 |
|------|------|--------|------|
| **geometry（CurveMath/TriangleUnfold/Vec2/Units/Angle）** | ~1.5k 行 | ★★★★★ | 纯数学、值语义、零 Qt 依赖（仅 Qt6::Core/Gui 的 Vec2 包装，易剥离）。Bezier/弧长积分/求交是 Rust 数值代码的甜区；FFI 边界就是 POD 数组 |
| **ExpressionEvaluator** | ~650 行 | ★★★★★ | 自包含递归下降 + 栈机字节码，输入字符串输出 double，天然无状态边界。顺手根治静态缓存的线程安全问题（Rust 里根本写不出那种全局可变静态） |
| **序列化内核（DocumentSerializer 的 JSON↔模型映射）** | ~960 行 | ★★★☆ | serde + 表驱动枚举映射比 C++ 手写 constexpr 表更安全，schema 演进（v2 迁移框架）用 Rust 的 enum + serde tag 表达力更强。但需先解决模型类型跨语言重复定义的成本 |
| **求解数值内核（Block::resolve 中纯几何求解部分）** | 需抽取 | ★★★ | 单点约束求值（12 种 PointConstraint 的数学）可抽为纯函数库；但与 Block/Resolver 状态纠缠深，**先做"纯函数抽取"再谈换语言** |

### 不适合

- **ui / tools / canvas / app（~60% 代码量）**：Qt Widgets + ElaWidgetTools + 信号槽深绑定，Rust Qt 绑定（cxx-qt 等）成熟度不足以承载这种交互密度，重写=重写整个应用。
- **ParamDocument 门面**：QObject 信号是架构的血液循环，跨语言重建观察者模式得不偿失。
- **命令系统**：QUndoStack 与 Qt 生态绑定，且问题在架构不在语言。

### 若决定做，推荐路径

1. **先修架构再换语言**：完成 P0（undo 收口）和 P1-3（写路径收口）后，模块边界才真正清晰，FFI 边界才稳定。
2. **从 geometry 开刀**：零 Qt 依赖、有 test_curve 全量回归兜底，是最安全的试点。用 [corrosion](https://github.com/corrosion-rs/corrosion) 接入现有 CMake，cxx 做类型安全桥接。
3. **ExpressionEvaluator 第二步**：边界是 `QString in → double out`，替换后测试（test_expression）原样兜底。
4. **设定止损线**：若试点模块的 FFI 胶水代码超过模块本体 30%，说明边界不成熟，停下来继续用 C++。

---

## 六、行动优先级清单

> **状态（2026-12）：1 ✅、2 ✅、3 中 P2 六项已全部收口（P2-3 剩按文件分批的长期增量）；架构级两项（门面窄接口 / BlockRef 句柄化）与 Rust 试点仍未启动。**

1. ~~**本周**：undo 双栈收口（P0-1）；mergeWith ID 集中枚举（P0-2）；SegmentAnchorTab 接入命令系统（P0-3）。~~ ✅ **已完成（2026-12，详见顶部执行状态表）**
2. ~~**下一步（按 ROI 递减）**：P1-4 → P1-5 → P1-1 → P1-6 → P1-2 → P1-3（短期护栏）。~~ ✅ **全部完成（2026-12）**——分别为 kMaxSettleRounds 收口 10 处循环 + 未收敛可观测化、表达式缓存实例化（分代 + thread_local 兜底）、`Block::touchGeometry()` 收口 30 处 bump、`tools/check_layering.py` + canvas→tools 依赖倒置 + DeleteImpactConfirm 归位、`*Raw` 收进 `RawModelAccess` 友元通道、结构世代 + debug 指针范围校验。详见顶部执行状态表与第三节各条目的 ✅ 段落。
3. **剩余架构级（单独立项，勿混日常迭代）**：
   - P1-2 前半段：按域分组 public 接口（`doc->blocks().xxx()` 窄接口对象），把 ~130 个 public 方法收敛为若干子门面。
   - P1-3 中期：句柄（`BlockRef` = id + generation）替代裸指针，让编译器守住"跨变更不持有"的纪律。
   - P1-2 前半段、P1-3 句柄化（见上）。
   - P2-3 尾巴：233 处 `qWait` 按文件分批条件化（test_rotate_copy 120 / test_select_wkey 69 / 其余 44）。**别机械迁移**——`qWait(80)` 那批属过度等待，改条件前先确认要等的是什么。
   - ~~P2 六项~~ ✅ **全部收口（2026-12）**：测试 fixtures 内化、undo 上限、版本迁移框架、UI 组件归位 ui/、求解开销量化（结论「不做 worker 线程」）、GUI qWait 条件化（工具完备 + 抖动点根治，剩余按文件分批）。
   - 增量快照（undo 的治本方案）与 worker 线程求解**均已评估为不做**，理由分别见 P2-5 与 P2-6 条目。
4. **可选试点**：Rust 化候选 = **geometry**（~1.8k 行、纯数学零 Qt 依赖、29 测试兜底）> ExpressionEvaluator（~795 行；**其静态缓存隐患已由 P1-5 在 C++ 内解决**，Rust 化收益降为纯语言偏好）。前置条件 P0 ✅；P1-3 句柄化**不再是前置**（其短期护栏已完成，FFI 边界不依赖它）。**止损线不变**：胶水代码超过模块本体 30% 就停。
