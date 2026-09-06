# DETACH_SHADOW_PLAN.md —— 拆开影子线段 实现计划（已锁定分级树导出）

> 本文件是会话内锁定计划树的**落盘副本**，供新会话/新窗口重建同一口径计划。
> 权威语义见 `DETACH_SHADOW_DESIGN.md`（设计稿）；本文件只携带任务分解与验收标准。
> 使用方式：新会话直接读本文件重建分级计划（commit_star → edit_plan L1 → edit_plan L2），
> 或按本文档顺序实现并以「验收标准」逐条核验。

---

## 北极星（purpose）

为制版用户在拆开/重挂线段、以角度基准做几何操作时，交付「拆开去耦合(L1 旋转不影响 L2)、挂新线链式随动(L3 旋转带动 L2)、90° 偏移保留(含公式)」的可靠行为——通过隐藏影子基准线(冻结克隆 Block + 标准双连接链)实现，以 R1–R8 语义断言 + v2→v3 序列化 + 全量 ctest 回归作为可观测验收。

## 需求（15 条）

1. R1 去耦合：拆开后 L1(本体)旋转/移动不影响 L2，影子=方向快照
2. R2 保关系：L2 偏移量(90°)含公式/变量原样保留，基准=影子
3. R3 随新宿主：L2 挂 L3 后 L3 旋转链式带动 L2(位置+角度)
4. R4 影子形态：不可见/不可选中/不可捕捉/不可作为用户连接目标
5. R5 生命周期：挂回 L1=删影子+活引用；L1 删→影子删→L2 独立；再拆开=影子保留结构复位(方向冻结当前值)
6. R6 影子角度可编辑：面板影子角度输入框恒可编辑；offset 公式锁定时旋转手势转改影子角度不碰公式
7. R7 长度不联动(影子=冻结克隆)
8. R8 旋转轴心：拆开态 L2 绕 p3 原地转；p3 保持方向变
9. 三入口统一：D键快拆/面板连接点拆开/拖跟随线拆散都走 detachWithShadow 门面
10. 挂载路由：挂到非本体→影子挂 L3(Att1 反算 Δ)+Att2 恢复位置；挂到本体→删影子+活引用
11. L3 被删→影子弹回拆开态(冻结当前方向)
12. 序列化：kFormatVersion 2→3 + migrateV2ToV3 + 影子字段读写 round-trip
13. 行为翻案：既有 angleOnly 拆开活引用语义按新语义改造(如 test_commands::setAttachmentAngleOnly_keepsFollowAngle)
14. 面板/状态条：影子角度行+连接到复合读数(经影子)+清除影子入口+ContextStrip 徽标
15. 文档回写：DECISIONS.md(拆开保留角度翻案+新条目「拆开影子基准」)/AGENTS.md 摘要/DOCS_INDEX 落地状态

## 非目标（7 条）

1. 不做影子可视化渲染/虚线示意(R4 纯隐藏)
2. 不做影子长度与 L1 联动(R7 拍板删)
3. MVP 不做组件级连接/多段块/曲线段/桥接线的影子（此类拆开降级旧 angleOnly 行为）
4. 不删除 preserveAngleRefOnReattach 代码路径(保留给旧档/显式活引用场景，仅不再作为新默认)
5. 影子不进入图层面板/块列表/HUD 计数/删除影响报告/测量引用
6. 不改动角度双模(°/⌒ 弧长)与两维独立(angleOnly/angleIndependent)既有机制
7. 不做影子显式序列化兼容旧 v2 档的迁移转换(旧档无影子，零迁移)

## 假设（7 条）

