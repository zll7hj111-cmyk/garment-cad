# 旋转工具 / 角度模块统一设计（诊断 + 方案）

> **状态**：✅ **已落地**（2026-09，用户拍板 D1-D5 后一次性实施 S1-S5，见 §8 落地记录；代码/测试/文档均已同步，全量 ctest 64/64 绿）。
>
> 历史诊断（带 file:line）保留在 §1-§5 作为决策考古；**现行口径以 §8 + `TROUBLESHOOTING.md`「旋转工具 / 角度模块统一 S1-S5」条目为准**。
> **触发**：2026-09 用户 6 点反馈（黄圈反了 / 单选与框选是否同构 / 灰虚线应为世界 0° / 任意中心端点跳变 / 基准角与线段角两套域 / 历史包袱梳理）。
> **关联**：`docs/design/ROTATE_REDESIGN_DESIGN.md`（2026-08 设计稿，D2/D4 只落地了一半）、`TROUBLESHOOTING.md:217-221`、`CONVENTIONS.md:27/:36`。

---

## 1. 结论摘要

| # | 用户点 | 判定 | 根因一句话 |
|---|--------|------|-----------|
| 1 | 黄圈反了 | **真 bug** | gizmo 基准每帧从文档现读，而拖动中文档已被实时改写 ⇒ 「起点」跟着线段走、「活动边」飞在线外 |
| 2 | 单选 vs 框选 | **不等价（9 处差异）** | 两条入口的锚心端来源不同（点击最近端 vs 恒起点），而角度显示绑在锚心端上 |
| 3 | 灰虚线应指世界 0° | **语义错误** | 灰虚线当前 = `refBaseRad`（可变的「基准方向」），在自由线分支它恰好等于线段当前方向 |
| 4 | 任意中心端点跳变 | **违反设计稿的遗留实现** | 自由线写变换时把锚心端点钉到 pivot：`origin = pivot − anchorLocal.rotated(newRot)` |
| 5 | 两套角度域 | **设计上多数是必要的，但有 1 处真不一致** | 「世界方向角」在条带按折角 (−180,180] 显示、在属性卡按世界角 [0,360) 显示 |
| 6 | 历史包袱 | **存在，且是本轮多数问题的共同来源** | ①锚心端同时承担「旋转支点」与「角度基准」两个职责；②gizmo 基准量每帧重算而非起手捕获；③死代码/双写 |

**统一模型（三句话）**

1. **旋转 = 绕任意枢轴的刚体旋转**；只有「锚心正好是连接点」时才是参数化折角写入（跟随线特例）。
2. **gizmo 三元素各有固定语义**：灰虚线 = 世界 0° 射线（恒定）；黄虚线 = 起手姿态（按下瞬间冻结）；黄弧 = 起手姿态 → 当前姿态（活动边贴着线段）。
3. **角度分三个角色**：`WorldDirection`（世界方向，[0,360)）、`FoldPose`（折角，(−180,180]）、`DeltaAmount`（增量，带符号不折叠）；选域规则收口到单一映射函数。

---

## 2. 逐条诊断

### 2.1 点 1：黄圈方向反了（真 bug，已定位到公式）

自由线拖动**实时改写文档**：`src/tools/RotateSession.cpp:257-264` 直接写 `blk->transform.rotation/origin`，随后 `:268-269` `doc->resolveForDrag(...)` + `scene->syncBlockPositions()`。

而 gizmo 基准**每帧从文档现读**：`src/tools/ToolRotate.cpp:795` `.originalWorldRotRad = degToRad(originalWorldRotDeg())` → `src/tools/RotateSession.cpp:620-635` 自由分支读的是 `doc->findBlock(m_blockId)` 当前 `resolvedPos` 差 ⇒ 返回**当前**世界方向，不是起手方向。

于是 `src/tools/RotateDragMath.cpp:106-112` 自由分支：

```
refBaseRad     = normalizeRad(originalWorldRotRad)   // = 当前方向（灰虚线压在线身上）
deltaDeg       = currentAngleDeg - dragAngle0
currentPoseRad = refBaseRad + degToRad(deltaDeg)     // = 当前方向 + Δ（多转一倍）
```

