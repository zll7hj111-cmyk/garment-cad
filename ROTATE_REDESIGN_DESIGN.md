# 旋转工具交互重设计 ROTATE_REDESIGN_DESIGN.md（2026-13 提案 · 待拍板）

> 状态：**设计提案**（对齐 ETCAD 交互 + 保持参数化）。所有【建议】项需用户拍板；
> 拍板后本文档成为"旋转工具重设计"唯一权威设计记录（参照 COMPONENT_ROTATE_DESIGN.md 先例）。
> 实现接线清单见文末 §9，动代码前逐条核对。

---

## 0. 调研摘要（联网，2026-13）

| 产品 | 交互流程 | 对我们的启示 |
|---|---|---|
| **ET（ETCAD）** | 旋转工具内：框选（或单选）对象 → **右键确定** → 点画布/已有点设中心点（空白 = 临时锚点）→ **长按拖动旋转**；可一次性框选大量互不连接的线段 | 主流程模板：*选 → 右键确认 → 锚 → 拖动*；锚点临时（会话态）、可自由可吸附 |
| **AutoCAD ROTATE** | 选对象 → 指定基点 → 指定旋转角（输入或拖动预览；C=复制；R=参照角） | 基点是"第一击"，拖动跟随光标；复制是可选修饰键 |
| **Rhino Rotate** | 选对象 → 旋转中心 → 角度（可指定第一参考点；附带复制选项） | 同上；中心点吸附很重要 |
| **ideCAD / DraftSight QuickModify** | 选对象 → 基点 → 角度；QuickModify 把移动/旋转/复制合并进一次拖动手势 | 动作尽量一体：选择集 + 基点 + 变换三件事一次完成 |
| **CLO 3D** | 纸样 Gizmo 直接移动/旋转（借组件边界框）；Transform Point/Segment 提供数值精确变换 | 我们已有 RotateGizmo/角度 HUD，旋转是"手势 + 精确输入"双通道 |

**通用结论（主流 CAD 一致）**：旋转一律是 **"目标集 → 中心点 → 变换"** 三段，且 **中心点与目标解耦**（任意点，不要求在线/在端点上）；目标集支持 **批量、无连接关系**；拖动过程中 **对象跟光标（WYSIWYG）**，HUD/状态栏显示当前角。

**我们与主流的差距**：
1. 目标 = 单根线（无法框选批量）；
2. 锚心 = 必须是线的端点（任意基点只在组件整组模式下开放，COMPONENT_ROTATE_DESIGN.md §7 已记录"任意基点开放到单线"意向）；
3. 点选即拖（选中与旋转揉在一次 press 里），缺少"选好再确认再设锚"的节奏；
4. 无继承：无法先在**选择工具**里框好集合再进旋转。

---

## 1. 目标交互（ETCAD 式四段流）

```
    (入口 A) 进入旋转工具 R：候选集为空
    (入口 B) 在选择工具里已选好对象 → 按 R：候选集 = 继承选区（自动视为已确认）
              ┌─────────────────────────────────────────────┐
              │ 阶段 A · 选对象（候选集，可反复增减）           │
              │   单击线   = 单选（替换候选集）/ 多选模式=增减   │
              │   W 键     = 单选 ⇄ 框选（多选）模式切换        │
              │   空白拖   = 框选（Marquee，组件=最小选择单元）  │
              │   Ctrl+点  = 单选时快捷加选？否——Ctrl 保留给旋转复制│
              └──────────────────┬──────────────────────────┘
                                 │ 右键 = 确定候选集（无候选 → 退出旋转工具）
              ┌──────────────────▼──────────────────────────┐
              │ 阶段 B · 设锚心                               │
              │   点击画布：8px/zoom 内最近 resolved 点 → 吸附   │
              │             否则 → 自由锚点（临时锚点标记）      │
              │   锚心环 + 参照弧出现；HUD 出现（数值 = 当前角）  │
              └──────────────────┬──────────────────────────┘
                                 │ 按下（已设锚后）= 起手
              ┌──────────────────▼──────────────────────────┐
              │ 阶段 C · 长按拖动旋转                          │
              │   delta = atan2(光标−锚) − atan2(按下−锚)（(−π,π]）│
              │   Shift = 15° 吸附；HUD 实时；Ctrl = 旋转复制（单选）│
              │   每帧：resolveForDrag(S) + syncBlockPositions │
              └──────────────────┬──────────────────────────┘
                                 │ 松手 = 提交（一步 undo）；Esc = 取消回位
```