1. 影子=单线段块(两点+一段)，克隆自 leader 的 exit 线段，冻结几何
2. 挂载路由覆盖三处：ConnectGesture 连接手势 / ToolSelect tryReattachOnDragEnd 拖拽重挂 / 面板连接到输入回车重连
3. 拆开入口(命令层 SetAttachmentAngleOnlyCommand 等)经宏包裹：影子创建+Att2 换代合一步 undo
4. 旧档序列化的 angleOnly 附件不迁移(保持旧活引用行为)，仅新建拆开默认影子
5. 影子块=普通块参与求解(isShadow 仅影响交互/渲染/级联)，不新增 Resolver 逻辑
6. 现有测试翻案范围：test_commands(1)/test_select_wkey(2)/test_dialog_tabs(1)/test_serializer(1)/test_context_strip(1) 相关用例按新语义改造
7. 构建验证走 tools/build.bat + ctest；GUI 用例遵循 TestHelpers 三件套(禁固定 qWait 新增)

## 模式

`correct`（主体=可测量行为语义 R1–R8 + 数据格式 + 测试断言 → 以可复现断言与全量回归定义合格）

---

# 已锁定计划树（7 大类 / 20 小类）

## L1-1 影子实体与模型门面 ｜ 收官验:self

模型层引入影子概念：Block 新增 isShadow/shadowMasterBlockId；ParamDocument 新增门面 detachWithShadow/mountShadowTo/releaseShadowToDetached/removeShadow/findShadowOfMaster；本体删除级联删影子、挂载宿主(L3)删除弹回拆开态；deleteImpactReport 排除影子、本体删除影子级联计入。纯冻结克隆(长度常量，不联动)；不新增 Resolver 逻辑(链式附着现有已支持)。

组级验收：
- R1/R2/R5 门面行为可测：detachWithShadow 后旋转本体、跟随线方向不变且偏移量(含公式)原样
- findShadowOfMaster/removeShadow/级联规则满足状态机 ⑤⑥⑦(挂回/本体删/L3删)
- 引擎级 ctest(test_commands/test_serializer 改造后)绿；check_layering 绿

### L2-1.1 影子字段与冻结克隆 ｜ do:self verify:self
Concept: isShadow / 冻结克隆 / 锚点克隆

Spec：Block.h 新增 isShadow/shadowMasterBlockId；新增克隆助手(如 Block::cloneShadowOf(master, segId, anchorPointId) 或 ParamDocument 私有工具)：从 leader 的 exit 线段克隆单线段块(两点+一段)，方向/长度/位置全部固化为普通数值，锚点=克隆自旧 toPointId 的点。不引入任何新求解逻辑。

Accept：
- Block 编译含 isShadow/shadowMasterBlockId 字段且默认 false/null
- 克隆产物为单线段块：方向/长度/世界位置与 master exit 段逐位一致(断言 <1e-6)
- 锚点/线段 id 与 master 无引用关系(克隆=值拷贝，改 master 后影子不随动)

### L2-1.2 门面操作集合 ｜ do:self verify:self
Concept: 门面 / Att2 换代 / 反算保向

Spec：ParamDocument 新增 detachWithShadow(attId)/mountShadowTo(shadowId, toBlock, toPoint, toSeg)/releaseShadowToDetached(shadowId)/removeShadow(shadowId)/findShadowOfMaster(masterId)；每操作发出既有结构信号并 resolveAll。detachWithShadow：克隆影子+Att2 原地换代(toBlock/toPoint/toSeg→影子)+angleOnly=true+isLocked=false；mountShadowTo：Att1=影子挂目标(反算 Δ 保向)+Att2 angleOnly=false/isLocked=true；releaseShadowToDetached：删 Att1+Att2 angleOnly=true；removeShadow：删 Att2+影子块。

Accept：
- detachWithShadow 后：影子块存在(master=原 toBlock)、Att2 指向影子、angleOnly=true、offset(数值或公式)原样
- mountShadowTo 后：Att1.followerAngle=反算保向(挂载瞬间影子/L2 世界方向不变 <1e-6)、Att2 恢复位置钉点并重新焊接
- releaseShadowToDetached/removeShadow/findShadowOfMaster 行为逐条断言(结构信号+resolveAll 触发)