而黄弧 `src/canvas/overlay/TransientOverlay.cpp:318-339` 的 `sweepDeg = radToDeg(normalizeRad(prevPose − refBase))`，**固定边 = 灰虚线（跟着线段走）、活动边 = 黄虚线（飞在线外）** ⇒ 与用户期望完全相反。

**修法**：起手姿态必须一次性冻结。`src/tools/ToolRotate.cpp:623` `m_dragAngle0 = currentAngleDeg();` 在按下瞬间已捕获，直接用它当基准（连接/复制/多选分支同理，见 §3.2）。

### 2.2 点 2：单选线段 vs 框选单根 —— **不等价**

三条入口：
- PATH A 点击单选 `src/tools/ToolRotate.cpp:193-198` → `selectTarget`（`:531-559`，**带 clickWorld**，`:542 setupTarget(m_paramDoc, blockId, clickWorld)`）
- PATH B 框选 `src/tools/ToolRotate.cpp:365-368` → `handleMarqueeRelease`（`:307-335`，`:328-331 adoptSelection(hits); m_multi.setMarqueeSelected(hits.size() > 1);` **无 clickWorld**）
- PATH C 选择工具选中后切 R `src/tools/ToolManager.cpp:88-92`/`:105-109` → `adoptSelection`（`:503-529`，同 PATH B）

**相同**：selection、`m_selectionConfirmed`、phase、gizmo 显隐门槛、拖动公式、提交/撤销命令、吸附门槛、`m_localDir`。

**不同**（自由线、两端无附件时）：

| # | 差异 | 证据 |
|---|------|------|
| ① | 锚心端不同 → 默认支点不同（点击最近端 vs 恒起点） | `src/tools/RotateSession.cpp:57/:68-73` |
| ② | HUD 徽标差 180° | `src/tools/RotateSession.cpp:427` `if (m_anchor.isEnd) deg += 180.0;` + `src/tools/RotateDragMath.cpp:124-125` |
| ③ | ContextStrip 角度字段差 180° | `src/app/ContextStripDisplay.cpp:109-110` |
| ④ | 基准读数差 180° | `src/tools/ToolRotate.cpp:722-723`、`src/app/ContextStripDisplay.cpp:72-73` |
| ⑤ | 基准按钮端点顺序互换 | `src/app/ContextStripDisplay.cpp:284-288` |
| ⑥ | gizmo 枢轴换端 | `src/tools/RotateDragMath.cpp:107-109` + `src/tools/ToolRotate.cpp:807` |
| ⑦ | 组件成员 ≥2 时框选走 MultiRotateSession（Delta 徽标），点击永远单线会话 | `src/tools/MarqueeGesture.cpp:113/:58-70` → `src/tools/ToolRotate.cpp:513-515` |
| ⑧ | 框选只能在 Idle 起框，点击改选无此限制 | `src/tools/ToolRotate.cpp:206-212` |
| ⑨ | 「点另一端换锚心」仅点击路径有 | `src/tools/ToolRotate.cpp:188-191/:139-159` |

端点带 Attachment 时 `isEnd` 由附件决定（`src/tools/RotateSession.cpp:62-67`），两路径等价。

⇒ 差异 ②③④⑤⑥ **全部来自「角度显示绑在锚心端」**；①⑥ 来自「锚心端当默认支点」。两者都是 §3.1/§3.2 要拆开的职责。

### 2.3 点 3：灰虚线应为世界 0°（语义错误）

灰虚线当前沿 `refBaseRad` 绘制（`src/canvas/overlay/TransientOverlay.cpp:284-299`，55px、`st.gizmoBaseColor`）。在自由线分支 `refBaseRad` = 线段当前方向 ⇒ 灰虚线永远压在线身上，既不能当 0° 基准、也不能当弧的起点（弧起点应 = 起手姿态）。用户要求：**灰虚线 = 固定世界 0° 射线**。

### 2.4 点 4：任意中心端点跳变（违反设计稿 D2/D4）

`src/tools/RotateSession.cpp:260-263`：

```
const double anchorOffsetRad = m_anchor.isEnd ? kPi : 0.0;
const double newRot = degToRad(deg) - anchorOffsetRad - m_localDir;
blk->transform.rotation = newRot;
blk->transform.origin  = m_pivot - m_anchorLocal.rotated(newRot);   // ⇒ worldPos(anchor) ≡ m_pivot
```

pivot 不在端点时整块平移把端点搬到 pivot 上 = 可见跳变。影子分支同样式（`:289`）。

