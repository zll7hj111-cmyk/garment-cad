# 端点延长线功能设计（设计文档）

> 状态：**已落地（2026-08-26 源码落地批次，`44c940d`/`d03ec27`）**。本文档是"延长线"功能唯一权威设计记录。
> 用户拍板项均标注【拍板】；实现接线清单见文末，动代码前逐条核对。

---

## 1. 背景与目标

制版中常需把一条线段沿端点方向**往外延长**一段（如肩线延长），且延长量经常是一个
**变量/公式**（许多体型该变量为 0，仅个别体型需要调整）。

需求要点（与既有机制的区别）：

- **不是辅助点**：辅助点是线上加一个定位点，不改线段；延长线**修改端点**。
- **不是改长度变量**：改长度变量 = 重新求值"起点→终点"的距离公式（起点不动、终点按
  公式动）；延长线 = 在**原有端点参数化结果之上**叠加一个"往外多长一截"的参数。
  **原公式、原参数化一律保留，不清除。**
- **不拆连接**：被延长端点常连接着其他线段。不能做成"独立延长点/新线段"（那要拆开
  重连）；直接在端点上延长，与它相连的其他线段自动跟着实际端点走。

---

## 2. 术语（全文档统一）

| 术语 | 含义 |
|---|---|
| 本体段 / 本体位置 | 端点按现有全部参数化机制（Polar 公式、附着、省道、桥 pin）解出的、**未延长**的几何 |
| 延长量 | 端点参数：沿该端点"出方向"往外延伸的距离（mm；公式域 cm；≥0；默认 0） |
| 实际位置 / 有效段 | 本体 + 延长量 × 出方向（用户看到的、可交互的几何） |
| 出方向 | 终点 = 起点→终点方向；起点 = 终点→起点方向（即现有"延长方向"语义） |
| 粘死端 | 本线自己作为跟随线的吸附端点（fromPoint）；桥接线的两个 pin 端；省道线的计算端点 |
| 跨线共点 | 两条**独立线段**的端点落在同一位置（可已连接） |
| 同块角点 | 同一 Block 内多条线段共用的端点（折线 / 封闭轮廓的角点） |

---

## 3. 已拍板决策清单

| # | 决策 | 类型 |
|---|---|---|
| D1 | 属性面板**延长卡片**：起点、终点各一栏；**数值与公式均 cm 域**（2026-11 用户拍板：卡片统一 CM；内部存储仍 mm、公式 cm 域，数值经卡内 cm↔mm 转换）；公式优先；默认 0 | 【拍板】 |
| D2 | 允许向外延长（正值）与向内收缩（负值）；负值向内收缩严格受辅助点限位钳制（端点绝不越过任何已存在辅助点），无辅助点时保留 >=1mm 最小线长防退化 | 【拍板 2026-09 演进】 |
| D3 | 曲线不做延长；曲线线段延长卡片置灰 | 【拍板】 |
| D4 | 跨线共点：连着的线自动跟着实际端点走，不拆连接 | 【拍板】 |
| D4b | 同块角点（同 Block 折线/封闭轮廓共角）：MVP **不允许**，该端置灰提示 | 【拍板】 |
| D5 | 粘死端不允许延长（置灰）；推论：桥线、省道线整条延长卡片置灰 | 【拍板】 |
| D6 | 显示长度 = 实际长度（原长＋延长量）；测量所见即所得 | 【拍板】 |
| D7 | 延长尾巴算线的身体：可选中、可吸附、可连接、可测量；**不允许在尾巴上新建辅助点**（提示"请在本体范围内"） | 【拍板】 |
| D7b | 交叉点（Intersection 等"按点位置"定义）跟着实际线走；只有"按本体长度/比例"的定义保留本体语义 | 【拍板】 |
| D8 | 打断：仅本体范围内可切；延长量随端点归属；辅助点按本体照旧；新断点延长量为 0 | 【拍板】 |
| D9 | 组件暴露端点所在线段不允许延长（该端置灰），其他成员线允许 | 【拍板】 |

---

## 4. 核心模型：本体（base）与实际（effective）双轨

**一句话模型**：延长量只在"本体解算完成之后"作用于端点，端点**实际位置** =
本体位置 + 延长量 × 出方向；一切**按点位置**的定义用实际位置，一切**按线段本体
长度/比例**的定义用本体位置。

### 4.1 消费方分轨表

