#### 7.8.4 src/tools（42 条：已修 11 / 仍存在 31，其中 3 条部分收口 / 刻意不改 0）

**已修（11）**
- **TOOL-P1-4** — U1：src/tools 内 `M_PI` / `std::numbers` / `3.14159` 全 0 命中；唯一源 `src/geometry/Angle.h:10` `kPi`、`:15` `degToRad`。
- **TOOL-P1-9** — N2：半径单源 `src/tools/InteractionTolerances.h:19-37`；`src/tools/SnapEngine.h:43,48` 线身 8 / 点 12 分档；重复的 `kConnectGrabRadius` 已删。
- **TOOL-P1-11** — src/tools 裸 `QColor` 仅 3 处且由 token 派生：`src/tools/ConnectOverlapResolver.cpp:213,246`（`:208` snapNodeColor）、`src/tools/MarqueeGesture.cpp:85`（`:81`）。
- **TOOL-P1-12** — U8：`src/tools/MarqueeGesture.cpp:129`、`src/tools/MultiRotateSession.cpp:34`、`src/tools/ToolRotate.cpp:322` 全走 `isInteractiveBlock`（`src/tools/HitTester.h:21`）。
- **TOOL-P1-15** — U4：src/tools 内 `QString::number` / `asprintf` / `'f',` 全 0 命中，改走 `Units` format*（例 `src/tools/IntersectionToolVisuals.cpp:215`）。
- **TOOL-P1-22** — U1：src/tools 内 `std::numbers` 0 命中；`src/tools/IntersectionAngleAim.h:27,30,32` 用 `cad::geo::radToDeg`。
- **TOOL-P1-25** — src/tools 内 `toDouble` / `toFloat` 0 命中。
- **TOOL-P1-26** — N2：阈值全入 `src/tools/InteractionTolerances.h:19-54`；src/tools 仅剩 `src/tools/SnapEngine.cpp:42` `kTieDistSq`，旧散点消失。
- **TOOL-P1-32** — U10：`src/tools/MultiRotateSession.cpp:66-71` `isReleasedAcrossSelection(CrossSelectionOp::Rotate)` + `:76` `resolveForDrag`；`src/tools/SelectDragController.cpp:103` 同机制。
- **TOOL-P2-4** — U4：`src/tools/ToolSmartPen.cpp:636` `formatLength` / `:640` `formatDegValue`；`src/tools/IntersectionToolVisuals.cpp:215,225,228`。
- **TOOL-P2-5** — `kOverlapSelectThresholdPx` 全仓 0 命中（死常量已删）。