失配顺序：`src/tools/ToolRotate.cpp:350` 先 `rebuildAnchorState`（其中 `src/tools/RotateSession.cpp:182` `m_pivot = blk->worldPos(m_anchor.pointId);` 把 pivot 设成端点），随后 `:354` 用任意点击位置覆盖 `m_pivot`，而 `:181 m_anchorLocal = ap->resolvedPos;` 仍是端点局部坐标。

**设计稿早已点名**：`docs/design/ROTATE_REDESIGN_DESIGN.md:65` 「中心点与目标解耦（任意点，不要求在线/在端点上）」、`:114` D2 公式 `origin = pivot + (origin−pivot)·rot(delta)`、`:212` D4「锚点 = 任意点」标注【建议】；`:330` 的设计测试 `singleFreeArbitraryPivot` **在 tests/ 中不存在** ⇒ 从未落地。

连接线不存在此问题（Resolver 焊死）：`src/parametric/ResolverAttachment.cpp:220` `const geo::Vec2 newOrigin = targetWorldPos - rotatedOffset;`。

**修法**（现成先例 `src/tools/MultiRotateSession.cpp:115-117`）：

```
rotation = base.baseTf.rotation + Δ;
origin   = pivot + (base.baseTf.origin − pivot).rotated(Δ);
```

当 pivot = 锚心端点时与现行公式**等价**，故既有 4 处「pivot = 端点」断言（`tests/test_rotate_anchor.cpp:71-73/:135-136`、`tests/test_rotate_pivot_multi.cpp:301`）语义不变。**当前没有任何测试锁定「非端点 pivot 的最终几何」**（`tests/test_rotate_pivot_multi.cpp:195/:267` 只断言 pivot 值）。

配套必须一起改的（否则半改状态更糟）：
- `src/tools/RotateAimSnap.cpp:48` `return pivot + Vec2{cos,sin} * segLen;` —— 假定 pivot = 端点；任意 pivot 下应取「块绕 pivot 旋转后远端的真实世界位置」。
- `src/tools/RotateCopyGesture.cpp:79-81` 副本挂到 pivot 点（任意 pivot 无重合点，需重新定义）。
- `src/tools/RotateSession.cpp:289` 影子分支同公式。
- 显示层 ±180 偏移（见 §2.5）。

### 2.5 点 5：角度域（分域多数必要，1 处真不一致）

**必要**（勿合并）：`ChordLength` 的弦长 ↔ 圆心角反算必须用折角 `2r·sin(θ/2)`（无法区分 θ 与 360−θ），`src/geometry/Angle.h:75-97` 取 `abs`；`tests/test_rotate_angle_domain.cpp:79/:89` 锁定。存储域 `followerAngle` 恒 [0,360)（`src/parametric/FollowerAngle.h:34-56`）。

**真不一致 U1**：同一物理量（世界方向角）两套显示域 —— 条带「基准:」按折角（`src/app/ContextStripDisplay.cpp:71/:73/:80` 全 `normalizeDeg180`），属性卡「= 世界角度 N°」按世界角（`src/ui/SegmentAngleCard.cpp:312-318/:364`）。270° 宿主切向 ⇒ 条带显示 `-90`、属性卡显示 `270`。

**其余不一致**：U2「基准角度」同名三义（`src/app/ContextStrip.cpp:122` = refWorld 世界方向；`src/ui/SegmentShadowBasisCard.cpp:97/:100` = 影子折角；`src/ui/ComponentTab.cpp:197/:199` = 组原始角）；U3 文档自相矛盾（`CONVENTIONS.md:27` 说两个物理量、`:36` 说三个，代码是三分支 Delta/Fold/World）；U4 选域规则散落 4 处（`src/ui/SegmentAngleCard.cpp`、`src/app/ContextStripDisplay.cpp`、`src/tools/RotateDragMath.cpp:86-125`、`src/tools/RotateSession.cpp`）；U5 自由线角度读写域不对称（`src/ui/SegmentAngleCard.cpp:472-473` 写未归一化 vs `:56` 读 `normalizeDeg360`）；U6 状态栏 `baseAngleDeg` 未归一化（`src/tools/RotateHintTexts.cpp:37-41`）。

### 2.6 点 6：历史包袱清单