Esc 分级回退：`拖动中` → 取消回位；`锚已设` → 清锚重设；`已确认未设锚` → 回选对象阶段；`选对象中` → 清候选集退出。右键语义：`拖动中` = 取消回位；`锚已设/未设锚` = 取消本步回退（与 Esc 同）；`选对象中` = 确定候选集（ETCAD 主路径）。Shift/Enter/X 等修饰详见 §4。

**关键节奏变化**：今天的"单击线 → 立即 Ready + gizmo + HUD + 点线体即拖"改为"点线 → 候选集（仅选中高亮）→ **右键确认** → 锚点 → 拖"。多一次右键，换来批量与任意锚。**单选流程在 ET 里同样要右键确定**（用户描述），故统一。

---

## 2. 与参数化的映射（核心：怎么在"刚体旋转"下保住参数化）

| 场景 | 旋转语义 | 实现 | 参数化保护点 |
|---|---|---|---|
| 单选 · 自由线 | 绕任意锚点**刚体旋转**（delta 模式） | D2 公式：`rotation += delta`、`origin = pivot + (origin−pivot)·rot(delta)`（泛化现有 free 分支——现分支只支持锚=线端点且隐含"钉住端点"，任意锚改用刚体公式） | 公式变量/端点跟随不变（块内相对几何为刚体变换，不变式不破坏）；undo 恢复位姿 |
| 单选 · 连接线 · 锚=附着点 | **编辑跟随角**（参数化） | 现有 connected 分支（followerAngle / 弧长 / 公式锁定-烘焙）**不动** | 连接保持、公式锁定规则不变（测试锁定） |
| 单选 · 连接线 · 锚≠附着点 | 旋转 = 放弃跟随（快照+释放，undo/Esc 恢复） | 现有 `m_releaseAttId` 机制**不动** | 撤销一步恢复挂接（用户拍板 2026-08） |
| **多选（候选集 S，|S|≥2）** | S 全体**同一刚体变换**（与整组旋转 D2 同公式） | 泛化 D7 释放判定（下表，S 取代"组件成员集"）+ `RotateBlocksCommand` | **S 内关系保持**：附件/终点指向/省道全部指向 S 内 → 刚体变换下相对几何自洽（与组件同论据）；对外约束释放快照，undo 原样恢复 |
| 曲线块 / 多段块 | 按成员块刚体变换，不区分段 | 同组件 D10 | 曲线缓存惰性重建（transform-only 冻结） |

### D7 释放判定泛化表（S = 候选集）

| 特征 | 判定 | 处理 |
|---|---|---|
| att.fromBlockId ∈ S && att.toBlockId ∉ S | 成员线 → S 外 leader（含 pin） | 释放（快照） |
| att.fromBlockId ∈ S && att.toBlockId ∈ S | S 内连接 | **保持** |
| att.fromComponentId = C（组件级附件） | C ⊆ S（候选集含整组；见 D2 候选集规则——组件 = 最小选择单元，不存在半组） | 释放（批处理，不清 exposedPointId） |
| 成员块 endTargetBlockId ∉ S | 指向 S 外点 | 释放（清字段，快照全字段） |
| 成员块 endTargetBlockId ∈ S | S 内指向 | **保持** |
| 成员块 isDart() 且 start/ref ∉ S | 省道引用 S 外 | 释放（清 start/ref 降级，快照全字段） |
| isDart() 且 start/ref ∈ S | S 内省道 | **保持** |
| S 外 follower 附着在 S 上 | 组外跟随者 | **保持**（随 resolveForDrag 跟随） |

