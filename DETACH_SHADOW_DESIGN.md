# 拆开影子线段设计（设计文档）

> 状态：✅ **已落地（2026-09-03）**。本文档是「拆开 = 复制隐藏影子线段作为角度基准 + 影子可挂载新线形成跟随链」功能的唯一设计记录。
> 用户拍板项标注【拍板】；实现接线清单见 §10，动代码前逐条核对。
> ⚠️ 本设计**翻案** DECISIONS.md「拆开保留角度」（活引用）与「影子偏转·已删除」两条既有决策，历史先例与差异见 §5；实现落地后必须回写 DECISIONS.md / AGENTS.md 对应条目。
>
> **实现摘要（2026-09-03）**：
> - 模型层：`Block::isShadow`/`shadowMasterBlockId`（Block.h）+ `Block::cloneShadowOf`（冻结克隆，两点 Free 双写 freePos/resolvedPos）+ 门面五操作与三纯构建器（**src/parametric/ParamDocumentShadow.cpp**，新拆分文件）。
> - 命令层：`SetAttachmentAngleOnlyCommand` 影子化四模式（Legacy/FreshDetach②/ReDetach④/ReconnectMaster⑤）+ 新命令 `ShadowMountCommand`/`RemoveShadowCommand`/`ShadowRotateCommand`（AttachmentCommands/BlockCommands）。
> - 交互层：拆开三入口经命令影子化 + 门面影子感知路由；挂载路由三处（ConnectGesture `reattachShadowBased`/ToolSelect `tryReattachOnDragEnd`/面板 onTargetResolved + onStartConnectResolved）；旋转影子通道（ToolRotate `applyShadowAngleDeg`，R8 p3 轴心 origin 回写）。
> - 交互面排除：HitTester / SnapEngine（4 循环）/ ConnectGesture（attachToTarget + collectConfirmCandidates）/ PointRefEdit 名称解析 / CanvasScene 图元 / LayerPanel 列表 / deleteImpactReport（影子不计入十项计数，见 ParamDocumentBlocks.cpp 注释）。
> - 面板：SegmentRefCard「影子角度」行（objectName `shadowAngleEdit`）+「清除影子」（`clearShadowBtn`）+ 方向行影子基准显示本体对应点；LinePropertyDialog 复合读数（经影子/影子）；ContextStrip「· 影子基准」徽标。
> - 序列化：kFormatVersion 2→3 + `migrateV2ToV3`（"shadow-line"，纯直通 + 清理 shadowAnchorRotDeg/noFollowRotate 残留键）+ 块读写两字段（Optional since v3）+ 非法影子降级告警（DocumentSerializer deserialize）。
> - **与设计稿的差异**：①级联实现上「跟随线被删 → 影子失去 Att2 → 影子+Att1 一并清理」（设计稿 §6 只列了本体删除与宿主删除两分支，实现按"无消费者的系统对象不留孤儿"补齐）；⑤ 面板重连支持**影子换宿主**（Att1 原子替换，面板二次重定向需要）；「影子角度」行落在 SegmentRefCard（方向行同区）而非 LinePropertyDialog 顶层。

---

## 0. 一句话模型

拆开 L2 与 L1 的连接时，复制一条**隐藏的影子线段 L1.1**（L1 方向的冻结快照）作为 L2 的角度基准：

1. **去耦合（R1）**：L1 后续的旋转/移动不再影响 L2——影子是快照，不是活引用；
2. **保关系（R2）**：L2 与原 L1 的 90° 夹角**偏移量**原样保留（含公式/变量），只是基准换成影子；
3. **随新宿主（R3）**：L2 挂到新线 L3 时，**影子 L1.1 作为 L3 的 follower 挂上去**，L3 旋转 → L1.1 转 → L2 转（用户口中的「跟随再跟随」/L1 上带着 L2）。

---

## 1. 背景与动机