### L2-1.3 级联与影响报告 ｜ do:self verify:self
Concept: 级联删除 / 弹回拆开态 / 影响报告

Spec：removeBlock 级联：本体(master)被删→其影子块+Att1 一并删除；挂载宿主(L3)被删→Att1 清理、影子弹回拆开态(冻结当前方向)；deleteImpactReport：影子不计入线段/点计数，本体删除时影子级联计入系统清理；确保无悬空 Att2(影子没了→Att2 删除，L2 独立)。

Accept：
- 删除本体：影子+Att1+Att2 同步消失，跟随线 L2 转为独立线(方向冻结)
- 删除 L3：影子保留且 Att1 清理，影子方向=删除前最后值(冻结)
- deleteImpactReport 十项计数符合约定：影子不占位、本体级联清理正确入账

## L1-2 拆开与重挂路由 ｜ 收官验:self

把三处拆开入口(D 键快拆 ToolSelect::quickDetachSelection、面板连接点拆开 setAttachmentAngleOnly、拖跟随线拆散 endDrag)统一接入 detachWithShadow 门面(undo 宏一步)；挂载路由覆盖三处(ConnectGesture 连接手势、ToolSelect tryReattachOnDragEnd、面板连接到回车重连)：挂到非本体=影子挂 L3(Att1 反算 Δ)+Att2 恢复位置重新焊接，挂到本体=删影子+活引用；再拆开=releaseShadowToDetached 冻结当前方向。旧档/降级场景(组件/多段/曲线/桥线 leader)保持旧 angleOnly 行为。

组级验收：
- 三入口拆开均产生影子且 undo 一步回退(含影子删除)
- 三处挂载路由：挂 L3 生成 Att1+Att2 链、挂 L1 删影子转活引用、再拆开结构复位且方向冻结当前值
- 降级场景(组件级/桥线/multi-segment)行为与旧版一致(无影子、活引用)

### L2-2.1 拆开三入口接入 ｜ do:self verify:self
Concept: 三入口统一 / undo 宏 / 降级条件

Spec：ToolSelect::quickDetachSelection(D 键)、ToolSelect::endDrag(拖跟随线拆散)、ParamDocument::setAttachmentAngleOnly(面板连接点拆开)三路统一改走 detachWithShadow(仅线级、leader 非组件/桥线/多段/曲线时)；undo 宏一步回退(影子创建+Att2 换代+移动合一)。

Accept：
- 三入口拆开后均存在影子块(isShadow=true, master=原 leader)且 Att2 angleOnly=true
- undo 一步回到拆开前(影子消失、Att2 指向原 leader、锁定态还原)
- 降级条件(leader 为组件成员/桥线/多段块/曲线段)：无影子、行为与旧版逐位一致

### L2-2.2 挂载路由三处 ｜ do:self verify:self
Concept: 影子基准检测 / 挂 L3 vs 挂本体 / 链式跟转

Spec：ConnectGesture(attachToTarget/reattachAngleOnly)、ToolSelect::tryReattachOnDragEnd、面板「连接到」回车重连三处：目标线已有影子基准→挂 L3 走 mountShadowTo(不新建 Att2)；目标=影子本体(master)→删影子+Att2 转普通活引用(全连接)；校验排除自身/环照旧。

Accept：
- 挂 L3：生成 Att1(Δ=反算保向)+Att2 恢复位置重新焊接；L3 旋转→L2 链式跟转(断言 +30° 分解)
- 挂 L1(本体)：影子删除、Att2 toBlock=本体、angleOnly=false、活引用恢复
- 三处路由行为一致；非法目标(环/跨层/重复跟随)照旧拒绝且不残留半态

### L2-2.3 再拆开与降级保底 ｜ do:self verify:self
Concept: 结构复位 / 冻结当前值 / 旧档不迁移