| 类别 | 位置 | 处置建议 |
|------|------|---------|
| 死代码 | `src/tools/RotateSession.cpp:650-697` `calculateGizmoAngles`（无调用者，其语义正是用户要的「起点固定」） | 删除；语义进新 gizmo |
| 死字段 | `GizmoPoseInput::localDir`（上一轮已删）、`DragSample::localDir` 仅连接态用 | 保留后者，标注 |
| 双职责 | `m_anchor.isEnd` 同时是「支点端」和「角度基准端」 | 拆开：支点 = 任意点；基准端只对连接态有意义 |
| 双写 | 自由线拖动期间直接写 `transform` + `resolveForDrag`，gizmo 又从文档现读 | 起手快照（`m_base.baseTf` + `m_dragAngle0`）作为唯一基准源 |
| 启发式 | 点击最近端翻转锚心（`src/tools/ToolRotate.cpp:349`） | 随 §3.1 收窄到连接态 |
| 半落地 | 设计稿 D2/D4/`singleFreeArbitraryPivot` 未实现 | §4 阶段 2 |
| 命名 | 「基准角度」三义 | §4 阶段 4（改名 + 收口） |

---

## 3. 统一模型

### 3.1 M1 旋转 = 刚体绕任意枢轴（+ 连接线特例）

| 情形 | 写入 | 说明 |
|------|------|------|
| 自由线（锚心无附件） | `rotation = base.rotation + Δ`；`origin = pivot + (base.origin − pivot)·rot(Δ)` | 刚体；pivot 任意，不吸附端点 |
| 连接线 + 锚心 = 附着点 | 参数化写 `followerAngle`（现行 `writeFollowerAngleForMode`） | 折角域；位置由 Resolver 决定 |
| 连接线 + 锚心 ≠ 附着点 | 释放附件（`RemoveAttachmentCommand`）后按刚体 | 既有语义「旋转 = 放弃跟随」 |
| 多选 / 组件 | 现行 `MultiRotateSession` 刚体公式 | 已正确 |

Δ = `targetDeg − dragAngle0`（两者同域，±180 约定自动抵消）。

### 3.2 M2 gizmo 三元素（固定语义）

| 元素 | 语义 | 实现 |
|------|------|------|
| 灰虚线 | 世界 0° 射线 | 恒定，不依赖任何输入（`showRotateGizmo` 内部固定 0） |
| 黄虚线 | 起手姿态（按下瞬间冻结） | 自由/连接/复制/多选统一用「按下时捕获的基准角」 |
| 黄弧 | 起手姿态 → 当前姿态 | 活动边落在当前线段方向上 |
| 徽标 | 随物理量选域（Delta / Fold / World） | 现行 `formatRotationBadge` |

基准角捕获点：`src/tools/ToolRotate.cpp:623` `m_dragAngle0`（自由/连接）；`m_dragCursorAngle0`（多选）；`originalWorldRotRad` 改为**起手快照**而非现读。

### 3.3 M3 角度域三角色 + 单一映射

```
enum class AngleDisplayRole { WorldDirection, FoldPose, DeltaAmount };
double toDisplay(double deg, AngleDisplayRole role);   // World→[0,360) Fold→(−180,180] Delta→带符号
```

所有显示/输入口（条带、属性卡、影子卡、HUD 徽标、状态栏）只调这一处，禁止各自 `normalizeDeg*`。`ChordLength` 反算仍走折角（数学必需，单独标注）。

---

## 4. 分阶段落地（每阶段可独立编译 + 按影响面跑测）