**仍存在（31；3 条为部分收口）**
- **TOOL-P1-1** — `src/tools/ToolRotate.h:38` `RotatePhase`、`:122` `phase()` 仅 tests 读；`src/tools/ToolRotate.cpp` 17 处 `m_phase` 写点（`:105,113,…,822`）。
- **TOOL-P1-2** — 基准角换算未走 `effectiveAngleRefWorld`：`src/tools/RotateAimSnap.cpp:30` `refWorldRad + kPi - degToRad(...)`；`src/tools/RotateDragMath.cpp:45,94` 同式。
- **TOOL-P1-3** — 闭合基准 `180.0 -` 仍 15 处：`src/tools/LineFactory.cpp:319`、`src/tools/RotateCopyGesture.cpp:85,195,241,260,277,325`、`src/tools/RotateSession.cpp:617,673`，无 `closedBasisAngle()`。
- **TOOL-P1-5** — 手写 while 环绕 6 对：`src/tools/RotateAimSnap.cpp:97-98,188-189`、`src/tools/RotateSession.cpp:661-662,676-677,686-687`、`src/tools/ToolPlacePoint.cpp:279-280`。
- **TOOL-P1-6** — 15.0 硬编码：`src/tools/RotateDragMath.cpp:27,37,46,50,57`；45.0 于 `src/tools/IntersectionAngleAim.h:39,42`、`src/tools/ToolPlacePoint.cpp:285`；无 `snapDeg` 常量。
- **TOOL-P1-7** — 仍存在（部分） — U8 仅覆盖 3 处；`src/tools/RotateInputTracker.cpp:29-50`、`src/tools/RotateAimSnap.cpp:76,163`、`src/tools/SelectHoverFeedback.cpp:74-75` 仍手写过滤（无 `isShadow`）。
- **TOOL-P1-8** — 仍存在（部分） — 手写 `toWorld(pt.resolvedPos)` 约 18 处：`src/tools/RotateAimSnap.cpp:77,164`、`src/tools/MarqueeGesture.cpp:134-135`、`src/tools/ToolIntersection.cpp:316-317` 等。
- **TOOL-P1-10** — 裸字面量 3 处：`src/tools/RotateAimSnap.cpp:146` `r0 < 1e-4`、`src/tools/RotateCopyGesture.cpp:326` `< 1e-6`、`src/tools/RotateGizmo.cpp:83` `<= 1e-4`。
- **TOOL-P1-13** — `src/tools/RotateCopyGesture.cpp` begin `:23-113` vs convert `:115-231`、applyAngle `:233-250` vs applyFormulaValue `:252-265`；LineFactory 三骨架未合。
- **TOOL-P1-14** — `src/tools/RotateSession.cpp:656-663` 与 `:681-688` 逐行同；`:302` `cmToMm`+resolveAll vs `:254` `writeFollowerAngleForMode`。
- **TOOL-P1-16** — `src/tools/ConnectGesture.cpp` 三半径并存（`:204` hover 8 / `:259` snap 7.5 / `:521` grab 10）+ 局部守卫 `:189,359,446`。
- **TOOL-P1-17** — `src/tools/ConnectGestureAttach.cpp:112-122` 手写 refWorld 基准，与 `src/parametric/ParamDocumentAttachments.cpp:636-651` `effectiveAngleRefWorld` 同码未共用。
- **TOOL-P1-18** — `src/tools/LeaderCandidatePicker.cpp:115-116` 绕过 `effectiveAngleRefWorld`；`:36` `tol = 12px` vs `:137-138` `hoverRadiusPx` 8px。
- **TOOL-P1-19** — `src/tools/ToolSelectHitTest.cpp:25,33` 各调 `blockHitsAtScene`；`src/tools/ToolSelect.cpp:455+459`、`:494+498` 成对；`:75-110` 内联资格判定。
- **TOOL-P1-20** — `src/tools/ToolPlacePoint.cpp:116,120,189,228` 仍传 `ignoreLayerFilter=true`，`:395` `AddAuxPointCommand` 建附件（与 `src/tools/SnapEngine.h:60-63` 注释前提冲突，建议优先）。
- **TOOL-P1-21** — theta 重算三份：`src/tools/ToolIntersection.cpp:354,410`、`src/parametric/ResolverIntersection.cpp:139`；目标线段解析两份 `src/tools/ToolIntersection.cpp:316-320` vs `:401-406`。
- **TOOL-P1-23** — `src/tools/RotateGizmo.cpp:21,39` `(void)zoom`、`:83` `<= 1e-4` vs `src/tools/RotateDragMath.cpp:66` `<= 0.01`；`:29,46,64` 三段 show/hide 相同。
- **TOOL-P1-24** — `src/tools/ToolSmartPen.h:175` 声明 `toWorldAngleDeg` 无定义（实现仅 `src/tools/SmartPenStrokeInput.cpp:96`）；`:211-213` 与 `src/tools/LeaderCandidatePicker.h:58-60` 成员重复。
- **TOOL-P1-27** — `src/tools/OverlapDisambiguationController.cpp:83-85` 按距离排序 vs `src/tools/ConnectOverlapResolver.cpp:37-51` 按文档顺序 push。
- **TOOL-P1-28** — `src/tools/OverlapDisambiguationController.cpp:133-135` 与 `:168-170` 逐字同 `layers()` 找名循环。
- **TOOL-P1-29** — `BatteryCandidate` 两份：`src/tools/OverlapDisambiguationController.cpp:277-298` vs `src/tools/ConnectOverlapResolver.cpp:271-294`（两个 HUD）。
- **TOOL-P1-30** — `src/tools/ToolCurveEditHandles.cpp:106-107` 与 `:190-191` 逐字同 `toWorld(pLocal ∓ tanIn/3.0)`。
- **TOOL-P1-31** — `src/tools/SelectDragController.cpp:27` 只取 selection，vs `src/tools/ToolSelect.cpp:465-467` lockedClosure+closure；`src/tools/SelectDragController.h:39` 注释仍写闭包。
- **TOOL-P1-33** — 三套收尾命令：`src/tools/SelectDragController.cpp:169-199`、`src/tools/CopyDragController.cpp:94`、`src/tools/MultiRotateSession.cpp:161`。
- **TOOL-P1-34** — 三套刷新：`src/tools/CopyDragController.cpp:59,100,133` `refreshAllBlockItems`、`src/tools/MultiRotateSession.cpp:120` `syncBlockPositions`、SelectDrag 靠 `src/tools/ToolSelect.cpp:700`。
- **TOOL-P1-35** — `src/tools/ToolSelect.cpp:595` `blockHitsAtScene` 后 `:600` `cursorShapeFor` → `src/tools/SelectHoverFeedback.cpp:63` `findEndpointNear` 再全文档扫点。
- **TOOL-P2-1** — src/tools `setZValue` 27 处无中央表：`src/tools/MarqueeGesture.cpp:86`=9999、`src/tools/IntersectionToolVisuals.cpp:41`=100、`src/tools/ToolCurveEditAnchor.cpp:64`=103 等。
- **TOOL-P2-2** — 仍存在（部分） — 局部守卫 12 处仍在：`src/tools/ConnectGesture.cpp:189,359,446`、`src/tools/ConnectOverlapResolver.cpp:201,232`、`src/tools/ToolSelect.cpp:429,436,673,751` 等。
- **TOOL-P2-3** — 默认角 90.0 三处：`src/tools/ToolIntersection.cpp:531-532`、`src/parametric/ParamPoint.h:91`；提示文案逐字两份 `src/tools/ToolPlacePoint.cpp:222,261`。
- **TOOL-P2-6** — 9 个头文件 `reinterpret_cast<const char*>(u8"…")`（`src/tools/ToolSelect.h:75`、`src/tools/ToolRotate.h:120` 等）+ 十六进制转义 6 处（`src/tools/ToolMeasure.cpp:416` 等）。
- **TOOL-P2-7** — `src/tools/SelectDragController.h:33,37,61` `StateFn m_stateFn` vs `src/tools/CopyDragController.h:59` `m_setState`；`src/tools/ConnectGesture.h:129,195` `m_setState`。

**裁决备注**
- TOOL-P1-20 是本模块唯一「注释前提与实现直接冲突」项（`ignoreLayerFilter=true` vs `src/tools/SnapEngine.h:60-63` 前提），若续做建议排在重复实现之前。
- 仍存在项集中在四条主线：旋转会话相位机/角度基准/闭合基准（P1-1/2/3/5/6/14/23）、吸附过滤与命中扫描未走 U8/U9（P1-7/8/19/35）、同构重复实现（P1-13/27/28/29/30/33/34）、z 序与 zoom 守卫（P2-1/2）。