> 注：判定表与 COMPONENT_ROTATE_DESIGN.md 的 D7 逐行同构，S 取代组件成员集后组件整组旋转即其特例——**旧"W 整组模式"被多选旋转完全覆盖**（D1 翻案，见 §3）。

**每帧热路径铁律（沿用）**：`resolveForDrag(S)` + `scene->syncBlockPositions()`；禁 `resolveAll`/`refreshAllBlockItems`（AGENTS.md 2026-09 锁定）。

**锚点/候选集 = 会话态**：不进文档、不进序列化（与整组旋转 D8 同）；旋转结果 = 块 transform / followerAngle（已有字段）。

---

## 3. 决策清单（提案，【建议】= 推荐值，待拍板）

| # | 决策 | 建议 | 备选 / 说明 |
|---|---|---|---|
| D1 | **W 键语义翻案**：W = 单选 ⇄ 框选（多选）切换（与 ToolSelect 同语言）；**废弃 2026-12 的"W 组件整组旋转"模式**（被多选旋转覆盖，组件测试迁移） | 【已拍板 2026-13】 | 旧 W 整组模式删除，测试迁移到多选语义 |
| D2 | 候选集最小单元：**组件 = 整组**（点选/框选命中组件任一成员 → 整组入候选集，与 MarqueeGesture::expandWithGroups 现有语义一致） | 【建议】 | 允许半组（点选只加单线）→ 组件级附件释放规则变复杂、且"只转组件一根线"不是合理意图 |
| D3 | 右键 = 确定候选集 → 阶段 B；无候选右键 = 退出旋转工具（回选择工具） | 【建议】 | 无候选右键 = 切智能笔（与 ToolSelect 一致——但旋转中切回智能笔容易误触，选"回选择"更安全） |
| D4 | 锚点 = 任意点（吸附 or 自由临时锚），单选连接线锚=附着点 → 参数化角度编辑，其余一律刚体 | 【建议】 | 无（与 ET/D7 自洽） |
| D5 | **角度语义按对象类型分域**（用户 2026-13 指明：独立线/跟随线/组件适用的角不是同一个量，不能一套通用）——见 §3.5 分域矩阵 | 【建议，待讨论确认】 | — |
| D6 | HUD：caption「旋转 N 条线 · 绕锚点」（多选）/「跟随角度」（单选连接）/「绝对角度」（单选自由）；锚点确定后 HUD 出现在锚旁；Shift 15° 吸附；Enter 提交 / Esc 取消 | 【建议】 | — |
| D7 | 命令：`RotateBlocksCommand`（泛化 RotateComponentCommand：S 集 old/new transform + 释放快照，restore-then-replay，一步 undo）；组件整组旋转测试迁移到新命令 | 【建议】 | 保留 RotateComponentCommand 作为 S=组员的裸包装（二选一，推荐合并统一） |
| D8 | Ctrl 旋转复制：单选保留现有（RotateCopyGesture）；**多选旋转复制 = 阶段 P3**（本次不做，toast 提示） | 【建议】 | P1 就支持（工作量：泛化 RotateCopyGesture 到 S） |
| D9 | **继承选择工具选区**：ToolManager 切到旋转工具时快照 ToolSelect::selection() → `ToolRotate::adoptSelection(...)`，视为已确认（跳过右键，直接阶段 B）——通俗说：先在选择工具里把 8 根线框好，按 R 后它们**原样保留在旋转候选集**里（不用在旋转工具里重选一遍），直接点锚点、拖 | 【建议】 | 不做（纯工具内选择）——成本低、体验贴合"旋转和选择结合"，推荐做 |
| D10 | 桥线（isBridge）拒绝入候选集（现有 selectTarget 规则不变）；辅助层/灰显层不可选（与选择工具同）；层切换清空候选集（与 ToolSelect 同） | 【建议】 | — |
| D11 | 双击线 = 属性对话框、X 锚心切换（单选）、Esc 分级回退，均保留 | 【建议】 | — |

### §3.5 角度分域矩阵（D5 展开 · 待讨论确认）

用户 2026-13 指明：独立线段、跟随线段、组件之间……适用的角各不相同，不能一个方案通用。据此按**对象类型 × 交互场景**定义显示量/输入量：