| 消费方 | 读哪个 | 理由 |
|---|---|---|
| 辅助点（Interpolated：percent/constant/fromEnd/refPoint/偏移） | **本体** | 辅助点"仍以原起点按原长度确定"，不因延长漂移 |
| 曲线锚点（CurveAnchor，弦向/弧长%） | **本体** | 曲线本体不变（曲线本身不延长；端点随跨线共点被带走时按 4.3） |
| 曲线缓存锚点 | **本体** | 同上 |
| 本线自身作为跟随线的锚点（fromPoint pin） | 实际（=本体，粘死端无延长） | 附着以"两点重合"结算 |
| 附着目标（本线为 leader 的 toPoint） | **实际** | 连着的线跟随延长后的端点 |
| 桥 pin / 省道参考点 | **实际** | 所见即所得（桥/省道线段本身不允许延长，但其参考可来自延长线） |
| 画布绘制（直线端点） | **实际** | 用户看到的是延长后的线 |
| 画布绘制（曲线） | 本体曲线 + 直线尾巴 | 曲线本体不变，尾巴补画 |
| 命中 / 捕捉（端点、段身） | **实际** | 尾巴能摸到（D7） |
| 两点测量、长度标注 | **实际** | D6 |
| 交叉点（Intersection 宿主段几何） | **实际** | D7b |
| 中点 / OnSegment / 极坐标端点 / 终点指向 / 旋转复制几何 | **实际** | 它们引用"点" |
| 打断的本体长度 / 辅助点分派 / 公式切分 | **本体** | D8 |
| 出方向 / 角度基准 | **本体方向**（延长不改变方向；同块角点已禁，无斜拉情形） | 方向稳定 |

### 4.2 求解顺序与存储（关键不变式，实现定稿 2026-?）

**存储定稿（Option X）**：`resolvedPos` 保持 = 本体（参数化真值，一律不改）；
Block 在 `resolve()` 末尾新增 `applyEffectivePositions()`，把每个端点"实际位置"
（本体 + 延长量 × 本体出方向）写入 `m_effectiveLocal` 缓存，并缓存各段已求值
延长量（`m_extendEval`）。`worldPos()` 改为读"有效位置"；`effectiveLocalPos()`
供按位置消费方直接读取。

1. 先按现有机制解全部点（resolvedPos = 本体）—— 辅助点/曲线锚点/交点**零改动**；
2. 解曲线缓存（本体锚点，曲线本体不变）；曲线段自身不支持延长（D3）；
3. 末尾 `applyEffectivePositions()`：求值延长公式（数值 mm / 公式 cm 域，防御性
   clamp ≥0）→ 本体位置入缓存 → 叠加延长 → 与实际位置比较，有变化则 `++geometryEpoch`
   （本体不动但尾巴变 → 画布重绘铁律）；
4. 无延长段的块：缓存保持空，消费方回落 `resolvedPos`（同值，热路径零开销）。

**消费方分轨（与 4.1 表一致）**：位置类经 `worldPos()/effectiveLocalPos()` 读到
实际位置（SnapEngine/BlockItem/附着/测量/交点/长度显示已核对）；比例类继续读
`resolvedPos`（本体）—— 辅助点、曲线锚点、方向基准、打断分派等**无需改动**。
交叉点（Intersection）的**宿主段**几何按实际位置读取（D7b，Block.cpp +
Resolver.cpp 两处）。

### 4.3 跨线共点与曲线（D4/D4b/D3 的组合语义）

- 独立线段 A 的端点 P 被延长 → P 的实际位置外移 → 粘在 P 上的独立线 B 作为跟随线，
  其**整块跟随**（位置吸附、角度不变），B 内部参数化零扰动 → B 的辅助点世界位置随块
  平移（这是"拖动跟随"语义，非延长漂移）。
- 曲线被跨线共点带走 = 曲线作为跟随线整块移动（同上），曲线本体无变化 → **曲线无
  新问题**，无需尾巴逻辑。
- 同块角点 → D4b 禁止（避免"角点被推、邻边重算、辅助点落在画外"的不可兼得）。

---

## 5. 数据设计

`cad::param::Segment` 新增 4 字段：

```cpp
double  extendStartMm      = 0.0;  // 起点延长量（mm 数值）
QString extendStartFormula;       // 公式（cm 域；非空覆盖数值）
double  extendEndMm        = 0.0;  // 终点延长量（mm 数值）
QString extendEndFormula;         // 公式（cm 域；非空覆盖数值）
```

约束：

