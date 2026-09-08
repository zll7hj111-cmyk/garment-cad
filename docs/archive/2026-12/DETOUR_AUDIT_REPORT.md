# DETOUR_AUDIT_REPORT.md —— 全项目「拐弯路径实现」审计报告

> 2026-12 全量扫描。范围：`src/` 全部 291 个源文件，按模块边界拆 5 路并行审计。
> **性质**：代码异味/结构审计报告（分析类，非决策类）。结论供后续修复参考，不算设计决策。
> 审计纪律：只报带 `file:line` 证据的候选；"整库无调用/可安全删除"的死码判定均经裸子串 + src/tests 双侧复核。

---

## 状态总览（2026-12 全量修复，含验证）

**✅ 已落地——10 个修复候选全部闭环**，验证：架构七守卫 `ctest -R check_` 100% Passed；交付全量回归 `ctest -C RelWithDebInfo` **42/42 Passed**（37.46s）。

| # | 候选 | 状态 | 修复要点 |
|---|------|------|---------|
| 1 | SetVariableValueCommand 绕门面 | ✅ | `redo/undo` 拷贝 var → `m_doc->updateVariable(...)`，恢复 `variablesChanged()` |
| 2 | BlockItem 长度复减 | ✅ | 复用已算 `w1/w2`：`w1.distanceTo(w2)` |
| 3 | 隐藏点丢弃 | ✅ | `!pt.visible` 不再丢缓存；`PointCache` 加 `visible` 字段，悬停半透明唤醒 |
| 4 | expandWithGroups 恒等转发 | ✅ | 补全 `componentOfBlock` 展开组件成员 |
| 5 | ToolIntersection 重复几何 | ✅ | 引入轻量几何缓存重载，热路径消重 |
| 6 | 手搭图元未走 ManagedItems | ✅ | 接入 `ManagedItems` RAII |
| 7 | DeleteBlock/RemoveBlock 分叉 | ✅ | `using DeleteBlockCommand = RemoveBlockCommand` 别名收敛（两入口同命令） |
| 8 | layerEffectivelyVisible 死码 | ✅ | 删除，全库 0 残留 |
| 9 | boundingBoxOf 同构 | ✅ | 委托 `boundingBoxOfBlocks`（26 行 → 1 行） |
| 10 | refWorld 手写复制 | ✅ | 门面统一调 `effectiveAngleRefWorld`；**未动 Resolver 内联版** |

> 守住的三个风险点：⑦ 用别名收敛而非分叉（画布/图层面板真同命令）；① 门面单通道信号不双刷新；⑩ 未碰 `Resolver.cpp:759-794` 热路径内联版。

---

## 〇、首要红线：硬违规为 0（项目纪律良好）

- **`const_cast`**：全项目 **0 处**。
- **手动 `++geometryEpoch`**：**0 处**。`Block.h:73-84` 注释印证——原本散落约 20 处手写自增，已收口为 `touchGeometry()` 单一入口（`Block.h:84`）。这是本架构一项**已修掉的拐弯**。
- **`RawModelAccess` 绕行**：全部只出现在 `DocumentSerializer`（反序列化唯一 friend 通道）与各命令的 undo/redo 授权路径内，无违规。
- **手搭 HUD / 临时图元**：tools 层 70 处 `new QGraphics*` 均为工具自身的 marker/ring/预览视觉元素（非违规）；HUD 标签与临时图元统计规范走 `HudItem` / `ManagedItems`。仅 `ConnectOverlapResolver` / `MarqueeGesture` 例外（见候选 6）。

> 结论：项目在"格式纪律"层面极干净；真正的拐弯都藏在**无净贡献的转发 / 复算 / 绕门面**里。

---

## 一、Top 修复候选（按收益/风险比排序）