| 场景 | 显示量 | 输入语义 | 说明 |
|---|---|---|---|
| **单选 · 自由线**（独立线段，无任何挂接） | **绝对世界角** [0,360°)，逆时针为正（水平基线 0° = 朝右） | 绝对世界角 | 现 v3 约定、测试锁定；**锚点任意化不改变此显示**（线方向与锚无关，任意远锚只是"转 + 平移"） |
| **单选 · 跟随线**（连接线），锚 = 附着点 | **带符号折角** [−180°,+180°]（0 = 折叠重叠、±90° 垂直、±180° 开平；符号 = 折向），或弧长模式 | 折角 / 弧长 | 现 v3 约定；参数化输入（写 followerAngle / arcLength，公式锁定-烘焙规则保留） |
| **单选 · 跟随线**，锚 ≠ 附着点 | 绝对世界角（旋转 = 放弃跟随，释放后即自由线） | 绝对世界角 | 撤销一步恢复挂接（现有 m_releaseAtt* 机制） |
| **多选（S ≥ 2，含任意混合）** | **增量角 delta**（带符号，0 = 原始位姿） | delta | 组级唯一参数——S 内每根线各有自己的世界角/折角，**没有一个单线的角能概括整组**；只有"转了多少"对整组有意义 |
| **组件整组**（= 多选特例：S = 组件成员集） | **增量角 delta**（0 = 原始位姿） | delta | 沿用 2026-12 D5；组件 `defaultAngleDeg` 不被改写（D8） |
| **单选 · 旋转复制** | **相对角** [0,360°)，0 = 与原线重叠 | 相对角 | 现约定、测试锁定 |
| **多选 · 旋转复制**（P2） | **增量角 delta**（0 = 原始位姿） | delta | 暂缓（P2） |
| **拖动随手** | 全场景 WYSIWYG：线/组跟光标 | — | 显示量 = 语义角的目标值，拖动映射公式与现行各分支配对 |

> 判定原则：**显示量 = "这个对象此刻最想知道的那个角"**。
> 自由线想知道的 = "线朝哪"（世界角）；跟随线想知道的 = "相对于基准线摆了多少"（折角）；
> 多选/组件想知道的 = "整组转了多少"（delta）。三者是不同物理量，故分域而不是统一。

> 备注（用户讨论焦点）：拖动时"线跟光标"的 delta 驱动各域一致（几何推进层相同），
> 只是**显示/输入的刻度尺**按上表切换——这保证手势统一、读数各有意义。

**阶段划分**：
- **P1**：单线任意锚 + 右键确认流 + W 框选 + 继承选区 + D7 泛化（**已拍板一次落地**，组件整组模式同步废弃迁移）；
- **P2**：多选旋转复制（Ctrl 泛化到 S）；
- **P3**：参照角旋转（AutoCAD R 参照：把线转到与另一条线同向，服装 CAD 常用"对齐"）——仅记录意向，未排期。

---

## 4. 状态机（ToolRotate · 重构）

```
enum class RotateState {
    Idle,       // 无候选集 / 候选集为空
    Selecting,  // 候选集非空，未确认（可继续增减、W 切换模式、框选）
    Ready,      // 已确认；锚未设（等待第一击）或有锚（等待按下）
    Rotating,   // 拖动中（delta 归一化到 (−π,π]；Shift 15° 吸附）
};
```

输入表（阶段分派，冲突仲裁优先级从上到下）：