- 数值与公式约定（2026-11 更新）：**卡片界面数值与公式均 cm 域**（用户拍板统一 CM）；内部存储 `extendStartMm/extendEndMm` 仍 mm、公式仍 cm 域——卡片层做 cm↔mm 换算（与其他卡片如 dartOffset 的"数值 mm / 公式 cm"不同，本卡以 2026-11 决策为准）。卡片数值解析为**纯数字**（用户拍板：不解析单位，正常输入不带单位；"5cm" 等带单位文本按公式处理 → 求值失败回退数值字段 → 读数显示 0，属可见行为）。
- 默认 0 / 空 → 旧档、旧行为零变化。
- 延长量随线段复制、保存、撤销；不随旋转/平移改变（是长度型参数）。

### 5.1 非法/置灰判定（卡片端）

| 情况 | 行为 |
|---|---|
| 曲线段 | 整卡片灰（D3） |
| 桥接线 / 省道线 | 整卡片灰（D5 推论） |
| 粘死端（本线自身从点） | 该端置灰（D5） |
| 同块角点（该端被同 Block 其他段共享） | 该端置灰（D4b） |
| 组件暴露端点 | 该端置灰（D9） |
| 负值 / 非数 / 公式非法 | 拒绝输入并提示（D2） |

---

## 6. 交互设计（属性面板"延长卡片"）

- 位置：LinePropertyDialog 属性页新增"延长"卡片（与连接卡片/终点指向卡片同规格）。
- 内容：起点/终点两栏，各含：数值输入（**cm**，2026-11 用户拍板统一 CM；提交时卡内转 mm 存储）、公式输入（cm 域，支持变量引用）、
  当前原长/延长量/实际长 三行只读显示（D6，cm）。
- 编辑提交：写值 → 命令（undo 一步）→ resolve → 画布刷新 → 跟随线联动。
- 灰色态：见 5.1；置灰时显示原因 tooltip。
- 新建辅助点（智能笔点段身）：若落点在本体范围外（尾巴上）→ 拒绝并提示
  "请在本体范围内建立辅助点"（D7）。

---

## 7. 兼容性与联动（评审确认的"必须接上"清单）

### 7.1 必须显式接线的三处（漏 = 改了不动的假 bug）

1. **画布刷新**：延长量只改尾巴不动本体 → 现有 epoch 检测不会触发重绘，必须显式
   刷新通道（geometryEpoch 或 extensionEpoch）。
2. **跟随线重解**：leader 端点实际位置变了 → 以该端为 toPoint 的跟随线必须重新求解。
3. **变量/公式引用扫描**：延长量公式可引用变量/测量 → 变量值或测量值变化时必须把
   引用它的线段纳入受影响集（blockReferences / formulaRefs / MeasurementStore 消费
   检测，与 lengthFormula 同规）。

### 7.2 其他操作联动

| 操作 | 规则 |
|---|---|
| 打断 | 仅本体范围内可切；原起点延长量 → 前段；原终点延长量 → 后段；新断点 = 0；辅助点按本体分派（读 basePos）；不做"本体与尾巴交界处"的特殊切断（分离尾巴 = 卡片清零） |
| 复制 / 旋转复制 | 延长量随副本复制 |
| 保存 / 读档 | 新字段序列化（Optional since v10），旧档缺省 0 兼容 |
| 撤销 / 重做 | 延长量编辑单步撤销，快照含数值 + 公式 |
| 旋转 / 整体移动 | 延长量保持不变（尾巴随线转/移） |
| 图层 / 辅助层 | 无特殊 |
| 删除线段 | 无新增善后（延长量随段删除）；若删除的是延长量引用的变量，走既有变量删除链路 |
| 交叉点 | 宿主段几何按实际位置（D7b）；延长方向 = 本体方向 → 一次收敛 |
| 测量变量 | 读实际位置（D6），value 随延长联动；被 lengthFormula 类消费时正常刷新 |

---

## 8. 实现接线清单（状态：✅ 已实现 / ⬜ 待做）

### 数据层
- [x] `src/parametric/Segment.h`：extendStartMm/extendStartFormula/extendEndMm/extendEndFormula（含注释）。
- [x] `src/document/DocumentSerializer.cpp`：字段成对写入/读取 + 读档 clamp ≥0、缺省 0。
- [x] `src/parametric/Duplicate.cpp`：Block 深拷贝自动携带（`Block clone = *orig`，已核对）。
- [x] `src/document/commands/BlockCommands.h/.cpp`：新增专用 `SetSegmentExtendCommand`
      （数值+公式快照，redo/undo 显式 +epoch + resolveAll）。

