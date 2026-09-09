# WildWind Pattern — 全项目「属性一致性 / 重复实现」审计报告

> 范围：`src/` 349 个 .h/.cpp（59,337 行）、`tests/` 55 个测试文件、`tools/` 7 个守卫脚本。
> 方法：全库正则统计 + 逐点人工取证 + 4 个只读子代理分模块审计（src/tools、src/ui、src/parametric+document、src/canvas+geometry+app）。
> 原则：项目内已有大量「收口」注释（2026-08-28 收口 A3/A6、2026-09 审核 F0/F1），本报告只列**仍然存在**的问题。

---

## 0. 结论摘要

| 级别 | 数量 | 一句话 |
|---|---|---|
| P0 | 4（跨模块） | 颜色守卫**假通过**（含 `lstrip` 使 canvas 豁免失效）；同一物理角在同一张卡/同一手势上显示两个不同数字；Theme↔CanvasStyle 手工同步无任何校验；旋转手势三套读数且写入域随模式切换 |
| P0（模块内） | 36 | §4 四个子代理合计：ui 8、parametric+document 7、canvas+geometry+app 10、src/tools 11 |
| P1 | 8+（跨模块） | 同一属性在多处各定义一份（图层名/角度域/容差/精度/序列化键/数字色）；4 个子代理合计再报 122 条模块内问题（见 §4） |
| P2 | 5 类（跨模块） | 562 组跨文件逐字重复行 + 194 组 4 行以上重复块、4 套 HUD 字体、73 组中文文案重复 |

> **口径说明**：模块内 P0 = ui 8 + parametric 7 + canvas 10 + tools 11 = **36**；其中 TOOL-P0-1/P0-3 与 §P0-2、PAR-P0-1 同根（followerAngle 双域/三域），与跨模块 §1 的 4 条 P0 也有重叠，去重后**独立缺陷约 33 条**。

**7 个守卫脚本全部 exit=0**（`check_hardcoded_colors.py`、`check_layering.py`、`check_file_size.py`、`check_bool_flags.py`、`check_test_fixtures.py`、`check_header_classification.py`、`check_test_split.py`）——本报告列出的**全部问题均未被守卫捕获**。

---

## 1. P0 级

### P0-1 颜色守卫脚本存在结构性盲区，输出「已收口」是假结论

- `tools/check_hardcoded_colors.py:46`：`HEX_COLOR = re.compile(r'#[0-9A-Fa-f]{6}\b')` —— **只查 hex 字面量**，不查 `QColor(r,g,b)` 与 `Qt::<named>`。
- 实跑输出：`hardcoded colors OK: no undeclared hex colors in src/ (all widget colors come from ThemeTokens / CanvasStyle)` → 与事实不符。
- 全库统计：`QColor(数字)` **115 处**，其中 53 处在豁免的 token 表内（`src/canvas/CanvasStyle.cpp` 31、`src/canvas/CanvasStyle.h` 22）→ **token 表外仍有 62 处数字 QColor**；hex 字面量 108 处；`Qt::<named>` 4 处（`src/ui/CardBase.cpp:90`、`src/ui/CardBase.cpp:215`、`src/ui/FormulaGroupHeader.cpp:71`、`src/ui/VariablePanel.cpp:168`）。
- 跨文件重复的数字色 **11 组**（同一 RGB 出现在 ≥2 文件）：

| RGB | 次数 | 位置 |
|---|---|---|
| rgb(38,166,154) teal | 9 | `src/canvas/overlay/TransientOverlay.cpp:239/242/252/266`、`src/canvas/BlockItemPainter.cpp:229`、`src/tools/ConnectOverlapResolver.cpp:207/211/238/243` |
| rgb(20,20,19) HUD 阴影 | 3 | `src/canvas/HudItem.cpp:91`、`src/canvas/OverlapBatteryHud.cpp:238/308` |
| rgb(176,171,160) / 0x9E9E9E | 2+2 | `src/canvas/BlockItemPainter.cpp:62` 与 `src/canvas/CurveItem.cpp:153`（**逐字相同**） |
| rgb(100,100,100) | 2 | `src/canvas/BlockItemPainter.cpp:86` / `src/canvas/CurveItem.cpp:168` |
| rgb(0,110,60) | 2 | `src/canvas/BlockItemPainter.cpp:156` / `src/canvas/CurveItem.cpp:212` |
| rgb(47,111,237) 蓝 | 2 | `src/canvas/CanvasScene.cpp:158` / `src/tools/ConnectOverlapResolver.cpp:176` |
| rgb(255,152,0) amber | 2 | `src/tools/ToolAngleMeasure.cpp:155` / `src/tools/ToolMeasure.cpp:306` |
| rgb(233,30,99) ETCAD pink | 2 | `src/canvas/BlockItemPainter.cpp:216` / `src/tools/ToolCurveEditAnchor.cpp:62` |
| rgb(176,176,176) | 2 | `src/ui/CardBase.cpp:215` / `src/ui/FormulaGroupHeader.cpp:71` |
| rgb(30,30,30) 默认线色 | 2 | `src/canvas/BlockItemPainter.cpp:205` / `src/parametric/Segment.h:80` |
| rgb(250,249,245) canvasBg | 3 | `src/canvas/CanvasStyle.h:106`（token）与 `src/canvas/CanvasView.cpp:74/101` 硬编码同值 |

- **注意**：CanvasStyle 里的 `attachmentNodeColor = QColor(42,123,136)` 与上面 9 处 teal `QColor(38,166,154)` **不是同一个颜色**，即同一语义（吸附节点）已存在两套色值。
- `src/canvas/HudItem.cpp:14-16` 的 `kPillBg(250,249,245,245)`/`kPillBorder(213,208,197,220)`/`kPillFg(20,20,19)` 与 `CanvasStyle::hudBackground/hudText/crosshairColor` 同值重复；`src/canvas/HudItem.cpp:110-114` 的暗色胶囊配色 `QColor(31,30,29,245)/QColor(236,233,226)/QColor(77,73,67,220)` **在 token 表中根本不存在**（hudBackground/hudText 只有亮色）。
- QSS 里的 `rgba()` 硬编码同样漏检：`src/ui/FormulaCard.cpp:355`、`src/ui/PointRefEdit.cpp:277`、`src/ui/SegmentAlignPointCard.cpp:134`（`rgba(220,38,38,32)` 重复 2 处）、`src/ui/ConditionDialog.cpp:110-112`。

**建议**：扩展 `tools/check_hardcoded_colors.py`，把 `QColor\(\s*\d` 与 `Qt::(black|white|gray|red|...)` 纳入检测，豁免集仍只保留 `Theme.cpp/.h`、`CanvasStyle.cpp/.h`；然后把上表 11 组色提升为 token。

---

### P0-2 同一个物理角度，在同一张卡片上显示两个不同的数字（用户举例命中）

`src/ui/SegmentAngleCard.cpp` 对**同一条线段**：

- 输入框回填：`SegmentAngleCard.cpp:185` `return cad::geo::Units::formatDegValue(cad::geo::normalizeDeg180(value));` → 折角域 (−180,180]
- 同卡副标签「= 绝对角度」：`SegmentAngleCard.cpp:563` `const double absDeg = cad::geo::normalizeDeg360(refWorldDeg + 180.0 - constDeg);` → [0,360)
- 同卡副标签「= 世界角度」：`SegmentAngleCard.cpp:596-598` 走 `worldOrNominalDegOfSegment`（`SegmentAngleCard.cpp:43-44` 用 `normalizeDeg360`）
- 同卡跟随角读数：`SegmentAngleCard.cpp:80-82` `evaluatedFollowValueText` 用 `normalizeDeg360`

→ 存储角 270° 时，输入框显示 **-90**，紧邻的标签显示 **= 绝对角度 270°**。这就是用户描述的「同一个物理角度显示出两个数字」。

`src/app/ContextStripDisplay.cpp` 同一个状态条内同样混用：

| 字段 | 行 | 归一化 |
|---|---|---|
| 基准角输入 | `ContextStripDisplay.cpp:79-91` | `normalizeDeg180` |
| 线段角输入 | `ContextStripDisplay.cpp:119-122` | `normalizeDeg360` |
| 跟随角 | `ContextStripDisplay.cpp:107-108` | `normalizeDeg180` |

**往返折叠导致「输入框数字 ≠ 用户输入的数字」**：
`src/app/ContextStripDisplay.cpp:130-141` `foldedArcDisplay`、`:143-154` `foldedChordDisplay` 把存储弧长/弦长经 `arcMmToDeg → normalizeDeg180 → degToArcMm` 往返，超过 180°（弧长 > πr）时输入框显示负值。`src/ui/SegmentAngleCard.cpp:176-183` `formatAttachmentDisplayValue` 是同一套逻辑的**逐行近似重复**。

**契约依据**：`CONVENTIONS.md:25` 规定存储域 = `normalizeDeg360`（[0,360)），带符号折角显示 = `normalizeDeg180`（(−180,180]）。→ 同卡内 0~360 副标签属违反既有契约。

**测试已锁定（改前必须先改契约）**：
- `tests/test_context_strip.cpp:1269-1271` 锁定 `baseAngleEdit()->text() == "-90"`（270° 世界角）
- `tests/test_dialog_tabs_angle_conn.cpp:331/367/426` 锁定副标签 `followLbl->text() == "= 2°"`
- `tests/test_curve.cpp:656-698` 锁定 `normalizeDeg180/360` 语义与 inf/nan→0

**建议**：`Angle.h` 增加显式两域 API（`toStorageDeg()` / `toDisplayFoldDeg()`），卡片内所有读数强制走同一域；输入框与副标签同域或副标签显式标注「存储域」。
**全库量化：两套归一化在同一文件内混用（12 个文件）**

| 文件 | normalizeDeg180 | normalizeDeg360 |
|---|---|---|
| `src/ui/SegmentAngleCard.cpp` | 4 | 8 |
| `src/app/ContextStripDisplay.cpp` | 6 | 2 |
| `src/tools/RotateSession.cpp` | 11 | 3 |
| `src/tools/RotateDragMath.cpp` | 3 | 3 |
| `src/tools/RotateAimSnap.cpp` | 1 | 4 |
| `src/parametric/FollowerAngle.h` | 2 | 2 |
| `src/tools/LineFactory.cpp` | 1 | 2 |
| `src/document/commands/BreakFinish.cpp` | 1 | 1 |
| `src/tools/ConnectGesture.cpp` / `RotateCopyGesture.cpp` / `ToolSmartPen.cpp` | 1 | 1 |

全库合计：`normalizeDeg180` **46 次 / 19 文件**，`normalizeDeg360` **41 次 / 17 文件**，其中 **12 个文件同时使用两套**。仅 `src/geometry/Angle.h:36-45` 的注释声明了两域并存，但没有任何类型/API 强制区分 —— 这是「同一物理角显示两个数字」的结构性根因。



---

### P0-3 Theme ↔ CanvasStyle 手工同步，无任何校验

- `src/ui/Theme.h:10-14` 与 `src/canvas/CanvasStyle.h:26-30/130` 互相声明「Kept in sync with cad::ui::ThemeTokens **by hand**」。
- 全库**没有任何测试或守卫**校验两者一致（`tests/` 仅 `test_nav_smoke.cpp:498-500/558/600/616` 断言 `chipBorder`；`test_aux_layer.cpp:86` 注释提到读 CanvasStyle token）。
- 已知漂移实例：hud 暗色三色只存在于 `HudItem.cpp:110-114`，token 表里没有；teal 两套值（P0-1）。

**建议**：新增 `tools/check_theme_sync.py`（或单测 `test_theme_sync.cpp`）断言两表同名 token 数值相等；把 hud 暗色补进 token。

---

### P0-4 旋转手势里同一个角度有 3 套读数，且写入域按模式切换

**HUD 徽标（同一个 `currentAngleDeg`，三种显示）** —— `src/tools/RotateDragMath.cpp:91-99`：

```cpp
if (in.isMultiOrMarquee) {
    if (std::abs(out.deltaDeg) > 0.01)
        out.badgeText = QString::asprintf("%.1f°", std::abs(out.deltaDeg));      // 绝对值，丢符号
} else if (in.isConnected) {
    out.badgeText = QString::asprintf("%.1f°", cad::geo::normalizeDeg180(in.currentAngleDeg)); // −90
} else {
    out.badgeText = QString::asprintf("%.1f°", cad::geo::normalizeDeg360(in.currentAngleDeg)); // 270
}
```

→ 同一个旋转姿态：多选/框选显示「转角绝对值」，连接段显示「−90.0°」，自由段显示「270.0°」。

**写入域按 rotationMode 切换** —— `src/tools/RotateSession.cpp:251-270` 同一函数内：

| 模式 | 行 | 写入 | 域 |
|---|---|---|---|
| ArcLength | `:255` `const double alpha = cad::geo::normalizeDeg360(deg);` → `:258` `a->arcLength = degToArcMm(alpha, radius);` | 弧长 | **360 域** |
| ChordLength | `:263` `const double foldDeg = cad::geo::normalizeDeg180(deg);` → `:264` `a->chordLength = degToChordMm(foldDeg, radius);` | 弦长 | **180 域** |
| Angle | `:268` `a->followerAngle = alpha;` | followerAngle | **360 域** |

因为 `src/geometry/Angle.h:70-72` `degToArcMm = deg * kPi / 180.0 * radiusMm`（带符号）、`:90-96` `degToChordMm` 在 `deg < 0` 时返回负弦长 —— **同一个 270° 姿态下弧长为正、弦长为负**；切到「开度」模式再切回来时还会叠加 `src/parametric/FollowerAngle.h:126` 的 `std::fmod` 负角不折叠问题（见 PAR-P0-2）。

**测试锁定**：存储侧被 `tests/test_rotate_anchor.cpp:366`、`tests/test_rotate_strip.cpp:269`、`tests/test_reverse_segment_commands.cpp:494` 锁 270.0；**HUD 徽标文本无任何测试覆盖**（`tests/` 中 `badgeText` 只断言 `src/app/ContextStripDisplay.cpp:370` 的状态条徽标，与 `RotateDragMath.cpp:94/96/98` 无关）→ 徽标统一零测试成本。

**建议**：旋转家族统一在**内部用 360 域**，只在**显示边界**（徽标/输入框）转 180 域，并把 `RotateDragMath.cpp:91-99` 三个分支合并为一个 `formatRotationBadge(deg, mode)`。

## 2. P1 级

### P1-1 默认图层名硬编码 3+ 处，菜单标签写死

| 字符串 | 位置 |
|---|---|
| `"辅助层"` | `src/parametric/LayerRegistry.cpp:16`、`src/document/DocumentSerializer.cpp:877`、`src/document/FormatMigration.cpp:43`（默认参数） |
| `"图层 1"` | `src/parametric/LayerRegistry.cpp:20`、`src/document/DocumentSerializer.cpp:858`、`src/document/FormatMigration.cpp:36` |

这三处必须永远一致（序列化/迁移/新建），目前靠人。另外 `src/tools/ToolSelectActions.cpp:224-236` 用 `layersView().auxLayerId()` 正确定位图层，但菜单标签写死 `QString::fromUtf8("辅助层")`（`:228`）而不是 `layer.name` → 用户重命名辅助层后，右键菜单名与图层面板不一致。

**建议**：`LayerRegistry` 导出 `kDefaultAuxLayerName`/`kDefaultWorkingLayerName`；菜单改用模型里的 `layer.name`。

### P1-2 `findFollowerAttachment` 同一查询 8 份实现

逐字相同的自由函数 6 份：`src/ui/LineEndpointSectionConn.cpp:16-25`、`src/ui/LineGeometrySection.cpp:42`、`src/ui/SegmentAlignPointCard.cpp:20`、`src/ui/SegmentAngleRefCard.cpp:29-39`、`src/ui/SegmentRefCard.cpp:17`、`src/ui/SegmentShadowBasisCard.cpp:26`；成员函数逐字相同 2 份：`src/ui/SegmentAngleCard.cpp:271-280` 与 `src/ui/SegmentConnectionCard.cpp:50-59`；点级超集 1 份：`src/app/ContextStripEdit.cpp:253-274`。

`ParamDocument.h` 只有 `findAttachment(id)`（`:238/239`）、`findAtt1OfShadow/findAtt2OfShadow`（`:297-300`）、`attachmentsView()`（`:316`），**没有按 fromBlockId 查的门面 API**。

**建议**：`ParamDocument` 增加 `findFollowerAttachmentOf(blockId)`，8 份实现全部删除。

### P1-3 像素容差 8 套各写一份

`src/canvas/CanvasStyle.h:150` `m_hoverRadiusPx=8.0`；`src/tools/ToolSelectActions.cpp:340` `kTolerancePx=8.0`；`src/canvas/BlockItem.cpp:65` 与 `src/tools/LeaderCandidatePicker.cpp:129` 硬编码兜底 `8.0`；`src/tools/ConnectGesture.h:32` `kConnectSnapRadius=7.5`；`src/tools/ConnectOverlapResolver.cpp:24` `kSourcePortRadiusPx=5.0`；`src/tools/RotateAimSnap.cpp:68` 与 `:157` `searchRadiusPx=15.0`（**同一文件两处**）；`src/tools/SnapEngine.h:20` `kSnapOverlapEps=0.5`；`src/tools/OverlapDisambiguationController.cpp:87` `kOverlapEps=0.5`；`src/tools/ToolMeasure.cpp:28` `kAxisZeroEps=0.05`；`src/tools/SelectHoverFeedback.h:28` `kDragThresholdPx=5.0`；`src/tools/ToolSelect.cpp:39` `kOverlapSelectThresholdPx=5.0`；`src/tools/RotateAimSnap.cpp:70` `alignTolRad=2°`。

**建议**：新增 `src/tools/InteractionTolerances.h`（或并入 `CanvasStyle`），至少把 8.0 / 5.0 / 0.5 三档命名。

### P1-4 几何容差字面量 269 处，无中央 epsilon

分布：`1e-9`×149、`1e-12`×48、`1e-6`×44、`1e-10`×10、`1e-4`×8、`1e-3`×5、`1e-8`×5。
局部已命名但各写一份：`src/geometry/TriangleUnfold.cpp:60` `kEpsilon=1e-10`、`src/parametric/BlockExtend.cpp:94` `kEps=1e-6`、`src/parametric/BlockResolve.cpp:400` `eps=1e-6`、`src/parametric/ResolverIntersection.cpp:140` `eps=1e-6`、`src/parametric/ExpressionEvaluator.cpp:283/294` `kEps=1e-9`；默认参数 `src/geometry/RayCast.h:22` `eps=1e-6`、`src/geometry/CurveMath.cpp:734` `eps=1e-6`。

**建议**：`src/geometry/Epsilon.h` 定义 `kGeomEps=1e-9`、`kGeomEpsLoose=1e-6`、`kGeomEpsTight=1e-12` 三档语义。

### P1-5 `M_PI` 与内联 deg↔rad 换算绕过 Angle.h

- `#define M_PI 3.14159265358979323846` 在 4 个文件各自定义：`src/geometry/CurveMath.cpp:9`、`src/tools/RotateAimSnap.cpp:14`、`src/tools/RotateSession.cpp:22`、`src/tools/ToolRotate.cpp:28`。
- `M_PI` 共 **115 次 / 29 文件**；`kPi|degToRad|radToDeg` 仅 65 次 / 21 文件 → 已有收口点 `Angle.h` 基本没被用。
- 内联换算 **76 处**（`* 180.0 / M_PI` / `* M_PI / 180.0`）。热点：`src/tools/RotateSession.cpp` 11、`src/tools/RotateAimSnap.cpp` 8、`src/parametric/BlockResolve.cpp` 7（`:123/241/283/357/359/525/600`）、`src/ui/SegmentAnchorTab.cpp` 6、`src/tools/LineFactory.cpp` 5、`src/ui/SegmentAngleCard.cpp` 5、`src/tools/ToolPlacePoint.cpp:275/289/301`、`src/parametric/ResolverIntersection.cpp:126/128`、`src/parametric/Resolver.cpp:335`、`src/parametric/ResolverAttachment.cpp:134`、`src/canvas/BlockGeometryCache.cpp:178`、`src/tools/IntersectionAngleAim.h:28/31/33`、`src/ui/LineOrthoOffsetCard.cpp:199/206`。
- `block->transform.rotation * 180.0 / M_PI` 精确 6 处：`src/app/ContextStripDisplay.cpp:118`、`src/app/ContextStripEdit.cpp:120`、`src/tools/RotateCopyGesture.cpp:156`、`src/ui/SegmentAngleCard.cpp:54/80/464`。

**建议**：`Angle.h` 补 `radToDeg(block->transform.rotation)` 收口 + 守卫脚本禁止新增 `M_PI`/`180.0` 内联换算。

### P1-6 同一物理量两种显示精度

- `interpOffsetDist`（mm）：`src/app/PlacedPointStripBar.cpp:121` `QString::number(pt->interpOffsetDist / 10.0, 'f', 1)`（1 位）vs `src/ui/PlacedPointDialog.cpp:189` `'f', 2`（2 位）；`PlacedPointStripBar.cpp:172` 也是 1 位。
- 内联 `/ 10.0`、`* 10.0` 绕过 `Units::mmToCm/cmToMm` 共 9 处：`PlacedPointStripBar.cpp:121/255`、`ToolPlacePoint.cpp:162/298/368`、`PlacedPointDialog.cpp:189/203`、`BlockItemPainter.cpp:290-291`。
- `src/tools/ToolSmartPen.cpp:639` 角度用 `'f', 0`（0 位），全库别处角度 1 位。
- 现有 API 未被统一使用：`Units::formatLength(mm, precision=2)`、`formatCmTrimmed`、`formatNumberTrimmed`、`formatDegTrimmed`、`formatDegValue`；`'f', N` 共 23 处、`setDecimals` 7 处。
- **同一长度值两种格式**：`src/ui/MeasureResultDialog.cpp:60` `Units::formatLength(valueMm)`（`Units.h:47-49` → 2 位小数 + " cm"）vs `src/ui/MeasureCard.cpp:69` `Units::formatCmTrimmed(valueMm)`（`Units.h:75-77` → 去尾零）→ 同一次测量，结果对话框显示 `12.50 cm`、卡片显示 `12.5 cm`。
- **同一文件内混用**：`src/ui/LineGeometrySection.cpp:326` 弧长用 `formatLength(arcMm)`（2 位），而同文件 `:310` 长度输入用 `formatNumberTrimmed(lenCm)`、`:454/459` 滑动量用 `formatCmTrimmed`（均去尾零）。
- 用量分布：`formatLength` 12 处、`formatCmTrimmed` 6 处、`formatNumberTrimmed` 13 处、`formatDegValue` 20 处、`formatDegTrimmed` 3 处、`formatAngle` **仅 1 处**（`Units.h:59` 自身）→ 已有 API 并未被统一采用。
- 无测试锁定 `interpOffsetDist` 显示精度（`tests/test_place_point.cpp:96-97/211-222` 只断言模型值）。

