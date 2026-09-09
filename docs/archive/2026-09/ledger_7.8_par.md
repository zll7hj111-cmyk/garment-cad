#### 7.8.2 src/parametric + src/document（40 条：已修 4 / 仍存在 33 / 刻意不改 3）

**已修（4）**
- **PAR-P1-1** — 唯一诊断去重口 `src/parametric/ResolveDiagnostics.h:13-20` `appendDiagnostic`（新收口头），`src/parametric/Resolver.cpp:28` 改调；src/parametric 内 `report(` 命中 0。
- **PAR-P1-11** — U3 `commitRawChange`：`src/parametric/ParamDocumentResolver.cpp:253-257`，12 处改调（例 `src/document/commands/AttachmentLifecycleCommands.cpp:203`）。
- **PAR-P1-23** — G4 守卫下 src/ 内 `M_PI` 命中 0（tests/ 内 67 处，超本次范围）；`src/geometry/Angle.h:10` `kPi = std::numbers::pi`。
- **PAR-P1-27** — U1 收口：`src/tools/LineFactory.cpp:315,319` `normalizeDeg360`、`:537` `cad::param::followerAngleToStorage`。

**仍存在（33）**
- **PAR-P1-2** — refWorld 仍 4 份：`src/parametric/ResolverAttachment.cpp:36-77` 自算；`src/parametric/ParamDocumentAttachments.cpp:631` 另一入口。
- **PAR-P1-3** — `src/parametric/ResolverAttachment.cpp:189-204` 与 `:207-231` 两分支各自 solve/moved。
- **PAR-P1-4** — 模式→角度换算 3 份：`src/parametric/ResolverAttachment.cpp:97`、`src/parametric/ParamDocumentBlocks.cpp:391`、`src/ui/SegmentAngleCard.cpp:130`。
- **PAR-P1-5** — 部分收口：反算已统一走 `src/parametric/FollowerAngle.h`；clear 集仍 3+3 份：`src/parametric/ParamDocumentAttachments.cpp:284,331,548`、`src/document/commands/AttachmentAngleCommands.cpp:66-69,123-126,190-193`。
- **PAR-P1-6** — 部分收口：求交内核已统一 `cad::geo::raySegmentOrCurveIntersect`（`src/parametric/BlockResolve.cpp:387`、`src/parametric/ResolverIntersection.cpp:153`）、世界角语义分叉已修、退化自举均调 `Block::polarEndpointCycleSeed`；但两个约 100 行求解体仍各一份：`src/parametric/BlockResolve.cpp:295-402` vs `src/parametric/ResolverIntersection.cpp:48-177`。
- **PAR-P1-7** — 弦坐标数学 2 份：`src/parametric/BlockResolve.cpp:404-441`（正算）与 `src/parametric/ParamDocumentResolver.cpp:604`（反算）。
- **PAR-P1-8** — 级联快照 3 份：`src/document/commands/BlockLifecycleCommands.cpp:59`、`src/document/commands/ComponentCommands.cpp:89`、`src/parametric/ParamDocumentBlocks.cpp:496`。
- **PAR-P1-9** — `src/parametric/BlockQuery.cpp:57-77` 与 `:160-174` 两重载各复制曲线切线分支。
- **PAR-P1-10** — 部分迁移：命令层仍手写 11 处 `touchGeometry`：`src/document/commands/SegmentPropertyCommands.cpp:81,92,142,152,172,353,362`、`src/document/commands/BlockTransformCommands.cpp:191,207`、`src/document/commands/BreakExecution.cpp:190`、`src/document/commands/ReverseSegmentCommand.cpp:433`；`src/document/commands/CurveCommands.cpp` 8 处已改 `touchAndResolve`（:52,70,118,132,220,232,322,336）。`src/document/commands/SegmentPropertyCommands.h:52-53` 注释说明 SetSegmentExtendCommand 显式 touchGeometry 属画布重绘铁律。
- **PAR-P1-12** — `src/document/commands/AttachmentSlideCommands.cpp:78-108` redo 不 `resolveAll`；`:112-162` 两侧都 resolve。
- **PAR-P1-13** — `src/document/commands/VariableCommands.h` 21 个手写 `QUndoCommand` 子类（`:19,34,49…337`），无模板基类。
- **PAR-P1-14** — `src/document/commands/LayerCommands.cpp:106-133` 与 `:137-193` 两份移动图层命令。
- **PAR-P1-15** — 名字唯一性校验仍缺失：`src/parametric/VariableStore.cpp:22`、`src/parametric/LayerRegistry.cpp:55`、`src/parametric/ParamDocumentParameters.cpp:76` 直接覆盖；全 src `nameExists|isNameTaken|uniqueName|ensureUniqueName` 命中 0。
- **PAR-P1-16** — `src/parametric/VariableStore.cpp:57-69` `findVariable`、`:117-129` `findFormula` 各 mutable/const 两份。
- **PAR-P1-17** — 图层下限保护两份：`src/parametric/LayerRegistry.cpp:66-68` 与 `src/parametric/ParamDocumentIndexes.cpp:40-45`（均 `if (n <= 2) return;`）。
- **PAR-P1-18** — chord 钳制 4 处：`src/app/ContextStripEdit.cpp:99`、`src/ui/SegmentAngleCard.cpp:435`、`src/tools/ConnectGestureAngleSession.cpp:168`、`src/tools/RotateSession.cpp:326`；`src/geometry/Angle.h:83` 内部又 clamp 一次。
- **PAR-P1-19** — `src/document/DocumentSerializer.cpp:441,449` 枚举裸 int 直写，`:478-481,492-495` 手写钳制（与 `:70-129` 四组字符串表模式不统一）。
- **PAR-P1-20** — `src/document/DocumentSerializer.cpp:133-138` 与 `:663-677` 两组枚举仍手写映射；N3 已登记未迁移。
- **PAR-P1-21** — `kFormatVersion=4`（`src/document/FormatMigration.h:38`）与 22 处「Optional since vN」注释冲突（`src/document/DocumentSerializer.cpp:292` 等，v2..v12）。
- **PAR-P1-22** — 读端默认值复制模型默认值：`src/document/DocumentSerializer.cpp:248,251,256,270` 对应 `src/parametric/ParamPoint.h:84,91,116,133`。
- **PAR-P1-24** — `src/document/commands/BreakFinish.cpp:222` `normalizeDeg180`（Chord）vs `:229` `normalizeDeg360`（Angle）。
- **PAR-P1-25** — 「闭合基准 180°−x」写值无共享 helper：`src/tools/RotateCopyGesture.cpp:85,195,241,260`。
- **PAR-P1-26** — `src/tools/ConnectGestureAngleSession.cpp:139,158,170,175,185` 五处手写写集。
- **PAR-P1-28** — `src/document/commands/SegmentPropertyCommands.cpp:81/84,92/95,142/144,152/154,172,353/354,362/363` 手写 `touchGeometry+resolveAll` 对（与 PAR-P1-10 同源）。
- **PAR-P1-29** — `src/document/commands/EndpointCommands.cpp:294-296,306-308,352-354,375-377` 重复前置守卫（`if (!m_doc) return; if (!blk) return;`）。
- **PAR-P1-30** — `src/document/commands/AttachmentAngleCommands.cpp:12-25` 手写 11 字段 `restoreAngleState`；`src/document/commands/ReverseSegmentCommand.h:86`、`src/document/commands/SegmentPropertyCommands.h:150` 同类。
- **PAR-P2-1** — 部分已修：名称已收口为 `src/parametric/LayerRegistry.cpp:5-6` 常量（`kDefaultAuxLayerName`/`kDefaultWorkingLayerName`），但仍是 `QStringLiteral` 无 `tr()`（项目中文单语，i18n 未列入本次审计）。
- **PAR-P2-3** — `src/parametric/BlockResolve.cpp:49-51` 注释仍写「sub-nanometre」，`:54` 阈值 `cad::geo::kGeomEpsLoose`=1e-6 且比较的是**平方**距离（等效 1e-3 mm=1 µm，差约 1000×）。
- **PAR-P2-4** — `src/parametric/ParamDocumentAttachments.cpp:627-628` 注释引用 `Resolver.cpp:725-769`，该文件仅 354 行（失效行号引用）。
- **PAR-P2-7** — `src/parametric/VariableStore.cpp:45-55` `updateVariable` 未命中仍 `emit variablesChanged()` + `recomputeFormulas()`。
- **PAR-P2-8** — `src/document/commands/AttachmentAngleCommands.cpp:61-62` 与 `:73` 两分支都写 `slideMode = None`。
- **PAR-P2-9** — `src/document/commands/AttachmentAngleCommands.cpp:232-238` redo 只改 `fromPointId`；`:240-247` undo 另 `restoreAngleState`（redo/undo 不对称）。
- **PAR-P2-10** — `src/document/commands/AttachmentAngleCommands.cpp:50-52` 整份 `m_oldAtt = *a`；`:12-25` 仅恢复 11 字段（快照过宽）。

