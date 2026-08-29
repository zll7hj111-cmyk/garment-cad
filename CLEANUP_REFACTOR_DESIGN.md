# 清理与收口重构任务书

> 产出日期：2026-08-28。本任务书供另一个 AI/执行者独立完成「重复构建残留清理 + 源码重复实现收口 + 绕路写法简化」。
> 所有结论均带 file:line 证据（2026-08-28 排查时点）。执行前请先以源码为准复核行号（代码可能已漂移）。

---

## 0. 背景与铁律（执行前必读）

- 项目：WildWind Pattern（野风帖），C++23 / Qt6 参数化服装 CAD，构建 `tools\build.bat`（Ninja Debug，binaryDir=`build/out`），验证 `cd build/out && ctest`。
- **ctest 基线**：26 用例中 2 红为既有基线（test_serializer::bridgeAuxPointSnappableAndAttachable + test_component::dragComponentLeaderCurveFollowStable）；test_intersection_update 5 例 + test_extend::savedDocFormulaStartExtendRenders 依赖活档 `E:/3.gcad`，属环境漂移红，**非回归**。验收标准是"不比基线多红"。
- **文件编码**：所有新建/编辑的源文件必须 UTF-8 with BOM。Write/Edit 工具写出的文件无 BOM（Edit 还会剥掉原有 BOM），改完含中文的文件后必须补 BOM：
  `pwsh -c "[System.IO.File]::WriteAllText($p, [System.IO.File]::ReadAllText($p), [System.Text.UTF8Encoding]::new($true))"`
- **经验库规则**：动手前先 grep `TROUBLESHOOTING.md` 相关关键词；修完把根因+验证追加到对应分组；触碰 AGENTS.md 声明的事实时同步修订。
- **架构约定不可破坏**：ParamDocument 门面、分层单向依赖、隐藏实体语义、12 处约束类型分派点登记（本任务书 B4 在其范围内操作，不新增/修改枚举，不违反约定）。

---

## 任务 P0：删除根目录构建残留（60 个文件）

**背景**：2026-08-27 某次会话未走 `tools\build.bat` + Ninja，而是手工拼 cl.exe 命令行逐文件编译/链接，留下一批临时产物。已验证全仓（md/txt/json/toml/cmake/bat）只有自引用，CMake 与文档均无依赖。

**删除清单**（全部位于仓库根目录）：

- 环境与单文件编译：`msvc_env.bat`、`one_cc.bat`、`cl_t.obj`
- 逐模块编译脚本：`c_0.bat` `c_1.bat` `c_2.bat` `c_3.bat` `c_4.bat` `c_5.bat`
- 手工链接脚本：`lk_0.bat` `lk_1.bat` `lk_2.bat` `lk_3.bat`
- 逐测试编译脚本：`tc_0.bat` `tc_1.bat` `tc_2.bat`
- 拼装式构建脚本：`build_libs.bat` `build_t2.bat` `build_probe3.bat` `build_tests2.bat` `build_final3.bat`、`exes.txt`
- 链接响应文件：`rsp_*.rsp` 共 43 个（`rsp_lib_gcad_{geometry,parametric,canvas,document,ui,tools,app}.rsp`、`rsp_exe_WildWindPattern.rsp`、`rsp_exe_test_*.rsp` 35 个）

**验收**：删除后 `tools\build.bat` 全量构建通过；`git status` 确认删除集与清单一致、无误删。

---

## 任务 A1：角度归一化收口（最高价值，含语义分叉修复）

**问题**：`src/geometry/Angle.h` 已收口 `normalizeDeg360/normalizeDeg180`（2026-08 既定约定），但全库仍约 18 处手写 `fmod(x,360); if(<0)+=360` 或 `while(>180)-=360` 副本。