**建议**：`Units` 增加 `formatCm(mm)`（统一 2 位去尾零）与 `formatDeg0/formatDeg1`，禁止裸 `'f', N`。

### P1-7 序列化键在 serializer 与 migration 各写一份

跨文件重复的 JSON 键 11 个：`document`（`DocumentFile.cpp`/`DocumentSerializer.cpp`/`FormatMigration.cpp`）、`variables`、`id`、`name`、`visible`、`type`、`layer`、`blocks`、`layers`、`activeLayer`、`attachments`（后 9 个均在 `DocumentSerializer.cpp` 与 `FormatMigration.cpp` 各写一遍）。`FormatMigration` 写 v0 JSON 的键必须与 reader 完全一致，否则迁移静默产不出可读文件。

**建议**：`src/document/SchemaKeys.h` 收口键名常量（`kFormatVersion` 已在 `FormatMigration.h:38` 收口，可并入同一文件）。

### P1-8 诊断去重/徽章/字体助手逐字重复

- `report()` 诊断去重助手逐字重复 2 份：`src/parametric/Resolver.cpp:21-28` 与 `src/parametric/ResolverAttachment.cpp:17-24`。
- `kbdBadge()` 逐字重复 2 份：`src/app/ContextStripDisplay.cpp:31-36` 与 `src/app/ContextStripSessions.cpp:22-27`（同一段 `<span style="background:%1; border:1px solid %2; border-radius:2px; padding:1px 4px; font-family:'Consolas',monospace; font-size:10px; font-weight:600; color:%3;">%4</span>`）。
- 画布字体助手 4 套各自为政：`src/canvas/BlockItemPainter.cpp:18-40` 与 `src/canvas/CurveItem.cpp:19-36`（`nameFont` 逐字相同、`lengthFont` 逐字相同）、`src/canvas/HudItem.cpp:18-23`（Segoe UI 11px）、`src/canvas/OverlapBatteryHud.cpp:17-29`（Consolas/Segoe UI）、`src/canvas/overlay/TransientOverlay.cpp:339`（Segoe UI 9 Bold）。
- 字号阶梯外：`font-size: 14px` 只出现在 `src/ui/LayerPanel.cpp:179` 与 `:239`（Theme 阶梯为 10/11/12/13/15/18）；`font-size: NNpx` 硬编码共 **119 处**（10px×22、11px×70、12px×22、13px×3、14px×2）。

### P1-9 默认外观值在「模型层」与「画布层」各定义一份

| 属性 | 模型层 | 画布层 |
|---|---|---|
| 默认线宽 1.2 | `src/parametric/Segment.h:81` `double weight = 1.2;` | `src/canvas/CanvasStyle.h:18` `double lineWidth = 1.2;` |
| 默认线色 | `src/parametric/Segment.h:80` `QColor color = QColor(30, 30, 30);` | `src/canvas/BlockItemPainter.cpp:205` `QColor(30, 30, 30)` 兜底 |

两处必须一致，但没有任何测试/守卫关联它们；改一处即静默漂移。

**建议**：`CanvasStyle` 的默认值改为从模型默认推导（或反之，模型默认值从 token 取），并加一致性断言。

---

## 3. P2 级：重复样板

跨文件逐字相同的非注释行（>45 字符）共 **562 组**。高价值样本（行 ×出现次数）：

| 重复行 | 次数 | 代表位置 |
|---|---|---|
| `if (sp && ep && sp->resolved && ep->resolved) {` | 23 | `src/parametric/BlockQuery.cpp` 多处、`src/app/ContextStripDisplay.cpp:61/85` |
| `const auto* sp = block->findPoint(seg->startPointId);` | 16 | 点解析对取用样板 |
| `const auto* ep = block->findPoint(seg->endPointId);` | 12 | 同上 |
| `geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;` | 7 | `BlockExtend`/`BlockQuery`/`BlockResolve` |
| `for (const auto& att : m_paramDoc->attachments()) {` | 13 | `ContextStripEdit.cpp:263`、`MainWindowSegmentMenu.cpp:64`、`ConnectGesture.cpp:115`、`SelectDragController.cpp:33/45/64` |
| `auto* block = m_paramDoc->findBlock(m_blockId);` | 21 | 上下文查找样板 |
| `auto* seg = block ? block->findSegment(m_segmentId) : nullptr;` | 16 | 同上 |
| `if (auto* a = m_doc->findAttachment(m_attId))` | 7~11 | `AttachmentAngleCommands.cpp:205/328/342`、`AttachmentLifecycleCommands.cpp:200/237/264/272/288` |
| `pts.erase(std::remove_if(pts.begin(), pts.end(),` | 12 | `BreakExecution.cpp:184/203`、`CurveCommands.cpp:66/115/321`、`EndpointCommands.cpp:230` |
| `if (event->button() != Qt::LeftButton) return;` | 12 | 工具样板 |
| `void setTarget(const QUuid& blockId, const QUuid& segmentId);` | 12 | `Line*Section.h`、`LinePropertyDialog.h`、`Segment*Card.h` |
| `const QUuid& blockId, const QUuid& segmentId,` | 15 | 同上 |
| `double zoom = m_scene ? m_scene->currentZoom() : 1.0;` | 12 | 画布项 |
| `if (m_scene) m_scene->refreshAllBlockItems();` | 11 | 工具 |
| `const cad::geo::Vec2 clickPos(sp.x(), sp.y());` | 7 | 工具 |
| `QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();` | 6 | `IntersectionToolVisuals`/`ToolAngleMeasure`/`ToolMeasure`/`ToolSmartPen` |
| `m_scene->views().first()->setCursor(Qt::CrossCursor);` | 6 | 工具 |
| `stack->push(new cad::cmd::SetEndTargetCommand(` | 5 | `LineEndpointSectionConn.cpp:165/184`、`SegmentConnectionCardConn.cpp:289/314/325` |
| `"font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;"` | 6 | `CompoundChip.cpp:211/259`、`FormulaCard.cpp:69/102/122/496` |
| `"QScrollArea { background: %1; border: none; }"` | 5 | `CardTabBase.cpp:43/112`、`ComponentTab.cpp:90`、`MeasureTab.cpp:139/354` |
| `"font-size: 11px; color: %1; background: transparent;"` | 5 | `CardTabBase.cpp:74/126`、`LayerCard.cpp:138`、`LayerPanel.cpp:187/244` |

**中文 UI 文案跨文件重复 73 组**，典型：

- `"起点 → 终点。换向后修改长度/角度将驱动对端。"`：`src/app/ContextStrip.cpp:196` 与 `src/app/ContextStripDisplay.cpp:332` 逐字
- `"清空输入框并粘贴剪切板内容"` + `"填入剪贴板"`：`src/ui/AuxPointForm.cpp:71-72` 与 `src/ui/LineGeometrySection.cpp:129-130`
- `"自动测量，不可编辑"`：`src/ui/AngleMeasureCard.cpp:109`、`src/ui/LinkedCard.cpp:84`、`src/ui/MeasureCard.cpp:133`
- `"绝对角度"`：`src/tools/IntersectionToolVisuals.cpp:222` 与 `src/tools/ToolIntersection.cpp:552/556`
- `"重新连接位置"`/`"拆开位置连接"`：`src/app/ContextStripDisplay.cpp:198/201` 与 `src/ui/SegmentConnectionCardRefresh.cpp:91/95` 各一份
- `"重连"` ×5

**快捷键文案不统一（同一按键多种写法）**：`Esc 回位`（`src/tools/RotateHintTexts.cpp:36`）、`Esc清除`（`:47`）、`Esc返回选区`（`:51`）、`Esc重选中心`（`:57`）——**同文件内有/无空格混用**；`右键/Esc取消`（`src/tools/ToolAngleMeasure.cpp:38`）、`Esc取消`（`src/tools/ToolCurveEdit.cpp:25`）、`%1 取消 · 落点后自动锁定`（`src/app/ContextStripSessions.cpp:140`）、`%1 确认 · %2 取消`（`src/app/ContextStripDisplay.cpp:338`）。确认键同样混用「回车 / Enter / 确认 / 提交」（`RotateHintTexts.cpp:44/47` 用「回车确认」、`ContextStripDisplay.cpp:338` 用 kbdBadge(Enter)+「确认」、`RotateHintTexts.cpp:36` 用「松手提交」）。

**键盘处理样板**：`Qt::Key_Escape`/`Key_Return|Key_Enter`/`Key_Tab|Key_Backtab` 判断在 `src/app/ContextStrip.cpp:474-518`、`src/app/PlacedPointStripBar.cpp:305-334`、`src/ui/CompoundChip.cpp:472-493`、`src/ui/CopyChip.cpp:176-180`、`src/ui/PointRefEdit.cpp:321-326`、`src/ui/NoteButton.cpp:192`、`src/tools/ConnectGesture.cpp:605-615` 等 7+ 处逐字重复。工具快捷键散落于各 Tool 的 `d.shortcut`（`ToolBreak.cpp:33` B、`ToolAngleMeasure.cpp:37` A、`ToolSmartPen.cpp:56` L、`ToolCurveEdit.cpp:24` C、`ToolRotate.cpp:39` Ctrl+T、`ToolIntersection.cpp:29` I、`ToolSelect.cpp:53` V、`ToolPlacePoint.cpp:28` P），无集中注册表，冲突需人工排查。

---

### 3.5 跨文件「4 行以上」逐字重复代码块（脚本实测 **194 组**）

方法：把 src/ 全部 .cpp/.h 的非注释行做空白归一化，取长度 ≥4 的连续窗口，统计出现在 ≥2 个不同文件的窗口。结果 194 组，以下是「同一逻辑被抄成多份」最典型的样本（含组内文件数与出现位置）：

| 组 | 重复内容（首行） | 出现位置 |
|---|---|---|
| 6 文件 | `void mousePress/mouseMove/mouseRelease/keyPress(QGraphicsSceneMouseEvent*)` 四行 override 声明 | `src/tools/ToolAngleMeasure.h:38`、`ToolCurveEdit.h:38`、`ToolIntersection.h:29`、`ToolMeasure.h:40`、`ToolPlacePoint.h:28`、`ToolSmartPen.h:53` |
| 6 文件 | 命令构造样板 `QUndoCommand* parent) : QUndoCommand(parent), m_doc(doc), m_blockId(blockId)` | `src/document/commands/BlockTransformCommands.cpp:108`、`BreakCommands.cpp:16`、`CurveCommands.cpp:16/80/151/199` … |
| 6 文件 | 头文件前置声明块 `namespace cad::param { class ParamDocument; class Block; struct Segment;` | `src/document/commands/BreakAnalysis.h:6`、`src/ui/LineAppearanceSection.h:12`、`LineEndpointSection.h:11`、`LineGeometrySection.h:15`、`LineOrthoOffsetCard.h:12`、`SegmentAuxTab.h:13` |
| 5 文件 | 命令成员样板 `m_doc/m_blockId/m_segmentId`（.h private 段） | `BreakCommands.h:39`、`CurveCommands.h:30/51/159`、`EndpointCommands.h:96/153` |
| 4 文件 | `findFollowerAttachment` 自由函数体 `if (!doc) return nullptr; for (const auto& att : doc->attachments()) { if (att.isPin) continue; if (att.fromBlockId == blockId)` | `src/ui/SegmentAlignPointCard.cpp:23`、`SegmentAngleRefCard.cpp:32`、`SegmentRefCard.cpp:20`、`SegmentShadowBasisCard.cpp:29` |
| 3 文件 | 「组件序列号 + 名称」标签构造 `const QUuid fs = fb->exitSegmentAtPoint(att.fromPointId); … Serial::tag(fsg->serial)` | `src/tools/ToolSelectActions.cpp:158-159`、`src/ui/LineEndpointSection.cpp:401-402`、`src/ui/SegmentAuxTab.cpp:241-242` |
| 3 文件 | 辅助点构造 `pt.constraint = Interpolated; pt.hostSegmentId = seg->id; pt.isAuxiliary = true; pt.visible = true; pt.showName = false;` | `src/app/MainWindowSegmentMenu.cpp:166-168`、`src/tools/SmartPenAuxPoint.cpp:67-69`、`src/tools/ToolBreak.cpp:285-287` |
| 3 文件 | toolPill QSS `"QFrame#toolPill { background: %1; border: 1px solid %2; border-radius: 2px;"` | `src/app/MainWindow.cpp:170-171`、`MainWindowPanelWindow.cpp:319-320`、`MainWindowToolBar.cpp:56-57` |
| 2 文件 | 命中测试 `const QPointF scenePt = Coord::toScene(worldPos.x, worldPos.y); const auto hits = blockHitsAtScene(...); return hits.empty() ? QUuid() : hits.front().blockId;` | `src/tools/ToolRotate.cpp:847`、`src/tools/ToolSelectHitTest.cpp:23` |
| 2 文件 | 跨块参照段世界坐标 `if (const auto* rseg = block->findSegment(ep->refSegmentId)) { … transform.toWorld(...)` | `src/canvas/BlockGeometryCache.cpp:169`、`src/ui/LineOrthoOffsetCard.cpp:201` |
| 2 文件 | 极坐标端点写回 `st.endConstraint = Polar; st.endRefPointId = seg->startPointId; st.endDistance = sp->resolvedPos.distanceTo(ep->resolvedPos);` | `src/app/ContextStripEdit.cpp:115`、`src/ui/SegmentAngleCard.cpp:459` |
| 2 文件 | QuickAuxDialog 构造（含 `m_scene->views().first()` 兜底） | `src/tools/SmartPenAuxPoint.cpp:76`、`src/tools/ToolBreak.cpp:294` |

→ 结论：

- **命令层构造样板是真重复且可收口**：全 `src/document/` 共 **76 个命令类全部直接继承 `QUndoCommand`（无共享基类）**，每个都重复 `m_doc/m_blockId/m_segmentId` 成员 + 构造转发 4 行。建议加 `class DocCommand : public QUndoCommand` 持有 `ParamDocument* m_doc`，可一次性消掉 60+ 份样板。
- **工具头文件的 4 行 override 声明是「假重复」**：`src/tools/Tool.h:166` `virtual void mousePress(QGraphicsSceneMouseEvent* event) = 0;` 为纯虚，派生类**必须**声明 override，无法通过宏/基类消除 —— 此组不列入重构清单（若追求一致性可让所有工具统一继承一个 `ToolWithMouse` 中间类，但收益为 0）。
- **必须提为公共函数**：`findFollowerAttachment`（4 份自由函数体逐字相同）、「组件序列号 + 名称」标签构造（3 份）、辅助点构造（3 份）、toolPill QSS（3 份）、命中测试封装（2 份）。

### 3.6 数值显示格式收口现状（全库实测）

`src/geometry/Units.h` 已提供 7 个格式化器，但调用分布极不均匀，且同一物理量在不同界面走不同格式化器：

| 格式化器 | 调用次数 | 输出特征 |
|---|---|---|
| `formatDegValue` | 21 | 角度值，1 位小数 |
| `formatLength` | 13 | 2 位小数 + " cm"（如 `12.50 cm`） |
| `formatNumberTrimmed` | 13 | 去尾零（如 `12.5`） |
| `formatCmTrimmed` | 6 | 去尾零 + " cm"（如 `12.5 cm`） |
| `formatDegTrimmed` | 3 | 去尾零 + "°" |
| `formatPoint` | 2 | 2 位小数 + "cm"（状态栏坐标） |
| `formatAngle` | 1 | **死代码**（仅定义，无调用，见 CAN-P1-18） |

**绕过 Units 的裸格式化**：`QString::number(..., 'f', N)` 全库 13 处，其中同一物理量 `interpOffsetDist`（mm）两处精度不同：
- `src/app/PlacedPointStripBar.cpp:121` `QString::number(pt->interpOffsetDist / 10.0, 'f', 1)` → `12.0`
- `src/ui/PlacedPointDialog.cpp:189` `QString::number(pt->interpOffsetDist / 10.0, 'f', 2)` → `12.00`
- 对比 `src/app/ContextStripDisplay.cpp:65-66` `formatNumberTrimmed(mmToCm(mm))` → `12`
（`PlacedPointStripBar.cpp` 内另有 `:172` 1 位、`:255` 写回 `* 10.0`；`ToolPlacePoint.cpp:298/368` 同样内联 `/10.0` + 1 位。）

**绕过 Units 的单位换算**：内联 `/ 10.0`、`* 10.0` 共 9 处（`PlacedPointStripBar.cpp:121/255`、`PlacedPointDialog.cpp:189/203`、`ToolPlacePoint.cpp:162/298/368`、`BlockItemPainter.cpp:290-291` 量化用），未走 `Units::mmToCm/cmToMm`。

**裸解析**：`text.toDouble(&ok)` 29 处，其中 `src/app/PlacedPointStripBar.cpp:216/234/254/256` 与 `src/ui/ComponentTab.cpp:316/337`、`src/ui/IntersectionForm.cpp:83/120` 均不支持公式，而 `src/app/ContextStripEdit.cpp:53/77` 走 `parseNumberOrFormula` → 同一个数值框家族输入行为不一致。

**src/tools 侧「1 位小数 + °」有 4 套实现**：

| 实现 | 位置 |
|---|---|
| `Units::formatDegValue` | `src/tools/RotateHintTexts.cpp:28`（`旋转复制 %1°`）、`:37-38`（`基准: %1° · 角度: %2°`） |
| `QString::number(x, 'f', 1)` | `src/tools/IntersectionToolVisuals.cpp:215`（`指向点 %1 = %2°`）、`:225`（`%1 = %2°`，附 `:228` `t = %1` 用 `'f', 3`） |
| `QString::arg(x, 0, 'f', 1)` | `src/tools/ToolPlacePoint.cpp:368-369`（`偏置距离 %1 cm \| 偏置角度 %2°`） |
| `QString::asprintf("%.1f°")` | `src/tools/RotateDragMath.cpp:94/96/98`（旋转徽标，三套域） |

**同一物理角在 src/tools 内的域声明也散落成注释**（无任何强制）：`src/tools/ConnectGesture.cpp:33`「存储域 α ∈ [0, 360°)，跟随角度显示 = 带符号折角 [−180°, +180°]」、`src/tools/ToolSmartPen.cpp:603-606`「附着 leader 时 = 带符号折角 … 自由起点 = 水平基准绝对角（0~360° 逆时针为正）」、`src/tools/LinePreInput.h:20-21` 同义 —— 三处注释与代码实现必须人工比对。

**测试锁定**：`tests/` 全部 55 个文件里只有 `tests/test_rotate_copy_endtarget.cpp:245` 一处断言格式化后的显示字符串（`"= 10 cm"`），`interpOffsetDist` 虽被断言 40+ 次但**全部是原始 mm 值** → 统一显示格式几乎无测试成本。

## 4. 分模块深挖（4 个只读子代理）

### 4.1 src/ui（37 条：P0=8 / P1=15 / P2=14）

#### P0 —— 用户可见的「同量不同值 / 同值不同显示」

- **UI-P0-1 世界角度两套归一化**：`src/ui/SegmentAngleCard.cpp:305-306` `"= 世界角度 %1°"` 用 `formatDegValue(*d)`，d 来自 `worldOrNominalDegOfSegment`（`:43/:55` 用 `normalizeDeg360`）→ 0..360；画布条带 `src/tools/RotateDragMath.cpp:96` `QString::asprintf("%.1f°", cad::geo::normalizeDeg180(in.currentAngleDeg))` → −180..180。同一条自由线：属性对话框 270°、画布条带 −90°。
- **UI-P0-2 同一 `m_lblFollowValue` 两种归一化**：`src/ui/SegmentAngleCard.cpp:160` `normalizeDeg180(attachmentEffectiveAngleDeg(...))` → `:169` `"= %1°"`；而 `:81-82` `evaluatedFollowValueText` 用 `normalizeDeg360(r.value + rotDeg)`。同一标签按分支显示 −45° 或 315°（`:315/:331` 两处写入）。
- **UI-P0-3 同一弧长两种显示**：`src/ui/SegmentAngleCard.cpp:162-163` `"= %1 cm"` + `formatNumberTrimmed`（去零 → "= 10 cm"）vs `src/ui/LineGeometrySection.cpp:326` `m_lblArcLength->setText(formatLength(arcMm))`、`:383`（固定 2 位 → "10.00 cm"）。**已复核**：`tests/test_rotate_copy_endtarget.cpp:245` 锁 `"= 10 cm"`，`:209` 注释还写「显示 = 10.00 cm」——注释与断言已漂移。
- **UI-P0-4 同一长度输入框两种文本**：`src/ui/LineGeometrySection.cpp:310` `m_editLength->setText(formatNumberTrimmed(lenCm))`（"12.5"）vs `:420` `m_editLength->setText(formatLength(lenMm))`（"12.50 cm"，桥接线分支，字段被 `:416` 禁用）。**已复核**：同控件两种文本，且带单位文本会进解析路径（仅靠 `:294` `QSignalBlocker` 挡住写回）。
- **UI-P0-5 角度副值两种**：`src/ui/ComponentTab.cpp:207`（`:247` 同）`QStringLiteral("= %1°").arg(rr.value, 0, 'f', 1)` → "= 45.0°" vs 卡片族 `formatDegValue` → "= 45°"；`:194-196` `QString::number(normalizeDeg360(c.defaultAngleDeg), 'f', 1)` → "45.0"（无 °）。
- **UI-P0-6 角度测量两处不同**：`src/ui/AngleMeasureCard.cpp:59` `formatDegTrimmed(valueDeg)` → "45°" vs `src/tools/ToolAngleMeasure.cpp:199` `QStringLiteral("%1°").arg(res.angleDeg, 0, 'f', 1)` → "45.0°"。
- **UI-P0-7 带 ° 的值写进可编辑框、解析却不剥 °（真 bug）**：`src/ui/SegmentConnectionCardRefresh.cpp:134-137` 用 `QSignalBlocker` 回填 `formatDegTrimmed(block->endTargetOffset)`（"5°"）到 `m_editEndOffset`；`src/ui/SegmentConnectionCardConn.cpp:281-284` `const QString raw = m_editEndOffset->text().trimmed(); bool isNum=false; const double val = raw.toDouble(&isNum); if (!isNum && !raw.isEmpty()) { refreshEndRow(block); return; }` —— 不 `remove(QChar(0x00B0))`。**已复核**：用户打开卡后直接回车 → "5°" 判为公式 → 静默回滚，编辑无效。对照已正确剥 ° 的 `src/ui/SegmentShadowBasisCard.cpp:131-132`。
- **UI-P0-8 CardBase 失效态值标签丢等宽族 + 字号跳变**：`src/ui/CardBase.cpp:318-324` 失效分支 `"font-size: %1px; font-weight: 600; color: %2; background-color: rgba(...)"`（FontSm=11、无 `kMonospaceFamily`）vs `:336-338` 正常分支 `"%1font-size: %2px; font-weight: 600;".arg(kMonospaceFamily, FontLg)` → 引用失效瞬间字体族切换、行宽跳动。

