# 连接（Connection）重构设计

> **⚠️ 本文已废弃（2026-08-31）**：本文按技术视角（AttachEnd / LandMode / ConnMode 枚举）设计，用户反馈方向不对。
> 现行方案见 **`PANEL_REDESIGN_DESIGN.md`**（从操作逻辑出发重排面板）。本文保留仅作历史记录，其中的源码考古结论仍然有效。
>
> 状态：~~待评审~~ 已废弃
> 日期：2026-08-31

## 1. 背景

「连接」在 UI 上是属性页的一个分区，但模型层从未统一——它是 5 套各自独立的机制。加需求时没有复用既有机制，而是外挂零件凑合，导致若干用户可见的缺陷与一处死代码。

本文给出收敛方案。核心主张：**把"两端各一条约束"从两套机制，收敛成一套机制的两个实例。**

## 2. 现状：5 套机制

| # | 机制 | 载体 | 约束内容 | 求解时机 | UI 位置 |
|---|------|------|----------|----------|---------|
| 1 | 起点连接 | `Attachment` | 位置钉点 + 角度跟随 | Step 3 | 连接行 |
| 2 | 终点连接 | `Block::endTarget*` | 只驱动旋转（瞄准） | Step 7 | 终点连接行 |
| 3 | 桥接线 | `Block::isBridge` + 2×`isPin` | 双端纯位置钉 | Step 4/5 | 无独立 UI |
| 4 | 组件连接 | `Attachment::fromComponentId` | 整组刚体跟随 | Step 3 | 无（画布拖拽） |
| 5 | 省道线 | `Block::dart*` | 起点钉 A、终点由 B 算出 | Step 8 | 省道行（挂在连接卡里） |

### 2.1 已确认的事实（含源码引用）

- **`isBridge` 无生产路径**：全仓只有置 `false`（`Duplicate.cpp:201`、`ParamDocumentAttachments.cpp:538`）与读档（`DocumentSerializer.cpp:389`），无任何位置置 `true`。Resolver Step 4/5 桥接求解与 `isPin` 分支（`Resolver.cpp:263/348`）仅服务老档。
- **桥接线有两套矛盾定义**：`Block.h:145-159` 定义为「两个 pin attachment」；`SegmentConnectionCardRefresh.cpp:38` 用 `hasAtt && hasEnd` 判定。
- **终点落点是外挂的**：`ConnectEndCommand::redo`（`BlockCommands.cpp:589`）仅在 `canDrive`（终点为 Polar 约束且 `distanceFormula`/`lengthFormula` 均空）时创建 `MeasureVariable`，写入 `ep->distanceFormula` 与 `s->lengthFormula`；否则静默退化为「仅指向」。
- **拆开终点连接不彻底**：`SegmentConnectionCardConn.cpp:303` 注释明示「测量保留，长度公式不动」。
- **滑轨双入口冲突**：下拉框 `onSlideModeChanged` 与输入框隐式推断 `onSlideOffsetEdited`（`Conn.cpp:423-432`）互相覆盖用户选择。
- **注释与实现不符**：`Attachment.h:164` 称「AlongLeader 忽略 slideAlongMm」，但 `Resolver.cpp:840-848` 两轴均参与定位；「锁定」实际只体现于 `ParamDocumentAttachments.cpp:403-409` 拖拽仅回写自由轴。
- **崩溃 workaround**：`Conn.cpp:454-460` 为规避对话框析构期 Qt 6.11 `assertObjectType` 崩溃，主动不 emit `changed(SlideModeChanged)`。
- **建立连接入口 13 处** `addAttachment()` 调用点，分散于 LineFactory / ConnectGesture / BreakCommands / RotateCopyGesture / DocumentCommands / BlockCommands / SegmentConnectionCardConn。

## 3. 理论基础：自由度收支

一条线段（刚体 + 可伸缩长度）共 4 个自由度：位置 X、Y、旋转、长度。

| 约束 | 消耗 | 余额 |
|------|------|------|
| 起点钉点（X、Y 落在基准点） | −2 | 2 |
| 起点角度跟随（followerAngle） | −1 | 1 |
| 终点落点（X、Y 落在目标点） | −2 | **−1（超定）** |
| 放弃 followerAngle（旋转让给终点） | +1 | **0（可解）** |

**结论：双端连接不是 bug，是超定。** 现有实现靠 `preserveEndTargetRotation`（`Resolver.cpp:796`）让 `endTarget` 覆盖 attachment 的旋转，行为正确，但这条规则从未被显式表达——UI 上起点行的角度控件照常显示，实际已失效。

这条收支表是整个方案的依据：任何设计都必须回答"超定的那 1 个让谁放弃"。本方案选择**放弃起点角度跟随**，与现状一致，但改为显式规则。

## 4. 目标模型

### 4.1 Attachment 长出端点角色