| 阶段 | 内容 | 主要文件 | 测试影响 |
|------|------|---------|---------|
| **S1 显示层** | 灰虚线 → 世界 0°；黄虚线 → 起手姿态；黄弧 → 起手→当前；基准角改为起手快照 | `src/tools/RotateDragMath.cpp`、`src/canvas/overlay/TransientOverlay.cpp`、`src/tools/ToolRotate.cpp` | `test_transient_overlay`、`test_rotate_copy_flow`（gizmo 断言） |
| **S2 旋转模型** | 自由线刚体化（任意 pivot）；影子分支同步；`RotateAimSnap` 目标点改由块变换推；复制手势挂点重新定义 | `src/tools/RotateSession.cpp`、`src/tools/RotateAimSnap.cpp`、`src/tools/RotateCopyGesture.cpp` | 新增「非端点 pivot 刚体」断言；既有 4 处 pin 断言不变 |
| **S3 入口统一** | 单选/框选同构：锚心启发式收窄到连接态；默认支点一致；`isMarqueeSelected` 分支与单选对齐 | `src/tools/ToolRotate.cpp`、`src/tools/RotateSession.cpp` | `test_rotate_anchor`（换锚心用例）、`test_rotate_pivot_multi` |
| **S4 角度域** | `AngleDisplayRole` 收口；世界方向统一 [0,360)；命名消歧（基准方向/基准折角/组原始角）；U5/U6 修正 | `src/ui/SegmentAngleCard.cpp`、`src/app/ContextStripDisplay.cpp`、`src/tools/RotateHintTexts.cpp`、新增头 | `test_rotate_angle_domain:130-131`（防合并断言**保留**）、`test_context_strip:1242/:1271`（断言需改） |
| **S5 清债** | 删死代码 `calculateGizmoAngles`；文档同步（`TROUBLESHOOTING.md`/`CONVENTIONS.md:27`/`ROTATE_REDESIGN_DESIGN.md` 状态列） | — | 全量 ctest 1 次 |

**每阶段收尾**：`tools\build.bat WildWindPattern` → `ctest -C RelWithDebInfo -R "test_rotate|test_transient_overlay|test_context_strip|test_segment_angle"`；全部完成后全量 64 用例 1 次。

---

## 5. 风险与约束

- **undo 安全**：`RotateBlockCommand` 只快照 transform(+endTarget+释放附件)，刚体旋转不引入新状态；但 `src/document/commands/BlockTransformCommands.cpp:124-148` redo/undo **未** `touchGeometry()`（`RotateBlocksCommand` `:191/:207` 有）—— S2 需补。
- **附件驱动**：`src/parametric/ResolverAttachment.cpp:220` 会把位置焊回，刚体只对自由线成立。
- **几何 epoch**：只能走 `resolveForDrag` + `syncBlockPositions`（`src/tools/RotateSession.cpp:268-269`），epoch 唯一入口 `touchGeometry()`。
- **吸附来源**：现行只吸附线段端点（`src/tools/RotateInputTracker.cpp:30/:37`，12px），无线身/中点；如需任意中心更好用，可接 `src/tools/SnapEngine.h:116 findSegmentSnap`（S2 可选）。
- **分层**：`src/tools` 不得引入 QWidget（`tools/check_layering.py` 把关）。

---

## 6. 待拍板决策

| # | 决策 | 选项 | 建议 |
|---|------|------|------|
| D1 | 自由线角度显示是否去掉「锚心端 ±180」 | A 去掉（世界角恒定 start→end）／B 保留 | **A**：这是点 2 的 5 处差异根因，且与 §3.3 一致 |
| D2 | 「换锚心」（X 键 / 点端点）在自由线下的新语义 | A 变成「把枢轴移到另一端」／B 保留旧语义／C 直接取消 | **A**：保留快捷操作、语义变清晰 |
| D3 | 世界方向角显示域 | A 统一 [0,360)／B 统一 (−180,180]／C 维持现状 | **A**（点 5 的直接诉求） |
| D4 | 落地节奏 | A 按 S1→S4 分阶段（每阶段可验收）／B 一次性整体改 | **A**（用户实际选 **B**：一次性整体改 S1-S5） |

**拍板结果（2026-09 用户全选推荐项，D4 除外）**：D1 = A（去掉自由线锚心 ±180）；D2 = A（X 键/点端点 = 把枢轴移到另一端）；D3 = A（世界方向统一 [0,360)）；**D4 = B（一次性整体改 S1-S5，非分阶段）**；D5 = A（灰虚线世界 0° 仅旋转 gizmo 生效）。
| D5 | 灰虚线世界 0° 是否只在旋转工具生效 | A 仅旋转 gizmo／B 顺带加画布坐标系参考线 | **A** |

---

## 7. 明确不做

- 不合并「折角 / 世界方向 / 增量」三个物理量（`ChordLength` 数学必需；`tests/test_rotate_angle_domain.cpp:130-131` 有防合并断言）。
- 不改连接线的 Resolver 焊点语义（`TROUBLESHOOTING.md:221` 2026-08 用户拍板）。
- 不改存储域 `followerAngle` ∈ [0,360) 与序列化原样（`src/document/DocumentSerializer.cpp:439/:472`）。

