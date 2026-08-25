# 组件整组旋转 设计文档 ROTATE_COMPONENT_DESIGN.md（用户拍板 2026-12）

> 状态：设计定稿，已实现。本文档是"组件整组旋转"唯一权威设计记录。
> 用户拍板项均标注【拍板】；实现接线清单见文末，动代码前逐条核对。

---

## 1. 背景与目标

旋转工具现状（单线语义）：

- 目标 = 一根线（ToolRotate 只认单个 Block）；
- 锚心 = 该线端点（默认起点，X 键/点击端点切换）；连接线锚心 = 附着点；
- 旋转语义 = 自由线绕锚心刚体转（= 改方向角）、连接线改跟随角。

**用户痛点迭代史（2026-12 多轮探讨，最终拍板）**：

1. 最初提案"两个角度参数 + 无主锚"（A→B 315° / B→A 135° 双视角、主锚自适应）——探讨后削掉：直线段两视角恒差 180°，是同一参数的别名，无法做成两个独立自由度；
2. 真正痛点 = **组件**：组件/多选组没有"整组绕点旋转"入口，旋转工具命中组件内一根线时只转该线（连接线 = 只调它的角度），整组不动；
3. 最终方案：旋转工具加 **W 键**（仅组件可用）切换**整组旋转模式**——整个组件绕**任意锚点**刚体旋转；锚点数据**不存储**（操作参数，非对象属性）。

### 需求要点

- **默认单线语义不变**：选中组件里的线、不按 W，还是现在的行为（自由线转角度 / 连接线调跟随角）；
- **W 后整组转**：目标 = 整组件（所有成员块施加同一刚体变换，绕任意锚点）；
- **锚点 = 任意点**：画布上任何可捕捉点（主动吸附）或任意自由位置；点别的线/组件 = 只是锚点所在地，目标不变；
- **无新增持久化**：锚点不存进文档；旋转结果 = 成员块 transform（本来就在块上）；undo 命令快照前后状态。

---

## 2. 已拍板决策清单

| # | 决策 | 类型 |
|---|---|---|
| D1 | 旋转工具新增 W 键切换：**默认单线模式**，按 W 进入**整组旋转模式**，再按 W 回单线；**仅当目标线属于某组件**时 W 生效（普通独立线 W 无效果 + toast 提示） | 【拍板】 |
| D2 | 整组旋转模式：目标 = 该组件全部成员；**所有成员施加同一刚体变换**（绕锚点旋转 delta）：rotation += delta、origin = pivot + (origin − pivot) 旋转 delta | 【拍板】 |
| D3 | 锚点 = **任意点**：第一击（点击）设定锚点——靠近可捕捉点（8px/zoom 容差）则吸附到点，否则用光标位置（自由点）；点别的线/空白均可 | 【拍板】 |
| D4 | 交互两段式（主流 CAD ROTATE 同构）：**第一击 = 设锚点**（显示锚心环）；**第二击按下 + 拖动 = 旋转**（增量角，Shift 15° 吸附），**松手 = 完成**（一步 undo），**Esc = 取消回位** | 设计定稿 |
| D5 | HUD：caption“整组旋转”；数值 = **绕锚点转了多少度**（增量角，带符号，0° = 原始位置） | 【拍板】 |
| D6 | **防呆高亮**：整组旋转模式激活时画布显示**组件虚线包围盒**（成员几何并集 + 5px 外扩，随旋转逐帧更新），确认“转的是整组” | 设计定稿 |
| D7 | **外部约束释放**（旋转开始前快照 + 释放，结束 restore-then-replay，一步 undo 恢复）：组件级 attachment、成员线→**组外**线的 attachment（含 pin）、成员**指向组外点**的 endTarget、**引用组外点**的省道线（降级，快照全字段）；**组内**的保持（整组刚体旋转下相对关系自洽） | 【拍板】 |
| D8 | 锚点**不存储**（操作参数）；旋转后**defaultAngleDeg（原始角）不变**（回正仍回原始角） | 【拍板】 |
| D9 | Ctrl 旋转复制在整组模式不启用（本次范围外） | 设计定稿 |
| D10 | 多段块成员：整组旋转按成员块刚体变换，**不区分段**（与组件机制一致） | 设计定稿 |

---

## 3. 交互流程（ToolRotate）

    选中组件内某线（press/click，单线语义默认; 状态 Ready）
      └─ 按 W ──> 进入整组旋转模式:
           · 组件虚线框高亮出现
           · 锚心环隐藏（锚心概念不适用）
           · HUD caption「整组旋转」，值 0
           · 锚点未设：等待第一击
      ── 第一击（点击任意处）──> 锚点设定:
           · 吸附: 8px/zoom 内最近 resolved 点 → 吸附到点（记 m_groupPivotPointId）
           · 否则: 自由位置（m_groupPivotPointId = null）
           · 锚心环出现在锚点
      ── 第二击按下 + 拖动 ──> Rotating:
           · delta = atan2(光标−锚点) − atan2(按下点−锚点)（归一化 (−π,π]）
           · Shift: 15° 吸附
           · 全体成员 transform 更新 + resolveForDrag(memberIds) + syncBlockPositions
           · HUD 数值 = 带符号 delta；黄弧 = 0 → delta（基准 = 拖起始方向）
      ── 松手 ──> 提交（restore-then-replay → RotateComponentCommand，一步 undo）
      ── Esc（拖动中）──> 取消：恢复释放的约束 + 全体 transform 回位
      ── Esc（Ready/已设锚）──> 清锚点重选（仍在整组模式；锚点“点错了”不必退出）
      ── Esc（Ready/未设锚）──> 退出整组旋转模式（回单线，不丢目标）
      ── 右键 ──> 取消旋转 + 退出整组模式 + 取消目标（现状语义）