**⚠️ 语义分叉（必须先决策）**：`src/tools/SegmentAnchorTab.cpp:329-331, 336-338` 的手写版边界是 `< -180`，而 Angle.h 公共版是 `<= -180`——输入恰好 -180° 时两者结果不同（公共版映射到 +180，手写版保持 -180）。**默认建议：统一采用 Angle.h 语义**（一处定义全局一致）；若担心 -180° 在锚点角度场景有特殊含义，先全仓搜该路径的测试覆盖再拍板。

**替换清单**（全部改为 `cad::geo::normalizeDeg360/180`，删除本地副本；命名空间以 Angle.h 实际声明为准）：

| 文件 | 位置 |
|------|------|
| src/app/SegmentEditBar.cpp | :30-35 私有 `normalizeDeg`（与 `geo::normalizeDeg360` 逐字节等价，删）；:192, :416, :425 调用点 |
| src/tools/SegmentAnchorTab.cpp | :329-331, :336-338（分叉点，见上） |
| src/tools/SegmentAngleCard.cpp | :327-328, :375-376, :501-502, :545-546, :609-610 |
| src/tools/SegmentAuxTab.cpp | :291-293, :300-301 |
| src/tools/ConnectGesture.cpp | :1151-1152 |
| src/tools/LineFactory.cpp | :187-193 |
| src/tools/ToolRotate.cpp | :1564-1565, :2003-2005（紧邻 :2001 已有 normalizeDeg180 调用，合并） |
| src/document/commands/BreakCommands.cpp | :1182-1183 |
| src/tools/ToolSmartPen.cpp | :1014-1019（displayOf lambda 可一步写为 `normalizeDeg180(180-rel)`） |

**验证**：构建 + `ctest -R "segment_edit_bar|rotate_copy|commands|break"`。

---

## 任务 A2：「公式求值+判 ok+cmToMm」惯用法收口（约 20 处）

**问题**：形状恒为
```cpp
if (!f.isEmpty()) { auto r = ConditionEngine::evaluate(f, ...); if (r.ok) xMm = Units::cmToMm(r.value); }
```
散落在 Block/Resolver/ToolRotate/ConnectGesture/SegmentAngleCard/ToolSmartPen/SegmentAuxTab/BreakCommands。

**方案**：ConditionEngine 增加一个静态助手（参考签名，以现有 evaluate 重载为准）：
```cpp
// 返回是否求值成功；成功时 outMm 写入毫米值
static bool evaluateLengthMm(const QString& formulaCm, /* params/conditioned/ctx 与现有 evaluate 对齐 */,
                             double& outMm);
```
内部完成：空串检查 → evaluate → ok 判定 → `Units::cmToMm`。

**替换清单（代表，全仓 grep `cmToMm` 邻近 `evaluate` 找齐）**：Block.cpp:176,286,524,548,612,629,1136,1141；Resolver.cpp:606,750,820,825；ToolRotate.cpp:1216,1376,1534,1796；ConnectGesture.cpp:1078,1148；SegmentAngleCard.cpp:221,498,540；ToolSmartPen.cpp:374,724；SegmentAuxTab.cpp:280；BreakCommands.cpp:461,492。

**验证**：构建 + 全量 ctest 基线对比。

---

## 任务 A3：角度↔弧长双模切换收口

**问题**：`src/tools/ConnectGesture.cpp:1145-1170` 与 `src/tools/SegmentAngleCard.cpp:491-520` 几乎逐行复制（arcLengthFormula 求值 → curDeg 归一 → 按 rotationMode 写回 followerAngle/arcLength）。换算式另散见：ToolRotate.cpp:1621、SegmentAngleCard.cpp:511,546、ConnectGesture.cpp:1164、Resolver.cpp:752。

**方案**：
1. 提自由函数 `arcMmToDeg(arcMm, radiusMm)` / `degToArcMm(deg, radiusMm)`（建议放 Angle.h 或 CurveMath.h，就近原则拍板）；
2. 两处 mode-toggle 写回逻辑提为一个共享函数（参数：formula、radius、mode、当前值；返回写回后的 followerAngle/arcLength 对）。

