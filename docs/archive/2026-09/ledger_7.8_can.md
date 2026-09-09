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