```cpp
enum class AttachEnd { Start = 0, End };  // 旧档缺省 Start，零迁移
enum class LandMode  { Aim, Exact };      // 仅 End 角色：仅指向 / 精确落点
```

`Attachment` 新增三字段：

| 字段 | 用途 |
|------|------|
| `AttachEnd endRole` | 本约束作用于跟随线的起点还是终点 |
| `LandMode landMode` | 仅 End 角色有意义：只朝向 / 精确落点 |
| `QUuid landMeasureId` | 精确落点使用的测量变量 |

**为何保留测量变量而非废除**：`M_xxx` 是一等公民，可被其它公式引用（如 `袖长 = M_abc * 2`）。问题不在它存在，而在归属关系是靠 `ownerBlockId` 隐式推断的。显式持有 `landMeasureId` 后：

- 删连接 → 按 id 删测量（不再靠猜）
- 测量被单独删 → 连接可感知，自动降级为 `Aim` 并提示（不再状态骗人）
- 拆开 → 明确知道要清测量与两个公式（不再拆不干净）

### 4.2 Resolver：删补丁，立明规则

删除 `preserveEndTargetRotation` 参数，改为显式优先级：

> **有 End 连接时，旋转归 End 连接；否则归 Start 连接。**

行为与现状逐位一致，但该规则同时要在 UI 上可见：双端都连上时，起点行角度控件置灰并标注「角度由终点连接接管」。

### 4.3 状态机：5 个开关收敛为一个枚举

现状 4 个布尔/枚举（`isLocked` / `angleOnly` / `angleIndependent` / `slideMode`）的互斥规则散落于 5 处。改为：

```cpp
enum class ConnMode {
    Full,       // 位置钉 + 角度跟随（默认）
    Slide,      // 位置沿一轴 + 角度跟随
    AngleOnly,  // 位置自由 + 角度跟随（「拆开」）
    AngleFree,  // 位置钉 + 角度自管（「独立角」）
    Free,       // 位置自由 + 角度自管（双拆开）
};
```

五值覆盖「位置 × 角度」两个维度的全部合法组合（2×2）加滑轨。收益：**非法状态在类型层面不可表达**——`angleOnly && slideMode != None` 这类矛盾态写不出来。

`isLocked`（焊接）保持独立布尔，它语义正交。但所有状态转移收敛到唯一入口：

```cpp
void setConnectionMode(id, ConnMode newMode);
```

副作用（解焊、清公式、快照滑轨坐标、清测量）集中于此，不再散落。

### 4.4 滑轨：砍掉一个遥控器

模式**只由下拉框决定**；输入框回归本职，只填数值，不再隐式推断模式。

同时修正语义与实现的错位：既然两轴都参与定位，就不该称「锁定」。UI 文案改为「可拖动方向」——模式决定**手拖时哪个方向会动**，两个输入框都标注「可填」。语义与实现对上。

## 5. 实施路线

### M1 止血（不动架构）

| 项 | 改法 |
|---|---|
| 落点静默降级 | `canDrive` 不满足时 toast 明说原因（终点非 Polar / 已有长度公式） |
| 测量被删状态骗人 | 删除测量时回查 owner，连接降级为 Aim 并提示 |
| 拆开不彻底 | 拆终点连接时一并清测量与两个公式 |
| 滑轨双入口 | 删除输入框隐式推断，模式只归下拉框 |
| 崩溃 workaround | 修信号生命周期，恢复 emit |

验收：ctest 保持基线（既有 2 红不变），新增 3-4 个用例覆盖前三项。

### M2 终点连接收敛（核心，风险最高）

1. `Attachment` 加 `endRole` / `landMode` / `landMeasureId`
2. 读档把 `Block::endTarget*` 迁移为 End attachment；旧字段保留反序列化，不再写出
3. Resolver 合并 Step 3/7，删 `preserveEndTargetRotation`
4. UI 两端行共用一套控件

**风险点**：环检测需从 Block 级放宽到端点级。放宽后「A 终点连 B + B 起点连 A」会形成以往不可能出现的环。
**建议**：保守起见先维持 Block 级检测——双端连接（A 起点连 B、A 终点连 C）并不触发它，够用。

验收：老档迁移逐位一致测试 + 双端连接用例。

### M3 清死代码

- 删 `isBridge` / `isPin` / Resolver Step 4/5。新模型下「两端都连上」即桥接线，不需要标志位
- `ConnMode` 枚举替换散落布尔
- 省道线移出连接卡（第三种语义，不应挂在此处）

## 6. 待拍板

1. **测量变量保留还是废除？** 本方案保留并显式化归属。废除更干净，但会破坏「M_xxx 可被其它公式引用」这一既有能力——需确认是否有活档依赖。
2. **M2 环检测保守还是放开？** 保守（Block 级）安全但限制表达；放开（端点级）灵活但需重写检测与收敛保证。
3. **先 M1 还是直冲 M2？** 倾向先 M1：修掉用户可见的错，同时暴露 M2 需考虑的边界。