Spec：挂载态再拆开(D 键/面板/拖拆)=releaseShadowToDetached：结构回到拆开态(Att1 删、Att2 angleOnly)，影子方向冻结当前值；降级路径(组件级/桥线/多段/曲线)全程不产生影子，保持旧 angleOnly/活引用语义；对旧档已序列化 angleOnly 附件不做迁移(加载后维持活引用)。

Accept：
- 再拆开：Att1 消失、Att2=angleOnly、影子方向=再拆开前最后姿态(冻结，不跳变)
- 降级场景与旧版行为逐位一致(旋转 leader 跟随线仍跟转)
- 旧档 angleOnly 附件加载后不产生影子(活引用保持)

## L1-3 序列化与迁移 ｜ 收官验:self

kFormatVersion 2→3；FormatMigration 追加 {2, "shadow-line", migrateV2ToV3}(旧档纯直通，保留既有影子旧键清理步骤不回归)；DocumentSerializer 读写 Block isShadow/shadowMasterBlockId(Optional since v3)，读入校验(影子必须单段、master 存在，否则降级普通不可见块+告警)；快照/undo 路径经 RawModelAccess 天然含影子。

组级验收：
- v2 旧档加载：无影子、零迁移行为、旧键清理步骤仍生效
- v3 round-trip：影子块+Att1+Att2 全字段往返一致；非法影子(缺 master/多段)降级+告警
- test_serializer/test_migration 绿(含新影子用例)

### L2-3.1 版本与迁移链 ｜ do:self verify:self
Concept: kFormatVersion / 直通迁移 / 旧键清理

Spec：FormatMigration.h kFormatVersion 2→3；FormatMigration.cpp 追加 migrateV2ToV3(纯直通，保留既有 v1/v2 对 shadowAnchorRotDeg/noFollowRotate/angleRefFrozen*/baselineOffsetDeg 的清理步骤不回归)；registry() 追加 {2, "shadow-line", &migrateV2ToV3}。

Accept：
- kFormatVersion=3 且应用链含 {2, shadow-line} (诊断可列出)
- v2 旧档加载：零影子、行为不变、旧键清理步骤仍被执行且告警可测
- 无缺口：链上 1→2→3 完整，缺少环节拒绝加载(既有契约)

### L2-3.2 影子读写与降级校验 ｜ do:self verify:self
Concept: Optional since v3 / 降级校验 / 快照兼容

Spec：DocumentSerializer 块序列化写出 isShadow/shadowMasterBlockId(Optional since v3)；反序列化读入并校验：影子必须单线段、masterBlockId 存在，否则降级为普通不可见块并追加告警(不拒绝加载)；快照(undo RawModelAccess)路径天然包含新字段。

Accept：
- v3 round-trip：isShadow/masterBlockId 往返一致(含多影子文档)
- 非法影子(多段/缺 master)加载=降级普通块+告警条目，不崩溃
- 序列化字段名与既有 Optional 惯例一致，旧档无该键=默认 false/null

### L2-3.3 往返与迁移用例 ｜ do:self verify:self
Concept: round-trip / v2 fixture / 守卫合规

Spec：tests/test_serializer.cpp 新增影子 round-trip 用例；tests/test_migration.cpp 新增 v2→v3 直通用例(v2 含旧键 fixture 加载后旧键被清理)；夹具不放仓库外。

Accept：
- test_serializer 影子用例绿(块+Att1+Att2 全字段往返)
- test_migration v2→v3 用例绿：加载无影子、旧键清理告警、v3 可回写
- 仓库内 fixture，无绝对路径(check_test_fixtures 绿)

## L1-4 旋转影子通道与交互排除 ｜ 收官验:self