| 输入 | Idle | Selecting | Ready（未设锚） | Ready（有锚） | Rotating |
|---|---|---|---|---|---|
| 左键 press 线 | 入候选集→Selecting | 单选:替换候选集 / 多选:增减 | 任意:设锚（吸附/自由） | 任意:设锚**或**起手旋转* | — |
| 左键 press 空白 | — | 多选:框选 / 单选:无 | 设锚（自由） | 起手旋转 | — |
| 左键拖动 | — | 框选（Marquee） | — | 旋转 | 旋转 |
| 左键 release | — | 框选提交 | — | — | 提交（一步 undo）→ Ready(有锚) |
| **右键** | 退出工具 | **确定候选集**→Ready | 回退 Selecting | 清锚回退 | 取消回位→Ready(有锚) |
| 双击线 | 属性对话框 | 属性对话框 | 属性对话框 | 属性对话框 | 先取消→属性对话框 |
| **W** | — | 单选⇄框选（toast） | 回 Selecting | 回 Selecting | 忽略（拖动中） |
| **X** | — | — | 单选线:锚＝起点/终点 | 单选线:锚心切换 | 忽略 |
| Ctrl+press | 复制候选 | 单选:旋转复制 | 单选:旋转复制 | 单选:旋转复制 | 拖动中按下 = 转复制（现状） |
| **Esc** | — | 清候选退出 | 回 Selecting | 清锚（回到未设锚） | 取消回位→Ready(有锚) |
| Shift | — | — | — | — | 15° 吸附 |
| Enter（HUD） | — | — | — | — | HUD 提交 |

*Ready(有锚) 下点击已有点/空白再设锚 = **重新设锚**（拖动起点≠锚点不易；但"点别的点改锚"在 Ready 有锚时应优先起手旋转——主流 CAD 语义：基点确定后一切按压 = 旋转，改锚靠 Esc 清锚。**建议：有锚后一切按压 = 起手旋转，改锚先 Esc**（与整组旋转 2026-12 拍板一致：整组模式下一切按压 = 锚点/旋转，不切换目标）。表格按此理解修正：*Ready(有锚) 左键 press 任意 = 起手旋转*；*Ready（未设锚）左键 press 任意 = 设锚*。X 键仍是单线"终点锚⇄起点锚"快捷切换（锚在端点上才有意义；锚在自由点时 X = 切到最近端点并吸附，作为兜底）。

---

## 5. 模型与命令

- **无新增持久化字段**（锚点/候选集 = 会话态；结果落块 transform / followerAngle）。
- **RotateBlocksCommand**（`src/document/commands/`，并入 gcad_document 源列表）：

```cpp
class RotateBlocksCommand : public QUndoCommand {
    // m_blockIds: S 集；m_oldTf/m_newTf: 各块 transform 前后
    // m_releasedAtts/m_releasedTargets/m_releasedDarts: 释放快照（复用 AimRelease/DartRelease）
    // redo: 写 newTf → removeAttachments(released) → 清外部 endTarget/省道字段 → resolveAll()
    // undo: 写 oldTf → addAttachmentsRaw → 恢复全字段 → resolveAll()
};
```

- 现有 `RotateComponentCommand`：推荐**删除**、测试迁移到 RotateBlocksCommand（组件 = S 的特例，S = 成员集时判定表逐行同构）。撤销路径行为等价。
- ParamDocument 反查零新增（候选集由工具持有，`findComponentByMember`/`componentOfBlock` 已有）。

---

## 6. 复用组件清单（避免重复造轮子）

| 组件 | 复用方式 |
|---|---|
| `MarqueeGesture` | 框选手势 + `expandWithGroups` 组件展开（现成）；ToolRotate 持有一个实例（Selecting 态空白拖） |
| `AngleHud` | `setCaption`（已支持「旋转 N 条线 · 绕锚点」）+ `setMode` + 输入/Esc/Enter 通道 |
| `RotateGizmo` | 锚心环 + 参照虚线 + 弧（delta 弧）；锚点自由时参照 = 拖起始方向（现状整组模式即此语义） |
| `RotateCopyGesture` | P1 保留单选；P2 泛化到 S（复制 S 全体，副本集同样刚体） |
| 自由线 free 分支 / `m_releaseAtt*` / `snapAnyPoint` | 单线专用逻辑保留；`snapAnyPoint` 从整组模式泛化到所有锚心 |
| D7 收集/恢复（现 ToolRotate 整组函数） | 泛化为 `collectRotateExternal(const QSet<QUuid>& S)` / `restoreRotateExternal()`，单线与整组共用 |

---

## 7. 测试清单（tests/test_rotate_copy.cpp 迁移 + 新增）