**刻意不改（3）**
- **PAR-P2-2** — `src/parametric/Attachment.h:142` `bool isLocked = false;` 与 `src/parametric/ParamDocumentAttachments.cpp:113,160` 强制 true、读端 `src/document/DocumentSerializer.cpp:487` 给 false 是**已注释定案的设计**：`src/parametric/Attachment.h:142-152` 写明「新建连接强制 true；字段默认 false 仅供反序列化/undo 回放与解焊态；旧档案 false 保持解焊（不迁移）」。
- **PAR-P2-5** — `src/document/DocumentFile.cpp:23` `kAppVersion="0.1.0"` 与 `src/document/FormatMigration.h:38` `kFormatVersion=4` 是两个不同物理量：`appVersion` 写入文档供诊断（`src/document/DocumentFile.cpp:34`）、`version` 供迁移链（`:33`），不合并。
- **PAR-P2-6** — `src/parametric/ParamDocumentStores.cpp:27-128`（变量/公式/组/图层/联动/测量 CRUD）与 `:133-164`（`RawModelAccess`）纯转发仍在，但 `:25` 注释「子域存储门面转发 (2026-08 拆分)」即设计意图，`docs/archive/plans/FILE_SPLIT_PLAN_V2.md:12` 记录「门面转发保障既有调用者零感知」；审计 `docs/archive/2026-09/AUDIT_consistency_duplication.md:383`（原路径 `docs/`，已移档）前言亦已把它列入「已剔除已收口项」——P2 段落为重复登记，以前言为准。