ToolRotate：offset 公式/变量锁定(isAngleLocked)时旋转不再拒绝，改走影子角度通道(拆开态=影子 transform.rotation、挂载态=Att1 followerAngle)，不碰公式；拆开态旋转轴心=L2 p3(同步回写 origin 保 p3 不动，R8)。影子全交互面排除：ToolSelect/HitTester/SnapEngine/ConnectGesture/图层面板/HUD 计数过滤 isShadow；canvas BlockItem 对 isShadow 不创建图元(不可见 R4)。

组级验收：
- 公式锁定时旋转手势改影子角度且公式原样存活；数字 offset 时仍改 offset(影子不动)
- 影子不可选中/不可悬停/不可捕捉/不可作为连接目标/不可见(画布无图元)
- R8：拆开态旋转影子 L2 绕 p3 原地转(p3 位置不变，方向变)

### L2-4.1 旋转影子通道 ｜ do:self verify:self
Concept: isAngleLocked 改道 / 影子角度写目标 / 公式存活

Spec：ToolRotate：isAngleLocked(公式/变量锁定)时不再返回拒绝，改走影子角度通道——拆开态写影子块 transform.rotation，挂载态写 Att1.followerAngle(Δ)，公式/变量不烘焙不清除；offset 为数字时维持现状(写 followerAngle，影子不动)。

Accept：
- 公式锁 offset：拖转后影子角度变化(rotation/Δ)且 followerAngleFormula 原样
- 数字 offset：行为与现状一致(写 followerAngle，影子 rotation/Δ 不变)
- undo/Esc：影子角度回到拖前值(命令快照一致)

### L2-4.2 R8 轴心回写 ｜ do:self verify:self
Concept: p3 轴心 / origin 回写 / 接点轴心

Spec：拆开态旋转影子(或面板改影子角度)时 L2 绕 p3 原地转：旋转工具/门面同步回写 L2 origin 保持 p3 世界位置不动；挂载态旋转即绕接点(L3 p5=影子 p2.1=L2 p3)自然成立，不额外回写。

Accept：
- 拆开态拖转后：L2 p3 世界位置不变(<1e-6)，L2 方向按增量变化
- 挂载态拖转：接点不动、L2 绕接点转(位置+方向断言)
- 多帧拖转(预览)逐帧满足轴线不变(无抽搐)

### L2-4.3 交互面排除 ｜ do:self verify:self
Concept: 交互过滤 / 无图元 / 计数排除

Spec：isShadow 块从全部用户交互面排除：ToolSelect 选中/悬停、HitTester 命中、SnapEngine 捕捉、ConnectGesture 连接目标候选、ToolRotate 目标、图层面板/块列表、HUD/工具提示计数；canvas BlockItem/CanvasScene 对 isShadow 不创建图元(不可见)。

Accept：
- 影子不可被鼠标命中/选中/捕捉/选为连接目标(工具级断言)
- 画布无影子图元(场景 item 计数不含影子)
- W 循环/计数/图层面板不出现影子；现有关键回归用例不红

## L1-5 面板与状态条 ｜ 收官验:self

LinePropertyDialog 角度区新增「影子角度」输入行(恒可编辑，显示带符号折角，写目标=影子 rotation/Att1 Δ)；连接到行复合读数(挂载态「连接到:L3(经影子)」、拆开态「连接到:空+角度基准:L1(影子)」，不暴露影子 id)；「清除影子」入口附加基准点拆开旁；ContextStrip 状态徽标加影子基准标记；SegmentConnectionCard/SegmentRefCard 影子感知刷新。

组级验收：
- 影子角度输入对公式锁 offset 恒可编辑，写后 L2 方向随之变化且公式不损
- 复合读数/清除按钮在拆开态与挂载态正确切面；清除影子后 L2 变纯自由线
- GUI 用例(test_dialog_tabs/test_context_strip 改造+新增)走 TestHelpers 三件套全绿

### L2-5.1 影子角度输入行 ｜ do:self verify:self
Concept: 影子角度行 / 双态写目标 / 不受公式锁