关键点：

- 整组模式下**一切按压 = 锚点/旋转**，**不再切换目标**（点别的线只是锚点——主流 CAD 语义，用户拍板）；
- 单线模式的一切行为（X 锚心切换 / Ctrl 旋转复制 / 已连接禁止切锚）在整组模式**禁用**；
- 目标切换：右键取消 → 点另一组件线 → W。

---

## 4. 模型与命令

### 4.1 无新增持久化字段

- 锚点 = 会话态（ToolRotate 成员 m_groupPivot / m_groupPivotPointId），不进文档、不进文档序列化；
- 旋转结果 = 成员块 transform（已有字段）；
- 组件 record（Component）零改动。

### 4.2 ParamDocument 反查助手

    const Component* findComponentByMember(const QUuid& blockId) const;  // nullptr = none

### 4.3 RotateComponentCommand（components 命令族）

快照成员 transform 前后 + 释放的外部约束（restore-then-replay，同 RotateBlockCommand 范式）：

    struct AimRelease { QUuid blockId; QUuid endTargetBlockId; QUuid endTargetPointId;
                        double endTargetOffset; QString endTargetOffsetFormula; };
    struct DartRelease { QUuid blockId; QUuid dartStartBlockId; QUuid dartStartPointId;
                         QUuid dartRefBlockId; QUuid dartRefPointId; QUuid dartRefSegmentId;
                         double dartOffsetMm; QString dartOffsetFormula;
                         double dartAngleDeg; QString dartAngleFormula; };

    class RotateComponentCommand : public QUndoCommand {
        // oldTf/newTf: member transform 前后
        // releasedAtts: 被释放的外部 attachment（含组件级）快照（redo 删 / undo 原样 addAttachmentsRaw）
        // releasedTargets / releasedDarts: endTarget / 省道释放
    };

- redo：全体成员写入 newTf → removeAttachments(released ids) → 清外部 endTarget 字段 → 清外部省道引用（降级普通线）→ resolveAll()；
- undo：全体成员写入 oldTf → addAttachmentsRaw → 恢复 endTarget/dart 全字段 → resolveAll()；
- 组件级 attachment 释放走 removeAttachments（批处理，不清 exposedPointId）。

### 4.4 释放判定（D7，工具层 gesture 开始前一次性收集）

对组件 C（成员集 S）：

| 特征 | 判定 | 处理 |
|---|---|---|
| att.fromComponentId == C | 组件级整体跟随 | 释放 |
| att.fromBlockId ∈ S && att.toBlockId ∉ S | 成员线对组外 leader（含 pin） | 释放 |
| att.fromBlockId ∈ S && att.toBlockId ∈ S | 组内连接 | 保持 |
| 成员块 endTargetBlockId ∉ S（且非空） | 指向组外点 | 释放（清字段） |
| 成员块 endTargetBlockId ∈ S | 组内指向 | 保持 |
| 成员块 isDart() && (start/ref ∉ S) | 省道引用组外 | 释放（清 start/ref 降级，快照全字段）；组内省道保持 |
| 组外 follower（fromBlockId ∉ S 附着在 S 上） | 跟随者 | 保持（随 resolveForDrag 跟随） |

---

## 5. 测试清单（tests/test_rotate_copy.cpp 追加）

| 用例 | 断言 |
|---|---|
| wTogglesGroupModeOnlyForComponent | 独立线 W 无效（toast）；组件成员线 W → groupMode=true；再 W → false |
| groupRotateAboutFreePivotRotatesAllMembers | 两成员自由线绕自由锚点拖动：全体成员同一 delta、origin 精确按刚体公式；一步 undo 回位 |
| groupRotateAboutSnappedPivot | 锚点吸附成员端点：绕自身端点转 90°，该端点世界位置不变；组外线不受影响 |
| groupRotateKeepsInternalAttachment | 组内 A→B 跟随：整组转后 attachment 仍在、相对几何不变；undo/redo 后仍在 |
| groupRotateReleasesExternalConstraints | 成员对组外 leader 的 attachment + 指向组外点的 endTarget：旋转后消失；一步 undo 全部恢复 |
| escCancelsGroupRotate | Esc 后 transform 回位 + 释放约束恢复 + undo 栈无命令 |

> 注：拖动终点经视口像素取整 (< 0.5px ≈ 0.2°@50mm 半径)，角度断言用全体
> 成员同 delta + 刚体公式精确成立 + 大小/方向区间（±1°）三条替代单值断言。

---

## 6. 实现接线清单（动代码前逐条核对）

1. src/parametric/ParamDocument.h/.cpp（ParamDocumentBlocks.cpp）：findComponentByMember；
2. src/document/commands/ComponentCommands.h/.cpp：RotateComponentCommand + AimRelease/DartRelease（gcad_document 源列表已含该文件）；
3. src/tools/ToolRotate.h/.cpp：整组模式状态 + W 键 + 两段式手势 + HUD/gizmo 分支 + 释放收集与恢复；
4. 每帧热路径：resolveForDrag(memberIds) + syncBlockPositions()（沿用 applyAngleDeg 铁律）；
5. tests/test_rotate_copy.cpp：上表用例；
6. TROUBLESHOOTING.md 第 0 组快捷键登记表：新增 W 行；
7. AGENTS.md 领域建模决策：新增条目（条目需带源码引用）。

---

## 7. 后续（非本次范围，用户意向记录）

- 整组旋转复制（Ctrl 语义扩展到组件）——待议；
- 旋转后“把当前姿态设为默认角”（defaultAngleDeg 更新）——拍板：手动旋转不改原始角，回正仍回原始角；
- 任意基点在单线模式也开放（本次仅组件整组模式开放任意点）。