**验证**：构建 + `ctest -R "segment_edit_bar|rotate_copy"`，并手工核对一次角度模式/弧长模式来回切换的 UI 行为。

---

## 任务 A4：CanvasScene::currentZoom() 收口（43 处，收益大风险小）

**问题**：取缩放的三行惯用法 `views().first()->transform().m11()` 全库 43 处，连 `src/canvas/CanvasScene.cpp:168,510` 自己都手写。

**方案**：CanvasScene 增加：
```cpp
double currentZoom() const;  // views() 为空时返回 1.0；取 first()->transform().m11()
```
各调用点一行替换。代表位置：ConnectGesture.cpp:152,225,283,329,766,842,1292,1336,1372,1383；ToolBreak.cpp:53,106；ToolSmartPen.cpp（多处）；ToolMeasure.cpp:75 上下文；ToolAngleMeasure.cpp:60,87；LeaderCandidatePicker.cpp:31；CanvasScene.cpp:168,510。**全仓 grep `transform().m11()` 找齐**。

**验证**：构建 + 全量 ctest 基线对比。

---

## 任务 A5：CardBase::setCommentSilently 收口（5 处）

**问题**：五张卡片各自复制 `blockSignals(true); setText(...); blockSignals(false);`（只读三卡还带相同 hasFocus 守卫）：VariableCard.cpp:64-66、FormulaCard.cpp:110-112、LinkedCard.cpp:62-64、MeasureCard.cpp:88、AngleMeasureCard.cpp:65。CardBase（2026-08 抽取）已收 commentEdit 成员（`createCommentEdit`）但回填逻辑未收。

**方案**：CardBase 增加 `void setCommentSilently(const QString& text);`——内部 QSignalBlocker + hasFocus 守卫（守卫语义以只读三卡现状为准，统一到五卡），五卡各删 3 行。

**验证**：构建 + `ctest -R "dialog_tabs|hold_show"`。

---

## 任务 A6：「数值或公式」解析入口收口（约 10 处）

**问题**：`toDouble(&ok)` 成功走数值、失败走 `ConditionEngine::evaluate` 的两段式重复：ToolSmartPen.cpp:365-378,385,715,734；SegmentEditBar.cpp:233,259；SegmentAngleCard.cpp:396；SegmentExtendCard.cpp:228；ConnectGesture.cpp:1053；LinePropertyDialog.cpp:954。

**方案**：提共享解析入口（建议放 ConditionEngine 或 Units 附近）：
```cpp
struct ParsedInput { bool isNumber; double valueMm; QString formula; /* 原样保留用户公式串 */ };
ParsedInput parseCmOrFormula(const QString& text, /* doc/ctx 按需 */);
```
注意保留各处对空串/非法输入的现有差异化处理——若有调用点错误处理语义不同，不要强行统一，只收共同主干。

**验证**：构建 + `ctest -R "smartpen_aux|segment_edit_bar"`。

---

## 任务 B1：QSignalBlocker 改造（修 blockSignals 泄漏隐患）

**问题**：`src/tools/SegmentAnchorTab.cpp:278-283` 与 `370-375`：6 个控件 12 次手工 blockSignals，阻塞/恢复跨约 90 行，中途新增 return 即泄漏阻塞态，且只保存了第一个控件的 wasBlocked 却套用到全部 6 个。散见手工成对：ConnectGesture.cpp:1016,1177；ToolRotate.cpp:1687；MeasureResultDialog.cpp:79；CopyChip.cpp:112。

**方案**：全部改 `QSignalBlocker` 栈对象（SegmentAnchorTab 处可 `std::array<QSignalBlocker,6>` 或逐个声明），RAII 保证早退安全。**注意行为差异**：QSignalBlocker 析构恢复的是"构造时各自的状态"，比现状（统一恢复为第一个控件的状态）更正确——这是顺手修掉的隐性 bug，需在提交说明中点明。