Spec：LinePropertyDialog 角度区新增「影子角度」行(仅当 Att2 基准=影子时显示)：恒可编辑(不受 offset 公式锁影响)，显示带符号折角(复用 formatDegValue)，写目标=拆开态影子 rotation / 挂载态 Att1 followerAngle；编辑后 resolveAll+重绘。

Accept：
- offset 公式锁定时影子角度行仍可编辑且可写入
- 写入后 L2 方向按预期变化；公式 offset 不损
- 拆开态/挂载态写目标正确(影子 rotation vs Att1 Δ)

### L2-5.2 复合读数与清除入口 ｜ do:self verify:self
Concept: 复合读数 / 清除入口 / 刷新双入口

Spec：连接到行复合读数：挂载态「连接到:L3(经影子)」+角度基准读数；拆开态「连接到:空」+「角度基准:L1(影子)」；不暴露影子 id；「清除影子」按钮附加基准点拆开旁(删除影子→L2 纯自由线，Att2 移除)；SegmentConnectionCard/SegmentRefCard 影子感知刷新。

Accept：
- 拆开态/挂载态连接到行与角度基准读数正确切面(不显示影子 id)
- 清除影子：影子+Att2 移除，L2 独立(方向冻结)，按钮/读数复位
- 刷新路径(卡片 setTarget/refresh)双入口一致；GUI 用例走 TestHelpers 三件套

### L2-5.3 状态条徽标 ｜ do:self verify:self
Concept: 徽标 / 与四态徽标共存

Spec：ContextStrip 状态徽标：Att2 基准=影子时显示「影子基准」标记(与两维独立四态徽标并列不冲突)；tooltip 说明影子含义与入口(清除/影子角度)。

Accept：
- 拆开态与挂载态徽标正确出现；双拆开/独立角徽标共存不冲突
- tooltip 文案含影子说明；快捷键登记表如需同步更新

## L1-6 行为翻案与测试矩阵 ｜ 收官验:redteam

按新语义改造既有用例(test_commands::setAttachmentAngleOnly_keepsFollowAngle 等 5 处)并新增影子用例矩阵：R1(本体旋转不影响)/R2(偏移保留)/R3(L3 旋转链式带动)/R5(挂回/本体删/再拆开)/R6(公式锁+影子通道)/序列化 round-trip/每状态×入口×undo；测试夹具不落仓库外。

组级验收：
- 既有翻案用例逐一改判并注明新语义依据，不残留旧断言
- 新增用例覆盖 R1/R2/R3/R5/R6 与状态机全转换+undo/redo
- 相关 ctest 全绿(单类门禁内逐小类)

### L2-6.1 既有用例翻案改造 ｜ do:self verify:redteam
Concept: 行为翻案 / 改判依据 / 活引用消失

Spec：按新语义改造：test_commands::setAttachmentAngleOnly_keepsFollowAngle(删「旋转 leader A +30°→B 跟随」旧断言，改为 R1 断言：本体旋转不影响+偏移保留)、test_select_wkey::quickDetachKeyD/angleOnlyEndpointDragReconnects、test_dialog_tabs::reattachPreservesAngleRef、test_context_strip 相关用例；每条注明新语义依据(设计稿编号)。

Accept：
- 5 处既有用例按 R1/R2/R3 新语义改判，注释注明依据(DETACH_SHADOW_DESIGN §3/§6)
- 不残留与活引用语义冲突的旧断言(全库 grep 复核)
- 改造后相关测试文件全绿

### L2-6.2 影子矩阵新增 ｜ do:self verify:self
Concept: 语义断言 / 状态机覆盖 / 断言纪律

Spec：新增用例：R1(本体旋转不影响)/R2(偏移含公式保留)/R3(L3 旋转链式带动 +30°)/R5(挂回删影子、本体删→独立、再拆开结构复位冻结)/R6(公式锁+影子通道)/R8(绕 p3 轴心)/序列化 round-trip/状态机 ①→⑦ × 每入口 × undo/redo；夹具仓库内，遵循 waitUntil/settle 约定。

