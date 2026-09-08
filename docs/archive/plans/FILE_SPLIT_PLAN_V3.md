# FILE_SPLIT_PLAN_V3.md —— 文件职责治理第三轮实施计划（智能体任务书）

> **版本**：v3.0（2026-12 审计轮）
> **上游依据**：《文件拆分实操规则 v2.0》（`E:/e/file_split_rules_v2_final.md`）、`redline_exceptions.json` 基线契约、`docs/archive/plans/FILE_SPLIT_PLAN_V2.md`（阶段 0/1/2 + 阶段 3 首轮已落地）。
> **本轮背景**：v2.0 计划落地后，部分豁免文件在基线上继续膨胀（最严重 ContextStrip +511 行），且守卫只查"是否登记"不查"是否膨胀"。2026-12 全量审计（19 个独立审计代理交叉复核，行数为守卫同口径含空行）产出本任务书。
> **本文档定位**：可直接派发的任务卡集合。每个任务节自包含——别的智能体只需读对应任务节 + 第二节全局纪律即可开工，无需重读审计过程。

---

## 一、如何使用本文件（执行智能体必读）

### 任务领取流程

1. 选定任务 ID（如 `T1.2`），先查第九节依赖矩阵确认无冲突；
2. 读本任务的「现状证据 → 拆分方案 → 验收标准」三段，方案内的行数为估算，以实测为准；
3. 严格按第二节全局纪律执行（构建/测试/登记），**违规=返工**；
4. 完成后在任务卡末尾的「执行记录」区登记：执行日期、执行者、实际行数变化、验证结果、遗留问题。

### 审计口径说明（判断"是否达标"的唯一标准）

- 行数统计与守卫 `tools/check_file_size.py` 同口径：文件总行数（含空行），Python `sum(1 for _ in f)` 或 PowerShell `(Get-Content $f).Count`；
- 各层阈值：geometry ≤500 / document\commands ≤400 / ui ≤800 / ParamDocument.h ≤800 / 其余 src ≤900 / tests 警告线 1500、强制线 2500；
- 职责数判定："能用『负责…』列出 ≥4 个互不相同职责 → 必须拆"；bool 组合标记 >5 且互相组合影响分支 → 必须拆；
- 停止规则：拆后改一个功能需开 5+ 文件、或需大量前向声明/friend 绕路 → 说明边界划错，回退合并。

### 审计确认的非任务项（勿再立项）

- `DocumentSerializer.cpp`（987 行）：规则九豁免，职责单一，不动；
- `ParamDocument.h`（796 行）：聚合门面头，距 800 上限仅 4 行余量——**禁止再往里加新职责**，新增 API 先考虑拆 Detail 头；
- `ToolSelect.cpp` / `LinePropertyDialog.cpp` / `SegmentAngleCard.cpp`：经停止规则评估为函数级整改，**禁止整体拆文件**（见任务 3.x 各自的"禁止项"）。

---

## 二、全局执行纪律（所有任务必须遵守）

### 2.1 构建与测试铁律（违反 = 浪费 1~2 分钟链接风暴）

```powershell
tools\build.bat WildWindPattern      # 日常唯一允许的编译验证（~0.3s）
tools\build.bat <test_name>          # 构建单个测试 target
ctest -C RelWithDebInfo -R <name>    # 只跑受影响单测
ctest -C RelWithDebInfo -R "check_"  # 单跑七守卫
# ⛔ 禁止无参数 tools\build.bat / 无过滤 ctest —— 仅全局收尾（第八节）允许各 1 次
```

- 构建/跑测一律走 PowerShell；日志 `*>` 落盘再 `iconv -f UTF-16LE -t UTF-8` 读（PowerShell 不回显 stdout）；
- 智能体构建/跑测统一走 `build/out-reldeb`（`tools\build.bat` 已封装）。

### 2.2 新增文件登记规则