**现状**（用户 2026-xx 反馈）：拆开（`angleOnly`）后 L2 的角度仍由**活的** L1 基准线驱动——
L1 旋转，L2 跟着旋转（`Resolver::applyAttachment` angleOnly 分支仍取 `refWorld`，Resolver.cpp:843-856）。
这给后续几何操作带来很大干扰；但用户又**想要** L2 对原 L1 的角度联动（90° 关系不能丢）。

**为什么现有机制都不行**：

| 现有机机制 | 行为 | 违反 |
|---|---|---|
| `angleOnly`（拆开保留角度） | 位置释放，角度**活跟随 L1** | R1（L1 牵动 L2） |
| `preserveAngleRefOnReattach`（重连保基准） | 位置挂新线，角度基准**仍活引用 L1** | R1；且 R3 不成立（L3 旋转只带动 L2 平移，不带动旋转——Ref 世界角来自 L1） |
| 自动态重连（反算 followerAngle） | 位置+角度基准都换新线，**反算覆盖 90°** | R2（偏移被重算） |
| `angleRef*` 引用字段 | 只能"读方向"，**不能作为 follower 被挂载** | R3（无法形成链） |

**结论**：需要一个新的**中间实体**——影子。它既继承 L1 的姿态（快照），又能像普通线段一样**被挂载**（成为 follower），从而把"新线旋转"链式传导给 L2。

---

## 2. 术语（全文档统一）

| 术语 | 含义 |
|---|---|
| 本体（L1） | 拆开前的基准线。拆开后与影子无任何耦合（不活引用） |
| 影子（L1.1） | 拆开时复制的隐藏线段：冻结几何（方向快照 + 长度常量），**不可见、不可选中、不可作为用户交互目标**；可被系统挂载到新线 |
| 影子锚点（p2.1） | 影子上克隆自 L1 连接点（p2）的点；L2 的 Att2 钉点 |
| Att2 | L2 → 影子的连接（挂载链第二环）：L2 的偏移量 offset（原 90°，含公式）相对影子度量 |
| Att1 | 影子 → 新宿主（L3）的连接（挂载链第一环）：偏移量 Δ 挂载时反算保向 |
| 偏移量 offset | L2 相对影子的折角（继承自拆开前 followerAngle / 公式）——**属于 L2** |
| Δ | 影子相对 L3 的折角——**属于影子**（挂载时反算 = L1 姿态相对 L3 的差，保证无跳变） |

---

## 3. 需求清单（讨论收敛，全部【拍板】）

| # | 需求 | 语义 |
|---|---|---|
| R1 | 去耦合 | 拆开后 L1 旋转/移动不得影响 L2（影子 = 快照） |
| R2 | 保关系 | L2 与原 L1 的夹角偏移量保留（数值或公式原样），基准 = 影子方向 |
| R3 | 随新宿主 | L2 挂 L3 后，L3 旋转 → L2 跟着转（链式传导，位置+角度） |
| R4 | 影子形态 | 不可见、不可选中、不可捕捉/不可作为用户连接目标；纯系统对象 |
| R5 | 生命周期 | 挂回 L1 删影子（L2 恢复活引用）；L1 被删 → 影子删 → L2 独立；**再拆开 = 回到"连接前状态"**（影子保留，结构复位） |
| R6 | 影子角度可编辑 | 面板「影子角度」输入框；L2 偏移被公式/变量锁定（旋转锁）时，旋转手势转去改影子角度（**不碰公式**） |
| R7 | 长度不联动 | 影子长度 = 拆开时冻结常量（用户拍板：**不需要长度联动**；角度链本就不使用基准线长度，Resolver.cpp:811 弧长模式取的是跟随线自己段长） |
| R8 | 轴心（旋转） | 拆开态旋转影子/改影子角度时，L2 **绕自己的 p3 原地转**（p3 保持、方向变）；挂 L3 态即绕接点转 |

### 3.1 已解决开放项（讨论定稿）