#### P1 —— 重复实现 / 维护风险

- **UI-P1-1 `findFollowerAttachment` 8 份**（判据 `if (!att.isPin && att.fromBlockId == blockId) return &att;`）：`src/ui/SegmentAngleCard.cpp:271`、`src/ui/SegmentConnectionCard.cpp:50`、`src/ui/LineEndpointSectionConn.cpp:16-25`、`src/ui/LineGeometrySection.cpp:42-51`、`src/ui/SegmentAlignPointCard.cpp:20-30`、`src/ui/SegmentAngleRefCard.cpp:29-39`、`src/ui/SegmentRefCard.cpp:17-27`、`src/ui/SegmentShadowBasisCard.cpp:26-36`。（与本报告 P1-2 同一条，子代理给出完整 8 处清单）
- **UI-P1-2 影子 Att1 循环逐字重复**：`src/ui/SegmentShadowBasisCard.cpp:87-89` 与 `:136-138`。
- **UI-P1-3 连接语义两套近乎逐行相同（约 150 行）**：`src/ui/SegmentConnectionCardConn.cpp:44-210` 与 `src/ui/LineEndpointSectionConn.cpp:47-125`（影子拓扑挂载 + `preserveAngleRefOnReattach` + `ReconnectAttachmentCommand`/`AddAttachmentCommand`）。
- **UI-P1-4 CardTabBase 同套 QSS 写两遍**：`src/ui/CardTabBase.cpp:42-44` vs `:111-114`、`:48-49` vs `:116-118`、`:65-67` vs `:120-123`、`:73-75` vs `:125-127`、`:83-87` vs `:130-134`。
- **UI-P1-5 MeasureTab 绕过 CardTabBase 自建列表页**：`src/ui/MeasureTab.h:25` `class MeasureTab : public QWidget`（其余三个是 `public CardTabBase`：`VariableTab.h:13`、`FormulaTab.h:18`、`LinkedTab.h:13`）；`MeasureTab.cpp:131-169` 与 `CardTabBase.cpp:34-102` 同构（连 objectName `cardListArea`/`cardListContainer` 都重复），空态字号 `MeasureTab.cpp:151` FontMd vs `CardTabBase.cpp:62` FontXl + `:70` 11px。
- **UI-P1-6 tab 样板 4 份重复**：`VariableTab.cpp:31-52/:117-129`、`LinkedTab.cpp:46-82/:106-116`、`MeasureTab.cpp:172-249/:263-298`、`FormulaTab.cpp:73/:430-434`。
- **UI-P1-7 FormulaTab 手工 known 表 dangling 过滤不一致（潜在错值）**：`src/ui/FormulaTab.cpp:384-414`：`:393` `if (!lv.dangling && !lv.refName.isEmpty())`、`:409` `if (!am.dangling && ...)`，但 `:401` `if (!mv.refName.isEmpty())` **无 dangling 检查** → 失效长度测量把陈旧值喂进公式环境。收口点 `src/parametric/ParamDocumentParameters.cpp:32,45,85`。
- **UI-P1-8 setRange/精度各自为政**：全 src/ui 仅 7 处 `setRange`：`LineAppearanceSection.cpp:117` `setRange(0.5, 10.0)`、`ConditionDialog.cpp:23-26` `setRange(-99999,99999); setDecimals(2); setSuffix(" cm"); setSingleStep(0.5)`、`SegmentAnchorTab.cpp:74/:92` `setRange(-360,360)`+1 位+°、`:81/:99` `setRange(0,999)`+2 位+" cm"、`VariableCard.cpp:98-100`。**全 src/ui 无任何 `setValidator` 调用**（`PlacedPointDialog.cpp:6` 仅 include）。
- **UI-P1-9 两段式 toDouble 绕过 parseNumberOrFormula**：`ComponentTab.cpp:316,337`、`AuxPointForm.cpp:193,204`、`IntersectionForm.cpp:83,120`、`LineGeometrySection.cpp:368,537-538`（`:347` 已正确用 parseNumberOrFormula）、`PlacedPointDialog.cpp:202,206`、`QuickAuxDialog.cpp:75`、`SegmentAngleCard.cpp:498`、`SegmentConnectionCardConn.cpp:283`、`SegmentShadowBasisCard.cpp:131-132`。正确用法：`LineGeometrySection.cpp:347`、`LineEndpointSection.cpp:570`、`LineOrthoOffsetCard.cpp:222`。
- **UI-P1-10「数值或公式」双框与单框并存**：`PlacedPointDialog.cpp:79-96`（`:80` "0.0"、`:82` "公式 (如 w/4)"、`:91` "90.0"、`:93` "公式"）+ `:202-208` 两框可同时非空、无互斥、解析失败静默忽略；单框收口常量 `FormScaffold.h:22-23`（`kPlaceholderAngleOrFormula`/`kPlaceholderCmOrFormula`）只在 `ComponentTab.cpp:190,221`、`LineOrthoOffsetCard.cpp:62`、`LineGeometrySection.cpp:92`、`SegmentAngleCard.cpp:96,98,101,221,359,365` 使用。
- **UI-P1-11 提示文案不统一**：`AuxPointForm.cpp:54` "如 0.5 或公式"、`:61` "如 0.7 (cm)或公式"、`IntersectionForm.cpp:32` "如 90 (°)或公式"、`FormulaCard.cpp:402` "输入公式，如: 胸围/4+1.5"、`LineEndpointSection.cpp:144` "名称，如"肩点"" vs `LinePropertyDialog.cpp:196` "名称，如"肩线""侧缝""、`SegmentAlignPointCard.cpp:51` "P#" vs `PointRefEdit.cpp:48` "P#/L#/名称…"。
- **UI-P1-12 两套按钮条 helper 并存**：`makeDialogButtons`（`ConditionDialog.cpp:147`、`LinePropertyDialog.cpp:88`、`PlacedPointDialog.cpp:108`、`PointRefEdit.cpp:234`）vs `makeFormButtonBar`（`MeasureResultDialog.cpp:118`、`QuickAuxDialog.cpp:89`）。
- **UI-P1-13 PlacedPointDialog 手搭 QFormLayout 未套 applyFormGrid**：`PlacedPointDialog.cpp:54,74`（其余 5 处均调用：`AuxPointForm.cpp:104`、`IntersectionForm.cpp:64`、`MeasureResultDialog.cpp:66,109`、`QuickAuxDialog.cpp:62`）→ 标签列不受 88px 栅格约束。
- **UI-P1-14 魔数 /10.0、*10.0**：`PlacedPointDialog.cpp:189` `QString::number(pt->interpOffsetDist / 10.0, 'f', 2)`、`:203` `pt.interpOffsetDist = distCm * 10.0;`。
- **UI-P1-15 ComponentTab 自成一套绕过 CardBase**：`ComponentTab.cpp:147-160` 手搭 `QWidget#componentCard` + `bar->setFixedWidth(3)` + 内联样式；`:171` `QStringLiteral("组件 %1").arg(index + 1)`（未用 `CardBase::createIndexLabel`）；`:263-266` 四按钮未走 `makeFormButtonBar`。（CardBase indexLabel 契约 `CardBase.cpp:255` 在 5 张卡上均合规）

#### P2 —— 样式 / 整洁度

- **UI-P2-1 死 QSS 规则**：`src/ui/Theme.cpp:211-215` `QLabel#cardValue`/`#cardValue[dangling="true"]` —— 全仓无 `setObjectName("cardValue")`（**已复核**：grep 全仓仅 Theme.cpp 两行命中）→ 永不生效，实际样式在 `CardBase.cpp:169-175/:318-338` 内联。
- **UI-P2-2 `QLabel#cardIndex` 双来源**：`Theme.cpp:216` 与 `CardBase.cpp:144-148` 内联（inline 覆盖 app 样式）。
- **UI-P2-3 CardBase ctor 字号死值**：`CardBase.cpp:142/:168/:181` `new ElaText(QString(), 13, this)` 随后被 QSS 覆盖为 FontXs/FontLg/FontXs。
- **UI-P2-4 `dimValueStyle` 半成品**：`Theme.cpp:347-352` `"color:%1; font-size:11px;"`（硬编码 11 而非 FontSm、不含等宽族）→ 调用方各自补：`LineGeometrySection.cpp:151-152`、`LinePropertyDialog.cpp:182-183`；`LineOrthoOffsetCard.cpp:38` 整串自拼 `'Consolas', monospace`（与 Theme 的 `'Consolas','Courier New',monospace` 不一致）。
- **UI-P2-5 硬编码 `font-size: NNpx` 约 68 处**（FontSm=11 已存在），含无 token 的 14px：`LayerPanel.cpp:179,239`。
- **UI-P2-6 非 token 圆角**：3px（`LayerPanel.cpp:104,115,203,209`、`LayerCard.cpp:77,243,258,432`、`FormulaCard.cpp:354`、`SegmentAlignPointCard.cpp:133`）、8px（`NoteButton.cpp:124`）、15px（`PointRefEdit.cpp:60,276`）、1px（`ComponentTab.cpp:159`）—— AGENTS.md 规定 chrome 圆角勿超 `RadiusBadge=4px`。
- **UI-P2-7 硬编码颜色**：`SegmentAlignPointCard.cpp:132-135`、`PointRefEdit.cpp:276-277` `rgba(220,38,38,32)`；`FormulaCard.cpp:355` `rgba(0,0,0,0.06)`；`CardBase.cpp:215` `QColor(0xB0,0xB0,0xB0)`；`Theme.cpp:344` `QColor(QStringLiteral("#6B5B88"))`。
- **UI-P2-8 高度常量重复**：`constexpr int kFieldH = 30` 八处（`SegmentAngleCard.cpp:31`、`SegmentShadowBasisCard.cpp:23`、`SegmentAngleRefCard.cpp:25`、`SegmentConnectionCardBuild.cpp:27`、`LineGeometrySection.cpp:40`、`LineAppearanceSection.cpp:31`、`LinePropertyDialog.cpp:42`、`LineOrthoOffsetCard.cpp:30`）；同对话框 `LineEndpointSection.cpp:36` 却是 `kFieldH = 26; kLabelW = 64`（7 处）；实际高度还有 34/32/28/26/24/22/20/18（`VariableCard.cpp:76` `setFixedHeight(34)`、`PlacedPointDialog.cpp:104` 32、`FormulaCard.cpp:382` 24）。
- **UI-P2-9 `refChipWidth` 不一致**：`LinkedCard.cpp:89` 与 `MeasureCard.cpp:138` = 72，`AngleMeasureCard.cpp:114` = 84。
- **UI-P2-10 单位呈现方式不统一**：值内联（`AngleMeasureCard.cpp:59` 的 ° 在 15px 等宽值里；`MeasureCard.cpp:71-78` 把 "水平 "/"垂直 " 前缀塞进值标签）vs 独立 unit caption（`MeasureCard.cpp:139` `spec.unit = "cm"`，`CardBase.cpp:178-186` 10px text3）；`VariableCard.cpp:96` 注释「数值输入框 (cm)」但无 suffix，而 `ConditionDialog.cpp:25`、`SegmentAnchorTab.cpp:83` 用 `setSuffix(" cm")`；`SegmentAnchorTab.cpp:71` 标签写「入切线角(°):」又 `:76` 加 suffix "°"（双单位）。
- **UI-P2-11 placeholder 不带单位**：`LineGeometrySection.cpp:191/:204` `setPlaceholderText("0")`（值域 cm，`:541` `cmToMm(alongCm)`）；`LineEndpointSection.cpp:181` `setPlaceholderText("0")` 但 `:183-184` tooltip 写「延长量 (cm)…只允许 >= 0」且无 setRange；`SegmentConnectionCardBuild.cpp:174` `setPlaceholderText("0")`（偏移(°)）。
- **UI-P2-12** `LineGeometrySection.cpp:327` `QString::number(seg.tension, 'f', 2)` → "1.00"（可用 formatNumberTrimmed）。
- **UI-P2-13** `SegmentAnchorTab.cpp:5-6` 重复 include `ElaTabWidget.h` 两次。
- **UI-P2-14 数值框回填四种格式**：`'g',6`（`IntersectionForm.cpp:90,108`、`AuxPointForm.cpp:177,183`）、`'f',2`/`'f',1`（`PlacedPointDialog.cpp:189,193`）、`formatNumberTrimmed`（多数）、`formatDegValue`/`formatDegTrimmed`（角度族）→ 同一数值在不同对话框显示 "0.5"/"0.50" 不定。

### 4.2 src/parametric + src/document（47 条：P0=7 / P1=30 / P2=10；已剔除已收口项：四组枚举表已表驱动 `DocumentSerializer.cpp:70-129`、`kMaxSettleRounds` 唯一定义 `Resolver.h:42`、迁移链完整、`ParamDocumentStores` 纯转发）

#### P0 —— 用户可见错误

- **PAR-P0-1 `followerAngle` 存储域三套并存 → 同一附件在「组件」页签 270.0°、角度卡片/上下文条 −90.0°**
  - 写端 A（(−180,180]）：`src/parametric/FollowerAngle.h:34` `return cad::geo::normalizeDeg180(cad::geo::radToDeg(refWorldRad + kPi - followerRotRad - localDirRad));`
  - 写端 B（[0,360)）：`src/document/commands/BreakFinish.cpp:228`、`src/document/commands/ReverseSegmentCommand.cpp:424` `att->followerAngle = cad::geo::normalizeDeg360(ac.followerAngle + 180.0);`、`src/tools/LineFactory.cpp:314`、`src/tools/ConnectGestureAngleSession.cpp:175`、`src/ui/SegmentAngleCard.cpp:432`
  - 写端 C（不归一化「闭合基准 180°−x」）：`src/tools/RotateCopyGesture.cpp:240` `a->followerAngle = 180.0 - (m_baseOffsetDeg + deg);`、`src/tools/RotateSession.cpp:268` `a->followerAngle = alpha;`
  - 显示端：`src/ui/ComponentTab.cpp:240-241` `QString::number(cad::geo::normalizeDeg360(a.followerAngle), 'f', 1)` → 270.0 vs `src/ui/SegmentAngleCard.cpp:185` `formatDegValue(normalizeDeg180(value))` → −90.0 vs `src/app/ContextStripDisplay.cpp:107-108`
  - 测试锁定（统一必红）：`tests/test_reverse_segment_commands.cpp:494` `QVERIFY(std::abs(att2->followerAngle - 270.0) < 1e-9);`、`tests/test_rotate_anchor.cpp:366`（`:356` 注释「存储不归一化，≡ −90°」）、`tests/test_rotate_strip.cpp:269`
  - 建议：`FollowerAngle.h` 增加唯一写入口 `setFollowerAngleDeg` 固定一域，显示层一律 `formatDegValue`，同步改 3 个测试。
- **PAR-P0-2 `followerModeSwitchValues` 自身域不一致（`fmod` 负数不折叠）→ 三模切换弧长符号翻转**：`src/parametric/FollowerAngle.h:106/:113` `normalizeDeg360(curDeg)`、`:126` `out.arcMm = degToArcMm(std::fmod(curDeg, 360.0), radiusMm);`（存 −90 → 弧长为负；存 270 → 为正）、`:129` 弦用 `normalizeDeg180`、`:132` `out.angle = curDeg;`。调用方直写模型：`src/app/ContextStripEdit.cpp:200` `st.followerAngle = res.angle;`、`src/ui/SegmentAngleCard.cpp:526`。测试只锁 45°（`tests/test_dialog_tabs_angle_conn.cpp:292`、`tests/test_context_strip.cpp:1094`），**负角/270° 无覆盖**。
- **PAR-P0-3 跨块交点缺曲线分支 → 交点落到弦上**：本块版有曲线 `src/parametric/BlockResolve.cpp:367-377` `if (seg->isCurve()) { ... geo::rayCurveIntersect(...) ... }`；跨块版无 `src/parametric/ResolverIntersection.cpp:133` `double denom = d.cross(segDir);`、`:140-142` `constexpr double eps = 1e-6; bool validT = (t >= -eps && t <= 1.0 + eps);`（Bezier/Arc 宿主按弦求交）。`tests/test_intersection.cpp` grep `isCurve|curve` = 0 命中。建议合并为 `geo::raySegmentOrCurveIntersect(...)`。
- **PAR-P0-4 收敛判定同量两容差（1e-9 vs 1e-6）→ 假 NotConverged 警告进状态栏**：`src/parametric/Resolver.cpp:264` 调 `block.resolveInterpolatedPoints(...)`（内部 `src/parametric/BlockResolve.cpp:51` `distanceSquaredTo(prevPos[i]) > 1e-6`）vs `src/parametric/Resolver.cpp:268-269` `if (k < prevPos.size() && p.resolved && p.resolvedPos.distanceSquaredTo(prevPos[k]) > 1e-9) progressed = true;` → 残差落在 (1e-9,1e-6] 时每轮判 progressed，4 轮后报 NotConverged（经 `src/parametric/ParamDocumentResolver.cpp:30-35` → `src/app/MainWindowStatusBar.cpp:40-51` 显示）。`src/parametric/BlockResolve.cpp:46-47` 注释「sub-nanometre」实为 1e-3mm=1µm，差 1000×。
- **PAR-P0-5 组件级连接走另一套 refWorld → 组件角度基准与普通连接不同**：通用 `src/parametric/ParamDocumentAttachments.cpp:628-629` 后有「母线端点1→端点2」覆盖（`:633-643`）；组件级 `src/parametric/ParamDocumentBlocks.cpp:386-387` `const double refWorld = toBlk.transform.rotation + toBlk.exitDirectionAtPoint(...);` **无母线覆盖**，随后 `:407` `const double targetRot = refWorld + M_PI - angleRad - localDir;`。`tests/test_component.cpp:126/140/237/280/296` 全用 180/0（两基准同向不可区分）。建议改调 `effectiveAngleRefWorld(this, att)`。
- **PAR-P0-6 角度输入框写 360 域、显示 180 域 → 输入 270 回车后回显 −90**：`src/ui/SegmentAngleCard.cpp:432` `newAngle = cad::geo::normalizeDeg360(targetDeg);` vs `:185`（见 P0-1）。无测试断言。
- **PAR-P0-7 angleOnly 命令版与门面版语义分叉**：门面 `src/parametric/ParamDocumentAttachments.cpp:245-249` `it->angleOnly = angleOnly; it->isLocked = !angleOnly; it->slideMode = SlideMode::None;`（无条件清）vs 命令 `src/document/commands/AttachmentLifecycleCommands.cpp:248-255` `if (m_newAngleOnly) { a->isLocked = false; a->slideMode = None; } else { a->isLocked = true; }`（false 时保留 slideMode）→ undo/redo 后滑轨模式残留或丢失。

#### P1 —— 重复实现 / 维护风险（30 条，摘要）