Accept：
- R1/R2/R3/R5/R6/R8 各至少一条逐位断言用例
- 状态机全转换覆盖(①→②→③→④→⑤/⑥/⑦)且 undo/redo 各断言一次
- 新用例无固定 qWait 占位、夹具仓库内(check_test_fixtures 绿)

## L1-7 文档回写与全量回归 ｜ 收官验:self

实现落地后回写：DECISIONS.md「拆开保留角度」翻案注明+新条目「拆开影子基准(2026-xx)」+旧「影子偏转·已删除」条目补指针；AGENTS.md 领域决策摘要同步；DETACH_SHADOW_DESIGN.md 状态改「已落地」；DOCS_INDEX.md 状态列同步；最后全量 ctest(含 check_layering/check_hardcoded_colors/check_test_fixtures)收尾。

组级验收：
- DECISIONS/AGENTS/DOCS_INDEX/DETACH_SHADOW_DESIGN 四处状态与内容同步(带源码引用 file:line)
- 全量 ctest 0 红(排除既有环境漂移基线)
- 无跨层违规/硬编码颜色/仓库外夹具(check_* 三守卫绿)

### L2-7.1 决策与摘要回写 ｜ do:self verify:self
Concept: 决策翻案 / 新条目 / 摘要同步

Spec：DECISIONS.md：「拆开保留角度」条目追加翻案说明(新语义=影子冻结基准，引用 DETACH_SHADOW_DESIGN §5/§6)；新增「拆开影子基准(2026-xx)」条目(核心：隐藏克隆+双连接链+生命周期)；「影子偏转·锚点推导」旧条目补指针(被本设计翻案回归+链式扩展)；AGENTS.md 领域建模决策摘要同步。

Accept：
- DECISIONS.md 三处更新齐全(翻案说明/新条目/旧条目指针)，带源码引用 file:line
- AGENTS.md 摘要与 DECISIONS 一致(压缩版只留一行)
- 无与源码矛盾的事实残留(核对 Block.h/ParamDocument 新字段存在)

### L2-7.2 设计文档状态与索引 ｜ do:self verify:self
Concept: 状态翻转 / 索引同步

Spec：DETACH_SHADOW_DESIGN.md 状态「📝 设计稿」→「✅ 已落地(2026-xx)」，补实现摘要(落地范围/差异)；DOCS_INDEX.md 状态列同步；TROUBLESHOOTING.md 如有新坑(迁移/级联)按主题追加。

Accept：
- DETACH_SHADOW_DESIGN.md 状态已落地+实现差异说明
- DOCS_INDEX.md 状态列同步；TROUBLESHOOTING.md 无缺失的新坑记录

### L2-7.3 全量回归收尾 ｜ do:self verify:self
Concept: 全量回归 / 守卫脚本 / 基线对照

Spec：全量 ctest(含 check_layering/check_hardcoded_colors/check_test_fixtures)确认 0 红(排除既有环境漂移基线红名单)；无跨层违规/硬编码颜色/仓库外夹具；构建走 tools/build.bat，日志落盘转码复核。

Accept：
- 全量 ctest 0 红(白名单外)；check_* 三守卫绿
- 构建/测试日志留档可复核
- 回归基线未新增(与 CONVENTIONS 名单对照)

---

## 新窗口使用指引

1. 先读 `AGENTS.md`（仓库规则）→ `DETACH_SHADOW_DESIGN.md`（设计稿，权威语义）→ 本文件（计划树与验收口径）。
2. 重建分级计划（commit_star 引本文「北极星/需求/非目标/假设/模式」→ edit_plan L1 → edit_plan L2），或直接按本文件顺序实现。
3. 每小类完成 → mark_task 打卡；组级验收按 verify 字段执行（L1-6 收官走 redteam 裁决；L1-7 收尾全量 ctest）。