| 开放项 | 结论 |
|---|---|
| 再拆开时影子方向 | **冻结在当前值**（带过挂载期间 L3 转过的量，不跳线）【拍板】 |
| L3 被删（挂载态） | 影子**弹回拆开态**（冻结当前方向），L2 继续跟随影子【拍板】 |
| 清除影子 | 提供显式入口：附加在「基准点拆开（角度独立）」按钮旁；影子在本体删除/挂回 L1 时自动消失【拍板】 |
| `preserveAngleRefOnReattach` | 影子路径**替代为其默认行为**；旧路径代码保留，仅用于旧档/显式需要活引用的场景【拍板】 |

---

## 4. 核心模型：影子 = 隐藏克隆 Block + 标准双连接链（零新数学）

```
挂载态（L2 挂在 L3 上）:
  L3 ──Att1(标准连接, Δ)──> L1.1(影子) ──Att2(标准连接, offset=90°)──> L2

拆开态（未挂载）:
  L1.1(冻结在空间里) ──Att2(angleOnly: 位置释放、角度跟随)──> L2
```

**为什么零新数学**：

- Att2 是**标准连接**：`toBlockId = 影子`、`toPointId = 影子锚点`。Resolver 自动取
  `refWorld = 影子.transform.rotation + 影子.exitDirectionAtPoint(锚点)` —— 影子本身
  就是 toBlock，**不需要 angleRef 字段**。
- Att1 也是**标准连接**：影子是 L3 的 follower。挂载瞬间反算 Δ（`backSolveFollowerAngle`，
  FollowerAngle.h:30-36）保证无跳变；此后 L3 旋转 → 影子随动（标准附着驱动）→ Att2 传导
  → L2 随动。**R3 由"连接链"天然获得**，无需新增 Resolver 逻辑（链式附着现有模型已支持，
  即现行 L2→L1→L3 场景）。
- 影子几何 = L1 的**冻结克隆**：两点 + 一条线段，方向/长度/位置全部固化为普通数值
  （R7：长度不联动；方向作为可编辑参数，见 §7.2）。

## 5. 历史先例与翻案声明（⚠️ 重要）

本设计**不是全新概念**。项目历史上有两个高度相关的被删除机制：

| 时间 | 机制 | 与本次关系 |
|---|---|---|
| 2026-08-27 | `Attachment::baselineOffsetDeg`（影子偏转角，组旋转跨边界保姿态） | 仅命名/思想相同；用途是**组旋转会话**，与拆开无关 |
| 2026-09 | `shadowAnchorRotDeg` 重设计（「影子挂靠连接线」心智模型）+ **冻结角度基准** `angleRefFrozen*` 三字段，**含「拆开 = 复制隐形 L2.1 线段」语义** | **与本次最接近**——即"拆开复制隐形线段"语义在 2026-09 曾被实现过 |
| 2026-09（当月） | 用户拍板**整体删除**上述机制；回归「拆开保留角度 = 活基准驱动」 | 本次设计的**翻案对象** |

**删除原因（可考）**：影子偏转"对用户不可解释，属历史包袱"（ROTATE_REDESIGN_DESIGN.md
头部说明）；冻结基准与「拆开保留角度=活引用」的既有语义冲突；且旧字段式冻结基准
无法表达"影子可挂载"。

**本次与旧案的关键差异**（决定这次是回归而非重蹈）：

1. **形态不同**：旧案 = Attachment 上三个冻结字段（引用式、无真实体）；本次 = **真实隐藏 Block**
   （可挂载、可入序列化、可被面板读数）。真实体是 R3（链式挂载）的**必要条件**——字段只能被"读"，不能被"挂"。
2. **语义不同**：旧案用于组旋转会话内的临时姿态；本次是**拆开/重连的持久生命周期**语义。
3. **用户诉求明确**：本次是用户针对现有"活引用拆开"明确提出的新拍板（R1 去耦合 + R3 随新宿主），
   且带完整生命周期与交互规则。