- **PAR-P1-1** `report()` 诊断去重逐字两份：`src/parametric/Resolver.cpp:21-28` 与 `src/parametric/ResolverAttachment.cpp:17-24`。
- **PAR-P1-2 refWorld 计算四份**：`src/parametric/ResolverAttachment.cpp:49-50`、`src/parametric/ResolverSlide.cpp:25-26`、`src/parametric/ParamDocumentAttachments.cpp:623-674`、`:536-537`（releaseBridge 内联）；`:620` 注释引用 `Resolver.cpp:725-769` 已失效。
- **PAR-P1-3 旋转+原点求解与 moved 判定两份**：`src/parametric/ResolverAttachment.cpp:203-217` 与 `:224-244`（`moved = |Δrot|>1e-9 || |Δorigin.x|>1e-6 || |Δorigin.y|>1e-6`）。
- **PAR-P1-4 模式→角度换算三份**：`src/parametric/ResolverAttachment.cpp:110-135`、`src/parametric/ParamDocumentBlocks.cpp:389-406`、`src/ui/SegmentAngleCard.cpp:125-152`（UI 版注释写「0~360° 归一化」但 `:151` `return constDeg;` 未归一化，调用方 `:160` 又 `normalizeDeg180`）。
- **PAR-P1-5 backSolve+清空三件套六份**：`src/parametric/ParamDocumentAttachments.cpp:275-282/:322-329/:539-546`、`src/document/commands/AttachmentAngleCommands.cpp:66-69/:123-126/:190-193`。
- **PAR-P1-6 交点求解两份 + 退化 bootstrap 两份**：`src/parametric/BlockResolve.cpp:292-416` vs `src/parametric/ResolverIntersection.cpp:45-165`；bootstrap `BlockResolve.cpp:327-335` vs `ResolverIntersection.cpp:84-104`；世界角语义差异 `BlockResolve.cpp:357` `theta = angleDeg * M_PI / 180.0 - transform.rotation;`（局部系）vs `ResolverIntersection.cpp:126` `theta = angleDeg * M_PI / 180.0;`（世界系）。
- **PAR-P1-7 CurveAnchor 弦坐标数学两份**：`src/parametric/BlockResolve.cpp:430-452`（正解）与 `src/parametric/ParamDocumentResolver.cpp:594-606`（反解 `pt.interpPercent = rel.dot(unitDir) / len; pt.interpOffsetDist = rel.dot(normal);`）。
- **PAR-P1-8 删除影响快照三份**：`src/document/commands/BlockLifecycleCommands.cpp:59-120`、`src/document/commands/ComponentCommands.cpp:87-133`、`src/parametric/ParamDocumentBlocks.cpp:494-570`。
- **PAR-P1-9 `exitDirectionAtPoint` 双重重载重复曲线切线分支**：`src/parametric/BlockQuery.cpp:57-74` 与 `:159-173`；CurveAnchor 分支只在旧重载 `:103-118`。
- **PAR-P1-10 `touchGeometry` 命令层手写 21 处**：`BlockTransformCommands.cpp:191,207`；`BreakExecution.cpp:189`；`CurveCommands.cpp:51,70,119,134,223,236,326,341`；`ReverseSegmentCommand.cpp:433`；`SegmentPropertyCommands.cpp:81,92,142,152,172,353,362`。
- **PAR-P1-11 `resolveAll(); emit structureChanged();` 成对模板手写 12+ 处**：`AttachmentLifecycleCommands.cpp:202-203,232-233,241-242,267-268,281-282,291-292,301-302`；`ShadowLifecycleCommands.cpp:57-58,73-74,103-104,115-116`。漏一处即面板/画布不同步。
- **PAR-P1-12 滑轨偏移两命令近似重复且 resolve 时机不同**：`src/document/commands/AttachmentSlideCommands.cpp:78-108`（SetSlideOffsetsCommand，redo 不 resolve）vs `:112-162`（SetAttachmentSlideOffsetsCommand，双向 resolve + 写公式 + 写 slideMode）。
- **PAR-P1-13 VariableCommands 18 个模板命令 + 唯一手写信号**：`src/document/commands/VariableCommands.cpp:189-202`。
- **PAR-P1-14 移动块到图层两命令重复**：`src/document/commands/LayerCommands.cpp:118-130` vs `:162-190`。
- **PAR-P1-15 名字唯一性校验全仓缺失**：`src/parametric/VariableStore.cpp:21-26` `addVariable` 直接 push_back；`src/parametric/LayerRegistry.cpp:52-59` `addLayer` 直接 push_back、`:97-106` `renameLayer` 不查重；`src/parametric/MeasurementStore.cpp:21-26` `addLinked` 不查重；`src/parametric/ParamDocumentParameters.cpp:75-78` `m_parameters[name] = cmValue;`（同名静默覆盖）。全仓「已存在/已被占用/重复的/同名」只在 `src/ui/PointRefEdit.cpp:51,52,211`、`src/ui/SegmentConnectionCardBuild.cpp:84,91,151`、`src/ui/SegmentConnectionCardConn.cpp:31` 命中。
- **PAR-P1-16** `findVariable/findFormula` 可变+const 双份：`src/parametric/VariableStore.cpp:56-68`、`:116-128`。
- **PAR-P1-17 图层下限保护两份**：`src/parametric/LayerRegistry.cpp:61-70` 与 `src/parametric/ParamDocumentIndexes.cpp:38-45`（均 `if (n <= 2) return;`）。
- **PAR-P1-18 chord 钳制四处**：`src/app/ContextStripEdit.cpp:97`、`src/ui/SegmentAngleCard.cpp:427`、`src/tools/ConnectGestureAngleSession.cpp:167`、`src/tools/RotateSession.cpp:326` 均 `chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);`；`src/geometry/Angle.h:83` 内部又 clamp 一次。
- **PAR-P1-19 rotationMode/slideMode 以 int 直存 + 手写范围钳制**：`src/document/DocumentSerializer.cpp:439` `{"rotationMode", static_cast<int>(a.rotationMode)},`、`:447`、读端 `:476-479`、`:490-493`（与 `:70-129` 四组字符串表防御模式不统一，插入枚举值即改变磁盘语义）。
- **PAR-P1-20 adjustMode/measureKind 两组枚举仍手写**：`src/document/DocumentSerializer.cpp:131-136`、`:661-670`。
- **PAR-P1-21「Optional since vN」注释与 `kFormatVersion=4` 全面冲突（22 处，v2..v12）**：`DocumentSerializer.cpp:290(v12)`、`:397(v3)`、`:398(v12)`、`:399(v3)`、`:401(v4)`、`:427(v9)`、`:459(v9)`、`:465-466(v12)`、`:484(v3)`、`:485(v4)`、`:486(v5)`、`:488(v6)`、`:496(v7)`、`:517(v10)`、`:529-531(v8/v10)`、`:731(v4)`。
- **PAR-P1-22 序列化读端默认值复制模型默认值**：`DocumentSerializer.cpp:249` `p.interAngle = o["interAngle"].toDouble(90.0);`（模型 `src/parametric/ParamPoint.h:91` 默认 90.0）、`:254` `interpPercent.toDouble(0.5)`（`ParamPoint.h:116`）、`:246` ratio 0.5、`:169` step 1.0、`:268-269` 缺键 true。
- **PAR-P1-23 手写 M_PI 23 处**：`BlockResolve.cpp:123,241,283,357,359,525,600`；`Resolver.cpp:335` `double offsetRad = offsetDeg * M_PI / 180.0;`；`ResolverAttachment.cpp:134`；`ResolverIntersection.cpp:126,128`；`MeasurementStore.cpp:449-450`；`BreakAnalysis.cpp:110,132,243`；`BreakFinish.cpp:221,227`；`ReverseSegmentCommand.cpp:392-393`。
- **PAR-P1-24 BreakFinish 同函数两分支两种归一化**：`src/document/commands/BreakFinish.cpp:221` `deg = normalizeDeg180(deg + st.refDeltaRad * 180.0 / M_PI);`（Chord）vs `:227-228` `normalizeDeg360`（Angle）。
- **PAR-P1-25「闭合基准 180°−x」写值无共享 helper**：`RotateCopyGesture.cpp:84,194,240,259`；`RotateSession.cpp:268,296,465,473,511,560`。
- **PAR-P1-26 ConnectGestureAngleSession 会话内三套写值**：`src/tools/ConnectGestureAngleSession.cpp:139`（`= m_initialAngle`）、`:175`（`normalizeDeg360(numVal)`）、`:193`（`= r.value` 公式不归一化）。
- **PAR-P1-27 LineFactory 同文件两种归一化**：`src/tools/LineFactory.cpp:314,318` `normalizeDeg360` vs `:536` `normalizeDeg180`。
- **PAR-P1-28 命令层 `touchGeometry+resolveAll` 成对重复**：`SegmentPropertyCommands.cpp:81/84,92/95,142/144,152/154,172,353/354,362/363`；`CurveCommands.cpp:51-52,70-71,119-120,134-135,223-224,236-237,326-327,341-342`。
- **PAR-P1-29 EndpointCommands 前置守卫重复**：`src/document/commands/EndpointCommands.cpp:293/295,305/307,351/353,374/376`（`if (!m_doc) return; if (!blk) return;`）。
- **PAR-P1-30 命令 undo 快照逐字段手写**：`AttachmentAngleCommands.cpp:11-24` `restoreAngleState`（11 字段）、`ReverseSegmentCommand.h:91`、`SegmentPropertyCommands.h:157`。新增字段漏拷贝即 undo 丢状态。

#### P2（10 条）

`LayerRegistry.cpp:12-23` 硬编码图层名（无 tr()）｜`src/parametric/Attachment.h:142` `bool isLocked = false;` 与 `ParamDocumentAttachments.cpp:104,151` 强制 true 双默认、`DocumentSerializer.cpp:485` 读端给 false｜`BlockResolve.cpp:46-47` 注释「sub-nanometre」与 1e-6 阈值差 1000×｜`ParamDocumentAttachments.cpp:620` 失效行号引用｜`DocumentFile.cpp:22` `kAppVersion = "0.1.0"` 与 `FormatMigration.h:38` `kFormatVersion=4` 两个版本号｜`ParamDocumentStores.cpp:27-128` 约 100 行纯转发｜`VariableStore.cpp:44-54` `updateVariable` 未命中仍 emit｜`AttachmentAngleCommands.cpp:61` 与 `:72` 两分支都写 `slideMode = None`｜`AttachmentAngleCommands.cpp:231-246` SetAlignPointCommand redo/undo 不对称｜`AttachmentAngleCommands.cpp:54-75` 快照过宽（改 1 字段恢复 11 字段）。

### 4.3 src/canvas + src/geometry + src/app（47 条：P0=10 / P1=25 / P2=12）

#### P0 —— 用户可见错误

- **CAN-P0-1 同一线段「基准角」与「角度」两套区间**：`src/app/ContextStripDisplay.cpp:79` `baseDeg = normalizeDeg180(radToDeg(refWorldRad));` vs `:119` `double worldDeg = normalizeDeg360(ep->angle + rotDeg);`；`:88` `baseDeg = normalizeDeg180(wd.angle() * 180.0 / M_PI);`。自由线段两框描述同一世界方向 → 一个 −30、另一个 330。测试锁定：`tests/test_context_strip.cpp:1271`（`"-90"`）+ `:1272` isReadOnly、`tests/test_rotate_strip.cpp:278`；角度框 0..360 侧无区分性锁定（`test_context_strip.cpp:939 "180"`、`:952 "0"、`:737/:1218 "45"` 两域同值）。
- **CAN-P0-2 线段长度三套口径**：上下文条 `src/app/ContextStripDisplay.cpp:62-64` `(ep->constraint == OrthoOffset) ? ep->distance : sp->resolvedPos.distanceTo(ep->resolvedPos)`（只显示主轴腿）；画布标签 `src/canvas/BlockGeometryCache.cpp:153` `w1.distanceTo(w2)`（`worldPos`，含延长尾巴，`:152` 注释「长度标注按实际画出的长度（本体+尾巴, D6）」）；权威 `src/parametric/BlockExtend.cpp:57-67` `Block::segmentEffectiveLength`（被 `LinkedVariable.cpp:19`、`MeasurementStore.cpp:153`、`BlockQuery.cpp:198/203`、`src/ui/LineGeometrySection.cpp:381/419` 使用）；退化回退 `LinkedVariable.cpp:24-25` `lv.value = std::hypot(ep->distance, ep->orthoOffsetDist);` → 上下文条 10 vs 画布/属性面板 10.44。
- **CAN-P0-3 放置点数值格式/单位绕过 Units（无测试锁定，可安全统一）**：`src/app/PlacedPointStripBar.cpp:121` `QString::number(pt->interpOffsetDist / 10.0, 'f', 1)`、`:122`（`:172/:175` 同）→ "12.0" vs `src/app/ContextStripDisplay.cpp:65-66` `formatNumberTrimmed(mmToCm(mm))` → "12"；写回 `PlacedPointStripBar.cpp:255` `newPt.interpOffsetDist = distCm * 10.0;`。
- **CAN-P0-4 悬停点高亮色/半径两套，CanvasStyle hover 语言被短路**：`src/canvas/BlockItemPainter.cpp:228-230` `if (pc.id == ctx.hoveredPointId) { pp.pointFill = QColor(38, 166, 154); pp.pointRadius = 1.6; }` vs 权威 `src/canvas/CanvasStyle.cpp:110` `case EntityState::Hover: return m_hoverTint;`（204,120,92）+ `:126` 半径 `base` → 点的 hover 颜色动画永远不可见。
- **CAN-P0-5 拾取容差两套：画布 8px vs 右键扫描 16px**：`src/app/MainWindowSegmentMenu.cpp:38` `const double tol = 16.0 / (zoom > 1e-4 ? zoom : 1.0);` vs `src/canvas/CanvasStyle.h:150` `m_hoverRadiusPx = 8.0`（经 `src/canvas/BlockItemPick.cpp:16-19`、`src/canvas/BlockItem.cpp:65`）；第三份 `src/canvas/CanvasView.cpp:472` `const double tolerance = 8.0 / zoom;`；`zoom > 1e-4` 自写哨兵绕过 `src/canvas/CanvasView.h:100` ZOOM_MIN=0.2。
- **CAN-P0-6 状态栏坐标初值与运行期格式不同**：`src/app/MainWindowStatusBar.cpp:61` `new ElaText("X: 0.000  Y: 0.000", 12, this)` vs `src/app/MainWindow.cpp:331` `m_coordLabel->setText(Units::formatPoint(x, y))`（2 位 + "cm"）→ 启动 3 位无单位、动一下变 2 位带 cm。
- **CAN-P0-7 面板页签键帽 P/L 与真实快捷键 Ctrl+1/2 不符且冲突**：`src/app/MainWindowPanelWindow.cpp:137` `QStringLiteral("P"),`、`:142` `"L",` vs `:193` `new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this)`；P/L 已被 `src/tools/ToolPlacePoint.cpp:28`、`src/tools/ToolSmartPen.cpp:56` 占用且经 `src/app/MainWindowMenuBar.cpp:88-89` `setShortcutContext(Qt::ApplicationShortcut)` 全局生效。
- **CAN-P0-8 画布背景色三份字面量 + 两条 QSS 路径**：`src/canvas/CanvasView.cpp:74` `setBackgroundBrush(QColor(250, 249, 245));`、`:101` 回退、`src/canvas/CanvasStyle.h:106` `canvasBackground = QColor(250, 249, 245)`；`CanvasView.cpp:88-96` `applyCanvasBackground` 与 `src/canvas/CanvasScene.cpp:300-309` `setStyle()` 逐行相同（后者 `:296-299` 注释自称「唯一权威主题切换路径」）。
- **CAN-P0-9 HudItem 胶囊色复制 CanvasStyle 且深色值漂移**：`src/canvas/HudItem.cpp:14-16` `kPillBg(250,249,245,245)`/`kPillBorder(213,208,197,220)`/`kPillFg(20,20,19)`（= `CanvasStyle.h:99-101`）；`:105-107` 深色边框 `QColor(77, 73, 67, 220)` 非任何 hud token；`:110-119` 深色 `bg = QColor(31, 30, 29, 245)` 复制 `CanvasStyle.cpp:177-178` 但 alpha 245≠240。
- **CAN-P0-10 右键命中几何源与绘制几何源不同（延长尾巴不可命中）**：`src/canvas/CanvasView.cpp:488-489` `blk->transform.toWorld(pSp->resolvedPos)` vs 绘制/标签 `src/canvas/BlockGeometryCache.cpp:136-137` `block->worldPos(...)`（含尾巴）→ 尾巴上右键找不到线段、左键却能选中。

#### P1 —— 重复实现 / 维护风险（25 条，摘要）

- **CAN-P1-1 颜色守卫对 canvas 静默失效（含 `lstrip` bug，已复核）**：`tools/check_hardcoded_colors.py:46` 只匹配 `#[0-9A-Fa-f]{6}`；`:56` `if rel.replace(os.sep, '/').lstrip('src/') in EXEMPT_FILES:` —— `lstrip` 按**字符集**剥离，`'src/canvas/CanvasStyle.h'` → `'anvas/CanvasStyle.h'`，**canvas 两个豁免文件从未命中**。建议改 `startswith` 前缀判断 + 增加 `QColor(r,g,b)` 扫描。
- **CAN-P1-2 TransientOverlay 15 处硬编码色 + 默认实参色**：`src/canvas/overlay/TransientOverlay.cpp:99` `QPen(QColor(0, 172, 193), 2.0)`、`:116` `(140, 100, 0)`、`:119` `(255, 193, 7)`、`:218` `(33, 150, 243)`、`:219` `(33,150,243,30)`、`:239` `(38, 166, 154)`、`:242` `(38,166,154,40)`、`:252`、`:266`、`:283` `(120, 144, 156)`、`:300` `(245, 124, 0)`、`:322` `(251, 140, 0)`、`:325` `(251,140,0,38)`、`:342` `(255,255,255)`、`:353` `(255,255,255,120)`、`:354` `(33,33,33,210)`；`src/canvas/overlay/TransientOverlay.h:71` 默认实参 `const QColor& color = QColor(150, 150, 150)`。
- **CAN-P1-3 BlockItemPainter 回退色/灰显层与 CurveItem 逐字重复**：`BlockItemPainter.cpp:62` `const QColor kGray = (style && style->dark) ? QColor(176, 171, 160) : QColor(0x9E, 0x9E, 0x9E);` ＝ `CurveItem.cpp:153`；`:64` 0.55/0.4 ＝ `CurveItem.cpp:155`；`:70` `constexpr int kGhostAlpha = 110;` ＝ `CurveItem.cpp:180`；回退色 `:86/:168`、`:156/:212`、`:204`、`:205`、`:207`、`:267`；点半径 `:206` 0.8、`:216-217` ETCAD pink 0.8、`:222-223` `QColor(255, 140, 0)` 1.1（= `CanvasStyle.h:141` `m_pointRadius=0.8`）。
- **CAN-P1-4 手写点到线段距离两处**（`Vec2::distanceToSegment` 已有唯一实现）：`src/canvas/BlockItemPick.cpp:70-84`、`src/canvas/CanvasView.cpp:506-518`；权威 `src/geometry/Vec2.h:76-83`。
- **CAN-P1-5 第二套命中测试**：`src/canvas/CanvasView.cpp:468-497` 自己遍历 block/segment，绕过 `src/tools/HitTester.h` 与 `BlockItemPick`。
- **CAN-P1-6 拾取半径注释与代码矛盾**：`src/canvas/BlockItemPick.cpp:44-45` 注释「PICK radius is unified at 2.5」但 `:47` `const double rPx = pc.isPlaced ? 6.0 : 2.5;`；`CanvasStyle.h:136-137` 注释称 shape 用 2.5 实际 `BlockItem.cpp:65` 用 `hoverRadiusPx`(8.0)；8.0 共 4 处（`CanvasStyle.h:150`、`BlockItem.cpp:65`、`CurveItem.cpp:108`、`CanvasView.cpp:472`）；`kPickMargin = 42.0` 两份（`BlockItem.cpp:47`、`CurveItem.cpp:93`，后者 `:92` 注释给出 `hoverRadiusPx(8)/ZOOM_MIN(0.2)=40` 推导）。
- **CAN-P1-7 注释「Points are deliberately NOT hover targets」与实现矛盾**：`src/canvas/BlockItemPick.cpp:59-62` vs `BlockItem.h:134` `hitTestPoint`、`BlockItem.h:104` `m_hoveredPointId`、`BlockItemPainter.cpp:228`。
- **CAN-P1-8 字体族/字号绕过 Theme（5 处）**：`src/canvas/HudItem.cpp:20` `QFont f("Segoe UI"); f.setPixelSize(11);`、`src/canvas/OverlapBatteryHud.cpp:27`、`:363` `standardFont(8.5, false)`、`src/canvas/overlay/TransientOverlay.cpp:339` `QFont("Segoe UI", 9, QFont::Bold)`、`src/canvas/BlockItemPainter.cpp:18-40` + `src/canvas/CurveItem.cpp:19-36`；Theme 无公开字体工厂，`Theme.cpp:372-379` 的中文回退栈（Noto Sans SC / Microsoft YaHei UI）全部丢失。
- **CAN-P1-9 手写角度换算/归一化 8 处**：`src/canvas/BlockGeometryCache.cpp:178`、`:108`、`src/app/ContextStripDisplay.cpp:88/:118`、`src/app/ContextStripEdit.cpp:120`、`src/canvas/overlay/TransientOverlay.cpp:314/:378`、`src/canvas/CanvasScene.cpp:546-547/:553-554`（= `Angle.h:48-53 normalizeRad`）、`src/geometry/CurveMath.cpp:8-10` `#ifndef M_PI` + `:166-171` `double normAnglePi(double a)`。
- **CAN-P1-10 CurveMath Thomas 算法两份 + flattenSpan 复刻点到线段距离**：`src/geometry/CurveMath.cpp:130` `solveTridiagonal(vector<double> a, b, c, vector<Vec2> d)` 与 `:148` `solveTridiagonalScalar(..., vector<double> d)` 仅 d 类型不同；`:892-895` 复刻 `Vec2.h:76-83`。
- **CAN-P1-11** `BlockGeometryCache.cpp:115-117` 与 `:143-145` LineStyle→Qt::PenStyle 映射写两遍。
- **CAN-P1-12 几何容差四档散落**：`src/geometry/Vec2.h:47` `if (len < 1e-12)`、`:79`、`src/geometry/RayCast.cpp:12` `1e-9`（默认 `RayCast.h:22` `eps = 1e-6`）、`src/geometry/TriangleUnfold.cpp:60` `kEpsilon = 1e-10`、`:71` `1e-8 * std::max({1.0, ...})`、`:156`、`CurveMath.cpp` 20+ 处。
- **CAN-P1-13 CanvasScene 手写两直线求交**：`src/canvas/CanvasScene.cpp:502-508`（`:505` `if (std::abs(denom) > 1e-9)`）vs `src/geometry/RayCast.cpp:12`。
- **CAN-P1-14 shape 缓存容差策略两份（魔法比例 0.02）**：`src/canvas/BlockItem.cpp:70-71` vs `src/canvas/CurveItem.cpp:120-121`；成员 `BlockItem.h:120-121` / `CurveItem.h:81-82`。
- **CAN-P1-15 缩放因子取值三份**：`CanvasScene.cpp:79-81`、`CanvasView.cpp:157-161` `zoomFactor()`、`OverlapBatteryHud.cpp:96`。
- **CAN-P1-16 上下文条脚手架重复**：`kbdBadge` 两份（`src/app/ContextStripDisplay.cpp:31-36` = `src/app/ContextStripSessions.cpp:22-27`，硬编码 `'Consolas',monospace; font-size:10px`）；serial chip QSS 三份（`ContextStrip.cpp:82-84`、`ContextStripDisplay.cpp:351-353`、`PlacedPointStripBar.cpp:41-43/:283-285`）；`addField/addPtField` 两份（`ContextStrip.cpp:87-101`、`PlacedPointStripBar.cpp:46-57`，`kFieldH = 30` 两份：`ContextStrip.cpp:77`、`PlacedPointStripBar.cpp:36`）；Tab/Backtab 焦点循环两份（`ContextStrip.cpp:505-529`、`PlacedPointStripBar.cpp:328-338`）；剪贴板清洗逐字两份（`ContextStripEdit.cpp:139` 与 `:152`）。
- **CAN-P1-17 输入解析不一致**：`src/app/PlacedPointStripBar.cpp:216/:234/:254/:256` 裸 `toDouble` vs `src/app/ContextStripEdit.cpp:53/:77` `parseNumberOrFormula` → 线段框支持公式、放置点框不支持。
- **CAN-P1-18 `Units::formatAngle` 死代码**：`src/geometry/Units.h:59-61` 全仓仅定义无调用；手写 `"= %1°"`：`src/ui/SegmentAngleCard.cpp:82/:169/:565`、`src/ui/RotateHintTexts.cpp:28`、`src/ui/SegmentAngleRefCard.cpp:162`。
- **CAN-P1-19 `foldedArcDisplay/foldedChordDisplay` 用 `formatDegValue` 格式化长度值**：`src/app/ContextStripDisplay.cpp:136-153`（角度格式化器输出 cm 长度）；`src/app/ContextStripEdit.cpp:97` `chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);`（`Angle.h:80-93` 内部已有 ratio 截断）。
- **CAN-P1-20 角度「显示归一 / 写回不归一」不对称**：显示 `src/app/ContextStripDisplay.cpp:119-121` `normalizeDeg360(ep->angle + rotDeg)` + 锚点 +180；写回 `src/app/ContextStripEdit.cpp:121-123` `st.endAngle = (targetDeg - anchorOffset) - rotDeg;`（不归一）→ 往返不闭合。
- **CAN-P1-21 颜色混合两份**：`src/canvas/CanvasAnimator.cpp:22-29` `lerpColor` vs `src/canvas/CanvasStyle.cpp:70-74`（ratio `m_hoverTintRatio=0.55`）。
- **CAN-P1-22 app 层 QSS 逐字重复**：toolPill 三份（`MainWindowToolBar.cpp:56-62`、`MainWindowPanelWindow.cpp:319-325`、`MainWindow.cpp:170-176`，且 `MainWindow.cpp:170` 设完又于 `:189` `refreshPanelChrome()` 再设一次）；stripBand 两份（`MainWindowPanelWindow.cpp:157`、`MainWindow.cpp:180`）；ghostBtn 两份（`MainWindowPanelWindow.cpp:85-89`、`:342-346`）。
- **CAN-P1-23 工具图标两套体系 + 快捷键三处分裂**：`src/app/MainWindowToolBar.cpp:24` `static const QHash<ToolType, ElaIconType::IconName> kIcons`（`:25-33` 9 项硬表）vs 菜单 `src/app/MainWindowMenuBar.cpp:83` `IconHelper::iconByName(d->iconName, ...)`（源 `src/tools/ToolRegistry.h:36` `QString iconName;`）；快捷键：`src/canvas/CanvasView.cpp:297` `if (event->key() == Qt::Key_N || event->key() == Qt::Key_M)`、MenuBar registry、工具内 `src/tools/ToolSelect.cpp:749`（`Key_W || Key_B`）、`ToolMeasure.cpp:149`、`ToolIntersection.cpp:135`、`ToolSmartPen.cpp:437`。
- **CAN-P1-24 字号/圆角/间距字面量绕过 Theme**：`MainWindowStatusBar.cpp:22` 13、`:28/:61/:66` 12、`:91` 11px；圆角 2px（`MainWindowToolBar.cpp:60`、`PanelWindow.cpp:87/323/344`、`StatusBar.cpp:91`）、4px（`PanelWindow.cpp:157/329`、`MainWindow.cpp:180`）；间距 `MainWindowToolBar.cpp:50` `setContentsMargins(10, 5, 10, 5)`、`:64/:65/:81/:83`、`PanelWindow.cpp:59/:60/:66/:160/:161/:173/:174`、`MenuBar.cpp:26-27`。
- **CAN-P1-25 `onAccent` token 存在却被手写 `#FFFFFF` 顶掉**：`src/app/MainWindowStatusBar.cpp:90` `"QToolButton { background: %1; color: #FFFFFF; border: none;"  // color-allow` vs `src/ui/Theme.h:42` `QColor onAccent; ///< Text on accent fills (纯白 #FFFFFF).`、`Theme.cpp:81/:120`。

