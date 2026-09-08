# PERF_AUDIT_REPORT.md —— 性能审计报告（重复计算 + 可维护性）

> 生成于 2026-12。本轮审计目标：在与既有性能决策（DECISIONS.md「Resolver 性能」/「曲线系统」锁定条目、CONVENTIONS.md Qt 性能与抗噪声断言）**不冲突**的前提下，找出仍然存在、可落到具体 file:line 的**重复计算**，以及几处**真实可维护性风险**。
>
> 结论先行：本工程在求解管线 / 缓存 / 懒重建 / 脏传播 / 引用索引 / snap 块缓存 / 曲线 span 指纹 memo / 几何 epoch / 公式拓扑序 / 画布 O(1) 平移快路径上已大量收口。下列条目是**已收口项之外的补充**，均带源码引用，按「收益 × 风险」给出优先级。
>
> **实施状态（2026-12）**：
> - **① 分段弧长缓存** — 已实现（`buildCumulativeArcLength` + `CurveSpanEntry::cumArcLengthMm` + SnapEngine/Block 复用）。
> - **A `runResolvePass` 收口** — 已实现（`ParamDocument::runResolvePass`，12 处 `Resolver::resolveAll(...)` 全部收口）。
> - **验证受限**：本会话环境无法运行 cmake/ninja 构建及 ctest（平台级进程创建被拒，见会话记录），故上述两项仅完成**静态审阅 + 分层检查（check_layering 通过）**，**未做编译/单测**。交由具备完整 MSVC 环境的环境执行：`tools\build.bat WildWindPattern` → `ctest -C RelWithDebInfo -R test_curve` + `-R test_resolver`（①）与 `-R test_block_commands`（A）。
> - 🥈③④② 与 🥉⑤C D 仍为待实施建议。

---

## 目录