4. **命名防混淆**：DECISIONS.md 已有已删除的「影子偏转」条目；实现后新条目应命名
   「**拆开影子基准**」以区分，并在旧条目上补"被本设计取代/翻案"的指针。

---

## 6. 状态机（生命周期）

| 状态 | Att2 (L2) | Att1 (影子) | 影子 | 面板读数 |
|---|---|---|---|---|
| ① 原始连接 (L2→L1) | L2→L1，全连接（活引用） | 无 | 无 | 连接到 L1 / 角度基准：自动 |
| ② 拆开 | L2→L1.1，**angleOnly**（位置释放、角度跟随影子） | 无 | 冻结克隆（方向=拆开瞬间 L1 姿态） | 连接到：空（释放）/ 角度基准：L1（影子） |
| ③ 挂 L3 | L2→L1.1，恢复位置钉点 | L1.1→L3，Δ=反算保向 | 随 L3 转（Δ 恒定） | 连接到：L3（经影子）/ 角度基准：L1（影子） |
| ④ 再拆开 (从③) | 回到 ② | 删除 | **冻结当前方向**（不带跳变） | 同 ② |
| ⑤ 挂回 L1（本体） | L2→L1，全连接（**活引用**恢复） | 删除 | **删除** | 同 ① |
| ⑥ L1（本体）被删 | 删除 | 删除 | **删除** | L2 独立（对齐方向清空 → 自由线） |
| ⑦ L3 被删（挂载态） | 回到 ② | 删除（悬挂清理） | **弹回拆开态**（冻结当前方向） | 同 ② |

不变式：

- 每个 L2 至多一条 Att2；每个影子至多一条 Att1（森林不变式，复用现有 `checkAttachment`）。
- 影子只可能被两条连接引用（Att1 / Att2），且都是**系统创建**，用户交互面不可见。
- 影子不存在时 Att2 不可能是影子基准（角基准回退自动态/活引用）。

## 7. 交互设计

### 7.1 拆开入口统一（三路全部改为"建影子 + angleOnly"）

D 键快拆（ToolSelect::quickDetachSelection，ToolSelect.cpp:1155-1190）、
面板「连接点」拆开（`setAttachmentAngleOnly`，ParamDocumentAttachments.cpp:206-220）、
拖跟随线拆散（ToolSelect::endDrag，ToolSelect.cpp:918-947）——三路统一走新的
`ParamDocument::detachWithShadow(attId)` 门面：

1. 克隆当前 toBlock（本体 L1）→ 生成影子块（`isShadow=true` + `masterBlockId=L1`）；
2. Att2 原地换代：`toBlockId/toPointId/toSegmentId` → 影子对应项；`angleOnly=true`；offset 原样保留；
3. 触发 `resolveAll()` + 结构信号；undo 宏包裹（快照命令）。

### 7.2 旋转（R6/R8）

- **offset 是数字**：旋转手势照旧写 `followerAngle`（ToolRotate connected 模式，ToolRotate.cpp:664-679），影子不动；
- **offset 是公式/变量（锁定）**：旋转手势**转写影子角度**（拆开态 = 影子 `transform.rotation`；挂载态 = Att1 的 `followerAngle` Δ），**不碰公式**——这是 R6 的落点。现状旋转锁定的拒绝逻辑（`isAngleLocked`，ToolRotate.cpp:566-575）改为"锁定 → 影子通道"；
- **轴心**：拆开态 L2 **绕 p3 原地转**（需旋转手势同步回写 L2 的 origin 保持 p3 不动——位置虽释放，轴心手感钉在接触点）【拍板】；挂载态绕接点（L3 p5 = 影子 p2.1 = L2 p3）自然成立。

### 7.3 面板