#### P2（12 条）

`CanvasStyle.h:77-83` 注释重复两遍｜`src/canvas/OriginCrosshair.h:17` `EXTENT = 100000.0` vs `src/canvas/CanvasView.h:103` `SCENE_BOUND=10000.0`（差 10 倍）、`OriginCrosshair.cpp:25` `QColor(200,200,200)`｜`src/canvas/DirectionMarker.h:29` `kSpread = 25.0 * M_PI / 180.0`｜`CanvasView.cpp:131-134` 注释「1cm」与代码 `dotStep = 1.0`（1mm）矛盾、`5.0 / zoom` 两处｜`BlockItem.cpp:298`/`:346` 魔法 Z 值 1.0/1.5｜amber 四值（`CanvasScene.cpp:432/:528` `QColor(0xFF,0x98,0x00)`、`TransientOverlay.cpp:119 (255,193,7)`、`:322 (251,140,0)`、`:300 (245,124,0)`、`BlockItemPainter.cpp:222 (255,140,0)`）｜`ToolDockStyle.h:70` `drawRoundedRect(r, 3, 3)`、`HudItem.cpp:125` 3.5、`:92` 4.0、`CanvasScene.cpp:433` 5.0｜`OverlapBatteryHud.cpp:203-204` 每帧构造回退样式、`:220` `tk.accent = s.previewLineColor`、`:238/:308` 阴影色、`:146` 8.0/2.0、`:170` 3.0、`:179` 4.0｜`CanvasAnimator.cpp:42` 16ms、`CanvasScene.cpp:557` `40.0 / zoom`、`:559` `kSamples = 40`｜缩放百分比两处（`MainWindow.cpp:337`、`MainWindowStatusBar.cpp:66`）+ 窗口尺寸两处（`MainWindow.cpp:76`、`MainWindowPanelWindow.cpp:361`）｜`MainWindowStatusBar.cpp:29` 与 `:62` 都用 `setObjectName("coordLabel")` 重名｜`MainWindowPanelWindow.cpp:55-56` vs `:363-364` 两套尺寸约束、`MainWindow.cpp:47` 暗色判断被 `:78-80` 无条件覆盖（死代码）、`MainWindowFileSession.cpp:132-133` `.gcad` 手写两处（权威 `src/document/DocumentFile.h:19` `kExtension`）、`MainWindowFileSession.cpp:58` 窗口标题遗留构建标签 `[P207-ABS]`。

#### 已确认无问题（勿重复报告）

`src/geometry/Vec2.h:76-83` `distanceToSegment` 唯一权威（`src/canvas/CanvasView.cpp:490`、`src/app/MainWindowSegmentMenu.cpp:49`、`src/tools/LeaderCandidatePicker.cpp:144`、`src/tools/ToolSelectActions.cpp:360` 均正确调用）｜ZOOM_MIN/ZOOM_MAX/SCENE_BOUND 唯一定义 `src/canvas/CanvasView.h:100-103`｜坐标转换已收口（`BlockItem.cpp:142/195`、`BlockGeometryCache.cpp:54/81/140/141/182/229`、`HudItem.cpp:56` 全走 `Coord::toScene/toUser`）｜CanvasStyle 与 Theme 颜色当前完全一致｜`CurveSplitter.cpp`、`TriangleUnfold.cpp` 无重复｜`BlockGeometryCache.cpp:113/:154` 正确用 `formatLength`｜N/M 按住显示键仅 `CanvasView.cpp:297-306/:313-327`。

### 4.4 src/tools（53 条：P0=11 / P1=35 / P2=7；含二次复核补充）

> **审计前提**：范围 = `src/tools/**` 全量（不含 QWidget），逐文件读**工作树**版本。审计时工作树本就有未提交修改（`src/tools/ToolRotate.*`、`ToolSelect.*`、`ToolSelectActions.cpp`、`SelectDragController.cpp`、`RotateHintTexts.*`、`ConnectGestureAttach.cpp`、`Tool.h`、`ToolManager.*`、`src/app/ContextStrip*`、`MainWindow*`、`src/document/commands/ReverseSegmentCommand.*`、`src/ui/SegmentAngleRefCard.cpp`、`tests/test_context_strip.cpp` 等）→ 行号对应工作树，不是 HEAD。二次复核子代理未返回，select/drag/curve 共 14 个文件由审计者本人逐行取证（仅缺交叉验证）。

#### P0（5 条，用户可见错误）

- **TOOL-P0-1 followerAngle 写入三域、显示三域（与 §P0-2、PAR-P0-1 同根，此处补全 src/tools 侧证据）**
  - 写端：`src/tools/LineFactory.cpp:314` `followerAngle = cad::geo::normalizeDeg360(opts.displayAngleDeg);` 与 `:318` `normalizeDeg360(180.0 - (angleDeg - refWorldRad * 180.0 / M_PI))` **vs** `:536-538` `att.followerAngle = cad::geo::normalizeDeg180(180.0 - (worldAngleDeg - refWorldRad * 180.0 / M_PI));`（`:308` 注释自写「归一化 [0, 360°)」，`:530` 同一公式 —— 同文件两域）；`src/tools/ConnectGestureAngleSession.cpp:139` `= m_initialAngle`（(-180,180]）、`:175` `normalizeDeg360(numVal)`、`:193` `= r.value`（公式原样，不归一化）；`src/tools/RotateSession.cpp:255` `normalizeDeg360(deg)` → `:268` `a->followerAngle = alpha;` vs `:263` chord 分支 `normalizeDeg180`；`src/tools/RotateCopyGesture.cpp:84/:194/:240/:259` `180.0 - (m_baseOffsetDeg + ...)`；`src/app/ContextStripEdit.cpp:102` `st.followerAngle = targetDeg;`（不归一化）vs `src/ui/SegmentAngleCard.cpp:432` `normalizeDeg360(targetDeg)`。
  - 显示端：`src/ui/ComponentTab.cpp:240-241` `QString::number(cad::geo::normalizeDeg360(a.followerAngle), 'f', 1)` → "270.0" **vs** `src/app/ContextStripDisplay.cpp:106-109` `formatDegValue(cad::geo::normalizeDeg180(att->followerAngle))` → "-90" **vs** `src/ui/SegmentAngleCard.cpp:185` `formatDegValue(cad::geo::normalizeDeg180(value))`、`:145` `double constDeg = att.followerAngle;`（原样）。
  - 统一建议：写端一律走 `src/parametric/FollowerAngle.h:30-36 backSolveFollowerAngle`（全仓唯一反算入口），存储定死一域，显示只用 `Units.h:94 formatDegValue` + 该域。
  - 测试锁定：**存储域 360 已锁** —— `tests/test_reverse_segment_commands.cpp:494`（270.0）、`tests/test_rotate_anchor.cpp:366`（270.0）、`tests/test_rotate_strip.cpp:159/:269`（1260 多圈）；**显示域无任何测试**。

- **TOOL-P0-2 同一 cm 长度两种显示精度（同值 "7.9" vs "7.85"）**
  - `src/app/ContextStripDisplay.cpp:136-140` `formatDegValue(cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius)))`（1 位）、`:143-154` chord 同式 **vs** `src/ui/SegmentAngleCard.cpp:176-183` 用 `formatNumberTrimmed`（2 位）。
  - 同族：`src/tools/ToolPlacePoint.cpp:368` `.arg(dist / 10.0, 0, 'f', 1)`（底栏 1 位）vs `src/ui/PlacedPointDialog.cpp:189` `QString::number(pt->interpOffsetDist / 10.0, 'f', 2)`（对话框 2 位）；4 处各写 `/10.0` 绕过 `mmToCm`。
  - 统一建议：cm 长度一律 `Units.h:47 formatLength(mm, 2)` 或 `:75 formatCmTrimmed`。测试：无锁定。

- **TOOL-P0-3 旋转角连接态 / 自由态两个归一化域（同朝向 270 vs −90）**
  - `src/tools/RotateSession.cpp:381-444 currentAngleDeg`：连接分支全 `normalizeDeg180`（`:393/:399/:413/:422/:427/:429`）**vs** 自由分支 `:442` `deg = cad::geo::normalizeDeg360(deg);`。
  - `src/tools/RotateDragMath.cpp:96` `QString::asprintf("%.1f°", cad::geo::normalizeDeg180(in.currentAngleDeg))` **vs** `:98` `normalizeDeg360`；`src/tools/RotateAimSnap.cpp:122-123` `normalizeDeg180` **vs** `:125` `normalizeDeg360`。
  - 危害：同一次旋转，连接块显示 −90、自由块显示 270；HUD 与吸附读数互相矛盾。建议 `currentAngleDeg` 统一一域（推荐 180 折角）并三处共用同一函数。测试：无锁定（grep `badgeText|currentAngleDeg` 仅断言「跟随/弧长/开度」字样）。

- **TOOL-P0-4 HUD 角度数字两套格式（"90.0°" vs "90"）**
  - `src/tools/RotateDragMath.cpp:94/:96/:98` `QString::asprintf("%.1f°", ...)` **vs** `src/tools/RotateHintTexts.cpp:28/:37/:38` `formatDegValue(...)`（去尾零 1 位）。同一角度两种文本。
  - 统一建议：全部走 `Units.h:94 formatDegValue`。测试：`tests/test_transient_overlay.cpp:144` 只是**传入**字符串 "45.0°"、无文本断言 → **§6 原「被该测试锁定」是误判，统一不会破测试**。

- **TOOL-P0-5 交点工具「悬停」与「点击」两套拾取半径（8px vs 12px）**
  - `src/tools/ToolIntersection.cpp:330` `m_scene->style()->hoverRadiusPx()` **vs** `:268` `m_snapEngine.snapRadius`。
  - 危害：离点 9~12px 悬停不显示瞄准标记、点击却借用该点 → 射线方向突跳。建议统一到 `CanvasStyle.h:150 hoverRadiusPx()`（或统一 `snapRadius`，必须择一）。测试：tests 内 grep `snapRadius|hoverRadiusPx` = 0 命中 → 无锁定。

#### P1 —— 重复实现 / 维护风险（26 条）

- **TOOL-P1-1 旋转双状态机**：`src/tools/ToolRotate.h:31-35 RotateState` 与 `:38-43 RotatePhase` 并存；`m_phase` 生产代码 17 处只写不读（`ToolRotate.cpp:107/115/125/203/223/245/271/274/356/409/414/419/518/548/573/821/823`），唯一读取在 `tests/test_rotate_pivot_multi.cpp:251/259/268/275/283/289` `QCOMPARE(tool->phase(), ...)` → 死状态机靠测试续命；删 `RotatePhase` 需同步改该测试。
- **TOOL-P1-2 连接角↔世界方向公式手写 ≥5 处**：`src/tools/RotateSession.cpp:632` `return (m_refWorldRad + M_PI - alpha * M_PI / 180.0) * 180.0 / M_PI;`、`:275-276` `const double newRot = deg * M_PI / 180.0 - anchorOffsetRad - m_localDir;`；`src/tools/RotateAimSnap.cpp:32-33/:93-94` `worldDirRad = refWorldRad + M_PI - normalizeDeg360(angleDeg) * M_PI / 180.0;`；`src/tools/RotateDragMath.cpp:44/:77/:80`；`src/tools/RotateCopyGesture.cpp:290/:295`。中央逆式 `FollowerAngle.h:30 backSolveFollowerAngle` 仅被 `RotateDragMath.cpp:46` 调用。
- **TOOL-P1-3 闭合基准 `180.0 - (baseOffset+rel)` 6 处无 helper**：`RotateCopyGesture.cpp:84/:194/:240/:259`（写）、`:276/:324`（反解）；`LineFactory.cpp:318`；`ToolSmartPen.cpp:609` `normalizeDeg180(180.0 - rel)`；`SmartPenStrokeInput.cpp:101` `refDirDeg + (180.0 - displayDeg)`。建议抽 `closedBasisAngle()`。
- **TOOL-P1-4 M_PI 本地重定义 3 处且都已 include Angle.h**：`src/tools/RotateAimSnap.cpp:13-15`、`src/tools/RotateSession.cpp:21-23`、`src/tools/ToolRotate.cpp:27-29`（`#ifndef M_PI / #define M_PI 3.14159265358979323846 / #endif`）；src/tools 内原始 `* M_PI / 180.0`、`* 180.0 / M_PI` 约 63 处（新增行号：`ToolSmartPen.cpp:245/:599/:620`、`SmartPenStrokeInput.cpp:71/:90`、`LineFactory.cpp:110/:256/:318/:443/:452/:538`、`ToolIntersection.cpp:352/:408`、`ToolPlacePoint.cpp:275/:289/:301`）。
- **TOOL-P1-5 手写角度环绕 while 8 处（等价 normalizeRad，NaN 会死循环）**：`RotateAimSnap.cpp:100-101/:192-193`、`RotateSession.cpp:676-677/:691-692/:701-702`；度数版 `ToolPlacePoint.cpp:277-278` `while (diffDeg > 180.0) diffDeg -= 360.0; while (diffDeg <= -180.0) diffDeg += 360.0;`（= normalizeDeg180）。正确用 normalizeRad 的：`RotateDragMath.cpp:19/:42`、`RotateCopyGesture.cpp:125`。
- **TOOL-P1-6 吸附步长硬编码**：15° 五处 `RotateDragMath.cpp:26` `return std::round(accumulatedAngleDeg / 15.0) * 15.0;`、`:36/:45/:49/:56`（文案 `RotateHintTexts.cpp:18/:20` 各写「约束15°」）；45° 四处 `ToolSmartPen.cpp:618` `const double snappedRel = std::round(relDeg / 45.0) * 45.0;`、`ToolPlacePoint.cpp:283`、`IntersectionAngleAim.h:40` 与 `:43`（同一头文件内自我复制）。建议 `Angle.h snapDeg(deg, step)`。
- **TOOL-P1-7 端点扫描绕过 SnapEngine（缺 selectable/图层/isShadow/bridgeNonAux 过滤）**：`RotateInputTracker.cpp:27-48`（`:27` zoom 守卫、`:28` `const double tol = 12.0 / zoom;`、`:37` `if (p && p->resolved)`、`:38` `blk.worldPos(pid)`）；`RotateAimSnap.cpp:76-80/:164-168` `if (!pt.resolved || !pt.selectable) continue;` + `blk.transform.toWorld(pt.resolvedPos)`（无延长线）；`LeaderCandidatePicker.cpp:43-47`（无 selectable/layer/isShadow）；`OverlapDisambiguationController.cpp:67-77`（只判 layer!=activeLayer + pt.resolved）；`ConnectOverlapResolver.cpp:48/:51/:76-78` `sp->resolvedPos.distanceTo(local) < kSnapOverlapEps`；`SelectHoverFeedback.cpp:51-65/:67-95`（`:78` isBridge、`:79` layer，无 isShadow）；`ToolIntersection.cpp:486-489/:496-499` 全表扫描两份。建议一律走 `SnapEngine::findSnap` 或 `HitTester.h:33 blockHitsAtScene`。
- **TOOL-P1-8 点世界坐标两套写法**：`Block::worldPos(pointId)`（= `transform.toWorld(effectiveLocalPos)`，含端点延长线，`src/parametric/BlockQuery.cpp:9-12`）**vs** 手写 `transform.toWorld(pt.resolvedPos)`（不含延长线）。手写处：`RotateAimSnap.cpp:80/:168`、`LeaderCandidatePicker.cpp:46`、`MarqueeGesture.cpp:129-130`、`ConnectOverlapResolver.cpp:48/:51/:76-78`、`ToolPlacePoint.cpp:209-210/:249-250`、`ToolIntersection.cpp:171-172/:194-195/:314-315/:399-400/:488`、`SmartPenAuxPoint.cpp:164`；正确用 worldPos 的：`SnapEngine.cpp:395`、`RotateDragMath.cpp:104-127`、`OverlapDisambiguationController.cpp:72`、`LineFactory.cpp:203/:401/:407` → 同一工具内（RotateAimSnap vs RotateDragMath）最近点结果不同。
- **TOOL-P1-9 拾取半径多值多单位**（见下表 18 处 /zoom + 常量）；线身吸附两派：8px（`ToolSmartPen.cpp:707`、`ToolBreak.cpp:89/:154`、`SmartPenEndConfirm.cpp:123/:181`）vs 12px（`ToolPlacePoint.cpp:118/:226` 传 −1.0 → `SnapEngine.h:54` 取 snapRadius、`ToolIntersection.cpp:157/:184` 传 snapRadius）；同名重复：`SelectHoverFeedback.cpp:12 constexpr double kConnectGrabRadius = 10.0;` 与 `ConnectGesture.h:37` 同名同值。
- **TOOL-P1-10 容差同语义多值**：`ToolAngleMeasure.cpp:267/:271/:277/:280 lengthSquared() < 1e-8` vs `:249-251` 1e-12 vs `:259 std::abs(denom) > 1e-9`；`RotateAimSnap.cpp:149 if (r0 < 1e-4)` vs 中央 1e-9；退化距离平方 1e-10（`ToolSmartPen.cpp:174/:466`、`LineFactory.cpp:409`）vs 长度 1e-12（`SmartPenStrokeInput.cpp:82`）vs 1e-9（`ToolPlacePoint.cpp:212/:252`、`ToolIntersection.cpp:317/:402`、`ToolBreak.cpp:245`）vs 角度 1e-6 度（`RotateCopyGesture.cpp:325`）vs 旋转 1e-9 rad / 原点 1e-6 mm（`RotateSession.cpp:509/:523-526/:551-556`、`MultiRotateSession.cpp:125-126`）。建议 `kEpsLen/kEpsLenSq/kEpsAngle`。
- **TOOL-P1-11 硬编码颜色绕过 CanvasStyle token**：`ConnectOverlapResolver.cpp:112 QPen pen(QColor(0xF39C12), 3.0);`、`:176 QColor(47, 111, 237, 120)`、`:207/:238 QColor(38, 166, 154)`、`:211 (38,166,154,50)`、`:243 (38,166,154,20)` vs `CanvasStyle.h:85 attachmentNodeColor(42,123,136)` 与 `OverlapBatteryHud.cpp:230 tk.teal = s.attachmentNodeColor`；`MarqueeGesture.cpp:78 QPen pen(QColor(0, 120, 215), 0);`、`:81 setBrush(QColor(0, 120, 215, 25));`（无暗色适配）；`ToolAngleMeasure.cpp:155` 与 `ToolMeasure.cpp:306` 同色 `QColor(0xFF, 0x98, 0x00)`；`ToolCurveEditHandles.cpp:113-116` 四个 QColor；`ToolCurveEditAnchor.cpp:62 QColor(0xE9, 0x1E, 0x63)`、`:180 QColor(0x4C, 0xAF, 0x50)`。
- **TOOL-P1-12 影子块/图层过滤缺失（违反 `HitTester.h:47`「影子不可命中」唯一规则）**：`MarqueeGesture.cpp:124 if (blk.layer != m_doc->activeLayer()) continue;`（无 isShadow/selectable → 影子块可被框选、点选却选不中）；`MultiRotateSession.cpp:29-33 if (!blk->isBridge)`、`ToolRotate.cpp:323` 框选同样只剔 isBridge。建议统一走 `HitTester.h:33`。
- **TOOL-P1-13 会话状态机复制粘贴**：`RotateCopyGesture.cpp:114-230 convert()` 与 `:22-112 begin()` 约 80 行重复（克隆创建/对齐点/endTarget/addLinked/addBlock/addAttachment 两份）、`:232-264 applyAngle` 与 `applyFormulaValue` 近乎逐行相同；`ConnectGesture.cpp:267-289 move()` 与 `:386-422 release()` 近乎逐行重复、`:293 const double threshold = (kConnectSnapRadius / zoom) * 3.5;`（魔数 3.5）；`LineFactory.cpp` 三处造线骨架 `:100-124/:252-268/:439-482`；`SmartPenEndConfirm.cpp:125-131` 与 `:183-190` 同一候选匹配循环两份；`SmartPenEndConfirm.cpp:145-149` 与 `SmartPenAuxPoint.cpp:126-130` 同四行「起点自由+终点吸附=翻转」。
- **TOOL-P1-14 `RotateSession.cpp:668-709 calculateGizmoAngles` 两分支几乎逐行相同**（仅 dashRad 不同）；`:310-335 applyModeValue` 的 arc/chord 走 `doc->resolveAll(); scene->refreshAllBlockItems();` 而角度走定向 resolveForDrag，`:317 a->arcLength = cad::geo::Units::cmToMm(value);` 不折叠 vs `:255-258` 折叠。
- **TOOL-P1-15 本地格式化绕过 Units**：`IntersectionToolVisuals.cpp:215/:225 QString::number(displayDeg, 'f', 1)`、`:228 QString::number(t, 'f', 3)`；`ToolAngleMeasure.cpp:199 QStringLiteral("%1°").arg(res.angleDeg, 0, 'f', 1)`；`ToolPlacePoint.cpp:368/:369 .arg(..., 0, 'f', 1)`；`ToolSmartPen.cpp:635 Units::formatLength(lenMm, 1)`（同值面板 2 位）、`:639 "  %1°").arg(m_snapAngleDeg, 0, 'f', 0)`（0 位 vs 交点 1 位）。
- **TOOL-P1-16 `ConnectGesture.cpp` 内半径混用与守卫不一**：半径 `:202/:463/:509` hoverRadiusPx 8、`:257/:270/:387` kConnectSnapRadius 7.5、`:628/:635` kConnectGrabRadius 10；zoom 守卫 `:186-187/:356-357/:443-444/:519` 有、`:506/:625/:634` 无。
- **TOOL-P1-17 `ConnectGestureAttach.cpp:110-120` 手写母线基准**：`double refWorld = toBlk->transform.rotation;` + `const geo::Vec2 w1 = toBlk->transform.toWorld(sp->resolvedPos);`/`w2...` + `if (w1.distanceTo(w2) > 1e-6) refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);`（与 `ParamDocumentAttachments.cpp:623-674` 逐行相同，退化阈值 1e-6 vs 中央 1e-9）；同文件 `:386-390` 组件候选路径又用未加母线覆盖的 `toBlk->transform.rotation + toBlk->exitDirectionAtPoint(...)` → 同文件两条连接路径两套基准；`:38` isShadow 不可连。
- **TOOL-P1-18 `LeaderCandidatePicker.cpp:106-108`** `m_refDirDeg = (lb->transform.rotation + lb->exitDirectionAtPoint(...)) * 180.0 / M_PI;` 绕过 `effectiveAngleRefWorld`；`:34` tol=snapRadius(12) vs `:129` tol=hoverRadiusPx(8)（同类两半径）。
- **TOOL-P1-19 `ToolSelectHitTest.cpp:21-35`** hitBlock/hitSegmentAt 各调一次 `blockHitsAtScene`（同一帧两次全场景扫描）；`:75-110 filterCands` 内联资格判定；`:124 < kSnapOverlapEps`。
- **TOOL-P1-20 放置点工具以 `ignoreLayerFilter=true` 在灰色非活动层持久建点**：`ToolPlacePoint.cpp:114/:118/:187/:226` 传 true，而 `:392-394 m_undoStack->push(new cad::cmd::AddAuxPointCommand(...));` —— 与 `SnapEngine.h:59-63` 该参数前提「For tools that reference points WITHOUT creating attachments」冲突。建议放置点改走 `HitTester.h:33`；交点工具（只建 Intersection 约束点）可保留。
- **TOOL-P1-21 `ToolIntersection.cpp:352 double theta = segAngleRad + m_currentAngleDeg * M_PI / 180.0;`** 与 `:408 double theta = baseAngle + angleDeg * M_PI / 180.0;`（`ResolverIntersection.cpp:128` 同式第三份）；`:350` 已调 computeIntersection 求交点、`:352` 又重算同一 theta；`:310-318` 与 `:395-404` 目标线段几何解析复制两份。
- **TOOL-P1-22 `IntersectionAngleAim.h:28 worldDeg = std::atan2(toAim.y, toAim.x) * 180.0 / std::numbers::pi;`**、`:31`、`:33 segAngleDeg = segAngleRad * 180.0 / std::numbers::pi;`（`:4` 已 include Angle.h）；`:40/:43` 硬编码 45.0 吸附。
- **TOOL-P1-23 `RotateGizmo.cpp:21/:39 (void)zoom;`**（参数未用）；`:83 std::abs(m_deltaDeg) <= 1e-4` vs `RotateDragMath.cpp:93 if (std::abs(out.deltaDeg) > 0.01)`（同语义差 100 倍）；`:29/:46/:64` 三段相同 show/hide。
- **TOOL-P1-24 死声明 / 重复成员**：`ToolSmartPen.h:175 [[nodiscard]] double toWorldAngleDeg(double displayDeg) const;`（真实现 `SmartPenStrokeInput.h:62 / .cpp:95`，调用点 `ToolSmartPen.cpp:300`，.cpp 内零引用）；`ToolSmartPen.h:211-213 m_leaderCandidates/m_leaderIndex/m_highlightBlockId` 与 `LeaderCandidatePicker.h:58-60` 同名同义重复。
- **TOOL-P1-25 数值解析**：src/tools 全目录 `toDouble|toFloat` = 0 命中 → 未绕过 `Units.h:99 parseNumberOrFormula`（该问题集中在 src/ui、src/app）。
- **TOOL-P1-26 交互阈值散落**：`SelectHoverFeedback.h:28 kDragThresholdPx=5.0`、`ToolSelect.cpp:39 kOverlapSelectThresholdPx=5.0`、`ToolSelectActions.cpp:340 kTolerancePx=8.0`、`ToolMeasure.cpp:28 kAxisZeroEps=0.05`、`SnapEngine.h:20` 与 `OverlapDisambiguationController.cpp:87 kOverlapEps=0.5mm`（重复）、`RotateAimSnap.cpp:70 alignTolRad=2°`、`ConnectOverlapResolver.cpp:24 kSourcePortRadiusPx=5.0` —— 同一「命中容差」家族 8 套值。