### 候选 1｜`SetVariableValueCommand` 绕过门面、丢面板刷新信号 — **✅ 已修复**
- **范围**：`src/document/commands/VariableCommands.cpp:50-56`
- **现状**：`redo()` 用裸指针直写 `v->value = m_newValue`，再手调 `m_doc->recomputeFormulas()`。而门面 `updateVariable`（`VariableStore.cpp:44-54`）= 改字段 + **emit `variablesChanged()`** + recompute。
- **拐弯性质**：**漏绕正道而非多绕**——复刻了字段写与重算，唯独丢了 `variablesChanged()` 面板刷新信号。
- **佐证**：`updateVariable` 的唯一真正消费者在 `VariablePanel.cpp:680`（面板输入框 → 门面更新，带信号）。另一同类命令 `SetVariableCommand`（`VariableCommands.cpp:88-89`）**走的是** `m_doc->updateVariable(...)`——即同类"修改变量"操作被拆成两个命令，一个守规（带信号）、一个绕过（丢信号）。
- **直线路径**：拷贝旧 `Variable` 改 value 后调 `m_doc->updateVariable(...)`。
- **收益**：修漏刷新 + 省裸指针直写；与 `SetVariableCommand` 行为对齐。
- **风险**：需实测确认 `variablesChanged()` 缺失是否已被 `documentChanged` 兜住；若已兜住则实际影响变小，但绕门面本身仍属违规。
- **置信度**：中高。

### 候选 2｜`BlockItem` 同帧对同一组端点算两次 worldPos（冗余重算） — **✅ 已修复**
- **范围**：`src/canvas/BlockItem.cpp:852-853` vs `869-870`
- **现状**：`w1 = block->worldPos(seg.startPointId)`、`w2 = block->worldPos(seg.endPointId)` 已在 852-853 算出；869-870 却又各调一次 `block->worldPos(...)` 再 `distanceTo`。`worldPos` 内部 = `toWorld(effectiveLocalPos(...))`，同帧重复。
- **直线路径**：`lenMm = w1.distanceTo(w2)`。
- **收益**：每 resolve 批次每段省 2 次 `toWorld` + `effectiveLocalPos`。纯计算，零风险。
- **置信度**：高。

### 候选 3｜`rebuildCache` 保留隐藏段、却丢弃隐藏点 — **✅ 已修复**
- **范围**：`src/canvas/BlockItem.cpp:773-776`（保留 hidden 段）vs `942-943`（`if(!pt.visible||!pt.resolved) continue` 丢弃 hidden 点）
- **现状**：隐藏段"故意保留"（注释明说为悬停/双击重开属性），隐藏点却直接跳过不进 `m_points` 缓存。
- **拐弯性质**：违反 AGENTS.md「隐藏实体是纯视觉属性，不影响交互；rebuildCache 必须包含所有实体」规约——隐藏点的交互行为与隐藏段不一致，导致悬停隐藏点无法命中/重开属性。
- **直线路径**：与段一致，`!pt.visible` 点也入缓存（配可见性标志）。
- **收益**：修复隐藏点与隐藏段交互不一致。
- **风险**：建议先查既有隐藏点悬停用例，确认非历史有意取舍。
- **置信度**：中（正确性）。

### 候选 4｜`MarqueeGesture::expandWithGroups` 恒等转发（假抽象） — **✅ 已修复**
- **范围**：`src/tools/MarqueeGesture.cpp:54-57`
- **现状**：函数体 `return ids;`，`doc` 参数完全未用；`update`/`end`（`:88`、`:98`）仅把它当 `toggleOf(...)` 透传。
- **拐弯性质**：恒等转发 + 名不符实的"组件展开语义"（`ROTATE_REDESIGN_DESIGN.md:210/313/328` 引用该方法作为"组件命中→整组候选"语义，但实现从未展开组件）。
- **直线路径**：`update`/`end` 直接用 `toggleOf(...)` 返回值。
- **收益**：去 2 次拷贝 + 消除语义悬空。
- **置信度**：高。