- **「影子角度」输入行**：角度区新增（靠近现有「方向：点1→点2 [独立]」行，因为影子本身就是那个"方向基准"的载体化）。显示 = 相对当前基准的带符号折角（复用 `formatDegValue` 约定）；恒可编辑，不受 offset 公式锁影响；写目标 = 拆开态影子 rotation / 挂载态 Att1 Δ。
- **连接到行复合读数**：挂载态显示 `连接到：L3（经影子）`；拆开态显示 `连接到：空` + `角度基准：L1（影子）`（不暴露不可交互的影子 id 本身）。
- **清除影子**：按钮附加在「基准点拆开」旁，显式删除影子 → L2 变纯自由线。
- ContextStrip 状态徽标同步：出现"影子基准"标记。

### 7.4 挂载路由

ConnectGesture / 拖拽重挂流程检测到"目标线有影子基准"时：

- **挂到 L3（非本体）**：不再走 `reattachAngleOnly`（旧活引用路径），改走新路由：
  Att1 = 影子挂 L3（反算 Δ 保向）+ Att2 恢复位置钉点（`angleOnly=false`、`isLocked=true` 重新焊接）；
- **挂到 L1（本体）**：检测 `toBlockId == masterBlockId` → 删影子 + Att2 转普通活引用（R5 ⑤）。

### 7.5 交互面排除（R4）

影子块必须从以下全部用户交互面排除：选中/悬停（ToolSelect、HitTester）、捕捉（SnapEngine）、
连接目标（ConnectGesture 候选）、图层/块列表（LayerPanel、attributes）、HUD 计数、删除影响报告
（`deleteImpactReport`，影子不计入十项计数；本体删除时影子随删属系统级）、测量引用。
**实现方式**：`Block::isShadow` 贯穿各过滤器（与 `isAuxiliary` 同风格）；画布 BlockItem 遇
`isShadow` 直接不创建/不绘制。

## 8. 存储与序列化

### 8.1 新字段（唯一新增：Block 层）

```cpp
// Block.h
bool isShadow = false;        ///< 影子线段：隐藏、系统级、可被挂载的冻结克隆。
QUuid shadowMasterBlockId;    ///< 影子本体（拆开前的基准线块）；本体删除或挂回本体时影子随之删除。
```

- 影子必为单线段块（两点+一段）；其锚点 = 克隆自旧 `toPointId` 的点，由 Att2 的 `toPointId` 指回（无需额外字段）。
- Att2/Att1 复用现有 Attachment 全字段：**零新增**。
- "影子角度"复用现有量：拆开态 = 影子 `transform.rotation`；挂载态 = Att1 `followerAngle`——**零新增**。

### 8.2 序列化

- `kFormatVersion` 2 → **3**；`FormatMigration::registerStep` 追加 `{2, "shadow-line", &migrateV2ToV3}`。
- `migrateV2ToV3`：纯直通（旧档无影子，零迁移负载）；但须保留 v2 已存在的对
  `shadowAnchorRotDeg/noFollowRotate/angleRefFrozen*` 旧键的**清理步骤**（现位于
  DocumentSerializer.cpp:493 与 FormatMigration.cpp:185-205 的 v11 清理逻辑），防旧档垃圾键回潮。
- DocumentSerializer：块写 `isShadow`/`shadowMasterBlockId`（Optional since v3）；读入校验
  （影子必须单段、master 必须存在，否则降级为普通不可见块并告警）。

### 8.3 Undo/Redo

- `detachWithShadow`、`mountShadowTo`、`releaseShadowToDetached`、`removeShadow` 均为
  **单命令/宏**（快照式，沿用现有 `AddBlockCommand`/`SetAttachmentAngleOnlyCommand`/
  `ReconnectAttachmentCommand` 组合或新增组合命令）；undo 一步回到动作前状态。
- 影子属于全量模型快照（`kUndoStackLimit = 150` 不涉及新问题）。

## 9. 影响面清单（实现前逐项过）