- 新增 `.cpp` 必须加入对应模块静态库源列表（`gcad_geometry`/`gcad_parametric`/`gcad_canvas`/`gcad_document`/`gcad_ui`/`gcad_tools`/`gcad_app`），增删源文件后重跑 `tools\build.bat WildWindPattern` 即可（Ninja 自动感知，一般无需 reconfig）；
- 新测试 target 源列表**只放 tests/*.cpp**，切勿加项目头文件（AUTOMOC 重复定义 LNK2005）；
- 新文件一律 **UTF-8 with BOM**（Write 工具产出无 BOM，写完必须补：`Set-Content -Encoding utf8BOM`）；
- **CMakeLists.txt 是全局热点**：多任务并行改它会冲突，同一会话内串行编辑。

### 2.3 架构铁律（拆分不得破坏）

- 分层单向依赖：`gcad_geometry → gcad_parametric → gcad_canvas/gcad_document → gcad_ui → gcad_tools → gcad_app`，由 `ctest -R check_layering` 把关；
- `ParamDocument` 是门面：公共 API 与信号签名不变；模型变更只走门面方法或 `RawModelAccess`；禁止 `const_cast`；
- `Block::geometryEpoch` 唯一 bump 入口 = `touchGeometry()`；只改显示/语义的命令必须显式调用；
- `src/tools/` 只放手势与状态机，**不含任何 QWidget**；几何算法属于 `src/geometry/`；
- 卡片抽取范式：子卡持 doc 指针 + 目标 id，`setTarget`/`refresh` 双入口，`changed(ChangeKind)` 信号回报；
- 禁止建 `Common`/`Utils`/`Base` 无语义收容所文件名；
- 角度换算统一走 `src/geometry/Angle.h`（normalizeDeg360/180 等），禁止手写 while 归一化循环。

### 2.4 行为保持与验证通则

- 本计划全部为**行为保持重构**（纯搬移/抽取，禁止顺手改逻辑；逻辑修复单独立任务，唯一例外 = T3.1 的门面绕过修复，已单独标注）；
- 每个任务动手前：先跑该任务「影响面测试」记录基线全绿；搬移完成后原样再跑一遍；
- GUI 时序抖动基线（单跑即过、整批偶发）：test_dialog_tabs::switchBackAfterTyping / test_aux_layer / test_rotate_copy / test_select_wkey::componentConnect* —— 遇到先单跑确认，勿当回归修；
- 环境漂移红（非代码问题）：test_intersection_update 全部 + test_extend::savedDocFormulaStartExtendRenders（依赖活档 E:/3.gcad）。

---

## 三、审计结论总表（2026-12 实测，守卫同口径）

### 3.1 🔴 违约项：豁免基线膨胀（违反 redline_exceptions "只许缩短"契约）

| 文件 | 申报基线 | 实测 | 膨胀 | 处置任务 |
|---|---:|---:|---:|---|
| `src/app/ContextStrip.cpp` | 1154 | **1665** | **+511 (+44%)** | T1.2 |
| `src/app/MainWindow.cpp` | 1650 | 1841 | +191 | T1.1 |
| `src/canvas/BlockItem.cpp` | 941 | 1040 | +99 | T1.4 |
| `src/ui/VariablePanel.cpp` | 942（ui 阈值 800）| 1030 | +88 | T1.5 |
| `src/parametric/Block.cpp` | 1432 | 1498 | +66 | T1.3 |
| `src/geometry/CurveMath.cpp` | 906 | 928 | +22 | Backlog B.1 |
| `src/document/commands/ReverseSegmentCommand.cpp` | 451 | 458 | +7 | 暂缓，刷新登记即可 |

**好消息**：`Resolver.cpp` 960→886 已回落 900 线内 → 豁免条目随 T2.1 退役；这验证"拆职责"比"压行数"有效。

### 3.2 🔴 一级红线（职责维度）命中清单

| 文件 | 实测行 | include | 职责数 | bool 组合 | 判定 | 任务 |
|---|---:|---:|---:|---:|---|---|
| `src/app/ContextStrip.cpp` | 1665 | 30 | 11 | **8 个**（爆炸）| must_split | T1.2 |
| `src/app/MainWindow.cpp` | 1841 | 62 | 10 | 2 | must_split | T1.1 |
| `src/ui/VariablePanel.cpp` | 1030 | 39 | 9 | 0 | must_split | T1.5 |
| `src/parametric/Block.cpp` | 1498 | 9 | 7 | 4 | must_split | T1.3 |
| `src/tools/ToolCurveEdit.cpp` | 783 | 17 | 9 | 2 | must_split（同类分卷）| T2.3 |
| `src/ui/SegmentRefCard.cpp` | 763 | 26 | 6 | 0 | must_split（卡片范式）| T2.2 |
| `src/canvas/BlockItem.cpp` | 1040 | 24 | 6 | 2 | must_split | T1.4 |
| `src/parametric/Resolver.cpp` | 886 | 12 | 6 | 0 | must_split | T2.1 |
| `src/tools/ToolIntersection.cpp` | 852 | 20 | 7 | 3 | must_split（几何下沉）| T2.4 |
| `src/tools/ToolRotate.cpp` | 895 | 24 | 7 | 1 | must_split（抽纯函数）| T2.5 |
| `src/tools/ToolSelect.cpp` | 857 | 52（~20 死 include + 1 重复）| 7 facet（同角色）| 1 | function_level_refactor | T3.1 |
| `src/ui/LinePropertyDialog.cpp` | 781 | 42（~10 死 include）| 1 主体 + 4 薄壳 | 3 | function_level_refactor | T3.2 |
| `src/ui/SegmentAngleCard.cpp` | 749 | 23 | 3（内聚）| 0 | function_level_refactor | T3.3 |

### 3.3 🔵 测试治理（全在 1500 警告线之上、2500 强制线之下）

| 测试文件 | 实测行 | 用例数 | 域数 | 任务 |
|---|---:|---:|---:|---|
| `tests/test_resolver.cpp` | 1798 | 33 | 5+ | T4.1 |
| `tests/test_rotate_copy_gestures.cpp` | 1759 | 24 | 6 | T4.2 |
| `tests/test_dialog_tabs.cpp` | 1599 | 16 | 4+ | T4.3 |
| `tests/test_rotate_copy.cpp` | 1550 | 22 | 5 | T4.4 |
| `tests/test_attachment_commands.cpp` | 1525 | 16 | 5+ | T4.5 |
| `tests/test_break.cpp` | 1515 | 19 | 4 | T4.6 |

> ⚠️ 口径不一致遗留：规则文档第八节写">1500 必须拆"，守卫 `check_test_split.py` 只把 >2500 设为 FAIL、>1500 仅警告。T0.3 处理。

---

## 四、批次 0：基线修复与守卫加固（最先做，后续任务的前置）

### T0.1 刷新 redline_exceptions.json 基线

- **内容**：
  1. 六膨胀文件的登记行数刷新为 3.1 表实测值，reason 注明"2026-12 审计刷新，拆分进行中（对应任务 ID）"；
  2. `Resolver.cpp` 条目**保留**（行数达标但职责 6 个，随 T2.1 拆分完成后退役）；
  3. `DocumentSerializer.cpp`（987）条目行数刷新；
  4. 登记 6 个 >1500 测试文件进 `test_size_exceptions`（理由："T4.x 拆分排期"，守卫 >1500 仅警告不强制登记，登记是为台账完整）。
- **验收**：`ctest -C RelWithDebInfo -R "check_file_size|check_test_split"` 全绿。

### T0.2 守卫加固：file_size 棘轮检查

- **问题**：`tools/check_file_size.py` 只查"超线且未登记"，不查"已登记但膨胀"——ContextStrip 式漂移无人发现。
- **实施**：脚本增加检查——已登记豁免文件当前行数 > 申报行数时 FAIL，输出膨胀量，提示"刷新基线须随拆分任务同步（只允许最终收敛时改小）"。注意与 T0.1 的交互：首次加检查时基线须已是刷新值。
- **同批可加**：`check_bool_flags.py` 对 ContextStrip 拆分的后验（T1.2 完成后 ContextStrip bool 成员应 ≤5，届时从 bool_flag_exceptions 移除）。
- **验收**：脚本对人为膨胀的临时文件正确报错（自测后复原），`ctest -R check_file_size` 绿。

### T0.3 口径对齐（决策项，需用户拍板）

- 1500 测试线：文档"必须拆" vs 守卫"仅警告"。建议方案：守卫升级为 >1500 强制 FAIL + 白名单过渡。**本任务为文档守卫口径修订，不动源码**；未拍板前维持守卫现状。

---

## 五、批次 1：五大膨胀文件拆分

### T1.1 MainWindow.cpp 同类分卷（1841 → 壳 ~470）

**现状证据**：62 个 include（含 3 个 `document/commands/*.h` 渗入 UI 装配层）；10 个职责：
① 骨架装配/析构 ② 菜单栏与全局动作 ③ 悬浮工具坞与图标 ④ 状态栏 chrome 家族（elide 引擎/badge/flash）⑤ 文件会话 I/O ⑥ 主题切换编排 ⑦ 面板悬浮窗与页签导航状态机 ⑧ 线段右键菜单业务策略（~190 行含命令推送）⑨ 活动图层切换与图层芯片 ⑩ ContextStrip 信号桥接。

**拆分方案**（同类 partial-class 拆法：成员函数跨 .cpp 实现，`MainWindow.h` 零改动、无 friend/前向声明，Qt 主窗口惯例）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/app/MainWindowFileSession.cpp` | onNew/onOpen/onSave/onSaveAs/onOpenRecentFile/maybeSave/clearDocument/updateTitle/addRecentFile/updateRecentFilesMenu + showLoadWarnings；带走 QFileDialog/QUndoStack/DocumentFile.h 相关 include | 330 |
| `src/app/MainWindowMenuBar.cpp` | setupMenuBar 全文（五菜单/工具 QActionGroup/H 切换/主题/方向箭头 action/关于框） | 150 |
| `src/app/MainWindowToolBar.cpp` | setupToolBar/refreshToolIcons/refreshLayerChip + 匿名空间 toolDockIcon/panelActiveDot | 260 |
| `src/app/MainWindowStatusBar.cpp` | setupStatusBar/refreshStatusBarChrome/setToolHint/applyToolHintElide/flashStatus/诊断 badge 明细弹层/undo flash 反馈 | 300 |
| `src/app/MainWindowPanelWindow.cpp` | setupPages/onPageTabChanged/syncPanelTabs/panelVisible/refreshPanelChrome/ensurePanelWindowPosition + Qt::Tool 定位与 QSettings 几何记忆 | 330 |
| `src/app/MainWindowSegmentMenu.cpp` | onSegmentContextMenu 全块（DetachOption 扫描/拆开释放/发布 LinkedVariable/QuickAuxDialog 流转/BakeMeasureCopyCommand 烘焙） | 200 |
| `src/app/MainWindow.cpp`（壳） | 构造析构/connectSignals 与 ContextStrip 会话桥接/toggleTheme 编排/actionToggleAuxLayer/eventFilter/updateEditBand/closeEvent | ~470 |

**影响面测试**：`ctest -R "test_dialog_tabs|test_context_strip|test_aux_layer|test_select_wkey|test_rotate_copy|test_hold_show|test_mode_indicator"`（GUI 家族）+ `ctest -R "check_"`。
**验收**：主壳 ≤900；六处搬移后行为不变；`check_layering` 绿；redline_exceptions 中 MainWindow 条目行数改小（或壳达标后删条目）。
**风险**：分卷时注意匿名命名空间函数随归属搬走；析构断连顺序留在壳内。

### T1.2 ContextStrip.cpp 拆分（1665 → 五文件）⚠️ 本轮最高优先

**现状证据**：基线膨胀 +511（最严重违约）；11 个职责 + **8 个组合 bool**（m_creationPinned/m_strokePreview/m_connectSession/m_rotateSession/m_rotateAnchorIsEnd/m_rotateCanToggle/m_isPlacedPointMode/m_placePointSession，hideBar/refreshChrome/eventFilter/applyAngle 均受其组合分支影响）——双一级红线齐发。

**拆分方案**：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/app/PlacedPointStripBar.h` + `.cpp` | **放置点域抽为独立 QWidget 子控件**（唯一非 partial 的抽取）：ptSerial/ptName/ptDist/ptAngle/ptBaseSegLabel/删除按钮/hint 全套控件、setTarget/begin/endPlacePointSession/updatePlacedPointValues/applyEdits（构造 EditPlacedPointCommand/RemovePlacedPointCommand）、内部 Tab/Esc/Enter 循环；对外暴露 placePointDistChanged/placePointAngleChanged/placePointCommitted/placedPointDeleted 信号。ContextStrip 聚合它并只管显隐切换。消除 m_isPlacedPointMode/m_placePointSession 与全部 pt* 成员 | 420 |
| `src/app/ContextStripSessions.cpp` | partial：外部工具寄生会话——连接角度 begin/endConnectAngleSession/setConnectAngleValid、旋转锚心 setRotateAnchorState、画线预览 showStrokePreview、cancelCreation、setUndoStack 回填槽 | 260 |
| `src/app/ContextStripDisplay.cpp` | partial 读路径：refreshFields/refreshChrome/foldedArcDisplay/foldedChordDisplay/badgeText/basisText/applyTheme（影子基准徽章/角度弧长弦长模换算/按钮资格 tooltip）| 470 |
| `src/app/ContextStripEdit.cpp` | partial 写路径：applyName/applyLength/applyAngle/onPaste*/onUnitToggled/onUnitSelected/onReverseClicked/onPosDetach/onAngleDetach/snapshotState/commitState/findEditAttachment | 420 |
| `src/app/ContextStrip.cpp`（壳） | ctor/setCanvasView/buildUi（线段 bar）/三态焦点状态机（setHoverTarget/flushHover/setPinnedTarget/clearHover/clearPinned/pinCreatedLine/hideBar）/inputHasFocus/returnFocusToCanvas/eventFilter | ~480 |