### 候选 5｜`ToolIntersection` 同帧重复几何计算（性能热点） — **✅ 已修复**
- **范围**：`src/tools/ToolIntersection.cpp:371-380` vs `571-580`
- **现状**：`updateAimPreview` 已算 `segAngleRad`；`computeIntersection` 又对同一 `block/seg/sp/ep` 重算 `w1/w2/segDir/baseAngle`。
- **直线路径**：复用已算角度，或让 `computeIntersection` 接收/返回角度。
- **收益**：aim 预览逐帧热路径减少一份完整线段几何 + 一次 `atan2`。
- **置信度**：高。

### 候选 6｜`ConnectOverlapResolver` / `MarqueeGesture` 手搭图元，未走 `ManagedItems` — **✅ 已修复**
- **范围**：`src/tools/ConnectOverlapResolver.cpp:120-241`、`src/tools/MarqueeGesture.cpp:101-107`
- **现状**：手写 `new QGraphics*` + `removeItem(); delete`；同仓其余文件（`RotateAimSnap`/`RotateInputTracker`/`RotateGizmo`/`ToolBreak`/`ToolMeasure`/`ToolCurveEdit`/`ToolIntersection`/`ToolSmartPen`）全部用 `m_managed.own(...)`。
- **拐弯性质**：规范性绕行——手工样板比 RAII 多写、漏置空即悬空/泄漏，与仓内既有模式不一致。
- **直线路径**：登记 `ManagedItems`，`remove()`/析构统一释放。
- **收益**：消除 3~5 处样板，统一 RAII。
- **置信度**：中。

---

## 二、收敛性 / 正确性候选（需先对齐语义再动）

### 候选 7｜`DeleteBlockCommand` vs `RemoveBlockCommand` 重复实现且影子级联分叉 — **✅ 已修复**（别名收敛）
- **范围**：`src/document/commands/DocumentCommands.cpp:41-115` vs `BlockLifecycleCommands.cpp:36-164`
- **现状**：前者快照**不含**影子块/长度烘焙消费者；后者**含**。分别被 `ToolSelectActions.cpp:99`（从选区删）与 `LayerPanel.cpp:557`（从图层面板删）使用 → **同一删除操作、两个入口行为不一致**。
- **风险**：`test_block_commands.cpp:136` 测后者；两入口语义是否本就该不同需先对齐，不可直接合并。
- **置信度**：中高。

### 候选 8｜`layerEffectivelyVisible` 纯死代码 — **✅ 已修复**
- **范围**：`src/parametric/ParamDocumentStores.cpp:69` + `LayersView.h:71-72`
- **现状**：函数体 `return layerVisible(...)`；全库（src+tests）无消费方、**无反射契约**（普通门面方法，非 QObject 槽，tests 无 `invokeMethod` 引用）。语义已被 `layerSnappable`（`ParamDocumentStores.cpp:70-75`）承担。
- **直线路径**：删方法 + `LayersView.h` 转发，消除"可见性三口径"混淆。
- **收益**：零风险纯删除。
- **置信度**：高。

### 候选 9｜`boundingBoxOf` 与 `boundingBoxOfBlocks` 逐行同构 — **✅ 已修复**
- **范围**：`src/parametric/ParamDocumentBlocks.cpp:330-356` vs `358-383`
- **现状**：两函数体除入参（componentId vs 块集）外完全一致。
- **直线路径**：`boundingBoxOf(componentId)` 展开成员后调 `boundingBoxOfBlocks(...)`，删 ~26 行重复。
- **置信度**：中（纯只读 AABB，无契约风险）。

### 候选 10｜refWorld 推导 3 处手写复制（已被跨模块印证） — **✅ 已修复**
- **范围**：`src/parametric/ParamDocumentAttachments.cpp:270-306` / `313-391` vs 收口版 `effectiveAngleRefWorld`（`ParamDocumentAttachments.cpp:682-726`）
- **现状**：门面 `setAttachmentAngleRef` / `setAttachmentAngleIndependent` 各自手写宿主出口 → 自定义基准点 / 点1→点2 连线 / 旧档 start→end 方向推导；收口版 `effectiveAngleRefWorld`（`FollowerAngle.h:51` 声明，标注"与 Resolver::applyAttachment 的 refWorld 逐位同构"）已存在。
- **跨模块印证**：命令层 `AttachmentAngleCommands.cpp:123` **已用**收口版 `effectiveAngleRefWorld`，门面方法却在另写一份——证明存在"一处漏改"的语义漂移温床（脱胎于 `TROUBLESHOOTING.md:331` 历史 bug）。
- **直线路径**：三处调用方直接调 `effectiveAngleRefWorld(doc, att)` 拿 refWorld 再反算 followerAngle。
- **收益**：消除语义漂移复发源。
- ⚠️ **不动** `Resolver.cpp:759-794` 内联版——热路径带 `ctx` 求值上下文，抽出去可能引入 per-pass memo 语义变化，仅作低优先建议。
- **置信度**：中。