1. [重复计算：还能减的项](#一重复计算还能减的项)
2. [可维护性：提升项](#二可维护性提升项)
3. [已做好的设计（勿动）](#三已做好的设计勿动)
4. [影响面与验证矩阵](#四影响面与验证矩阵)
5. [结论与建议落地优先级](#五结论与建议落地优先级)

---

## 一、重复计算：还能减的项

### 1. `projectPointOnCurve` 每次调用都重建弧长累积表 —— SnapEngine 最肥的重复

**位置**：`src/geometry/CurveMath.cpp:671-673`；调用点 `src/tools/SnapEngine.cpp:281`。

```cpp
// CurveMath.cpp:671-673 —— 每【一次】投影调用都重建
std::vector<double> cumLen(n + 1, 0.0);
for (int i = 0; i < n; ++i)
    cumLen[i + 1] = cumLen[i] + spanArcLength(spans[i]);   // 每 span 5 点 Gauss-Legendre
```

`findSegmentSnap` 在**每帧鼠标移动**、对**每条曲线**各触发一次 `projectPointOnCurve`。而 `spanArcLength`（`CurveMath.cpp:584-595`）本身就对每个 span 做 5 次 `evalBezierDerivative`。

**矛盾点**：`CurveSpanEntry`（`Block.h:25-41`）已在 `rebuildCurveCache` 时缓存了 `arcLengthMm`，**但没有缓存分段累积弧长表**（`cumLen`）。投影 / 插值每次都从零积分。

**建议**：在 `CurveSpanEntry` 加一个 `std::vector<double> cumArcLengthMm`（重建曲线缓存时一次算好），`projectPointOnCurve` 与 `arcLengthToParam` 直接读取，免去每 span 5 次求导 × 每帧每条曲线。

- 收益：对「多曲线文档 + 悬停 / 吸附」场景直接、明显；改动局部、风险低。
- 与既有决策关系：属于「曲线缓存惰性重建」决策的**未覆盖面**——该决策冻结了 spans 的重解，但未冻结 arc-length 分段表，此为其自然延伸。

---

### 2. 多条辅助点在同一曲线上反复积分弧长 —— Resolver 固定点内重复

**位置**：`src/parametric/Block.cpp:543-572`（`resolveInterpolatedPoint` 曲线分支）。

每个 Interpolated 点调用 `spansForSegment(...)` 取得同一组 spans，然后**各自**再 `totalArcLength(spans)` + `arcLengthToParam(spans, ...)`。N 个辅助点在同一曲线上 = N 次全弧长积分 + N 次 `cumLen` 表重建，且发生在 `kMaxSettleRounds` 固定点循环内。

**次要但真实**：`spansForSegment`（`Block.cpp:729-758`）对同一条曲线有指纹 memo，但返回是**按值拷贝** `return it->spans;`（`Block.cpp:752`），每个辅助点都复制一整份 `std::vector<BezierSpan>`。

**建议**：
1. 附带缓存该曲线的 `totalArcLength` 与 `cumLen`（与第 1 点同一张表，一处建表多处复用）。
2. `spansForSegment` 返回 `const std::vector<BezierSpan>&`（block 生命周期内 memo 稳定），避免按值拷贝。

- 收益：降固定点内几何开销；对含较多曲线辅助点的文档有效。
- 风险：须确认所有调用方都不会保存/移动返回引用（当前调用点均为只读消费）。

---

### 3. `collectMobileAuxBlocks` 每次测量 pass 都全量重建 —— 跨层文档的真实重复

**位置**：`src/parametric/MeasurementStore.cpp:134-136`；实现 `src/parametric/ParamDocumentIndexes.cpp:190-221`。

```cpp
// MeasurementStore.cpp:134-136
const auto mobileAux =
    (skipAuxSource && m_doc->hasCrossLayerAttachments())
        ? m_doc->collectMobileAuxBlocks() : QSet<QUuid>();
```

`collectMobileAuxBlocks` 每次扫描全部 attachments + 走一遍 followers BFS。它在 Phase 2 初始测量 + 固定点每轮 + 跨层沉降里被反复调用（跨层文档一帧可能跑约 10 次）。

**性质**：与 `m_crossLayerCount` 同类的**结构属性**——除非 attachments 结构变化，否则结果恒定，不应每测量 pass 重算。

**建议**：在 `recountCrossLayerAttachments`（`ParamDocumentIndexes.cpp:130-140`）时一并缓存 `m_mobileAuxBlocks`，仅在 attachments 增删时失效重算。

- 收益：跨层文档拖帧降重复；对无跨层文档路径（fast path 单整数测试）零开销。
- 风险：低；仅共享同一失效时机。

---

### 4. `findResolvedPointWorld` 线性扫全块表 —— 交叉点固定点内重复

**位置**：`src/parametric/Resolver.cpp:36-58`（`findResolvedPointWorld`）。

每个未解析的跨块交点，先查本地 block，miss 后**线性扫所有 block** 找 pointId。这在 Step 6 固定点里、每个 pass 的每个交点都会触发。

**矛盾点**：文档级已有一张 `pointId -> blockId` 归属表（`ParamDocumentIndexes.cpp:94-103` 的 `pointOwner`）——但它是**用完即弃的局部变量**（`ensureReferencesIndex` 内），没有保留给求解器复用。

**建议**：把 `pointOwner` 提为成员（随增删块/点失效），`findResolvedPointWorld` 由 O(blocks) 降为 O(1)。

- 收益：降交点固定点开销；对大文档（多块、多交点）有效。
- 风险：低；与引用索引共享失效时机（增删块/点、全量 resolve）。

---

### 5. 两个跨层扫描函数几乎一模一样的重复逻辑（维护性）

**位置**：`src/parametric/ParamDocumentIndexes.cpp:143-187`。

```cpp
bool ParamDocument::scanAuxIntersectionCrossRefs() const        // :143
bool ParamDocument::scanWorkingIntersectionAuxRefs() const      // :166
```

两段 O(work×aux×points) 的嵌套扫描，仅方向相反。**性能上已做到只在 full resolve 时重扫**（`ParamDocumentResolver.cpp:423-426` 缓存复用），所以不是热路径问题，但可维护性差：逻辑互为镜像，改一个易漏另一个。

**建议**：合并为一个「(交点所在层, 引用目标层) -> bool」的函数；更进一步，可复用引用索引（它已知道跨块引用），把复杂度降为 O(引用数)。

- 收益：维护性；纯重构无行为变化（须用现有 `m_auxIntersectToWorking` / `m_workingIntersectToAux` 回归）。

---

## 二、可维护性：提升项

### A. 【最高】把重复 15 次的 `Resolver::resolveAll` 调用收口成一个 pass 助手

**位置**：`src/parametric/ParamDocumentResolver.cpp`。

这一串 9 参调用在文件中出现约 **15 次**（Phase 1/2/2.5/3/4、follow 重解、follow 沉降等）：

```cpp
Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                     &diag, Scope::X, layerId, effAffected, &m_exprCache);
```

每次都要手写 `m_blocks`、`*passAttachments`、`m_parameters`、`m_conditioned`、`&m_exprCache`。**维护性 + 正确性双重风险**：未来任何一处**漏传 `effAffected`** 就会静默把收窄的拖帧升级成全量重解——比写错常数更难发现（性能退化但结果正确）。这是「每帧热路径铁律」最容易在未来被破坏的地方。

**建议**：抽一个成员函数：

```cpp
void ParamDocument::runResolvePass(Resolver::Scope scope,
                                   std::vector<ResolveDiagnostic>& diag,
                                   const QSet<QUuid>* affected);
```

内部统一组装 9 参并注入 `GCAD_PERF_SCOPE`。可 grep、可测、可改。

- 收益：可维护性 + 给「漏传 affectedOnly」这一整类隐患一个单点防线。

---

### B. 丢弃的诊断缓冲重复分配

**位置**：`src/parametric/ParamDocumentResolver.cpp`（`:328` `:431` `:471` `:496` `:527` 等出现 `std::vector<ResolveDiagnostic> auxDiag{}; // discarded`）。

多次 `// discarded` 说明这些诊断不被消费，但每次都分配一次 vector。可抽一个成员/可复用的 `m_discardDiag`，或用 `runResolvePass`（见 A）在签名里吞掉，省掉反复分配。

- 收益：微小但零风险；与 A 顺带完成。

---

### C. `m_referenceIndexDirty` 的失效粒度太粗

**位置**：`src/parametric/ParamDocumentResolver.cpp:244`。

```cpp
void ParamDocument::resolveAll() {
    m_referenceIndexDirty = true;   // 每次全量 resolve 都置脏
    ...
}
```

引用索引只在 `collectAffected`（拖帧路径）真正用到。一次**完全不涉及引用字段**的结构变更（如只改图层、移动块）也会强制下个拖帧重建 O(N·P) 的 `pointOwner` 表。

**建议**：区分「结构性脏（增删块/改引用字段）」与「求解脏」，仅在真正变更引用（`addBlock`/`removeBlock`/`addBlockRaw`/`clear`/@引用字段变更）时置 `m_referenceIndexDirty`，让拖帧首帧更快。配合第 4 点把 `pointOwner` 提成员，可一起做增量化。

- 收益：拖帧首帧更快；风险：需仔细核对所有引用字段变更点（终点指向/省道/曲线点 follow/polar/midpoint/交点/插值），确保不漏置脏。

---

### D. `findPoint`/`findSegment` 的 size-guard 是「隐性自愈」（关注的坑）

**位置**：`src/parametric/Block.cpp:847` 与 `:876`。

```cpp
if (m_pointIndex.size() != static_cast<int>(points.size()))
    rebuildPointIndex();
```

每次取点都跑一次 int 比较，靠**尺寸漂移**自愈：若原地改某一既有点（不增删），索引不会失效、可能返回旧 index——目前靠「不直接改 points 的纪律」兜底。

这是**可维护性 > 性能**的隐患：未来若批量原地改写 points，建议显式提供 `touchPointIndex()` / 排序稳定接口，而非依赖 size guard。

- 收益：防未来踩坑；风险：无（未改动现有行为）。

---

## 三、已做好的设计（勿动）

这些是既有性能决策已锁定的收口，报告**明确不推荐**改动：

- **分层脏标记 + 受影响子图收窄**（`ParamDocumentResolver.cpp` Phase 2.5/3/4 复用 `effAffected`）。
- **`Block::resolve` 只在几何真变时 `rebuildCurveCache`**（`Block.cpp:136-143`）；纯刚体拖拽冻结 spans（2026-09 曲线跟随抖动治本）。
- **`syncFromBlock` 用 rotation/epoch/pointCount 判定「纯平移 O(1)」**（`BlockItem.cpp:377-401`）。
- **公式依赖图拓扑缓存**（`VariableStore.cpp:269-314`）。
- **SnapEngine 每块坐标/段缓存**，键含 epoch + transform + count（`SnapEngine.cpp:347-446`）。
- **`collectAffected` 引用索引**（`ParamDocumentIndexes.cpp:89-127`）。
- **`EvalContext ctx` 每 pass 新建、按 pass 缓存求值结果**（`ExpressionEvaluator.cpp`/`ConditionEngine.cpp`）。**注意不要**提升为跨 phase 共享——变量表/测量值跨 phase 会变，跨 phase 复用将读到旧值。

---

## 四、影响面与验证矩阵

| 项 | 所属模块 | 影响面 | 验证用单测（按影响面） |
|---|----------|--------|------------------------|
| ① 投影弧长表 | geometry | 曲线捕捉/投影 | `ctest -C RelWithDebInfo -R test_curve` |
| ② spans 拷贝+弧长 | parametric+geometry | 曲线辅助点/交点 | `test_curve` + `test_resolver_*` |
| ③ mobileAux 缓存 | parametric | 跨层文档拖帧 | `test_aux_layer` + `test_resolver_*` |
| ④ pointOwner 成员 | parametric | 交点固定点 | `test_resolver_*` + `test_intersection_update` |
| ⑤ 合并跨层扫描 | parametric | 结构性扫描 | `test_resolver_*`（行为不变） |
| A `runResolvePass` 收口 | parametric | 全求解管线 | `test_block_commands`（行为不变）+ 编译一次 `reldeb` |
| C 引用索引失效粒度 | parametric | 拖帧 | `test_block_commands` + `test_aux_layer` |

**性能验证提示**（`CONVENTIONS.md` 抗噪声口径）：Debug 构建性能波动 30%+，断言用宽松边界（如 ≤2.0x）；验证优化应构造最坏场景（逆序深链 / 多曲线文档）而非常见微差。可用 `GCAD_PROFILE=1` 启动看 `resolve`/`snap.*` 桶的每 120 帧平均值（`PerfProbe.h`）。

---

## 五、结论与建议落地优先级

| 优先级 | 改动 | 类别 | 收益 |
|--------|------|------|------|
| 🥇 高 | ① 给 `CurveSpanEntry` 加分段累积弧长表，投影/插值直接读 | 重复计算 | 直接降每帧 snap / 固定点几何开销 |
| 🥇 高 | A `runResolvePass` 收口 15 处 9 参调用 | 可维护性 | 防「漏传 affectedOnly」升级成全量重解 |
| 🥈 中 | ③ `collectMobileAuxBlocks` 缓存为结构属性 | 重复计算 | 跨层文档拖帧降重复 |
| 🥈 中 | ④ `pointOwner` 提成员 + `findResolvedPointWorld` O(1) | 重复计算 | 交点固定点降重复 |
| 🥈 中 | ② spans 返回引用 + 弧长表复用 | 重复计算 | 曲线辅助点固定点降重复 |
| 🥉 低 | ⑤ 合并两个跨层扫描函数 | 可维护性 | 双镜像逻辑收敛 |
| 🥉 低 | C 引用索引失效粒度细化 | 可维护性 | 拖帧首帧更快 |
| 🥉 低 | D 显式 touchPointIndex 接口 | 可维护性 | 防未来踩坑 |

**建议**：优先做 **①（重复计算收益最直接）** 与 **A（维护性收益最大）**。① 属 geometry 路径（`test_curve`）；A 属纯重构（`test_block_commands` + 一次 `reldeb` 编译即可，行为无变化）。二者均不触碰已拍板的「Resolver 性能」决策改动面。