**验证**：构建 + `ctest -R "segment_edit_bar|dialog_tabs"`，手工核对锚点 tab 各控件联动。

---

## 任务 B2/B3/B5：小修三项

- **B2** `src/tools/SegmentConnectionCardConn.cpp:506-507`：`* 10.0` × 2 → `cad::geo::Units::cmToMm(...)`。
- **B3** 占位符字符串提共享常量（建议放 FormScaffold.h 或 Units 附近），统一 QStringLiteral：「数值（°）或公式」— SegmentAngleCard.cpp:62,332,360,380 + ComponentTab.cpp:184,213；「数值（cm）或公式」— LinePropertyDialog.cpp:398、SegmentAngleCard.cpp:350。
- **B5** `src/ui/FormulaCard.cpp:107,261`：`QString::number(*actualValueCm,'f',2)` → `Units::formatNumberTrimmed`（2026-12 已拍板去尾零，该处漏改）。

**验证**：构建 + `ctest -R "dialog_tabs"`。

---

## 任务 B4：序列化字符串映射表驱动（可选，放最后）

**问题**：`src/document/DocumentSerializer.cpp:37-112` 四组映射（pointConstraintStr/From、segmentTypeStr/From、segmentRoleStr/From、lineStyleStr/From）各自手写 switch + if 链双向映射，新增枚举要改两处且 From 侧漏改不报错（文件注释自承认）。

**方案**：每组改一张 `constexpr std::pair<Enum, const char*>[]` 表 + 双向查找循环。
**约束**：①不新增/修改枚举值，纯结构重构；②改动后必须同步核对 AGENTS.md「12 处约束类型分派点登记」条目表述（序列化成对映射那一处变成"表驱动"，登记点数与位置不变）；③test_serializer 全量过（除既有基线红外）。

---

## 执行顺序与总体验收

推荐顺序（风险从低到高，每步独立可提交）：

1. **P0** 删根目录残留（与代码完全解耦，先做）
2. **B2/B3/B5** 小修（最小改动热身）
3. **A4** currentZoom（机械替换，43 处）
4. **A5** setCommentSilently（5 处）
5. **A1** 角度归一化（先拍板 -180° 语义，默认采 Angle.h）
6. **A2** evaluateLengthMm + **A6** parseCmOrFormula（同在 ConditionEngine 加助手，可一个提交）
7. **A3** 弧长↔角度收口
8. **B1** QSignalBlocker（行为微调，单独提交并注明）
9. **B4** 序列化表驱动（可选，最后）

**每步**：`tools\build.bat` 通过 + 指定 ctest 子集过。
**全部完成后**：`cd build/out && ctest` 全量，确认不比基线（2 红 + 环境漂移红）多红；GUI 时序抖动用例（dialog_tabs/aux_layer/rotate_copy）单测复跑确认。

**收尾铁律**：
- 所有改过的含中文源文件补 BOM；
- `TROUBLESHOOTING.md` 追加本次收口的根因与验证记录（新增一个分组或归入对应主题）；
- 核对 AGENTS.md 中"角度工具收口""CardBase"等条目表述是否与改后事实一致，不一致则同步修订；
- 若助手函数放置位置与本任务书建议不同，以执行时源码结构判断为准，不必回问。

---

## 已排除的误报（不要再去"修"这些）

- CardBase 五卡均正确继承使用基类；crossLayerToast/crossLayerBadge 唯一实现在 tools/LayerFeedback.h；kSnapOverlapEps 唯一定义于 tools/SnapEngine.h；捕捉/悬停全部经 SnapEngine；序列化字符串映射仅 DocumentSerializer 一份；公式求值已有 EvalContext per-pass memo（ConditionEngine.h:36-43）。这些是正确遵守约定的现状，勿动。