| 用例 | 断言 |
|---|---|
| wTogglesBoxSelectMode | W 切换多选/单选（toast）；废弃 wTogglesGroupModeOnlyForComponent |
| clickSelectsSingleReplaces | 单选：单击线入候选、再次单击替换；多选：增减 |
| marqueeSelectsIntersecting | 空白拖框选：相交即入选；组件命中 → 整组入候选（expandWithGroups） |
| rightClickConfirmsThenAnchor | 右键 → Ready；无候选右键 → 退出工具信号 |
| singleFreeArbitraryPivot | 单选自由线任意锚（空白临时锚）旋转：D2 刚体公式精确成立；undo 回位 |
| singleConnectedAnchorAtAttachKeepsParametric | 锚=附着点 → 编辑 followerAngle（现有语义，回归） |
| singleConnectedAnchorElsewhereReleases | 锚≠附着点 → 释放 + undo 恢复（现有语义，回归） |
| multiRotateRigidSameDelta | 无连接两自由线：全体同 delta、刚体公式精确、一步 undo |
| multiRotateKeepsInternalAtt | S 内 A→B 跟随 + S 内 endTarget + 组内省道：旋转后相对几何不变 |
| multiRotateReleasesExternal | S 外 leader 附件/pin/指向 S 外 endTarget/引用 S 外省道：释放；一步 undo 全恢复 |
| componentLeastUnitByClick | 多选模式点击组件成员 → 整组入候选（组件级附件释放走批处理、exposedPointId 保留） |
| inheritSelectionFromSelectTool | ToolSelect 选区 → R → adoptSelection：跳过右键直接 Ready |
| escLevels (分级回退) | Rotating/Esc → 回位；有锚/Esc → 清锚；未设锚/Esc → Selecting；Selecting/Esc → 退出 |
| hudDeltaDisplay | 多选 HUD = 带符号 delta，0 = 原始位姿；输入 45 = 相对原始 45° |

---

## 8. 实现接线清单（动代码前逐条核对）

1. `src/tools/ToolRotate.h/.cpp`：状态机重构（Idle/Selecting/Ready/Rotating）+ 候选集（QSet<QUuid>，活动层限定、层切换清空）+ 鼠标/键盘分派表（§4）+ 锚点任意化（吸附/自由）+ `adoptSelection()`；
2. `src/tools/ToolManager.cpp`：`switchTool(Rotate)` 前快照 `ToolSelect::selection()` → 新实例 `adoptSelection`（D9）；
3. `src/document/commands/`：新增 `RotateBlocksCommand`（复用 AimRelease/DartRelease；删除 RotateComponentCommand 或将其作为薄包装——推荐删除统一）；
4. `src/tools/ToolRotate.cpp`：D7 泛化（`collectRotateExternal(S)`/`restoreRotateExternal()`，从整组函数抽出）；
5. `tests/test_rotate_copy.cpp`：迁移 + 新增（§7）；
6. `TROUBLESHOOTING.md` 第 0 组登记表：R 行更新（四段流）、W 行改「框选切换」；
7. `AGENTS.md` 领域建模决策：更新"组件整组旋转（2026-12）"条目（注明翻案：被多选旋转覆盖，W 语义改框选）、新增"旋转工具（2026-13）"条目（带源码引用）。

---

## 9. 开放问题（拍板前确认）

1. **W 键翻案**：已拍板（W = 框选切换，废弃 2026-12 整组模式）；
2. **P1 一次落地**：已拍板；
3. **角度分域矩阵（§3.5）**：待确认——各行显示/输入语义是否符合预期（讨论焦点：自由线=世界角、跟随线=折角、多选/组件=delta、复制=相对角）；
4. **D9 继承选区**：待确认（通俗版：选择工具里框好 8 根线 → 按 R → 这 8 根原样保留为旋转候选，直接点锚点拖；实现 = ToolManager 切换时快照 ToolSelect::selection() 传给旋转工具）；
5. **D2 组件最小单元**：点选组件线 = 整组入候选（推荐）？
6. **D3 无候选集右键**：退出旋转工具回选择（推荐）还是切智能笔？