#### P2（4 条）

- **TOOL-P2-1 zValue 全目录无中央常量（约 40 处）**：`ConnectOverlapResolver.cpp:116=101/:177=100/:212=9999/:244=9998`；`IntersectionToolVisuals.cpp:41=100/:66=102/:90=102/:116=101/:131=103/:156=103`；`MarqueeGesture.cpp:82=9999`；`ToolAngleMeasure.cpp:158/:181=101`；`ToolCurveEditAnchor.cpp:63=103/:184=106`；`ToolCurveEditHandles.cpp:125/132=104、:141/150=105`；`ToolMeasure.cpp:111=102/:310=101`；`ToolPlacePoint.cpp:320=100/:349=101`；`ToolSmartPen.cpp:335=100/:345=101/:354=102/:727=102`。建议 `CanvasStyle` 出 OverlayZ 常量表。
- **TOOL-P2-2 zoom 守卫写法约 30 处重复（1e-9 各写一遍）**：`SnapEngine.cpp:28/125/176/231`、`ConnectGesture.cpp:187/357/444/519`、`RotateSession.cpp:213`、`RotateAimSnap.cpp:69/158`、`RotateInputTracker.cpp:27`、`ToolRotate.cpp:773`、`ToolSelect.cpp:433/440/594/677/752`、`ToolSelectActions.cpp:316/383`、`OverlapDisambiguationController.cpp:55`、`LeaderCandidatePicker.cpp:30/126`、`ConnectOverlapResolver.cpp:200/230`、`CopyDragController.cpp:89`、`CurveAnchorDragSession.cpp:18`、`ToolCurveEditHandles.cpp:190`、`SelectHoverFeedback.cpp:35/43`。
- **TOOL-P2-3 默认角 90.0 硬编码三处**：`ToolIntersection.cpp:529-530 m_currentAngleDeg = 90.0; m_displayAngleDeg = 90.0;`、`ParamPoint.h:91`；`ToolPlacePoint.cpp:220` 与 `:259` 同一整句提示文案两份。
- **TOOL-P2-4 HUD 数字精度无约定**：`ToolSmartPen.cpp:609/:618/:639` 与 `IntersectionToolVisuals` 的 0/1/3 位混用。

#### 拾取半径速查表（同「拾取半径」多值多单位）

`SnapEngine.h:48` 12.0px｜`CanvasStyle.h:150` 8.0px｜`ConnectGesture.h:32` 7.5｜`ConnectGesture.h:37` 10.0｜`SelectHoverFeedback.cpp:12` 10.0（重复）｜`SelectHoverFeedback.h:28` 5.0｜`OverlapDisambiguationController.cpp:57` 10.0/zoom｜`RotateInputTracker.cpp:28` 12.0/zoom｜`RotateSession.cpp:213` 8.0/zoom｜`RotateAimSnap.cpp:68/157` 15.0px｜`ToolRotate.cpp:261` 5.0/zoom、`:315` 3.0/zoom｜`ToolSelect.cpp:679` 4.0/zoom、`:39` 5.0｜`ToolSelectActions.cpp:340` 8.0、`:384` 10.0/zoom｜`ConnectOverlapResolver.cpp:24` 5.0｜`SnapEngine.h:20` & `OverlapDisambiguationController.cpp:87` 0.5mm｜`ToolMeasure.cpp:28` 0.05。

#### 测试锁定汇总（src/tools 相关，统一化前必读）

- **锁定 followerAngle 存储域为 360**：`tests/test_reverse_segment_commands.cpp:494`（270.0）、`tests/test_rotate_anchor.cpp:366`（270.0）、`tests/test_rotate_strip.cpp:159/:269`（1260 多圈）→ 改 (−180,180] 会破这 4 处。
- **normalizeDeg180/360 语义**：`tests/test_curve.cpp:656-698`（270→−90、−180→180、inf/nan→0）。
- **RotatePhase**：`tests/test_rotate_pivot_multi.cpp:251/259/268/275/283/289`。
- **D15 确认门**：`tests/test_rotate_d15_gate.cpp` 5 用例（**不**锁 15° 步长）。
- **半径/容差常量**：tests 内 grep `kConnectSnapRadius|kConnectGrabRadius|snapRadius|hoverRadiusPx|searchRadiusPx` = 0 命中 → 全部无锁定，可安全统一。
- **旋转 HUD 角度文本**：无断言（`tests/test_transient_overlay.cpp:144` 仅传参 "45.0°"）→ 可安全统一。**修正 §6 原「被该测试锁定」的误判。**
- **显示精度/域（TOOL-P0-1 显示端、TOOL-P0-2）**：无测试 → 可安全统一。
- **未覆盖**：二次复核子代理未返回（`SelectDragController.cpp`、`CopyDragController.cpp`、`ToolSelect.cpp`、`ToolSelectHitTest.cpp`、`ToolSelectActions.cpp`、`SelectHoverFeedback.cpp`、`MarqueeGesture.cpp`、`CurveAnchorDragSession.cpp`、`ToolCurveEditAnchor.cpp`、`ToolCurveEditHandles.cpp`、`OverlapDisambiguationController.cpp`、`ConnectOverlapResolver.cpp`、`LeaderCandidatePicker.cpp`、`MultiRotateSession.cpp`）—— 上述条目均由审计者逐行取证，仅缺第三方交叉验证。

#### P0 补充（TOOL-P0-6 ~ P0-11，来自二次复核子代理交叉取证）