| 层 | 文件 / 位置 | 改动 |
|---|---|---|
| parametric | `ParamDocument.h/.cpp`（分文件） | 新增门面：`detachWithShadow` / `mountShadowTo` / `releaseShadowToDetached` / `removeShadow` / `findShadowOf(masterId)`；`removeBlock` 级联删影子 |
| parametric | `Block.h` | `isShadow` / `shadowMasterBlockId` 字段 |
| parametric | `ParamDocumentAttachments.cpp` | `setAttachmentAngleOnly` 拆开路径接入影子门面（三入口统一） |
| parametric | `deleteImpactReport` | 影子不计入；本体删除级联计入 |
| document | `FormatMigration.h/.cpp`、`DocumentSerializer.cpp` | v2→v3 + 影子字段读写 |
| canvas | `BlockItem` / `CanvasScene` | `isShadow` 不创建图元/不绘制 |
| tools | `ToolSelect`、`HitTester`、`SnapEngine`、`ConnectGesture`、`ToolRotate` | 影子排除过滤 + 挂载路由 + 旋转影子通道 |
| ui | `LinePropertyDialog`、`ContextStrip`、`SegmentConnectionCard(Refresh/Conn)`、`SegmentRefCard` | 影子角度行、复合读数、清除按钮 |
| tests | `test_commands.cpp`（`setAttachmentAngleOnly_keepsFollowAngle` 断言"旋转 leader A +30° → B 跟随"**需翻案**）、`test_select_wkey.cpp`（`quickDetachKeyD`、`angleOnlyEndpointDragReconnects`）、`test_dialog_tabs.cpp`（`reattachPreservesAngleRef`）、`test_serializer.cpp`、`test_context_strip.cpp` | 按新语义重写 + 新增影子用例 |

> ⚠️ **行为翻案自检**：`test_commands.cpp:1423-1431` 现断言"拆开后旋转 leader 30° 跟随线 B
> 跟着转"——新语义下**拆开态不再跟随本体旋转**（R1），该断言必须改为"挂载/重挂态才跟随"。

## 10. 验证方案（设计级验收断言）

1. **R1**：拆开（D 键/面板/拖拆）后旋转 L1 +30°，L2 方向不变；
2. **R2**：拆开前后 L2 偏移量（含公式字符串）原样保留；
3. **R3**：L2 挂 L3 后旋转 L3 +30°，L2 旋转 +30°（位置+角度链式）；
4. **R5**：挂回 L1 → 影子删除 + 活引用；L1 删除 → 影子删除 + L2 独立；再拆开 → 影子保留、结构复位；
5. **R6**：offset 公式锁定时旋转手势改影子角度且公式原样；面板「影子角度」输入恒可编辑；
6. **R7/R4**：影子无图元、不可命中/捕捉/连接；影子长度冻结；
7. 每状态 × 每入口 × undo/redo × 序列化 round-trip（v2→v3 迁移 + 影子字段往返）。

## 11. 实现顺序建议（落地时）

1. 模型层：`Block::isShadow` + 门面 + 级联（先跑引擎级测试）；
2. 序列化：v2→v3 + round-trip 测试；
3. 交互：拆开三入口接入 → 面板影子行/读数 → 旋转影子通道 → 挂载路由；
4. 回归：翻案用例改造 + 新增影子用例 + 全量 ctest（含 `check_layering`）。

---

## 附：与既有决策的关系（实现后回写清单）

| 既有条目 | 处置 |
|---|---|
| DECISIONS.md「拆开保留角度（2026-08 用户拍板）」 | **翻案**：拆开默认改为影子冻结基准；补充"挂回本体=活引用、挂新线=影子链"两层语义 |
| DECISIONS.md「影子偏转·锚点推导（2026-09 用户拍板删除）」 | 补充指针："拆开影子基准（2026-xx）为其翻案回归 + 挂载链扩展；旧字段式方案仍不回滚" |
| AGENTS.md「领域建模决策」索引 | 拆开摘要同步 |
| DOCS_INDEX.md | 本文档登记（状态：📝 设计稿）；落地后改「✅ 已落地」 |