**影响面测试**：`ctest -R "test_context_strip|test_dialog_tabs|test_rotate_copy_gestures|test_select_wkey"'`。重点盯：条带读数显示（foldedArc 多圈折叠有 test_rotate_copy_gestures 用例锁定）。
**验收**：五文件均 ≤900（app 按通用线）；bool 组合成员降至 ≤5（放置点两个随子控件走）；`check_bool_flags` 中 ContextStrip 豁免条目移除；redline_exceptions 行数改小；设计文档 `CONTEXT_STRIP_DESIGN.md` 头注追加拆分说明。
**风险**：子控件抽取是本计划唯一"非纯搬移"项之一——信号接线需逐条核对 MainWindow connectSignals 中对 ContextStrip 的转发；°/⌒/↔ 三键互斥 QButtonGroup 属于壳的 buildUi，勿拆散。

### T1.3 Block.cpp 六卷拆分（1498 → 六卷，Block.h 零改动）

**现状证据**：7 个职责，且各有独立设计文档背书（EXTEND_LINE_DESIGN.md / DETACH_SHADOW_DESIGN.md）：① Transform2D 换算 ② 点约束链求解（7 种 worker + polarEndpointCycleSeed）③ 曲线 spans 求解与多级缓存 ④ 端点延长线子系统 ⑤ 几何查询门面 ⑥ 生命周期几何转换（freeze/影子克隆）⑦ 点段容器与索引。

**拆分方案**（同类成员函数跨 TU 实现，`Block.h` 完全不动）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/parametric/BlockResolve.cpp` | Block::resolve/resolveUnresolved/resolveInterpolatedPoints/polarEndpointCycleSeed + 全部 per-constraint worker（Polar/OrthoOffset/Intersection/Interpolated/CurveAnchor/Midpoint/OnSegment，含 idbg 日志） | 660 |
| `src/parametric/BlockCurve.cpp` | 匿名空间 fpMix/curveAnchorFingerprint、collectCurveAnchors、spansForSegment（指纹 memo）、rebuildCurveCache、curveSpanEntry | 210 |
| `src/parametric/BlockExtend.cpp` | effectiveLocalPos/segmentExtendStart/End/segmentSnapWithinBase/clampExtendLimits/evaluateExtendValues/applyEffectivePositions | 280 |
| `src/parametric/BlockQuery.cpp` | worldPos/directionAtPoint/exitDirectionAtPoint 双载/exitSegmentAtPoint/segmentLengthAtPoint/segmentBaseLength/segmentEffectiveLength | 250 |
| `src/parametric/BlockLifecycle.cpp` | freezeSegmentGeometry + cloneShadowOf | 110 |
| `src/parametric/Block.cpp`（壳） | Transform2D + findPoint/findSegment/addPoint/addSegment/rebuildPointIndex/rebuildSegmentIndex | 130 |

**影响面测试**：`ctest -R "test_block_commands|test_resolver|test_attachment_commands|test_variable_layer_commands|test_reverse_segment_commands|test_serializer|test_migration|test_ortho_offset|test_break|test_curve|test_component"`（引擎全家族）。
**验收**：各卷 ≤900；私有状态（m_extendEval/m_spanMemo/m_curveSpans/m_curveSpanIndex）随函数同域迁移、头文件不泄露；redline_exceptions Block 条目删除（全部达标后）。
**风险**：`check_layering` 必须绿（parametric 不反依赖）；Block 是 68 fan-in 核心实体头，本任务不许动 Block.h 一行。

### T1.4 BlockItem.cpp 四层分离（1040 → 四件）

**现状证据**：6 职责（缓存构建/绘制/拾取/悬停仲裁/动画状态编排/模型双向同步）；24 include。

**拆分方案**（按领域概念 + 架构层，QGraphicsObject 虚函数契约留壳）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/canvas/BlockGeometryCache.h/.cpp` | LineCache/PointCache 结构体（从 BlockItem.h 迁出公开化）+ rebuildCache 全部构建逻辑（坐标变换/正交轴拐点/附着锁定点集合/长度文案预格式化/CurveItem 子项创建/包围盒累计/LayerMode）；build(blockId, doc) 纯构建接口 + 只读访问器 | 370 |
| `src/canvas/BlockItemPainter.h/.cpp` | paint() 全部：drawSegment lambda（画笔/leader 变色/灰显 ghost/正交轴虚线/方向箭头/标签）、点绘制（圆/菱形/锚点环/锁定环/选中环/网格标签去重）、共享 QFont 单例；输入为 BlockPaintContext 值结构，纯函数式不改状态 | 310 |
| `src/canvas/BlockItemPick.h/.cpp` | 拾取谓词：shape() 容差描边与缓存策略/hitTest/hitTestPoint/hoverThreshold 换算；无状态可单测 | 190 |
| `src/canvas/BlockItem.h/.cpp`（壳） | 构造/boundingRect 委托/hoverMove/hoverLeave/itemChange/曲线子项悬停仲裁/resolveState + animator 广播（三处重复循环提取 forEachEntityPushState 私有助手）/syncFromBlock 平移快进/位置回写 | ~420 |