---

## 三、有意设计的"拐弯"（按纪律排除，避免误改）

这些在别的项目像拐弯，但在本项目是**明文正道**，逐条排除：

| 结构 | 排除理由 |
|------|----------|
| DomainViews 六件套（`DomainViews.h`） | 官方 B1/B2/B3 门面分组，130+ 处真实消费，非假抽象 |
| ParamDocument → VariableStore/MeasurementStore/LayerRegistry 批量一行转发 | "ParamDocument 是门面"架构原则，转发即语义 |
| undo 命令带全量模型快照 | 刻意设计（`kUndoStackLimit=150`），任务明示不算拐弯 |
| 信号单点重发（`ParamDocument.cpp:33-48`） | 保持门面单一连接点的刻意设计 |
| 序列化 raw-int 容错 vs 表驱动混用 | 损坏档容错，刻意设计 |
| 命令层影子状态机重路由（`SetAttachmentAngleOnlyCommand`） | 需粒度 undo 快照，门面无子步，不宜单调 |
| `findBlock` → `blockById` 转发 | 改名兼容别名（1100+ 引用），保留 |
| `ConnectGesture` → `ConnectOverlapResolver` 的 8 个薄转发 | "阶段 3 拆分"刻意生命周期边界，非链式拐弯 |
| `ToolRotate` 对 `m_session` 的薄包装 | 工具内部 API 封装 |
| `RotateBlockCommand` vs `RotateBlocksCommand` | 场景不同 |
| `SetSlideOffsetsCommand` 非对称 resolve | 与 `MoveBlockCommand` 结算次序有关，刻意 |
| Break 流水线 re-acquire 裸指针 | P1-3 指针失效纪律 |

---

## 四、审计方法学两大教训（沉淀）

1. **Qt 反射调用是"死码判定"头号陷阱**：tests 用 `QMetaObject::invokeMethod(&obj, "函数名")` 反射调用槽函数，**用正常 `函数名(` grep 永远搜不到**。本次审计曾因此险些误删 `test_ortho_offset` 依赖的 `LineGeometrySection::onOrthoDistEdited`（`tests/test_ortho_offset.cpp:76/125/175` 反射调用）。**凡是判"无调用/可删死码"的 QObject 槽函数，必须用裸子串 + 在 src AND tests 搜 `invokeMethod`。** 已把该通则同步给 4 个并行子代理。
2. **"绕行"有两种方向**：既可能是**多绕**（转发链/冗余复算），也可能是**漏绕正道**（绕门面丢信号，如候选 1）。后者危害更大、更隐蔽，审计时两个方向都要查。

---

## 五、参考（已复核的跨模块证据）

- `VariableStore.cpp:44-54`（`updateVariable` 带 `variablesChanged()`）
- `VariablePanel.cpp:680`（唯一走门面的更新调用）
- `BlockItem.cpp:852-853` / `869-870`（worldPos 重复）；`773-776` vs `942-943`（hidden 段/点不对称）
- `MarqueeGesture.cpp:54-57`（恒等转发）
- `ParamDocumentStores.cpp:69`（死代码 `layerEffectivelyVisible`）
- `AttachmentAngleCommands.cpp:123`（命令层已用收口版 refWorld）vs `ParamDocumentAttachments.cpp:313-391`（门面手写）
- `tests/test_ortho_offset.cpp:76/125/175`（反射契约实例）