---

## 8. 落地记录（2026-09，一次性 S1-S5）

**几何 / gizmo（M1/M2）**
- `src/tools/RotateSession.cpp:264-268` 自由线 `applyAngleDeg` 改刚体：`baseWorldDeg = radToDeg(m_base.baseTf.rotation) + radToDeg((ep->resolvedPos − sp->resolvedPos).angle())`；`deltaRad = degToRad(deg − baseWorldDeg)`；`blk->transform.rotation = m_base.baseTf.rotation + deltaRad`；`blk->transform.origin = m_pivot + (m_base.baseTf.origin − m_pivot).rotated(deltaRad)`（pivot = 端点时与旧 pin 式等价 ⇒ `tests/test_rotate_anchor.cpp`/`test_rotate_pivot_multi.cpp` 的 pin 断言不变）。
- `src/tools/RotateSession.cpp`：`currentAngleDeg` 自由分支删 `if (m_anchor.isEnd) deg += 180.0;`；`originalWorldRotDeg` 自由分支改读快照；`anchorTag` 枢轴离锚心端点 > `kGeomEpsLoose` 返回「自由点」；新增 `pivotOnEndpoint()`（`.h:114`）。
- `src/tools/RotateSession.h/.cpp`：`setupTarget(ParamDocument*, const QUuid& blockId)` 删 clickWorld 形参 + 删「点击最近端翻锚心」启发式；删死代码 `GizmoAngles`/`calculateGizmoAngles`。
- `src/tools/ToolRotate.h:136-138` / `.cpp`：`selectTarget(const QUuid&)` 单参；`baseAngleDeg()` 两分支 `normalizeDeg360`；`m_dragAngle0 = currentAngleDeg()` 作为起手姿态；`updateStatusHint` 写 `.poseRole`（:779-781）；Ctrl 复制加 `pivotOnEndpoint` 守卫。
- `src/tools/RotateDragMath.h/.cpp`：`GizmoPose::refBaseRad` → `startPoseRad`；`computeGizmoPose` 四分支重写（multi 不变；copy 删 `isAnchorEnd` +π、refBase = `normalizeRad(originalWorldRotRad)`；connected `poseRad(α) = refWorldRad + π − degToRad(α) + anchorFlip`，`isRotating` 时 `refBase = poseRad(dragAngle0)`；free `refBase = isRotating ? degToRad(dragAngle0) : curRad`）。**S5 清债**：删 `GizmoPose::deltaDeg`（multi 徽标改用 `in.accumulatedAngleDeg`）与死字段 `GizmoPoseInput::baseAngleDeg`（构造点 `src/tools/ToolRotate.cpp:810`、`tests/test_rotate_copy_flow.cpp:341` 同步删）。
- `src/canvas/overlay/TransientOverlay.h/.cpp`：`showRotateGizmo(pivotWorld, startPoseWorldRad, currentPoseWorldRad, badgeText = {})` —— 删 `deltaDeg` 形参；灰虚线用 `kWorldZeroRad = 0.0`（`:286`）；弧 sweep = `normalizeRad(current − start)`；徽标锚点用 `currentPoseWorldRad`。
- `src/tools/RotateGizmo.h/.cpp`：成员改 `m_startPoseRad`/`m_currentPoseRad`；删 legacy 3 参 build 与 `refBaseRad()/prevPoseRad()/refWorldRad()`；**S5 清债**：删 `m_deltaDeg`/`deltaDeg()`，`update(zoom, startPoseRad, currentPoseRad, badgeText)` 不再收 `deltaDeg`，`isArcEmpty()` 改由 `normalizeRad(currentPoseRad − startPoseRad) ≤ 1e-4` 判定（旧实现读 `m_deltaDeg`，而该值在连接分支是 `dragAngle0 − currentAngleDeg`、自由分支是 `currentAngleDeg − dragAngle0`，符号相反 —— 只有量值被用到才侥幸正确）。
- `src/tools/RotateAimSnap.cpp`：新增匿名 `FreeAimTip freeAimTip(...)`（瞄准端 = 离枢轴最远端点，`dirOffsetRad = (tip.pos − pivot).angle() − segments.front()` 世界向），`endpointAtAngle`/`checkSnap` 自由分支刚体化；几何未解析时 `if (isFree && !freeTip.valid) { clear(); return; }`。
- `src/tools/RotateCopyGesture.cpp`：`begin()`/`convert()` 加 `if (!m_session.pivotOnEndpoint(o.m_paramDoc)) return;`。