**影响面测试**：`ctest -R "test_hold_show|test_aux_layer|test_select_wkey|test_overlap_battery|test_transient_overlay|test_component|test_canvas_perf"`。
**验收**：壳 ≤900；调用方（CanvasScene/ToolSelect/CurveItem）接口签名零变化；新增视觉属性改动触 ≤3 文件；redline_exceptions 条目删除或改小。
**风险**：PointCache/LineCache 内嵌 bool 多（7/5 个）但属数据快照 DTO 非状态机——迁出公开化时保持纯字段不加逻辑；paint 性能敏感（1200 FPS 路径），禁止在 Painter 引入逐帧堆分配。

### T1.5 VariablePanel.cpp 页签化拆分（1030 → 四件）

**现状证据**：9 职责（chrome 编排/空态脚手架/主题热重设/卡片虚拟化绑定/新增命名流/命令路由+条件对话框/分组交互/拖拽重排会话/视图同步聚合）；39 include（34-35 行 FormulaTabModel.h 重复包含）。**既有先例**：Tab3 测量页签已按同法抽为 MeasureTab。

**拆分方案**（镜像 MeasureTab 抽取范式：子页签持 doc 指针，信号上报计数/空态变化）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/ui/FormulaTab.h/.cpp` | 公式页签全部：FormulaCard/FormulaGroupHeader provider 与复用绑定、分组交互四槽（toggle/rename/dissolve/drop）转发自持的 FormulaTabModel、拖拽重排 eventFilter 整套（两种 mime/落槽数学 computeFormulaDropSlot/computeGroupDropSlot/指示线/MoveFormulaGroupCommand）、addNewFormula/onAddGroupClicked、onFormulaEdited/Deleted 短路与命令、onConditionsEditRequested 符号收集 + ConditionDialog 启动、syncFormulaCards | 400 |
| `src/ui/VariableTab.h/.cpp` | VariableCard provider/nextRefName 大小写不敏感去重/addNewVariable（命令+滚底+focusRef）/onVariableEdited 四字段短路 + SetVariableCommand/onVariableDeleted/syncVariableCards/空态文案 | 180 |
| `src/ui/LinkedTab.h/.cpp` | linkedSourceLabel 助手/LinkedCard provider 与值级 valueBinder（O(visible) 刷新 + GCAD_PERF_SCOPE 探针）/onLinkedEdited/onLinkedDeleted/syncLinkedCards/sourceClicked→highlightBlockRequested 转抛 | 140 |
| `src/ui/VariablePanel.cpp`（瘦身）| PanelSubTabBar 四页签 profile/计数 pill/按钮显隐策略/QStackedWidget 编排/setUndoStack 分发/applyTheme 顶层 QSS/updateCountLabel 聚合/MeasureTab 信号穿透/hideEvent 清高亮 | ~460 |

**影响面测试**：`ctest -R "test_dialog_tabs|test_variable_layer_commands|test_formula_groups"`。
**验收**：母体 ≤800（ui 线）；三页签信号对接不引 friend/前向绕路；拖拽重排行为有 test_formula_groups 锁定；buildListPage 空态脚手架若三页签重复明显，上提为语义化基类 `CardTabBase`（严禁落 utils）；删除重复 include。
**风险**：applyTheme 全树重刷的分层归属（母体 vs 各 Tab）参照 MeasureTab::applyTheme 先例；拖拽指示线 m_dropIndicator 是事件层状态，随 FormulaTab 走。

---

## 六、批次 2：职责超标二级目标

### T2.1 Resolver.cpp 工人迁出（886 → 壳 ~530）

**现状证据**：6 职责（管线编排/跨块交点/附件随动位姿/滑轨解算/桥接钉扎/终点指向旋转）；行数虽已 <900，但职责红线独立成立。

**拆分方案**（同类跨 TU，Resolver.h 不动）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/parametric/ResolverAttachment.cpp` | Resolver::applyAttachment 全文（leader 基准方向/母线两点参照 §6.4/角度弧长弦长三模式公式求值/闭合基准合成/preserveEndTargetRotation/angleIndependent/angleOnly/滑轨钉扎/原点吸附） | 245 |
| `src/parametric/ResolverIntersection.cpp` | Resolver::resolveCrossBlockIntersection 全文（射线-线段求交/指向点优先/退化段引导/idbg 日志） | 150 |
| `src/parametric/ResolverSlide.cpp` | 自由函数 computeSlideOffsets（滑轨投影快照——ParamDocument 拖拽路径独立调用入口，按调用者聚类单列） | 60 |
| `src/parametric/Resolver.cpp`（壳） | resolveAll Steps 1–7b 骨架/settleAttachments/runIntersectionFixpoint/report + findResolvedPointWorld | ~530 |

**影响面测试**：`ctest -R "test_resolver|test_serializer|test_attachment_commands|test_intersection|test_tool_intersection|test_resolve_scale"`。
**验收**：`check_layering` 绿；bridge aux 点用例（test_serializer::bridgeAuxPointSnappableAndAttachable）绿；完成后 redline_exceptions 删除 Resolver 条目。
**备注**：applyAttachment 内 rotationMode×slideMode×angleOnly×angleIndependent 组合分支建议后续表驱动（二级建议，不属本任务）。

### T2.2 SegmentRefCard.cpp 卡片范式拆分（763 → 四卡）

**现状证据**：6 职责（对齐点管理/方向基准点1点2/基准模式切换/影子角度双态反算/影子清除/八态显隐编排）；6 组互不相同的 undo 命令路径；refresh() 六因子八态显隐分支。行数 763 < ui 800，但职责红线独立成立。