- **TOOL-P0-6 同一次 mouseRelease「是否算拖动」两个阈值（4px vs 5px）**：¦src/tools/ToolSelect.cpp:679 if (moveDist < 4.0 / zoom && boxed.isEmpty()) { clearSelectionAndIdle(); }¦ **vs** ¦src/tools/CopyDragController.cpp:89-90 const double thresh = 5.0 / (zoom > 1e-9 ? zoom : 1.0); if (delta.length() > thresh && m_undoStack)¦。4~5px 区间：选择集已按 4px 清空、复制却被当点击丢弃 → 同一手势两种后果。统一：¦SelectHoverFeedback.h:28 kDragThresholdPx=5.0/zoom¦，并抽单一「点击 vs 拖动」判定。tests：无（¦tests/test_select_wkey.cpp:114¦ 只覆盖 copy 提交）。
- **TOOL-P0-7 移动拖动提交阈值是绝对 1e-10 mm²（与 zoom 无关，量纲错误）**：¦src/tools/SelectDragController.cpp:156 if (delta.lengthSquared() <= 1e-10) {¦（¦:153-155¦ 注释称「几乎没动的拖动按单击回退」）。1e-10 mm² ≈ 1e-5 mm，与屏幕像素无关 → 任何抖动都推 MoveBlockCommand，注释语义失效，且与复制侧 5px 不同量纲。统一：同 P0-6。tests：无。
- **TOOL-P0-8 跨选集 attachment 断开规则三份，且 pin 处理相反**：¦src/tools/SelectDragController.cpp:49 if ((fromIn && !toIn) || (!fromIn && toIn)) {¦（**无 !isPin**）**vs** ¦src/tools/MultiRotateSession.cpp:59 if (((fromIn && !toIn) || (!fromIn && toIn)) && !a.isPin) {¦ **vs** ¦src/parametric/Duplicate.h:43-50¦「Follower to OUTSIDE leader → dropped / Bridge losing outside pin → released」。同一焊接连接：拖动会断、旋转不断 → 同组对象换工具连接就消失。统一：param 层单一「跨边界连接策略」函数（操作类型作参数）。tests：¦tests/test_attachment_shadow.cpp¦ / ¦tests/test_attachment_slide.cpp¦ 覆盖 attachment 语义，但**无 pin 跨选集用例** → 未锁定。
- **TOOL-P0-9 「重叠候选」可见性过滤三套互不相同**：¦src/tools/OverlapDisambiguationController.cpp:68-69 if (!activeLayer.isNull() && blk.layer != activeLayer) continue;¦（**无 isShadow**）**vs** ¦src/tools/ConnectOverlapResolver.cpp:42 if (block.isShadow) continue;  // 影子不可作为连接目标 (R4, 拆开影子基准)¦（**无图层**）**vs** ¦src/tools/HitTester.h:47-48 if (blk->isShadow) continue; if (blk->layer != doc.activeLayer()) continue;¦。同一处点击：电池 HUD 列出影子块的点、连接确认列出隐藏层的点，而 hover/拾取两者都不给。统一：¦HitTester.h:33 blockHitsAtScene¦。tests：¦tests/test_overlap_battery.cpp:115/:212¦ 只测同层非影子 → 未锁定。
- **TOOL-P0-10 同一「曲线点/手柄」抓取半径 8px vs 10px**：¦src/tools/CurveAnchorDragSession.cpp:18 const double radius = 10.0 / std::max(zoom, 1e-9);  // 10px screen radius¦ **vs** ¦src/tools/ToolCurveEditHandles.cpp:190 const double radius = 8.0 / std::max(zoom, 1e-9);  // screen-space hit radius¦ **vs** ¦src/canvas/CanvasStyle.h:150 double m_hoverRadiusPx = 8.0;¦。同一根曲线切线手柄 8px 点不中、锚点 10px 能中。统一：¦CanvasStyle::hoverRadiusPx()¦。tests：无。
- **TOOL-P0-11 角度引导候选扫描无任何可见性过滤**（主报告列为 P1，二次复核定为 P0）：¦src/tools/LeaderCandidatePicker.cpp:43-47 for (const auto& pt : blk.points) { if (!pt.resolved) continue; const Vec2 wp = block.transform.toWorld(pt.resolvedPos);¦。隐藏层/影子/不可选点都能成为基准目标 → 跟随角取自用户看不见的几何，画面与数值不一致。统一：对齐 ¦SelectHoverFeedback.cpp:77-79¦（isBridge/图层/selectable）并改 ¦blk.worldPos(pt.id)¦。tests：无。

#### P1 补充（TOOL-P1-27 ~ P1-35，二次复核新增；P1-8/P1-9/P2-2 另有新增行号）

- **TOOL-P1-27 重叠候选顺序两套**：¦OverlapDisambiguationController.cpp:82-84¦ 按到光标距离排序 **vs** ¦ConnectOverlapResolver.cpp:40-54¦ 按 ¦blocks()/segments()¦ 文档顺序 push → W 键循环顺序与连接确认顺序不同。tests：无。
- **TOOL-P1-28 layerName 循环逐字重复**：¦OverlapDisambiguationController.cpp:133-135¦ 与 ¦:168-170¦ 同一 ¦for (const auto& l : m_paramDoc->layers()) if (l.id == blk.layer) { c.layerName = l.name; break; }¦。tests：无。
- **TOOL-P1-29 BatteryCandidate/HUD 构造两份**：¦OverlapDisambiguationController.cpp:277-304¦ 与 ¦ConnectOverlapResolver.cpp:268-297¦（两个独立 ¦m_batteryHud¦）。tests：¦test_overlap_battery.cpp¦ 只覆盖 Overlap 侧。
- **TOOL-P1-30 手柄世界坐标数学两份**：¦ToolCurveEditHandles.cpp:105-107 inWorld = block->transform.toWorld(pLocal - tanIn / 3.0); outWorld = ... + tanOut / 3.0;¦ 与 ¦:187-188¦ 逐字重复 → 改偏移会「画出的手柄」与「点得中的位置」错位。tests：¦tests/test_curve_edit.cpp:226¦ 只测切线点积。
- **TOOL-P1-31 选集语义两套且头注释与实现相反**：¦SelectDragController.cpp:23 const QSet<QUuid> dragSet = selection;¦（¦:22¦ 注释「不通过 lockedClosure 扩充未选对象」）**vs** ¦ToolSelect.cpp:469-471 m_paramDoc->componentsView().closure(m_paramDoc->attachmentsView().lockedClosure(m_selection))¦；而 ¦SelectDragController.h:39¦ 注释写「快照拖动集 (锁焊闭包 + 组件闭包)」→ Ctrl 复制带上锁定闭包未选对象、普通拖动不带。tests：无。
- **TOOL-P1-32 临时断连两套策略**：¦SelectDragController.cpp:96 m_paramDoc->resolveForDrag(m_blockIds, m_detachedAttachments);¦（¦ParamDocument.h:519-520¦ ignoredAttachments 仅解算时跳过）**vs** ¦MultiRotateSession.cpp:66-68 for (const auto& a : m_multiReleasedAtts) { doc->removeAttachment(a.id); }¦ 再 ¦:89/:151-153 addAttachmentRaw¦ 补回 → 旋转期间 attachment 真的不存在（序列化/其他观察者可见）。tests：未覆盖会话中途文档状态。
- **TOOL-P1-33 undo 提交路径三套**：¦SelectDragController.cpp:161-191¦ 一个 beginMacro 包 RemoveAttachment/SetAttachmentAngleOnly/SetSlideOffsets/MoveBlockCommand；¦CopyDragController.cpp:93-94¦ 单条 DuplicateBlocksCommand；¦MultiRotateSession.cpp:143-155¦ 手工还原 oldTf + addAttachmentRaw 再 RotateBlocksCommand。tests：仅 ¦tests/test_select_wkey.cpp:114¦ 覆盖 copy。
- **TOOL-P1-34 拖动后刷新三套**：¦CopyDragController.cpp:99/:132 if (m_scene) m_scene->refreshAllBlockItems();¦（一次复制两次全量重建）**vs** ¦MultiRotateSession.cpp:112 if (scene) scene->syncBlockPositions();¦ **vs** SelectDragController 无显式刷新（¦CurveAnchorDragSession.cpp:71-75¦ 注释确认为正确做法）。建议热点路径禁 ¦refreshAllBlockItems¦。
- **TOOL-P1-35 hover 光标判定叠加第二次全文档点扫描**：¦ToolSelect.cpp:604¦ → ¦SelectHoverFeedback.cpp:44 if (findEndpointNear(doc, pos, worldR))¦，而 ¦:599¦ 刚做过 ¦blockHitsAtScene¦。
- **补充行号（并入已有条目）**：
  - TOOL-P1-8（toWorld 绕过 worldPos）新增：¦CurveAnchorDragSession.cpp:34¦、¦ToolCurveEditAnchor.cpp:220¦、¦ToolSelectActions.cpp:358-359¦。
  - TOOL-P1-9（半径）新增：同一「曲线点/手柄」族见 TOOL-P0-10；¦ToolCurveEditAnchor.cpp:59 constexpr double r = 2.2;¦、¦:178 constexpr double r = 7.0;¦、¦:175/:210¦ 传 ¦-1.0¦（¦SnapEngine.h:69¦ 哨兵=12px）而 ¦:40¦ 用 ¦hoverRadiusPx()¦=8px → **同一工具 8/12/7 三套**。
  - TOOL-P2-2（zoom 守卫）新增：¦ConnectOverlapResolver.cpp:138 double zoom = m_scene->currentZoom();¦ 后**无守卫**直接 findSegmentSnap（同文件 ¦:200/:230¦ 有守卫）；¦SelectDragController.cpp:143 bool SelectDragController::end(const cad::geo::Vec2& pos, double zoom)¦ 的 zoom 参数**从未使用**；¦CopyDragController.cpp:88¦ 反向自取。建议抽 ¦CanvasScene::safeZoom()¦。
  - TOOL-P1-19 补充：¦ToolSelect.cpp:459 + :463¦、¦:498 + :502¦ 同样成对调用（同一帧两次全场景命中，与 ¦HitTester.h:31-32¦ 注释「为消除同一帧 2~3 遍全场景命中（M7 卡顿源）」的设计意图矛盾）。

#### P2 补充（TOOL-P2-5 ~ P2-7）

- **TOOL-P2-5 死常量**：¦src/tools/ToolSelect.cpp:39 constexpr double kOverlapSelectThresholdPx = 5.0;¦（全仓唯一命中=定义行，死代码，与 ¦SelectHoverFeedback.h:28 kDragThresholdPx¦ 同值异名）。
- **TOOL-P2-6 字符串编码两套**：¦ToolSelect.cpp:55 reinterpret_cast<const char*>(u8"选择")¦ **vs** ¦:51/:294/:303 QString::fromUtf8(...)¦；¦SelectDragController.cpp:162¦ 与 ¦ToolSelectActions.cpp:114¦ 用十六进制转义中文（不可 grep）。
- **TOOL-P2-7 回调命名两套**：¦SelectDragController.cpp:71 m_stateFn(SelectState::Dragging);¦ **vs** ¦CopyDragController.cpp:60 m_setState(SelectState::CopyDragging);¦（¦CopyDragController.h:59 std::function<void(SelectState)> m_setState;¦）。

#### 测试锁定补充（二次复核精确核验）

- **LineFactory 两域写入路径无测试**：无测试引用 LineFactory/displayAngleDeg；¦tests/test_attachment_angle.cpp:28¦「闭合基准: 90° = 垂直」与 ¦tests/test_aux_layer.cpp:949¦ 锁的是 (−180,180] → [0,360) 写入路径**无人守**。
- **放置点显示无锁定**：¦tests/test_place_point.cpp:96/:211/:455¦ 只断言 mm 值。
- **交点半径无锁定**：¦tests/test_intersection*.cpp¦ 只锁 interAngle 语义。
- **8px 是既有基线**：¦tests/test_select_wkey_gestures.cpp:64¦ 注释「命中带 = hoverRadiusPx(8) ÷ 0.5 = 16 场景单位」。
- **测试自身也写 M_PI**：¦tests/test_attachment_angle.cpp:214¦。

## 5. 建议的统一管理模块（按「投入产出比」排序，含具体 API）

> 原则：**已有的收口点不要新建文件，缺的才建**。下面每项都给出建议签名与「替换哪些调用点」，可直接当工单用。

### 5.1 改守卫（成本最低、收益最高，0 行产品代码）

| # | 动作 | 解哪些条目 |
|---|---|---|
| G1 | `tools/check_hardcoded_colors.py:46` 增加 `QColor\(\s*\d` 与 `Qt::(black\|white\|gray\|red\|…)` 与 QSS `rgba\(` 检测 | P0-1、CAN-P1-2、CAN-P1-3、CAN-P1-25 |
| G2 | `tools/check_hardcoded_colors.py:56` `lstrip('src/')` → `startswith` 前缀判断（**先修 G1 再修 G2**，否则 canvas 立刻红） | P0-1 的豁免失效 |
| G3 | 新增 `tools/check_theme_sync.py`（或 `tests/test_theme_sync.cpp`）：断言 `ThemeTokens` 与 `CanvasStyle` 同名 token 数值相等 | P0-3 |
| G4 | 新增 `tools/check_inline_units.py`：禁 `M_PI`、裸 `* 180.0 / M_PI`、裸 `/ 10.0`、裸 `'f', N`（豁免 geometry/Angle.h、Units.h） | P1-5、P1-6、CAN-P1-9、PAR-P1-23 |

### 5.2 扩已有收口点（不新增文件）

| # | 位置 | 建议新增 API | 替换调用点 |
|---|---|---|---|
| U1 | `src/geometry/Angle.h` | `toStorageDeg(double)` / `toDisplayFoldDeg(double)` / `clampChordMm(chord, r)` | 12 个同时用两域的文件（§P0-2 表）+ 4 处 chord 钳制 |
| U2 | `src/parametric/FollowerAngle.h` | `setFollowerAngleDeg(Attachment&, double)`（唯一写入口，固定一域）、`resolveAttachmentAngleRad(att, r, params, cond)`、`closedBaseFollowerAngle(base, rel)` | PAR-P0-1/P0-2/P1-4/P1-5/P1-24/P1-25/P1-26/P1-27 |
| U3 | `src/parametric/ParamDocument*.cpp` 门面 | `findFollowerAttachmentOf(blockId)`（8 份自由函数/成员合一）、`commitRawChange()`（`resolveAll + emit structureChanged` 12+ 处）、`touchAndResolve(blockId)`（21 处 `touchGeometry`）、`deleteImpactReport(blockId)`（命令层 2 份复用） | P1-2、PAR-P1-8/P1-10/P1-11/P1-28 |
| U4 | `src/geometry/Units.h` | `formatLength`/`formatCmTrimmed` 二选一定为标准（建议 `formatLength` 用于「量测结果」、`formatCmTrimmed` 用于「可编辑框」）；`formatAngle` 删除或接上；`mmToCm/cmToMm` 强制 | P1-6、CAN-P1-18、§3.6 全部裸格式化 |
| U5 | `src/geometry/Vec2.h` / `RayCast.h` | 已有 `distanceToSegment`、`raySegment`；把 `CurveMath.cpp:892-895`、`BlockItemPick.cpp:70-84`、`CanvasView.cpp:506-518`、`CanvasScene.cpp:502-508` 改为调用 | CAN-P1-4/P1-10/P1-13 |
| U6 | `src/geometry/CurveMath.h` | `raySegmentOrCurveIntersect(...)`（合并 `BlockResolve.cpp:292-416` 与 `ResolverIntersection.cpp:45-165`）、`chordToLocal/localToChord` | PAR-P0-3、PAR-P1-6/P1-7 |
| U7 | `src/canvas/CanvasStyle.h` | 补 hud 暗色 token、`pointRadiusHover`、`formatRotationBadge(deg, mode)` | CAN-P0-4/P0-8/P0-9/P1-3、P0-4 |
| U8 | `src/tools/HitTester.h` | `bool isPickable(const Block&, const ParamDocument&)`（`isShadow` + 活动层 + `selectable` + `!isBridge` 四谓词合一） | TOOL-P0-9/P0-11、TOOL-P1-7/P1-12、CAN-P0-1 |
| U9 | `src/tools/InteractionTolerances.h`（即 N2）或 `SelectHoverFeedback.h` | `kDragThresholdPx`、`bool isDrag(const QSet<QUuid>& sel, const Vec2& delta, double zoom)` —— 单一「点击 vs 拖动」判定 | TOOL-P0-6/P0-7 |
| U10 | `src/parametric/`（新函数或 `Duplicate.h` 旁） | `crossSelectionPolicy(fromIn, toIn, isPin, isBridge, OperationKind)` —— 拖动/旋转/复制共用一套跨边界连接规则 | TOOL-P0-8、TOOL-P1-32 |
| U11 | `src/canvas/CanvasScene.h` | `double safeZoom() const`（`zoom > 1e-9 ? zoom : 1.0`） | TOOL-P2-2 的约 30 处重复 + TOOL-P1-35 缺失守卫 |

### 5.3 新增文件（仅当上面无法容纳）

| # | 新文件 | 内容 | 解哪些 |
|---|---|---|---|
| N1 | `src/geometry/Epsilon.h` | `kEpsGeom=1e-9`、`kEpsSqConverge=1e-6`、`kEpsPoint=1e-6`、`kEpsDisplay=1e-10` + 语义注释 | P1-4、PAR-P0-4、CAN-P1-12、264 处字面量 |
| N2 | `src/tools/InteractionTolerances.h` | `hoverRadiusPx=8.0`、`pickRadiusPx=2.5/6.0`、`connectSnapRadiusPx=7.5`、`portRadiusPx=5.0`、`aimSearchRadiusPx=15.0`、`dragThresholdPx=5.0`、`overlapEpsPx=0.5`、`axisZeroEpsPx=0.05` | P1-3、CAN-P0-5、CAN-P1-6 |
| N3 | `src/document/SchemaKeys.h` | 11 个 JSON 键 + 4 组枚举字符串表（含 `rotationMode/slideMode/adjustMode/measureKind`） | P1-7、PAR-P1-19/P1-20 |
| N4 | `src/ui/UiStrings.h` | 「自动测量，不可编辑」「绝对角度」「重新连接位置」「Esc 回位」等 73 组文案 + 统一 `kbdBadge`/`addField` 工厂 | P2 文案重复、CAN-P1-16 |
| N5 | `src/ui/FormScaffold.h` 扩展 | `makeCardInput(objectName, decimals, validator)`（当前全 src/ui 仅 7 处 setRange、0 处 setValidator） | UI-P1-8、CAN-P1-17 |

### 5.4 命令层机械重复的收口（P2 但量大）

- `QUndoCommand` 子类样板：**76 个命令类全部直接继承 `QUndoCommand`**（`src/document/commands/*.h` 与 `src/parametric` 命令层），每个重复 `m_doc/m_blockId/m_segmentId` + 构造转发 4 行 → 建议新增 `class DocCommand : public QUndoCommand`（持 `ParamDocument* m_doc`，可选 `m_blockId/m_segmentId`），预计消掉 60+ 份样板。
- 工具头文件 `mousePress/mouseMove/mouseRelease/keyPress` 四行 override **不是重复**：`src/tools/Tool.h:166` 起为纯虚接口，派生类必须声明 —— 不要为此引入宏或中间类。

---

## 6. 改之前必须先动的契约

| 现有锁定 | 位置 | 影响 |
|---|---|---|
| 基准角显示折角域 | `tests/test_context_strip.cpp:1269-1271`、`CONVENTIONS.md:25` | 统一角度域必须先改契约 + 该测试 |
| 副标签 `= 2°` | `tests/test_dialog_tabs_angle_conn.cpp:331/367/426` | 改副标签格式会破 |
| `normalizeDeg180/360` 语义 | `tests/test_curve.cpp:656-698` | 不能改语义，只能加 API |
| 文件尺寸例外 3 条 | `redline_exceptions.json`（`DocumentSerializer.cpp` 987/900、`CurveMath.cpp` 928/500、`ReverseSegmentCommand.cpp` 458/400） | 重构这些文件需同步更新例外 |
| 卡片副值 `= 45°` / `= 2°` / `= 10 cm` | `tests/test_rotate_copy_endtarget.cpp:205,280,245`、`tests/test_dialog_tabs_angle_conn.cpp:367,426` | 统一角度/弧长格式会破 |
| ~~HUD 角度文本 `45.0°`~~ | ~~`tests/test_transient_overlay.cpp:144`~~ | **误判（已修正）**：该测试只把字符串传参、无文本断言 → HUD 格式可安全统一（见 TOOL-P0-4） |
| 控件 objectName 契约 | `tests/test_dialog_tabs_angle_conn.cpp:73`（`startExtendEdit`）、`tests/test_ortho_offset.cpp:67,121,171,248,353`（`editOrthoDist`/`editLength`） | 重构表单结构会破 |
| 卡片索引标签 | `tests/test_formula_groups.cpp:269-304` | CardBase indexLabel 改动会破 |
| followerAngle 存储 270.0 | `tests/test_reverse_segment_commands.cpp:494`、`tests/test_rotate_anchor.cpp:366`（`:356` 注释「存储不归一化，≡ −90°」）、`tests/test_rotate_strip.cpp:269` | 统一 followerAngle 存储域必红，须同步改 3 处 |
| 颜色守卫豁免路径 | `tools/check_hardcoded_colors.py:56` `lstrip('src/')` bug | 修 bug 后 canvas 两文件才真正豁免，需先补 `QColor(r,g,b)` 检测再修，否则 CI 立刻红 |
| 无锁定的部分（可安全统一） | 跨块曲线交点、名字唯一性、组件级 refWorld、`interpOffsetDist` 精度、全部长度显示格式 | 见下 |

**实测：全 `tests/` 仅 1 处断言了「格式化后的显示字符串」** —— `tests/test_rotate_copy_endtarget.cpp:245` `QCOMPARE(val->text(), QStringLiteral("= 10 cm"));`（`:209` 注释写「= 10.00 cm」已漂移）。其余测试只断言原始 mm/deg 数值（例如 `interpOffsetDist` 在 6 个测试文件里被断言 40 余次，**全部是原始 mm 值**，无一处断言 UI 文本）。→ 统一长度/角度显示格式的代价极低，只需改这 1 处测试。
| 按控件序号取值的脆弱断言 | `tests/test_place_point.cpp:195`（第 4 个 `QLineEdit` 当角度框） | 增删控件顺序即破 |
| **未锁定（可安全统一）** | `Units::formatDegValue/formatDegTrimmed/formatNumberTrimmed/formatCmTrimmed` 本身、`kPlaceholder*` 文案、setRange/decimals、圆角、字号、unit caption | 无需改测试 |

---

## 7. 整改状态（2026-12 落地记录）

> 本节为整改实施轮次追加：记录 §5 工单的最终落点、验收方式与**刻意不统一**的分叉（含理由）。
> 每项落地均经 `tools\build.bat` + 按影响面 ctest 验证；收尾以 8 个守卫 + 全量用例为准。

### 7.1 守卫（§5.1）——全部落地

| # | 状态 | 落点 |
|---|---|---|
| G1 | ✅ | `tools/check_hardcoded_colors.py` 增 `QCOLOR_NUM`（`QColor(数字/0x)`）/`QCOLOR_STR`/`QSS_RGBA`/`QT_NAMED` 四类检测 |
| G2 | ✅ | 豁免判定改 `rel_posix.startswith('src/')` + `rel_posix[4:] in EXEMPT_FILES`（原 `lstrip('src/')` 按字符集剥离，豁免形同失效） |
| G3 | ✅ | `tests/test_theme_sync.cpp`：CanvasStyle ↔ ThemeTokens 同名 token 成对断言 + 刻意分叉登记 + `defaultAppearanceSingleSource` |
| G4 | ✅ | `tools/check_inline_units.py` 5 条规则（禁 `M_PI`/`std::numbers::`/`180.0 / kPi`/`'f', N`/`/ 10.0`）+ 同行 `// units-allow:` 豁免；已做变异测试 |

### 7.2 收口点（§5.2）

| # | 状态 | 落点 / 说明 |
|---|---|---|
| U1 | ✅ | `src/geometry/Angle.h`：`kPi`/`degToRad`/`radToDeg`（69 处换算）；存储/显示域归一化在 `src/parametric/FollowerAngle.h` 的 `followerAngleToStorage`/`followerAngleToDisplay` |
| U2 | ✅ | `writeFollowerAngleForMode(att, mode, rawDeg, radiusMm)` 唯一写入口；`attachmentValueDisplayText` 唯一显示入口 |
| U3 | ✅ 主体 / ⏳ 尾项 | `ParamDocument::findFollowerAttachmentOf` 成员化（6 份自由函数删除）；新增 `touchAndResolve(blockId)`（21 处）与 `commitRawChange()`（12 处）。**延期**：`BlockLifecycleCommands.cpp`/`ComponentCommands.cpp` 的 undo 快照循环语义不同（影子级联 vs 组件附件），提取 `collectCascadeDependents` 留后续 |
| U4 | ✅ | `Units::formatCm`（无后缀，可编辑框）/ `formatLength`（带 " cm"，量测结果）/ `formatNumberTrimmed` / `formatDegTrimmed` / `formatDegValue` / `formatPoint`（固定 2 位，刻意例外）；`formatAngle` 死代码已删 |
| U5 | ✅ | 新增 `Vec2::closestParamOnSegment`（`distanceToSegment` 复用）；迁移 `CanvasView`/`BlockItemPick`/`SnapEngine`/`ParamDocumentAttachments`/`CurveMath`；`raySegmentIntersect` 委托 U6 入口并去掉未用 `eps` |
| U6 | ✅ | `raySegmentOrCurveIntersect` + `RaySegmentHit` **落在 `src/geometry/RayCast.h/.cpp`**（CurveMath.h 只留 `rayCurveIntersect`；RayCast.cpp 从 46 → 83 行，CurveMath.cpp 961 → 924 行守住申报基线）；跨块版补曲线分支并统一在块局部坐标求解 |
| U7 | ✅ 部分 | HUD 暗色 token 与 `pointRadiusHover` 由 `test_theme_sync` 锁定；`formatRotationBadge` 落在 `src/tools/RotateDragMath.h`（显示策略属工具层，canvas 不反向依赖） |
| U8 | ✅ | `HitTester::isInteractiveBlock` / `isConnectTargetBlock`；**未**并入 `selectable`/`isBridge`——两者语义各自被工具消费，合并会改变行为 |
| U9 | ✅ | `InteractionTolerances::isDrag(delta, zoom, selectionEstablished)`（5/10px）+ `kDragThresholdPx` |
| U10 | ✅ | `src/parametric/CrossSelectionPolicy.h`：`crossesSelectionBoundary` / `isReleasedAcrossSelection(fromIn,toIn,isPin,op)` |
| U11 | ✅ | `cad::canvas::safeZoomOr`（`CanvasStyle.h`，阈值 `kGeomEps`）+ `CanvasScene::safeZoom()`；约 50 处本地三元/abs 守卫与裸 `currentZoom()` 迁移 |

### 7.3 新增文件（§5.3）

| # | 状态 | 落点 |
|---|---|---|
| N1 | ✅ 5 个保数值常量 | `src/geometry/Epsilon.h`：`kGeomEpsLoose=1e-6` / `kGeomEps=1e-9` / `kGeomEpsUltra=1e-10` / `kGeomEpsTight=1e-12` / `kGeomEpsSq=1e-8`（审计建议 4 个；1e-10 与 1e-12 不合并，避免改弦长/行列式语义） |
| N2 | ✅ | `src/tools/InteractionTolerances.h`（常量名强制 `Px/Mm/Deg` 后缀；含 `kOverlapEpsMm=0.5` / `kAxisZeroEpsMm=0.05` / `kAimAlignTolDeg=2.0`） |
| N3 | ✅ 部分 | `src/document/SchemaKeys.h` 11 个 JSON 键（DocumentSerializer/FormatMigration/DocumentFile 共用）；**枚举字符串表未迁**——四组枚举已表驱动，迁移无收益 |
| N4 | ✅ 分层落地 | 65 组跨文件重复文案中 59 组收口：`src/document/CommandTexts.h`（`cad::cmd::texts` 18 个，document/commands ↔ ui/tools/app 共用）+ `src/ui/UiStrings.h`（`cad::ui::str` 41 个，ui/app/tools 内部），43 个文件迁移。**未收口**：`kbdBadge` 工厂（§7.5 理由）、同文件内错误文案（ExpressionEvaluator / DocumentFile / LinkedVariable 已改文件内匿名命名空间常量）、`ReverseSegmentCommand.cpp` 线段不存在 x2（用户 WIP）。`addField` 工厂随 N5 缩减为 `NumericFieldSpecs`。 |
| N5 | ✅ 缩减落地 | 审计原建议 `FormScaffold::makeCardInput(objectName, decimals, validator)` 缩减为 `src/ui/NumericFieldSpecs.h`（`kLengthCmSpec`/`kAngleDegSpec`/`kWeightPxSpec` + `applyNumericSpec`，范围留调用点）+ **刻意不装 QDoubleValidator**（自由文本框接受公式，校验器会挡公式字符）；迁移 ConditionDialog / VariableCard / SegmentAnchorTab / LineAppearanceSection |

### 7.4 其余（§5.4 / §3 P2 / §4）

- §5.4 `DocCommand` 基类：**未做**（76 个命令类，机械改造量大、收益低；`Tool.h` 四行 override 已确认非重复）。
- §3 P2：**未做**（119 处 `font-size:NNpx`、194 组 ≥4 行逐字重复块）。
- §4 深挖：P0 级随 P0-1..P0-11 处理（UI-P0 系列见 §7.6）；其余 148 条 P1/P2 已逐条核销，见 §7.8。

### 7.5 复核后判定「刻意不统一」（勿再改）

| 项 | 位置 | 理由 |
|---|---|---|
| 世界方向 0..360 与折角 (−180,180] 并存 | `evaluatedFollowValueText` / `formatAttachmentFormulaFollowValue` | 是两种物理量，每条线段只命中一个分支 |
| `formatPoint` 固定 2 位小数 | `src/geometry/Units.h` | 坐标显示需列对齐，刻意不去尾零 |
| 1e-4 视图守卫 | `src/app/MainWindowSegmentMenu.cpp:44` | 判定「视图是否已布局」，非几何缩放 |
| `m11()` abs 守卫 | `HudItem.cpp` / `OverlapBatteryHud.cpp` | 需保留镜像负缩放语义 |
| `std::max(zoom, kGeomEps)` | `CurveAnchorDragSession.cpp` / `ToolCurveEditHandles.cpp` | 除零钳制而非守卫，钳后仍需原值 |
| 张力下限 / 有限差分步长 / 显示去抖 / 角度容差 | `CurveMath.cpp` 等 | 非几何容差，混入阶梯会改变数值行为 |
| 命名空间级 `kbdBadge` 自由函数 | `src/ui/TooltipFormatter.h` | 类静态成员无法用命名空间级 `using` 引入（MSVC C2885） |
| 自由文本输入框不装 `QDoubleValidator` | 全部 ElaLineEdit 输入框 | 同时接受公式（`parseNumberOrFormula`/`parseAngleText`），校验器会挡掉公式字符；范围约束用 QDoubleSpinBox |
| QDoubleSpinBox 范围不入 `NumericFieldSpecs.h` | `src/ui/*` 调用点 | 范围是领域约束（切线长度 ≥0 等），统一会放开非法输入 |

### 7.6 §4 深挖条目落地记录（2026-12）

| 条目 | 状态 | 落点 / 说明 |
|---|---|---|
| UI-P0-1 | ✅ | 旋转读数按物理量分域：`RotateBadgeQuantity{Delta, Fold, World}`（`src/tools/RotateDragMath.h`）+ `formatRotationBadge`（`RotateDragMath.cpp:62-76`）；`computeGizmoPose` 选择逻辑 :103-113 |
| UI-P0-2 | ✅ 刻意分叉 | 世界方向 0..360 与折角 (−180,180] 是两种物理量（§7.5）；已补域规则注释并修正 `attachmentEffectiveAngleDeg` 过期注释 |
| UI-P0-3 / 4 | ✅ | `SegmentAngleCard` 弧长/开度回显改 `formatLength`；`LineGeometrySection` 桥接分支输入框改 `formatCm`（输入框一律无后缀） |
| UI-P0-5 / 6 | ✅ 已过期 | 复核时 `ComponentTab` / `ToolAngleMeasure` / `AngleMeasureCard` 已用 `formatDegValue`/`formatDegTrimmed` |
| UI-P0-7 | ✅ 真 bug | 根因：`SegmentConnectionCardRefresh.cpp:134-137` 用 `formatDegTrimmed`（带 "°"）回填，`SegmentConnectionCardConn.cpp:282-285` 裸 `toDouble` 判成公式静默回滚。新增 `cad::geo::parseAngleText`（剥 U+00B0，公式原文不剥），8 个解析点切换 |
| UI-P0-8 | ✅ | `CardBase.cpp:318-323` 失效态两分支同族同字号（`kMonospaceFamily` + `FontLg`），只换 danger 色/8% 底色 |
| UI-P1-9 | ✅ | 10 处两段式 `toDouble` 改 `parseNumberOrFormula`/`parseAngleText`：AuxPointForm / IntersectionForm / PlacedPointDialog（含删死 include `QDoubleValidator`）/ QuickAuxDialog / LineGeometrySection 滑轨偏移 |
| CAN-P1-17 | ✅ | `PlacedPointStripBar` 与线段条带同权：公式回显优先、实时求值预览（`ConditionEngine::evaluate`）、提交写回 `interpOffset*Formula` |

### 7.7 收尾验收（2026-12）

- 构建：`cmd /c "tools\build.bat reldeb"` exit 0（无 FAILED / error C / error LNK）。
- 全量：`ctest -C RelWithDebInfo -j 4 --output-on-failure`（workdir `build/out-reldeb`）→ **100% tests passed out of 64**（56 功能 + 8 守卫），exit 0。
- 期间自伤修复：批量文案替换把「常量定义自身的字面量」也一并替换，产生 `const QString kErrExprTooDeep = kErrExprTooDeep;`（`src/parametric/ExpressionEvaluator.cpp` 三处）与 `kSaveFailedFmt`（`src/document/DocumentFile.cpp`）自初始化 → `test_expression::parseDepthGuard` 红（`tests/test_expression.cpp:359` `!r.error.isEmpty()` FALSE）。改回 `QStringLiteral` 字面量后全绿；全 src 复扫 `const QString (\w+) = \1;` 无残留。教训：先替换调用点、再插常量定义，或插入后从替换集合中排除定义行。
- 已知非回归抖动：`test_rotate_anchor` / `test_select_wkey` 在 `-j4` 偶发红，单跑与最终全量均过（GUI 时序抖动，见 CONVENTIONS.md 回归基线）。
- 仍未做（§7.4 不变）：§5.4 `DocCommand` 基类、§3 P2（`font-size:NNpx` / ≥4 行重复块）、`kbdBadge` 工厂、`ReverseSegmentCommand.cpp` 线段不存在 x2。


### 7.8 §4 观察条目逐条核销台账（2026-12 清账）

**口径**：范围 = 审计 §4 中「只观察、未排期」的 P1/P2 条目共 **148 条**（src/ui 29 / src/parametric+src/document 40 / src/canvas+src/geometry+src/app 37 / src/tools 42）；P0 已随 P0-1..P0-11 处理（UI-P0 见 §7.6），不在此重复登记。结论三档：**已修**（唯一出处已建立且调用点已迁移）/ **仍存在**（含「部分收口」，残留子项写在依据内）/ **刻意不改**（§7.5 已登记或属不同物理量，附理由）。依据中的行号均为清账当日（HEAD `d77b39b`）源码实测位置。

**汇总**：已修 **28** / 仍存在 **117**（其中 9 条为部分收口）/ 刻意不改 **3**。

| 模块 | 条数 | 已修 | 仍存在 | 刻意不改 |
|---|---|---|---|---|
| src/ui（§7.8.1） | 29 | 6 | 23 | 0 |
| src/parametric + src/document（§7.8.2） | 40 | 4 | 33 | 3 |
| src/canvas + src/geometry + src/app（§7.8.3） | 37 | 7 | 30 | 0 |
| src/tools（§7.8.4） | 42 | 11 | 31 | 0 |
| **合计** | **148** | **28** | **117** | **3** |

**读法**：已修 28 条全部落在本轮收口点（U1 角度唯一源 / U3 文档门面 / U4 Units format* / U5 `Vec2` / U8 命中资格 / U10 跨选策略 / U11 缩放守卫 / N1 容差 / N2 像素容差 / N4 文案 / N5 数值规格 / G1 颜色守卫）。仍存在的 117 条按根因可分四类：①「同一语义两份实现」（如旋转会话相位机、吸附过滤、z 序表、QSS 脚手架）；②「注释/文档与实现矛盾」（如 CAN-P1-7、TOOL-P1-20）；③「刻意分层但缺统一常量」（如字号/圆角/间距字面量）；④「已登记但未排期的重复块」（§3 P2 的 194 组，另计）。续做顺序建议见 §7.4 与本节各小节的「裁决备注」。

#### 7.8.1 src/ui（29 条：已修 6 / 仍存在 23 / 刻意不改 0）

**已修（6）**
- **UI-P1-1** — U3 唯一实现 `src/parametric/ParamDocumentAttachments.cpp:43` `findFollowerAttachmentOf`；原 8 份自由函数全删，仅 `src/ui/SegmentAngleCard.cpp:285`、`src/ui/SegmentConnectionCard.cpp:50` 两处薄委托。
- **UI-P1-8** 已修（缩减） — N5 `src/ui/NumericFieldSpecs.h:27,30,33` 统一精度/后缀/步长（`kLengthCmSpec`/`kAngleDegSpec`/`kWeightPxSpec`）+ `:36 applyNumericSpec`；`setRange` 仍 7 处（`src/ui/LineAppearanceSection.cpp:117`、`src/ui/ConditionDialog.cpp:23-26`、`src/ui/SegmentAnchorTab.cpp:74,81,92,99`、`src/ui/VariableCard.cpp:98-100`）按 §7.5 刻意留调用点；`setValidator` 全 src/ui 仍 0 处（§7.5）。
- **UI-P1-9** — §7.6：清单内全部改 `parseAngleText`/`parseNumberOrFormula`，例 `src/ui/PlacedPointDialog.cpp:202,206`、`src/ui/SegmentShadowBasisCard.cpp:122`、`src/ui/ComponentTab.cpp:323,345`。
- **UI-P1-14** — 魔数 `/10.0`、`*10.0` 已清：`src/ui/PlacedPointDialog.cpp:189` `Units::formatCm(pt->interpOffsetDist)`、`:203` `cmToMm(dist.value)`（U4 + G4 禁 `/ 10.0`）。
- **UI-P2-7** — 原 4 处非 token 色全迁 token：`src/ui/PointRefEdit.cpp:276-278` `rgbaCss(tk.danger, 0.125)`、`src/ui/CardBase.cpp:215` `tokens().text3`；仅 `src/ui/Theme.cpp:351` 紫徽章在 `EXEMPT_FILES` 且刻意固定色相（`tools/check_hardcoded_colors.py:26`）。
- **UI-P2-12** — `src/ui/LineGeometrySection.cpp:318` 改 `Units::formatNumberTrimmed(seg.tension)`（U4 + G4 禁 `'f', N`）。

**仍存在（23）**
- **UI-P1-2** — 影子 Att1 循环仍 2 份：`src/ui/SegmentShadowBasisCard.cpp:78-80` 与 `:127-129` 逐字同构；同判据另见 `src/ui/LineEndpointSection.cpp:368-372`（共 4 处内联循环）。
- **UI-P1-3** — 两套连接语义仍在（约 150 行）：`src/ui/SegmentConnectionCardConn.cpp:60-86` vs `src/ui/LineEndpointSectionConn.cpp:37-60`；重定向 `:114-158` vs `:61-92`、建连 `:189-198` vs `:105-113`，无共享 helper。
- **UI-P1-4** — `src/ui/CardTabBase.cpp` 同套 QSS 仍写两遍：`:42-44/:111-114`、`:48-49/:116-118`、`:65-67/:120-122`、`:73-75/:125-127`、`:83-87/:130-134`。
- **UI-P1-5** — `src/ui/MeasureTab.h:25` 仍 `class MeasureTab : public QWidget`（其余三个走 CardTabBase）；`src/ui/MeasureTab.cpp:141,146` 自设 `cardListArea`/`cardListContainer`，空态 `:151` FontMd vs `src/ui/CardTabBase.cpp:62` FontXl。
- **UI-P1-6** — provider/sync 样板仍 4 份：`src/ui/VariableTab.cpp:29-53,117-129`、`src/ui/LinkedTab.cpp:44-83,106-116`、`src/ui/FormulaTab.cpp:71-…,430-436`、`src/ui/MeasureTab.cpp:172-249,263-298`；空态骨架仅 3/4 走 `setupListPage`。
- **UI-P1-7** — `src/ui/FormulaTab.cpp:393`、`:409` 有 dangling 检查，`:401` `if (!mv.refName.isEmpty())` 无；`mv.dangling` 字段见 `src/parametric/MeasureVariable.h:49`。
- **UI-P1-10** — `src/ui/PlacedPointDialog.cpp:79-96` 双框并存（"0.0"/"公式"、无互斥），`:202-208` 解析失败静默忽略；单框收口常量仍只在 `src/ui/ComponentTab.cpp:194,225` 等 5 处使用。
- **UI-P1-11** — `src/ui/AuxPointForm.cpp:55`「如 0.5 或公式」vs `:62`「如 0.7 (cm)或公式」；`src/ui/IntersectionForm.cpp:33`；`src/ui/LineEndpointSection.cpp:145` vs `src/ui/LinePropertyDialog.cpp:198` 名称提示不同。
- **UI-P1-12** — 两套 helper 并存：`makeDialogButtons`（`src/ui/ConditionDialog.cpp:147`、`src/ui/PlacedPointDialog.cpp:108` 等 4 处）vs `makeFormButtonBar`（`src/ui/MeasureResultDialog.cpp:118`、`src/ui/QuickAuxDialog.cpp:91`）。
- **UI-P1-13** — `src/ui/PlacedPointDialog.cpp:54,74` 仍手搭 `new QFormLayout()`，未调 `applyFormGrid`（`src/ui/AuxPointForm.cpp:105`、`src/ui/QuickAuxDialog.cpp:65` 等 5 处已用）。
- **UI-P1-15** — `src/ui/ComponentTab.cpp:152-165` 手搭 `componentCard` + `bar->setFixedWidth(3)` + 内联 QSS；`:176` `new QLabel` 自设 `componentIndex`；`:267-279` 四按钮未走 `makeFormButtonBar`。
- **UI-P2-1** — `src/ui/Theme.cpp:211-215` `QLabel#cardValue`/`[dangling="true"]` 仍在，全仓无 `setObjectName("cardValue")` → 死规则。
- **UI-P2-2** — 双来源仍在：`src/ui/Theme.cpp:216` `QLabel#cardIndex` + `src/ui/CardBase.cpp:144-148` `createIndexLabel` 内联 QSS。
- **UI-P2-3** — `src/ui/CardBase.cpp:142,168,181`（另 `:291`）仍 `new ElaText(QString(), 13, this)`，随后被 QSS 覆盖为 FontXs/FontLg。
- **UI-P2-4** — `src/ui/Theme.cpp:354-359` `dimValueStyle` 仍硬编码 11px 且无等宽族；`src/ui/LineOrthoOffsetCard.cpp:38` 整串自拼，`src/ui/LinePropertyDialog.cpp:184-185` 自行追加等宽族。
- **UI-P2-5** — `src/ui` 内 `font-size:\s*\d+px` **93 处**（带空格 63 / 无空格 30；审计记约 68 处），例 `src/ui/CardTabBase.cpp:74,85,126,132`、`src/ui/LayerPanel.cpp:104,116,179,239`、`src/ui/LayerCard.cpp:97,111`；§7.4 登记未做。
- **UI-P2-6** — 非 token 圆角仍在：3px `src/ui/LayerPanel.cpp:104,115,203,209`、`src/ui/LayerCard.cpp:78,244,259,433`、`src/ui/FormulaCard.cpp:355`；8px `src/ui/NoteButton.cpp:125`；15px `src/ui/PointRefEdit.cpp:60,276`；1px `src/ui/ComponentTab.cpp:163`。
- **UI-P2-8** — `constexpr int kFieldH = 30` 仍 8 处（`src/ui/SegmentAngleCard.cpp:32`、`src/ui/LineGeometrySection.cpp:42`、`src/ui/LinePropertyDialog.cpp:44` 等；src/ui 内共 9 份本地定义）；`src/ui/LineEndpointSection.cpp:37` 仍 26；高度另有 34/32/28/24/22/20/18。
- **UI-P2-9** — `src/ui/CardBase.h:115` `int refChipWidth = 72`；`src/ui/LinkedCard.cpp:91`=72、`src/ui/MeasureCard.cpp:140`=72、`src/ui/AngleMeasureCard.cpp:116`=84 各写死。
- **UI-P2-10** — `src/ui/MeasureCard.cpp:71-82` 仍把「水平/垂直 」塞进值标签，`:141` 又有独立 `spec.unit="cm"`；`src/ui/SegmentAnchorTab.cpp:73` 标签「(°)」+ `:77` 后缀 "°" 双单位。
- **UI-P2-11** — `src/ui/LineGeometrySection.cpp:183,196,252` 仍 `setPlaceholderText("0")`；`src/ui/LineEndpointSection.cpp:182`、`src/ui/SegmentConnectionCardBuild.cpp:176` 同，均不带单位。
- **UI-P2-13** — `src/ui/SegmentAnchorTab.cpp:5` 与 `:6` 仍重复 `#include "ElaTabWidget.h"`。
- **UI-P2-14** — `'g',6` 仍 3 文件 6 处：`src/ui/IntersectionForm.cpp:89,107`、`src/ui/AuxPointForm.cpp:178,184`、`src/ui/QuickAuxDialog.cpp:81`、`src/ui/SegmentAngleCard.cpp:476`；与 `formatNumberTrimmed`/`formatDegValue` 并存（`src/ui/PlacedPointDialog.cpp` 的 `'f'` 已改）。

**刻意不改（0）** — 无新增；`setRange` 留调用点与不装 `QDoubleValidator` 已在 §7.5 登记（对应 UI-P1-8 的缩减结论）。

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
- **PAR-P2-6** — `src/parametric/ParamDocumentStores.cpp:27-128`（变量/公式/组/图层/联动/测量 CRUD）与 `:133-164`（`RawModelAccess`）纯转发仍在，但 `:25` 注释「子域存储门面转发 (2026-08 拆分)」即设计意图，`docs/archive/plans/FILE_SPLIT_PLAN_V2.md:12` 记录「门面转发保障既有调用者零感知」；审计 `docs/archive/2026-09/AUDIT_consistency_duplication.md:383`（原 `docs/`，后移档） 前言亦已把它列入「已剔除已收口项」——P2 段落为重复登记，以前言为准。

#### 7.8.3 src/canvas + src/geometry + src/app（37 条：已修 7 / 仍存在 30 / 刻意不改 0）

**已修（7）**
- **CAN-P1-1** — G1 守卫重写：`tools/check_hardcoded_colors.py:49-61` 五类扫描（HEX / QCOLOR_NUM / QCOLOR_STR / QSS_RGBA / QT_NAMED），`:71-76` 用前缀切片替代 `lstrip('src/')` 字符集 bug（`EXEMPT_FILES` 现为 `ui/Theme.cpp`、`ui/Theme.h`、`canvas/CanvasStyle.cpp`、`canvas/CanvasStyle.h`）。
- **CAN-P1-2** — `src/canvas/overlay/TransientOverlay.cpp` 内 `QColor(` / hex 字面量 0 处（守卫覆盖）；默认实参 `src/canvas/overlay/TransientOverlay.h:72` 取 `CanvasStyle::fallback().guideLineColor`。
- **CAN-P1-4** — U5：`src/canvas/BlockItemPick.cpp:76`、`src/canvas/CanvasView.cpp:494` 均调 `Vec2::distanceToSegment`（`src/geometry/Vec2.h:88`），原两处手写循环删除。
- **CAN-P1-6** — 拾取常量唯一源 `src/canvas/BlockItemPick.h:32,35,40`；8.0 唯一源 `src/canvas/CanvasStyle.h:14`（`kHoverRadiusPx`），注释与实现已对齐。
- **CAN-P1-12** — N1：`src/geometry/Epsilon.h:12-24` 五档容差；`src/geometry/Vec2.h:48,82,90`、`src/geometry/RayCast.cpp:66-73` 已迁移。
- **CAN-P1-17** — §7.6：`src/app/PlacedPointStripBar.cpp:221,244,272,279` 改 `parseNumberOrFormula` / `parseAngleText`，与线段条带同权。
- **CAN-P2-3** — U1：`src/canvas/DirectionMarker.h:30` `cad::geo::degToRad(25.0)`（M_PI 手算已删）。

**仍存在（30）**
- **CAN-P1-3** — 灰显/回退色仍两份：`kGhostAlpha = 110` 在 `src/canvas/BlockItemPainter.cpp:47` 与 `src/canvas/CurveItem.cpp:163`；点半径 0.8 字面量 `:178,189` vs `src/canvas/CanvasStyle.h:194`。
- **CAN-P1-5** — 第二套命中测试仍在：`src/canvas/CanvasView.cpp:478-501` 自遍历 `scene()->items` + `distanceToSegment`，未走 `BlockItemPick` / `HitTester`。
- **CAN-P1-7** — 注释与实现矛盾仍在：`src/canvas/BlockItemPick.cpp:62` 注释「Points are deliberately NOT hover targets」vs `src/canvas/BlockItem.h:104,134`、`src/canvas/BlockItemPainter.cpp:200-202`。
- **CAN-P1-8** 仍存在（部分收口） — 5 处字体构造已收口为单一入口 `src/canvas/CanvasFonts.h:14-70`；残留：族名仍只有 `"Segoe UI"`（`:57,66`），未接 `src/ui/Theme.cpp:372-379` 的 CJK 回退栈（canvas 层不能依赖 ui 层，需在 CanvasFonts 内自建族列表）。
- **CAN-P1-9** — 手写角度归一仍在：`src/geometry/CurveMath.cpp:164-169` `normAnglePi`、`src/canvas/CanvasScene.cpp:559-561` while 归一（未用 `Angle.h` `normalizeRad`）。
- **CAN-P1-10** — `src/geometry/CurveMath.cpp:128-143` 与 `:146-161` 两份 Thomas 求解（仅 `d` 类型不同）。
- **CAN-P1-11** — `src/canvas/BlockGeometryCache.cpp:116-118` 与 `:144-146` LineStyle→Qt::PenStyle 映射两份。
- **CAN-P1-13** — `src/canvas/CanvasScene.cpp:507-517` 手写两直线求交（`:512` 容差已 token 化，算法未走 `RayCast`）。
- **CAN-P1-14** — shape 缓存容差两份同魔法比例：`src/canvas/BlockItem.cpp:74-75` 与 `src/canvas/CurveItem.cpp:103-104`（同 `* 0.02`）。
- **CAN-P1-15** — 缩放因子三份：`src/canvas/CanvasScene.cpp:82`、`src/canvas/CanvasView.cpp:161`、`src/canvas/OverlapBatteryHud.cpp:81`。
- **CAN-P1-16** — 上下文条脚手架：`kbdBadge` 已收口 `src/ui/TooltipFormatter.h:14`（自由函数），但 serial chip QSS 仍 4 份（`src/app/ContextStrip.cpp:84` 等）、`kFieldH`/`addField`/Tab 焦点循环/剪贴板清洗各 2 份。
- **CAN-P1-18** — `Units::formatAngle` 死代码已删（`src/geometry/Units.h:101` 仅注释）；手写 `"= %1°"` 仍在 `src/ui/SegmentAngleCard.cpp:86,185,313` 等 5 处。
- **CAN-P1-19** 仍存在（部分收口） — `foldedArcDisplay/foldedChordDisplay` 已不存在（长度显示走 `src/parametric/FollowerAngle.h:66` `attachmentValueDisplayText`/`formatCm`）；残留 `src/app/ContextStripEdit.cpp:99` `chordMm = std::clamp(chordMm, -2.0 * radius, 2.0 * radius);` 冗余（`src/geometry/Angle.h:84` `chordMmToDeg` 内部已 clamp ratio）。
- **CAN-P1-20** — 显示归一 / 写回不归一仍在：`src/app/ContextStripDisplay.cpp:108-110` vs `src/app/ContextStripEdit.cpp:124`。
- **CAN-P1-21** — 颜色混合两份：`src/canvas/CanvasAnimator.cpp:22-29` vs `src/canvas/CanvasStyle.cpp:69-74`。
- **CAN-P1-22** — app 层 QSS 逐字重复：toolPill 3 份（`src/app/MainWindowToolBar.cpp:57`、`src/app/MainWindowPanelWindow.cpp:320`、`src/app/MainWindow.cpp:170`）、stripBand 2 份、ghostBtn 2 份。
- **CAN-P1-23** — 图标两套：`src/app/MainWindowToolBar.cpp:23-36` `kIcons` vs `src/app/MainWindowMenuBar.cpp:83` `IconHelper::iconByName`；快捷键散 `src/canvas/CanvasView.cpp:298`、`src/tools/ToolSelect.cpp:748` 等。
- **CAN-P1-24** — 字号/圆角/间距字面量：`src/app/MainWindowStatusBar.cpp:62,67,92`、`src/app/MainWindowToolBar.cpp:51`（11/12/13px、2/4px）。
- **CAN-P1-25** — `src/app/MainWindowStatusBar.cpp:91` `color: #FFFFFF` vs `src/ui/Theme.h:44` `onAccent`（`src/ui/Theme.cpp:81`，实为纯白 → 可 token 化）。
- **CAN-P2-1** — `src/canvas/CanvasStyle.h:95-102` 附件标记注释逐字重复两遍。
- **CAN-P2-2** — `src/canvas/OriginCrosshair.h:17` EXTENT=100000 vs `src/canvas/CanvasView.h:103` SCENE_BOUND=10000；`src/canvas/OriginCrosshair.cpp:25` `QColor(200,200,200)`。
- **CAN-P2-4** — `src/canvas/CanvasView.cpp:132` 注释「1cm」vs `:133` `dotStep = 1.0`（`5.0/zoom` 已消失）。
- **CAN-P2-5** — `src/canvas/BlockItem.cpp:30,302,350` 魔法 Z 值 1.0/1.5。
- **CAN-P2-6** — `src/canvas/CanvasScene.cpp:439,535` 仍硬编码 amber `(0xFF,0x98,0x00)`；TransientOverlay/BlockItemPainter 已 token 化。
- **CAN-P2-7** — 圆角 3/3.5/4.0 字面量：`src/canvas/ToolDockStyle.h:70`、`src/canvas/HudItem.cpp:85,103`、`src/canvas/OverlapBatteryHud.cpp:223,293`。
- **CAN-P2-8** — fallback 已静态（`src/canvas/CanvasStyle.h:164`）、阴影已 token；字面量仍在 `src/canvas/OverlapBatteryHud.cpp:172,223,293,312`。
- **CAN-P2-9** — `src/canvas/CanvasAnimator.cpp:42` 16ms；`src/canvas/CanvasScene.cpp:564` `40.0/zoom`、`:566` `kSamples = 40`。
- **CAN-P2-10** — 缩放文本 `src/app/MainWindowStatusBar.cpp:67` vs `src/app/MainWindow.cpp:337`；窗口尺寸 `src/app/MainWindowPanelWindow.cpp:362` vs `src/app/MainWindow.cpp:76`。
- **CAN-P2-11** — `src/app/MainWindowStatusBar.cpp:30` 与 `:63` 同 `setObjectName("coordLabel")`。
- **CAN-P2-12** — `src/app/MainWindowPanelWindow.cpp:56` vs `:364`；`src/app/MainWindow.cpp:47` vs `:78`；`src/app/MainWindowFileSession.cpp:132` 手写 `.gcad`、`:58` 遗留标签 `[P207-ABS]`。

**裁决备注（本轮清账时定案，后续勿翻案）**
- CAN-P1-8 记「仍存在（部分收口）」而非「已修」：审计条目的核心断言含「Theme 的 CJK 回退栈全部丢失」，收口为单一入口只解决了「绕过 Theme」，回退栈仍缺；补法是给 `CanvasFonts.h` 的 `uiFont/uiFontPt` 加族列表（Noto Sans SC → Microsoft YaHei UI → Segoe UI），不得反向 include ui 层。
- CAN-P1-19 记「仍存在（部分收口）」：`src/app/ContextStripEdit.cpp:99` 的 clamp 与 `chordMmToDeg` 内部 ratio 截断重复；虽属输入域钳制（存值会被截到 ±2r），仍属重复实现，可删。

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