**角度显示（M3）**
- `src/geometry/Angle.h:65-77`：新增 `enum class AngleDisplayRole { WorldDirection, FoldPose, DeltaAmount }` + `toDisplayDeg()`。
- `src/tools/RotateHintTexts.h/.cpp`：快照加 `poseRole`，`buildStatusHint` 走 `toDisplayDeg(..., WorldDirection)` / `toDisplayDeg(..., snap.poseRole)`；保留「锚心」措辞（`tests/test_rotate_anchor.cpp:271` 断言）。
- `src/app/ContextStripDisplay.cpp:86/:113`：基准角度框与角度框统一 WorldDirection（删本地 normalizeDeg180 与 `worldDeg += 180.0`）；基准按钮端点顺序（anchorIsEnd 时 ep→sp）**保留**（「哪端是枢轴」的可见提示）。
- `src/ui/SegmentAngleCard.cpp:472-473` **故意回退**：`st.endAngle = targetDeg − rotDeg;` 不包 `normalizeDeg360`（本地极角带符号等价；且 `normalizeDeg360(−0)` 会得 360）。

**连带**
- `src/document/commands/BlockTransformCommands.cpp:132/:147`：`RotateBlockCommand::redo/undo` 补 `b->touchGeometry();`（与 `RotateBlocksCommand` :195/:211 对齐）。

**测试**
- 改：`tests/test_transient_overlay.cpp`（4 处 4 参调用）、`tests/test_rotate_d15_gate.cpp`（slot 改名 `d15FreeAnchorIsAlwaysStart`，自由线锚心恒 `startId`）、`tests/test_context_strip.cpp`（基准角度框 −90→270；`rotateAnchorStateFlipsBasisAndRoutesReverse` 角度字段 180→0）、`tests/test_rotate_anchor.cpp`（`anchorSwitchSyncsStrip` 180→0）、`tests/test_rotate_copy_flow.cpp`（`gizmoDisplayConsistencyAcrossModes` 用 `startPoseRad()`/`currentPoseRad()`/`isArcEmpty()`；`gizmoConnectedAnchorPointsAtBody` 重写为 `GizmoProbe{startOff, currentOff, sweep}` 姿态差口径：静息三者 ≈ 0，拖动 `startOff` 冻结 0、`currentOff`/`sweep` = −30°，两条锚心一致）。
- 不变：`tests/test_rotate_angle_domain.cpp`（写域/读域 + 270/90 弦长防合并）、`test_rotate_pivot_multi.cpp`（pivot 值）、`test_ortho_offset.cpp`。

**验证**
- 构建：`WildWindPattern` + 12 个测试 target 全绿。
- `ctest -C RelWithDebInfo -R "test_rotate|test_transient_overlay|test_context_strip|test_ortho|test_tool_hints|test_attachment_angle|test_reverse_segment|test_triangle_unfold"` → **16/16 绿**（首轮唯一红 `test_rotate_copy_flow:356` 是测试自身断言口径错，已改）。
- **全量 `ctest -C RelWithDebInfo`（完工前 1 次）→ 64/64 绿**（含 S5 清债后的最终状态）；`check_layering` / `check_file_size` / `check_inline_units` / `check_bool_flags` / `check_header_classification` 全部 OK。

**踩坑（复用价值）**- `src/tools/RotateAimSnap.cpp` 的类型名是 `cad::param::ParamPoint`，不是 `cad::param::Point`（否则 C2039 + C4430/C2143/C2065）。
- `Angle.h` 的 `normalizeDeg360` 对极小负值（如 −1e-13）得 ≈360，不可用于可能取 −0 的写入侧。
- 连接线姿态角内含 `anchorFlip(π)` 才贴线体；跨度 = `normalizeRad(currentPose − startPose)` 与折角增量口径等价（`poseRad` 对 α 线性）。
- 拖动期「黄虚线仍须指向线体」是**旧口径**；M2 后拖动期黄虚线冻结在起手姿态，只有黄弧活动边跟线体走 —— 写断言时别把 `startPoseRad` 和 `currentPoseRad` 混用。