**拆分方案**（严格遵循卡片抽取范式：子卡持 doc + blockId/segmentId，setTarget/refresh 双入口，changed(ChangeKind) 回报）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/ui/SegmentShadowBasisCard.h/.cpp` | 影子（拆开基准）：m_shadowAngleEdit/m_btnClearShadow/onShadowAngleEdited（挂载写 Att1 Δ / 拆开态 ShadowRotateCommand 双 Transform2D 反算）/onClearShadowClicked（RemoveShadowCommand）/影子回显与双态 tooltip | 220 |
| `src/ui/SegmentAngleRefCard.h/.cpp` | 方向基准：点1/点2 PointRefEdit + [独立]/[链接当前线]/[重设基准] 三钮 + 三套槽函数（自引用/零向量校验/自动态固化点1/preservedBenchmarkAngle 清除重反算）+ 母线基准标签与灰显回显 | 380 |
| `src/ui/SegmentAlignPointCard.h/.cpp` | 对齐点：m_alignPointEdit/onAlignPointResolved（restrictToBlock + SetAlignPointCommand）/四态规则表回显锁定/非法输入红闪 900ms | 170 |
| `src/ui/SegmentRefCard.cpp`（主卡） | 八态可见性编排 + 子卡聚合 + 拒绝反馈 toast | ~230 |

**影响面测试**：`ctest -R "test_dialog_tabs"`（angleRef/shadow/attachment 用例全在此）+ `ctest -R "test_attachment_commands|test_component"`。
**验收**：任一功能改动 ≤2 文件；影子双态行为有 test_dialog_tabs::detachClearsConnectEditAndRetargetReconnects / reattachPreservesAngleRef 等锁定；ui 层所有文件 ≤800。

### T2.3 ToolCurveEdit.cpp 同类分卷（783 → 三卷）

**现状证据**：9 职责（元数据生命周期/事件路由状态机/放置预览/锚点增删/锚点拖拽会话/吸附跟随连接/手柄渲染/手柄拖拽编辑/弦坐标数学）。文件内已有三大注释分节，天然接缝。

**拆分方案**（ToolCurveEdit.h 不动）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/tools/ToolCurveEditAnchor.cpp` | 曲线锚点领域：曲线点放置预览（三条抑制分支）/placeCurvePoint/deleteCurvePoint（直线↔贝塞尔升降）/锚点拖拽会话五件套/吸附连接建立解除与绿圈/chordParams 弦坐标分解 | 330 |
| `src/tools/ToolCurveEditHandles.cpp` | 切线手柄领域：anchorTangents 自动手动求解/四图元渲染与锁定尖角着色/handleHitTest/拖拽编辑（autoTangent 固化/共线镜像/Alt 破锁/SetCurveTangentCommand） | 300 |
| `src/tools/ToolCurveEdit.cpp`（壳） | describe/onActivate/onDeactivate/clearGraphics/四级事件路由/三态状态机 | ~180 |

**顺手收口**：endCurveAnchorDrag 内联的弦重投影（约 381-394 行区）与 chordParams 重复，合并复用。
**影响面测试**：`ctest -R "test_curve_edit|test_curve"`。
**验收**：壳只留路由；改"加点"只开 Anchor.cpp，改"手柄"只开 Handles.cpp。

### T2.4 ToolIntersection.cpp 几何下沉（852 → 壳 ~520）

**现状证据**：7 职责；其中 78 行解析几何混在 tools 层违反 AGENTS.md 模块边界（"tools 只放手势与状态机"）；20 include。