### 求解层（Block）—— 存储定稿：resolvedPos=本体, 有效位置入 m_effectiveLocal
- [x] `src/parametric/Block.h/.cpp`：`applyEffectivePositions()`（resolve 末尾）；
      `effectiveLocalPos/segmentBaseLength/segmentEffectiveLength/segmentExtendStart/
      segmentExtendEnd/segmentSnapWithinBase`；实际位置变化 → `++geometryEpoch`；
      无延长段零开销回落（缓存空 = resolvedPos）。
- [x] `resolveInterpolatedPoint/Points`：**无需改**（比例类读本体 resolvedPos）。
- [x] `rebuildCurveCache`：**无需改**（曲线本体不变；本体曲线 = 本体锚点）。
- [x] 交叉点宿主段：Block.cpp Intersection 分支 + Resolver.cpp 交叉 step 改读
      `effectiveLocalPos`（D7b：交点跟实际线走）。
- [x] 循环依赖核对：延长方向依赖本体方向（先行解）→ 无环。

### 消费方（按 4.1 分轨）
- [x] 画布 `BlockItem.cpp`：线段经 `worldPos`（=有效位置）自动正确；长度标注改读
      实际长（D6）；点缓存改读有效位置。
- [x] 曲线尾巴：**无需补画** —— 曲线段自身不支持延长（D3）；曲线端点被跨线共点
      带走 = 整块变换跟随（曲线本体 local 不变）。
- [x] 捕捉/命中 `SnapEngine.cpp`：点/段缓存改读 `effectiveLocalPos`（尾巴可摸）；
      `ToolSmartPen/ToolBreak` 加 `segmentSnapWithinBase` 拦截（尾巴禁建点/禁打断）。
- [x] 附着/桥 pin/省道：`worldPos` 已 = 实际位置 → 自动跟随（核对无需改动）。
- [x] 测量/长度显示：读 `worldPos`（实际）→ 自动正确（D6）。

### 打断层
- [x] `src/tools/ToolBreak.cpp`：本体范围拦截（toast）。
- [x] `src/document/commands/BreakCommands.cpp`：前段断点延长量清零；后段继承
      原终点延长量（buildBackBlock 复制字段）；undo 整块快照自动还原（已核对）。

### UI 层
- [x] `src/tools/SegmentExtendCard.h/.cpp`（新增）+ CMakeLists 源列表。
- [x] `src/tools/LinePropertyDialog.*`：属性页新增"延长"卡片（起/终两栏 + 原长/
      延长量/实际长只读行 + 置灰判定 + tooltip + 负数拒绝 toast）；changed → refreshScene。
- [x] 置灰判定入口：曲线/桥/省道（整卡灰）、粘死端/同块角点/组件暴露端点（单端灰）
      —— SegmentExtendCard::refresh/endDisableReason。

### 刷新与联动
- [x] `src/parametric/ParamDocument.cpp`：extend*Formula 进 formulaRefs `consumedBy`
      + deleteImpactReport linkedFrozen 计数。
- [x] `MeasurementStore.cpp`：extend*Formula 消费测量变量检测（同 lengthFormula 规）。
- [x] 画布刷新：SetSegmentExtendCommand + applyEffectivePositions 均显式 +epoch。

### 测试清单（tests/test_extend.cpp, 新增 target）
- [x] 辅助点不漂移（percent / fromEnd / 公式 percent）。
- [x] 交叉点跟随实际线。
- [x] 跟随线联动（D4：follower 跟到有效端点）。
- [x] 负延长量钳制（D2）。
- [x] 打断归属 + undo（D8）。
- [x] SetSegmentExtendCommand undo/redo（数值+公式）。
- [x] 参数驱动公式（变量变化 → 延长量重算）。
- [x] 序列化 round-trip + 旧档缺省 0。
- [x] 尾巴范围判定（segmentSnapWithinBase）。
- [ ] 卡片置灰/编辑交互：UI 层手工验证（test_dialog_tabs 未覆盖延长卡片）。
- [x] ctest 全量回归：25 个 target 中 23 通过；2 红 = 既有基线且数值一致
      （test_serializer::bridgeAuxPointSnappableAndAttachable +
      test_component::dragComponentLeaderCurveFollowStable 31.4798mm）。

---

## 9. 已知限制（MVP 明确不做）

- 同块角点（折线/封闭轮廓共角）不允许延长（D4b）。
- 尾巴上不允许新建辅助点（D7）。
- 曲线不允许延长（D3）；曲线跟随跨线共点 = 整块移动（4.3）。
- 组件暴露端点所在线段不允许延长（D9）。
- 无"分离尾巴为独立线段"操作（需要时把延长量清零即可）。
- 负延长量（向内收缩）不做（D2；收缩请用长度变量）。