**拆分方案**：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/geometry/RayCast.h/.cpp` | raySegmentIntersect(origin,dir,w1,segDir,bidirectional,eps)→{hitPoint,t}；曲线分支 rayVsCurveSpans（世界射线→局部变换→rayCurveIntersect→t/spanCount）。零 ParamDocument/QGraphics 依赖，与 CurveMath::rayCurveIntersect 同层、**可纯单测**；geometry 层适用 500 行线 | 120 |
| `src/tools/IntersectionAngleAim.h` | computeAimAngles 纯角度换算（displayDeg/storageDeg），手写 while 归一化改用 Angle.h normalizeDeg360 | 70 |
| `src/tools/IntersectionToolVisuals.h/.cpp` | 六类临时图元懒创建/样式/显隐/ManagedItems + HUD 定位；构造注入 CanvasScene*，无状态语义接口（showSegHighlight/showRay/showIntersectDot/…/clearAll） | 260 |
| `src/tools/ToolIntersection.cpp`（壳） | 生命周期/事件分派/四态 handler/commitIntersection（ParamPoint 装配+undo）/aimPoint 扫描/modeIndicator 文案 | ~520 |

**附带治理**：`m_bidirectional` 全文件无任何置 true 入口（仅 resetState 置 false）却参与求交与持久化——潜伏开关，本任务内确认语义后修复或删除（允许的行为修正，需在执行记录注明）。
**影响面测试**：`ctest -R "test_tool_intersection|test_intersection|test_expression"` + geometry 单测 test_curve。⚠️ test_intersection_update 全族是既有环境漂移红（依赖活档），勿当回归追。
**验收**：geometry/RayCast 有新增纯单测（可挂进现有 test_curve 或新开小 target）；`check_layering` 绿（tools→geometry 单向合法）。

### T2.5 ToolRotate.cpp 纯函数抽取（895 → 壳 ~550）

**现状证据**：7 职责；24 include（含 ui/LinePropertyDialog.h 越层直耦）。既有 6 子组件（RotateSession/MultiRotateSession/RotateAimSnap/RotateInputTracker/MarqueeGesture/RotateGizmo）边界已成型，状态机核心必须留壳（强拆会触停止规则——已有 3 个 friend 声明不可再扩）。

**拆分方案**（只抽可值结构化的纯逻辑，输入输出走值语义）：

| 新文件 | 收纳内容 | ~行 |
|---|---|---:|
| `src/tools/RotateDragMath.h/.cpp` | computeDragTargetDeg/computeMultiDeltaDeg 纯函数：拖拽角度累积、三模约束吸附（无约束瞄定/母线基准 normalizeDeg180/世界角 backSolveFollowerAngle 往返/复制手势累积角）；DragSample 输入结构 | 110 |
| `src/tools/RotateGizmoPose.h/.cpp` | computeGizmoPose(GizmoPoseInput)→{refBaseRad,currentPoseRad,deltaDeg,badgeText}：四分支持态计算与徽标文案（±0.01° 阈值/normalizeDeg180/360 分域） | 130 |
| `src/tools/RotateHintTexts.h/.cpp` | buildStatusHint(HintSnapshot)/buildAnchorReason 文案纯函数 + openRotateLinePropertyDialog(scene,doc,pos) 自由函数（消除 cpp 对 ui/LinePropertyDialog.h 直耦） | 100 |

**影响面测试**：`ctest -R "test_rotate_copy|test_rotate_copy_gestures|test_component|test_mode_indicator"`。
**验收**：壳 ~550/include ≤18；状态机核心（事件推进/选区/锚心）未外迁；friend 声明数不增。

---

## 七、批次 3：函数级整改（禁止整体拆文件）

### T3.1 ToolSelect.cpp 整改（857 行，52 include）⭐ 含一个必修 bug

- **include 卫生**：删除符号计数为 0 的死 include（实证清单：QMenu/QInputDialog/QLineEdit/QFontMetrics/QPen/QGraphicsRectItem/QGraphicsSimpleTextItem/QEvent/QWidget、ConditionEngine.h、Serial.h、FollowerAngle.h、CurveMath.h、AttachmentGraph.h、LinePropertyDialog.h、PlacedPointDialog.h、DeleteImpactConfirm.h、Block/Endpoint/Component/Document/LayerCommands.h 各一）+ 第 42/54 行重复 AttachmentCommands.h。52→~28。
- **重叠电池簿记归还**：m_clickedOverlapPos/m_clickedOverlapCands 连同点击采集/toast/热键开合分支（Space/Alt/W/B）迁入既有 `OverlapDisambiguationController`（~60 行迁出，消除双处簿记）。
- **⚠️ 必修：门面绕过**（约 781-793 行）：放置点 Del 删除在无 undoStack 时**直接 erase auxPointIds/points + rebuildPointIndex + resolveAll**——违反"模型变更只走门面/RawModelAccess"铁律。修为统一走 `RemovePlacedPointCommand`；undoStack 缺失时 no-op 或上提门面方法。这是本计划唯一允许的行为修正（原静默回退即 bug）。
- **禁止项**：不得再拆 SelectionModel/键盘处理器等新文件（停止规则：再拆必新增 friend，已有 3 个）。
- **影响面测试**：`ctest -R "test_select_wkey|test_overlap_battery"`。**验收**：include ≤28；门面原则恢复；`check_layering`/`check_hardcoded_colors` 绿。

### T3.2 LinePropertyDialog.cpp 整改（781 行，42 include）

- 删除 ~10 个组件抽取残留死 include（QPushButton/QButtonGroup/ConditionEngine/AttachmentGraph/LinkedVariable/Units/Angle/FormScaffold/FollowerAngle/PointRefEdit）；
- `refreshConnHint` 连接拓扑摘要派生抽为语义化自由函数 `formatConnectionHint(doc, blockId)` → 新 `src/ui/ConnHintFormatter.h`（~60 行）；
- 匿名空间微件工厂三件套（makeSectionHeader/makeDivider/makeCompactEdit）上收既有 `src/ui/FormScaffold.h`（AGENTS.md 确权共享骨架落点），本文件 -60 行；
- **禁止项**：五个"职责"不得拆成新壳文件（4 个是薄委托壳共享 m_blockId/m_segmentId/m_paramDoc，硬拆必开 friend）。
- **影响面测试**：`ctest -R "test_dialog_tabs"`。**验收**：≤700 行；include ≤30。

### T3.3 SegmentAngleCard.cpp 函数级收口（749 行，3 职责内聚——禁止拆文件）

- "世界角 = atan2 + normalizeDeg360" 块重复 6 次 → 提自由函数 `worldDegOfSegment(block, seg)`；
- "driven 点挑选 + 公式求值 + followValue 回显"模式重复 4 次 → 合为 `evaluatedFollowValueText(...)`；
- 三模字段（value+formula）按 rotationMode 读写访问在 5 处 switch 级联 → 提 get/setByMode 访问器；
- `captureEditStripState` 迁为 `SegmentEditBarCommand::State::captureFrom` 静态工厂（命令自拥快照）；
- 清理 CanvasScene.h 等无效 include。
- **影响面测试**：`ctest -R "test_dialog_tabs|test_context_strip"`。**验收**：-150~200 行；include ~15；五态语义不变。

---

## 八、批次 4：测试治理（6 文件拆域）

> 通用步骤：新文件按域命名 `test_<域>.cpp`；复制原有 initTestCase/cleanup 夹具骨架；夹具重复 ≥3 处下沉 `tests/TestHelpers.h`；每个新 .cpp = **新 ctest target**（CMakeLists 注册，注意 13 个胖测试的 `/INCREMENTAL:NO` 标志惯例）；原文件删空后移除其 target；构建 `tools\build.bat <new_test>` 逐个验证；跑 `ctest -R <域前缀>`。

### T4.1 test_resolver.cpp（1798 行 / 33 例 → 4 卷）

- `test_resolver_points.cpp`：点约束系（freePoints/polar±Formula/orthoOffset×2/midpoint/interpolatedPercentAndConstantAdd，7 例）
- `test_resolver_attachment.cpp`：attachment 拓扑与校验（snapsBlocks/leaderStart/disambiguates/followerAngleCCW/rejectsDuplicate/rejectsCycle/acceptsChain/connPointRetarget，8 例）
- `test_resolver_curve_arc.cpp`：曲线锚点+弧长（curveAnchor×4/arcLength×3/rigidDragKeepsCurveCacheFrozen，8 例）
- `test_resolver_diag_misc.cpp`：诊断与杂项（dangling×2/healthyForest/bridge×2/measureLine/endTarget/aimRing/oneWayAim/structureEpoch/blockPointer，~10 例）

### T4.2 test_rotate_copy_gestures.cpp（1759 行 / 24 例 → 5 卷）

- `test_rotate_anchor.cpp`：锚心切换+跟随保护+Gizmo 反馈（8 例）
- `test_rotate_d15_gate.cpp`：D15 确认门状态机（4 例）
- `test_rotate_strip.cpp`：条带读数与 °/⌒/↔ 模式显示（5 例）
- `test_rotate_copy_flow.cpp`：Ctrl 复制与拖拽提交（4 例）
- `test_rotate_pivot_multi.cpp`：多选框选与任意锚心 CAD 流（3 例）
- ⚠️ 本文件含 GUI 抖动基线用例（test_rotate_copy 族）——拆后如遇偶发红先单跑确认。

### T4.3 test_dialog_tabs.cpp（1599 行 / 16 例 → 3 卷）

- `test_dialog_tabs_switch.cpp`：页签切换稳定性（switchBack×3/probe×2，5 例；含抖动基线 switchBackAfterTyping）
- `test_dialog_tabs_angle_conn.cpp`：角度与连接卡（endpointCardsStacked/connectCardUniformHeights/independentAngleInputDoesNotJump/followerAngleModeToggleToArc/angleFormulaPreserved/endConnectionRowAndBadge/detachClears…/reattachPreserves/linkCurrentLine/independentAngleKeepsRef，10 例）
- `test_dialog_tabs_aux.cpp`：辅助点挂载（auxPointOutgoingFollowerMountAndDetach）

### T4.4 test_rotate_copy.cpp（1550 行 / 22 例 → 3 卷）

- `test_rotate_copy_semantics.cpp`：复制挂接语义（clone×2/undoRedo/formulaLocked/ctrlDrag×2/escCancels/consecutiveCopies/commitRestoresStrip/lockedFollower×2/midGestureCtrl，12 例）
- `test_rotate_copy_shadow.cpp`：影子通道（shadowChannel×2 + 相关，2~3 例）
- `test_rotate_copy_endtarget.cpp`：终点指向瞄准（endTarget×5 + modeSwitchKeepsFormula/propertyDialogShowsFollowValue，7 例）

### T4.5 test_attachment_commands.cpp（1525 行 / 16 例 → 3 卷）

- `test_attachment_shadow.cpp`：影子生命周期独立卷（shadowDetach×3/shadowLifecycle/shadowReconnect + auxPointAttachment_detachAndReconnect，6 例）
- `test_attachment_slide.cpp`：slideMode 三例
- `test_attachment_angle.cpp`：angleOnly×2/angleRefTwoPointBasis×2/chordLength/endPinnedLength/curvePointAttach，7 例

### T4.6 test_break.cpp（1515 行 / 19 例 → 3 卷）

- `test_break_basic.cpp`：基础+公式（basic/constant/fromEnd/multiAux/undoRedo/formula×2/invalidOffset/inheritsLayer/refChainBack，10 例）
- `test_break_curve.cpp`：曲线保形 4 例
- `test_break_follower.cpp`：跟随者+组件+杂项（followerAtOriginalEnd/curveFollower/followerAtBreakpoint/componentAtBreakpoint/polarEndpointAnchorCycle/hoverReportsSegment，6 例）

---

## 九、任务依赖与并行矩阵、全局收尾

### 9.1 依赖矩阵

| 关系 | 说明 |
|---|---|
| T0.1/T0.2 → 所有批次 | 基线契约先行，否则拆完后守卫口径打架 |
| T1.x 之间 | 文件互不重叠，**可并行**；唯 CMakeLists.txt 串行编辑（同一时刻只允许一个任务改） |
| T2.x / T3.x / T4.x | 与 T1.x 及彼此间文件均不重叠，可并行；同上 CMake 串行 |
| T1.3（Block）前后关系 | 建议在 T2.1（Resolver）之前落地——Resolver 壳会消费 BlockQuery 访问器，但无硬依赖 |
| T4.2 ↔ T2.5 | 都涉 rotate 域：T2.5 改 src、T4.2 只搬测试，可并行，但 T4.2 验证应等 T2.5 落定后跑 |
| T3.1 → test_select_wkey 族 | 门面绕过修复会改变无栈删除语义，测试若依赖旧静默回退需同步修 |

### 9.2 全局收尾（全部任务完成后，仅允许一次）

1. `tools\build.bat reldeb`（唯一一次全量构建）+ `ctest -C RelWithDebInfo`（唯一一次全量）；
2. 基线核对：既有基线红 = 0；环境漂移红（test_intersection_update / test_extend::savedDoc…）不记分；
3. 文档同步（AGENTS.md 纪律"任务收尾防过时"）：
   - 更新 `DOCS_INDEX.md` 本文件状态 → 「已落地」；
   - `CONVENTIONS.md` 守卫清单若有 T0.2 新增检查则补记；
   - 若 T2.4 落地，AGENTS.md 模块边界表中 geometry 行补 `RayCast`；
4. `redline_exceptions.json` 终态：file_size_exceptions 应只剩 DocumentSerializer（豁免）/ CurveMath（Backlog B.1）/ ReverseSegmentCommand（暂缓）三件，bool_flag_exceptions 剩 ParamPoint/Props 两件；
5. 本文件归档至 `docs/archive/plans/FILE_SPLIT_PLAN_V3.md`（沿袭 V2 归档惯例）。

### 9.3 持续治理 Backlog（不在本轮，仅登记）

- **B.1 CurveMath.cpp**（928 行，geometry 阈值 500）：纯几何算法域拆分（Bézier 求值/弧长表/求交/射线分列），待 T2.4 RayCast 落地后一并规划；
- **B.2 ParamDocument.h**（796/800）：headroom 仅 4 行，下次新增 API 前先拆 `ParamDocumentDetail.h`；
- **B.3 ReverseSegmentCommand.cpp**（458，超 400 线 58 行）：暂缓，观察一个迭代；
- **B.4 Resolver::applyAttachment 模式组合分支表驱动化**（T2.1 二级建议）。

---

## 执行记录

> 任务完成后在此登记。格式：`- [T1.1] 2026-xx-xx @执行者：主壳 1841→4xx；验证 ctest -R ... 绿；遗留：...`

- [T0.1] 2026-09-08 @Antigravity: 刷新 redline_exceptions.json 六大膨胀文件基线行数（MainWindow 1841、Block 1498、ContextStrip 1665、VariablePanel 1030、BlockItem 1040、CurveMath 928、ReverseSegmentCommand 458、DocumentSerializer 987、Resolver 886），并登记 6 个 >1500 测试文件进 test_size_exceptions；验证 `ctest -R "check_file_size|check_test_split"` 全绿；遗留：无。
- [T0.2] 2026-09-08 @Antigravity: tools/check_file_size.py 增加已登记豁免文件的膨胀棘轮检查（当前行数 > 申报行数直接 FAIL 并报膨胀量），自测人为膨胀拦截成功并复原；验证 7 守卫 `ctest -R check_` 全绿；遗留：无。
- [T0.3] 2026-09-08 @Antigravity: 维持现有守卫策略（>1500 警告，>2500 强制 FAIL），测试台账已在 T0.1 登记完备，待 T4.x 测试拆分批次推进时逐步消除警告。
- [T1.2] 2026-09-08 @Antigravity: ContextStrip.cpp（原 1665 行）拆分为 PlacedPointStripBar（83+347行）、ContextStripSessions（148行）、ContextStripDisplay（360行）、ContextStripEdit（322行）及主壳（523行），所有文件均 ≤523 行（≤900 达标）；顶层 bool 降为 3 个（≤5）；从 file_size_exceptions 与 bool_flag_exceptions 彻底移除退役；5 项受影响单测及 7 项守卫全绿；遗留：无。
- [T1.1] 2026-09-08 @Antigravity: MainWindow.cpp（原 1841 行）拆分为 MainWindowFileSession（204行）、MainWindowMenuBar（148行）、MainWindowToolBar（167行）、MainWindowStatusBar（165行）、MainWindowPanelWindow（376行）、MainWindowSegmentMenu（202行）及主壳 MainWindow.cpp（528行），所有分卷均 ≤528 行（≤900 达标）；MainWindow.h 零改动；已从 redline_exceptions.json file_size_exceptions 移除退役；7 项受影响 GUI 单测及 7 项红线守卫全部通过；遗留：无。
- [T1.3] 2026-09-08 @Antigravity: Block.cpp（原 1498 行）拆分为 BlockResolve（626行）、BlockCurve（203行）、BlockExtend（279行）、BlockQuery（209行）、BlockLifecycle（116行）及主壳 Block.cpp（107行），所有分卷均 ≤626 行（≤900 达标）；Block.h 零改动；已从 redline_exceptions.json file_size_exceptions 移除退役；11 项受影响引擎单测及 7 项红线守卫全部通过；遗留：无。
- [T1.4] 2026-09-08 @Antigravity: BlockItem.cpp（原 1041 行）四层分离为 BlockGeometryCache（92+273行）、BlockItemPainter（34+305行）、BlockItemPick（31+114行）及主壳 BlockItem.cpp（447行），所有分卷均 ≤447 行（≤900 达标）；提取 forEachEntityPushState 消除重复循环；已从 redline_exceptions.json file_size_exceptions 彻底移除退役；7 项受影响单测（test_component/test_aux_layer/test_select_wkey/test_canvas_perf/test_hold_show/test_overlap_battery/test_transient_overlay）及 7 项红线守卫全部通过；遗留：无。
- [T1.5] 2026-09-08 @Antigravity: VariablePanel.cpp（原 1031 行）页签化拆分为 CardTabBase（52+143行基类）、VariableTab（31+131行）、LinkedTab（32+118行）、FormulaTab（56+438行）及主壳 VariablePanel.cpp（76+270行），所有分卷均 ≤438 行（≤800 ui红线达标）；消除重复 include；已从 redline_exceptions.json file_size_exceptions 彻底移除退役；3 项受影响单测（test_dialog_tabs/test_variable_layer_commands/test_formula_groups）及 7 项红线守卫全部通过；遗留：无。
- [T2.1] 2026-09-08 @Antigravity: Resolver.cpp（原 887 行）工人迁出为 ResolverAttachment（247行）、ResolverIntersection（167行）、ResolverSlide（37行）及主壳 Resolver.cpp（367行），所有分卷均 ≤367 行（≤900 达标）；Resolver.h 零改动；已从 redline_exceptions.json 彻底移除退役；7 项受影响单测（test_resolver/test_serializer/test_attachment_commands/test_intersection/test_tool_intersection/test_resolve_scale/test_intersection_update）及 7 项红线守卫全部通过；遗留：无。
- [T2.2] 2026-09-08 @Antigravity: SegmentRefCard.cpp（原 763 行）卡片范式拆分为 SegmentAlignPointCard（39+141行）、SegmentShadowBasisCard（42+196行）、SegmentAngleRefCard（57+407行）及主壳 SegmentRefCard.cpp（53+130行），所有文件均 ≤407 行（≤800 ui红线达标）；受影响单测 test_dialog_tabs、test_attachment_commands、test_component、test_select_wkey 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T2.3] 2026-09-08 @Antigravity: ToolCurveEdit.cpp（原 784 行）同类分卷拆分为 ToolCurveEditAnchor.cpp（331行）、ToolCurveEditHandles.cpp（309行）及主壳 ToolCurveEdit.cpp（164行），所有分卷均 ≤331 行（≤900 达标）；ToolCurveEdit.h 零改动；顺手收口 endCurveAnchorDrag 重复的弦重投影复用 chordParams；受影响单测 test_curve、test_curve_edit 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T2.4] 2026-09-08 @Antigravity: ToolIntersection.cpp（原 834 行）几何下沉与视觉分离为 RayCast.h/.cpp（38+53行，纯解析几何/零GUI依赖）、IntersectionAngleAim.h（54行，纯角度计算）、IntersectionToolVisuals.h/.cpp（57+249行，临时图元与HUD管理）及主壳 ToolIntersection.cpp（112+580行），所有分卷均 ≤580 行（geometry ≤500/tools ≤900 达标）；在 test_curve 中补充 RayCast 纯几何单测覆盖；保留 m_bidirectional 默认 false 并兼容模型序列化；4 项受影响单测（test_curve/test_tool_intersection/test_intersection/test_expression）及 7 项红线守卫全绿；遗留：无。
- [T2.5] 2026-09-08 @Antigravity: ToolRotate.cpp（原 896 行）纯函数抽取与文案对话框解耦为 RotateDragMath.h/.cpp（83+129行，纯拖拽数学/Gizmo位姿/引导点查找）、RotateHintTexts.h/.cpp（45+80行，状态提示/锚点锁定文案/对话框启动器）及主壳 ToolRotate.cpp（213+843行，≤900 达标）；彻底消除对 ui/LinePropertyDialog.h 的直接依赖；受影响单测 test_rotate_copy 及 7 项红线守卫全部通过；遗留：无。
- [T3.1] 2026-09-08 @Antigravity: ToolSelect.cpp（原 858 行）函数级整改：清理 24 个死 include（52→28 达标）；重叠电池点击簿记与热键处理（recordClickedOverlap/handleSpaceOrAltKey/handleWOrBKey）彻底归还 OverlapDisambiguationController（消除双处簿记，ToolSelect.h 移除两字段）；修复放置点 Del 删除绕过 undoStack 直改模型的 bug（统一通过 RemovePlacedPointCommand 门面提交）；行数 858→779 行（≤900 达标）；受影响单测 test_select_wkey、test_overlap_battery 及 7 项红线守卫全部通过；遗留：无。
- [T3.2] 2026-09-08 @Antigravity: LinePropertyDialog.cpp（原 781 行）函数级整改：清理 13 个死 include（42→29 达标）；连接拓扑摘要派生抽为 formatConnectionHint 自由函数落地新文件 ConnHintFormatter.h（65行）；匿名微件三件套（makeSectionHeader/makeDivider/makeCompactEdit）上收 FormScaffold.h 并同步消除 LineGeometrySection/LineOrthoOffsetCard 中的重复定义；行数 781→669 行（≤700 达标）；受影响单测 test_dialog_tabs、test_select_wkey 及 7 项红线守卫全部通过；遗留：无。
- [T3.3] 2026-09-08 @Antigravity: SegmentAngleCard.cpp（原 745 行）函数级整改（零拆分）：收口死 include；将 SegmentEditBarCommand::State::captureFrom 上收为命令门面静态方法；提炼 worldOrNominalDegOfSegment/formatAttachmentFormulaFollowValue/formatAttachmentDisplayValue 等无状态纯函数收口三模态与自由线/独立角重复分支；消除 applyAngle 中重复的角度/公式计算；简化 onModeToggle 目标模态流转与命令分派；行数 745→577 行（净减 168 行，达标 -150~200 行）；受影响单测 test_context_strip、test_dialog_tabs 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.1] 2026-09-08 @Antigravity: test_resolver.cpp（原 1798 行 / 33 例）按域拆分为 test_resolver_points.cpp（291行/7例）、test_resolver_attachment.cpp（456行/8例）、test_resolver_curve_arc.cpp（289行/7例）、test_resolver_diag_misc.cpp（545行/11例），所有新文件均 ≤545 行（远低于 1500 警告线）；原大文件及目标已彻底移除退役；从 redline_exceptions.json test_size_exceptions 移除；4 个独立测试 target 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.2] 2026-09-08 @Antigravity: test_rotate_copy_gestures.cpp（原 1759 行 / 24 例）拆分共享夹具 test_rotate_helpers.h（132行）并分卷为 5 个独立测试目标：test_rotate_anchor.cpp（595行/8例）、test_rotate_strip.cpp（282行/4例）、test_rotate_d15_gate.cpp（351行/5例）、test_rotate_copy_flow.cpp（284行/4例）、test_rotate_pivot_multi.cpp（313行/3例），所有新分卷均 ≤595 行；test_rotate_copy.h 收敛至 42 行；原大文件及异常登记已彻底移除退役；6 个关联测试目标（含 test_rotate_copy）及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.3] 2026-09-08 @Antigravity: test_dialog_tabs.cpp（原 1599 行 / 16 例）拆分共享夹具 test_dialog_tabs_helpers.h（92行）并分卷为 3 个独立测试目标：test_dialog_tabs_switch.cpp（480行/5例）、test_dialog_tabs_angle_conn.cpp（959行/10例）、test_dialog_tabs_aux.cpp（85行/1例），所有新分卷均 ≤959 行（远低于 1500 警告线）；原大文件已彻底移除退役；从 redline_exceptions.json test_size_exceptions 移除；3 个独立测试 target 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.4] 2026-09-08 @Antigravity: test_rotate_copy.cpp（原 1550 行 / 22 例）分卷为 3 个独立测试目标：test_rotate_copy_semantics.cpp（699行/12例）、test_rotate_copy_shadow.cpp（206行/2例）、test_rotate_copy_endtarget.cpp（694行/8例），所有新分卷均 ≤699 行（远低于 1500 警告线）；原大文件 test_rotate_copy.cpp 及 test_rotate_copy.h 已彻底移除退役；从 redline_exceptions.json test_size_exceptions 移除；8 个关联测试 target 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.5] 2026-09-08 @Antigravity: test_attachment_commands.cpp（原 1525 行 / 16 例）拆分共享夹具 test_attachment_helpers.h（78行）并分卷为 3 个独立测试目标：test_attachment_shadow.cpp（499行/6例）、test_attachment_slide.cpp（295行/3例）、test_attachment_angle.cpp（673行/7例），所有新分卷均 ≤673 行（远低于 1500 警告线）；原大文件已彻底移除退役；从 redline_exceptions.json test_size_exceptions 移除；3 个独立测试 target 及 7 项红线守卫全部 100% 通过；遗留：无。
- [T4.6] 2026-09-08 @Antigravity: test_break.cpp（原 1515 行 / 19 例）拆分共享夹具 test_break_helpers.h（204行）并分卷为 3 个独立测试目标：test_break_basic.cpp（779行/10例）、test_break_curve.cpp（138行/3例）、test_break_follower.cpp（419行/6例），所有新分卷均 ≤779 行（远低于 1500 警告线）；原大文件已彻底移除退役；从 redline_exceptions.json test_size_exceptions 移除（test_size_exceptions 清空至 0 项）；3 个独立测试 target 及 7 项红线守卫全部 100% 通过；遗留：无。
- [9.2] 2026-09-08 @Antigravity: 全局收尾验收完成。执行 tools\build.bat reldeb 全量构建 100% 成功；执行 ctest -C RelWithDebInfo 全量跑测 61/61 全部通过（100% 通过率，耗时 42.81s，基线红与新红均为 0）；redline_exceptions.json test_size_exceptions 降为 0，file_size_exceptions 仅剩 3 项历史豁免；DOCS_INDEX.md 状态更新为「已落地」，AGENTS.md 与 CONVENTIONS.md 同步完成，计划归档至 docs/archive/plans/FILE_SPLIT_PLAN_V3.md。

