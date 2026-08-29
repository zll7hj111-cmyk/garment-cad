# 工具（Tool）链路系统性代码审查报告

- 审查日期：2026-08-29
- 审查范围：`src/tools`（12,822 行 / 27 文件）、`src/app/MainWindow.*`、`src/app/ToolDockStyle.h`、`src/ui/AngleHud.*`、`src/canvas/InputDispatcher.h`
- 审查方式：通读工具基类 / 管理器 / 8 个工具的状态机与生命周期 / 工具坞装配与样式 / HUD 与提示实现，并交叉核对调用点与测试
- 结论摘要：**架构骨架是健康的（依赖倒置、手势剥离已有成效），但"工具状态的可视化"与"工具元数据的归属"这两块是系统性欠账**，由此派生出 4 个高危、10 个中危问题。

---

## 0. 现状一览

| 维度 | 现状 |
|------|------|
| 工具数量 | 8（Select / SmartPen / CurveEdit / Rotate / Break / Intersection / Measure / AngleMeasure） |
| 状态机分布 | 7 套各自独立的内部枚举（`SelectState` 9 态 / `RotateState` 3 态 / 智能笔 `State{Idle,Drawing,ConfirmEnd}` + `Mode{Line,Dart}` / 交点 4 态 / 曲线 3 态 / 两个测量工具各 2 态） |
| 注册机制 | **无**。`ToolType` 枚举 + `switchTool` 的 8 分支 switch（`src/tools/ToolManager.cpp:61-103`） |
| 工具显示元数据 | **分散在 MainWindow**：action 文本（:251-313）+ 图标（refreshToolIcons :129-142）+ 按钮序（specs[] :399-408）+ 提示文本（:863-884）+ 按钮高亮索引（:846-858），共 5 处手工对齐 |
| 生命周期 | 每次切换**销毁重建**（`ToolManager.cpp:105-128`），跨工具状态靠特例搬运（选区继承 :75-89） |
| 分层 | 良好：`InputDispatcher` 在 canvas 层声明、tools 层实现（`src/canvas/InputDispatcher.h:23-46`），由 `tools/check_layering.py` 把关 |
| HUD/提示实现 | **3 套并存**：`HudItem`（`ToolSmartPen.h:47-62`）、`AngleHud` QWidget（`src/ui/AngleHud.cpp`）、重叠提示手搭图元（`ToolSelect.cpp:1340-1368`），另加画布 toast（`CanvasScene.cpp:275-332`） |

值得肯定、重构时务必保留的部分：
1. `InputDispatcher` 依赖倒置——canvas 不知道 tools 存在。
2. 手势已从工具中剥离（`ConnectGesture` / `CopyDragController` / `MarqueeGesture` / `RotateGizmo` / `LineFactory` / `LeaderCandidatePicker`），"工具 = 状态机 + 手势编排"的方向已走了一半。
3. 输入包含保护（`SegmentEditBar.cpp:419-427`、`SmartPenPreInputBar.cpp:117-126`）已经意识到快捷键抢输入的问题——但只覆盖了两处（见 H1）。

---

## 1. 高危问题（High）

### H1. 旋转/连接角度 HUD 输入框缺少 ShortcutOverride 保护 —— 输入公式会被单字母快捷键劫持，工具被切换、会话被销毁

- **位置**：`src/tools/ToolRotate.cpp:1321-1353`（`showHud()` 结尾 `:1351-1352` 主动 `setFocus()` + `selectAll()`）、`src/tools/ConnectGesture.cpp:1012-1013`（同样主动聚焦）、`src/ui/AngleHud.cpp:163-172`（`eventFilter` 只处理 `Key_Escape`，**未** accept `QEvent::ShortcutOverride`）
- **关联位置**：`src/app/MainWindow.cpp:256/264/272/280/288/296/311`（8 个工具 QAction 全部单字母 V/L/C/R/B/I/A/H），配套的 `setShortcutContext(Qt::ApplicationShortcut)`（:257/265/273/281/289/297/312）使快捷键**全局生效、与焦点无关**
- **问题描述**：两个角度 HUD 都会在创建后立刻把键盘焦点交给 `QLineEdit`。项目里另外两个输入框（`SegmentEditBar`、`SmartPenPreInputBar`）都专门装了 `ShortcutOverride` 过滤器并在注释里写明"L, V, C, R, B, I, A, H 等窗口快捷键不得在编辑时触发"——说明该冲突在本项目是真实存在且已被认知的。`AngleHud` 恰恰漏了这道保护。
  实际后果：旋转工具选中线段后，在 HUD 里输入占位符自己推荐的公式 `b/4+5`，敲下第一个字符 `b` 即触发 `m_actionBreak` → `switchTool(ToolType::Break)` → `ToolRotate` 被析构（`ToolManager.cpp:105-111`）→ HUD 消失、未提交的旋转被丢弃、用户被踢到打断工具。同理 `a`→角度测量、`v`→选择、`l`→智能笔、`c`→曲线、`i`→交点、`h`→切换辅助层。连接手势的角度 HUD（`ConnectGesture::showAngleHud`）同此。
- **严重度**：**高**——数据丢失（会话被静默销毁）+ 操作路径完全中断，且触发方式是"照着提示输入推荐内容"。
- **修复建议**（二选一，推荐前者，改动最小）：
  1. 在 `AngleHud::eventFilter` 增加分支，与既有模式完全一致：
     ```cpp
     if (event->type() == QEvent::ShortcutOverride) {
         static_cast<QKeyEvent*>(event)->accept();
         return true;   // 输入包含：编辑期内吞掉所有快捷键
     }
     ```
     注意过滤器需同时装在 `m_edit` 与 `this` 上（当前已是，`AngleHud.cpp:81-82`）。
  2. 或把 8 个工具快捷键从 `Qt::ApplicationShortcut` 降级为 `Qt::WindowShortcut`，并在 `CanvasView` 有焦点时才激活。改动面更大、且会影响现有快捷键手感，不推荐。
- **验证**：手工复现路径已明确；建议补一个回归用例（构造 ToolRotate → `showHud()` → 向 `edit()` 注入 `QKeyEvent(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, "b")` → 断言 `tm.activeToolType() == Rotate`）。

### H2. 旋转「单线确认门」(D15) 是一个没有视觉表达、且提示文案描述的是已废弃行为的隐藏状态

- **位置**：状态定义 `src/tools/ToolRotate.h:249-250`；赋值点 `ToolRotate.cpp:120/125/253/304/316/513/546`；**`src/tools/RotateGizmo.cpp` 全文不读取该标志**（grep 无命中）；提示文案 `src/app/MainWindow.cpp:869-871`
- **问题描述**：D15 重构引入了"已选未确认 → 已确认"这道门：未确认时按压本线身是 no-op、拖动被禁止（`ToolRotate.cpp:156-200`），必须先右键或回车确认。但：
  1. 角度盘 gizmo 在两态下**绘制完全一致**，用户看不出自己处于哪一步；
  2. HUD 在两态下都显示、caption 相同，不提示"右键/回车确认"；
  3. 状态栏提示仍写着旧的「点击线段选中 | 拖动旋转(Shift吸附15°)」，而实际行为是"选中后**不能**直接拖"。
  三者叠加的结果：用户点线段后拖动毫无反应 → 判定工具坏了/卡死。这是本次审查中**对可用性伤害最大**的一处，因为它让一个正确实现的功能被感知为 bug。
- **严重度**：**高**（功能正确但认知断裂，且无自我救济路径）
- **修复建议**：
  1. `RotateGizmo` 接收确认态：选中态 = 虚线弧 + 半透明手柄；确认态 = 实线弧 + 实心手柄 + accent 强色。
  2. `AngleHud` 增加 caption 后缀：未确认时拼 `（右键/回车确认）`。
  3. 更新 `MainWindow.cpp:871` 文案为：「旋转：点击线段选中 | 右键或回车确认 | 拖动旋转(Shift 15°) | HUD 输入角度/公式 | X 切换锚心 | Esc 反悔」。

### H3. 角度测量工具复用了「选择」工具的状态栏提示（提示文本错配）

- **位置**：`src/app/MainWindow.cpp:863-884`
- **问题描述**：`ToolType` 有 8 个值（`ToolManager.h:21-30`），而提示 if/else 链只有 7 个分支：SmartPen / CurveEdit / Rotate / Break / Intersection / Measure + `else → 选择`。`AngleMeasure` 没有分支，直接落进 `else`，切到角度测量时状态栏显示的是「选择：点击选取实体（单选即操作）| W切换单选/多选 | …」——一个与当前工具完全无关的操作说明。`else` 兜底把一个具体工具的文案当默认值，是这个 bug 的根因。
- **严重度**：**高**（8 个工具里 1 个的状态说明 100% 错误；同类问题会随新增工具继续扩散）
- **修复建议**：改为以 `ToolType` 为键的静态表，并让测试兜底：
  ```cpp
  static const QHash<ToolType, QString> kToolHints = { ... };
  m_toolHintLabel->setText(kToolHints.value(type, QString::fromUtf8("…")));
  ```
  加一条断言/单测：`for 每个 ToolType: QVERIFY(kToolHints.contains(t))`。顺带把工具显示名从被丢弃的 `activeToolChanged` 的 `name` 参数（`MainWindow.cpp:824-826` 直接 `(void)name;`）接回来做兜底文案。

### H4. 基类 `Tool` 的上下文成员被 5 个派生工具同名遮蔽，基类契约形同虚设

- **位置**：基类 `src/tools/Tool.h:84-86`（`m_scene` / `m_paramDoc` / `m_hud`）；重复声明见于 `ToolSelect.h:196-197`、`ToolSmartPen.h:215-216`（另在 `:278` 声明了自己的 `HudItem* m_hud`）、`ToolRotate.h:242-243`（另在 `:289` 声明 `QPointer<cad::ui::AngleHud> m_hud`，**类型都不同**）、`ToolCurveEdit.h:97-98`、`ToolBreak.h:61-62`
- **问题描述**：C++ 名字隐藏使这 5 个工具内部所有 `m_scene` 指向自己的副本，基类成员恒为 null。由此：
  1. `Tool.h:25-41` 白纸黑字要求的"派生类 MUST 调用基类 activate / deactivate"被 5 个工具全部违反（对照：`ToolIntersection` / `ToolMeasure` / `AngleMeasure` 三个工具的 `deactivate()` 规规矩矩调了 `Tool::deactivate()`）。同一个继承体系里存在两套互不相容的用法。
  2. 基类提供的 HUD 设施成了**陷阱代码**：`Tool::ensureHud()`（`Tool.cpp:8-15`）第一行就 `m_scene->addItem(...)`，对这 5 个工具而言 `m_scene` 必为 null → 一旦有人调用即空指针崩溃。目前只有 `ToolAngleMeasure`（:158）和 `ToolIntersection`（:496）敢用。
  3. `Tool::releaseHud()` 对这 5 个工具永不执行；智能笔自己管自己的 `m_hud`（`ToolSmartPen.cpp:973` 手工释放），旋转工具自己管 `AngleHud`（`ToolRotate.cpp:48-55` 手工 delete）——**每个工具的清理都是手写的、各不相同的**。
  4. `tests/test_nav_smoke.cpp:704` 能直接栈上 `cad::tools::ToolSelect ts;` 并调用 `activate(scene, &doc)` 通过编译、跑通，恰恰是因为基类上下文是摆设。这条"能编译"掩盖了契约破裂。
- **严重度**：**高**（不是当前的崩溃，而是让所有基于基类的横向重构——统一 HUD、统一清理、统一生命周期——都无法落地；且 `ensureHud` 是已装填的枪）
- **修复建议**（后续重构的前置步骤）：
  1. 删除 5 处重复声明，统一使用基类 `protected` 成员；
  2. 把 `activate/deactivate` 改成**非虚模板方法**，只暴露虚钩子：
     ```cpp
     void Tool::activate(CanvasScene& s, ParamDocument* d) {  // 非虚
         m_scene = &s; m_paramDoc = d; onActivate(s, d);
     }
     void Tool::deactivate() {                                // 非虚
         onDeactivate(); releaseHud(); m_scene = nullptr; m_paramDoc = nullptr;
     }
     ```
     从语言层面杜绝"忘记调基类"，也让"必须按什么顺序清理"只有一个地方可写。
  3. `ToolRotate` 的 `m_hud` 重命名为 `m_angleHud`，避免与基类 `m_hud` 混淆（当前靠类型不同才没炸，是运气）。

---

## 2. 中危问题（Medium）

### M1. 重叠提示 HUD 与画布 Toast 都按场景坐标放置、不做缩放补偿

- **位置**：`src/tools/ToolSelect.cpp:1357-1366`（`showOverlapHint`）、`src/canvas/CanvasScene.cpp:294-308`（`showToast`）
- **问题描述**：两处都用 `QFontMetrics` 量出**字体像素**尺寸，却直接写进 `setRect(x, y, w, h)`——而 `QGraphicsRectItem` 的 rect 是**场景单位**，会随视图 transform 一起缩放。于是：放大到 4× 时提示条小到看不清；缩小到 0.2× 时提示条巨大并遮挡画布。偏移量 `+12.0` 同样是场景单位（缩放后与光标的视觉距离忽远忽近）。
  对照组：`ToolSmartPen.cpp:63-69` 的 `HudItem::moveToPoint` 明确做了 `setTransform(scale(1/zoom))` 补偿——说明团队知道这个问题，只是新写的两处 HUD 没沿用。
- **严重度**：**中**（缩放是 CAD 的高频操作，提示在高缩放下失效等于没有）
- **修复建议**：把 `HudItem` 从 `ToolSmartPen.h` 提到 `src/canvas/`（或 `src/ui/`）成为共享组件，统一实现 1/zoom 补偿（或用 `QGraphicsItem::ItemIgnoresTransformations`）；`showOverlapHint` 与 `showToast` 改用它。这一步同时消灭"3 套 HUD 实现"（见 P1）。

### M2. 重叠提示图元从不销毁 —— 每次切工具泄漏 2 个 QGraphicsItem

- **位置**：创建 `src/tools/ToolSelect.cpp:1346-1353`；"清理" `:1370-1376`（只 `hide()`）、`:1246-1251`（`deactivateOverlapContext` 只调 hide）；`ToolSelect` 无析构函数覆盖
- **问题描述**：`ToolManager::setActiveTool`（`ToolManager.cpp:105-111`）每次切换都 `std::move` 销毁旧工具，但 `ToolSelect` 的重叠提示 `QGraphicsRectItem` + `QGraphicsSimpleTextItem` 从不 `removeItem/delete`。它们被 `hide()` 后**永久留在场景图及其 BSP 索引里**——每切换一次 Select 泄漏一对，长会话累积后直接拖慢 `QGraphicsScene::items()`，而 items() 正是命中测试与悬停反馈的热路径（见 M7）。
- **严重度**：**中**（渐进式性能退化 + 内存泄漏，难归因）
- **修复建议**：给 `ToolSelect` 加析构（或在 `deactivate()` 中）执行 `removeItem + delete` 并置空两个成员。更彻底的做法见 P1 的 RAII 容器。

### M3. 工具坞 8 枚按钮没有 tooltip、没有快捷键说明；「禁用态」样式是死代码

> ✅ 已落地（2026-12）：tooltip = ToolDescriptor::describe().hintText 全文（P3 菜单生成时补）；禁用态确认为伪状态 —— 全仓工具按钮永不禁用，ToolDockStyle 的 text3 disabled 分支与 `&& enabled` 判断已删除（注释注明若未来引入禁用需重建）。

- **位置**：`src/app/MainWindow.cpp:409-420`（只 `setDefaultAction`，全仓无针对工具 action 的 `setToolTip`）；`src/app/ToolDockStyle.h:11-18` 与 `:44-68`
- **问题描述**：
  1. 未显式设置 tooltip → 悬停提示退化为 QAction 文本（含 `&V` 加速符），既无快捷键也无功能说明，用户只能靠图标猜。
  2. `ToolDockStyle` 的注释声称实现了"激活/按压/悬停/默认/禁用"五态矩阵，但全项目**没有任何地方禁用工具按钮**（`setEnabled(false)` 仅出现在最近文件菜单 `:1624` 与图层菜单 `:583`）→ 禁用分支（`:71-73` 的 `text3`）永远走不到，是一个伪状态。
- **严重度**：**中**（8 个入口零说明，学习成本高；伪状态误导后续维护者）
- **修复建议**：为 8 个 action 统一 `setToolTip(tr("%1 (%2)\n%3").arg(名称, 快捷键, 一句话功能))`；复核禁用态是否有明确需求，无则删除 `ToolDockStyle` 中的 disabled 分支与注释，避免"以为有、实际没有"的状态。

### M4. 菜单图标重复，两个工具对共用同一个图形

> ✅ 已落地（2026-12）：新绘 resources/icons/bezier-curve.svg（曲线）与 angle.svg（角度测量）入 qrc，describe().iconName 分别指向新图 —— 智能笔/曲线、旋转/角度测量不再同图。angle.svg 渲染后做像素重心校验（cy=167 偏下 39px 为全库最大偏差），上移 24px 修正至 cy=143（与 scissors 同级）。

- **位置**：`src/app/MainWindow.cpp:270`（曲线 = `pen`）与 `:266`（智能笔 = `pen`）；`:308`（角度测量 = `rotate`）与 `:277`（旋转 = `rotate`）
- **问题描述**：工具坞用 ElaIconType 字形能区分（PenNib / BezierCurve / Rotate / Angle），但菜单用的是同一批 SVG —— 同一个功能在两处呈现不同的图形，且菜单里「智能笔/曲线」「旋转/角度测量」两两同图，无法辨识。
- **严重度**：**中**（菜单是唯一带文字的地方，图标同图尚可忍受，但与工具坞不一致是实打实的视觉分裂）
- **修复建议**：补 `pen-bezier` / `angle` 两个 SVG（仓库已有 `resources/` 19 个 svg 的先例），或让菜单也走 ElaIconType 字体图标，统一图标语言。

### M5. 智能笔「直线 / 省道线」模式没有常驻视觉指示，且提示未提 W 键

> ✅ 已落地（2026-12）：状态栏提示变为**模式的常驻指示器** —— 新增运行期提示覆盖链 Tool::reportHintOverride → ToolHost::setHintOverride（非纯虚，测试桩可不实现）→ ToolManager::hintOverrideChanged → MainWindow::onToolHintOverride；智能笔 W 切模式时按 hintForMode() 即时改写状态栏文案，切工具时宿主清覆盖恢复 describe() 默认文案。

- **位置**：`src/tools/ToolSmartPen.h:100-103`（`Mode{Line, Dart}`）；`cycleMode()` 只有一次性 toast；`src/app/MainWindow.cpp:864-865`（智能笔提示）
- **问题描述**：W 在空闲时切换直线/省道线（`ToolSmartPen.cpp:554-571`），但切换后**没有任何常驻指示**。两种模式下下一次点击的语义完全不同：省道线起点必须吸附到已有点，否则直接 toast 拒绝（`:180-186`）。用户很容易在不知情的状态下进入省道模式，然后反复点、反复被拒。状态栏提示列出的操作里根本没有 W —— 这个模式在界面上是**不可发现的**。
- **严重度**：**中**（功能不可发现 + 状态不可见，两者叠加）
- **修复建议**：智能笔激活时预输入条已可见（`MainWindow.cpp:892-896`），在其左侧加一个可点击的模式 chip「直线 / 省道」，既是指示器也是切换入口；提示补 `W 切换直线/省道线`。

### M6. 「选中即操作」+ 5px 拖动阈值 —— 误触就会改几何

> ✅ 已落地（2026-12）：拖动阈值分档 —— press 未选中块 kDragThresholdPx=5px、press **已选中**块 kDragThresholdSelectedPx=10px（dragThresholdPxFor(pressWasSelected)）；选集已建立时误拖代价更高，5~10px 模糊带偏向"不动几何"。单选模式下 wasSelected 须在覆写 m_selection 之前取。

- **位置**：`src/tools/ToolSelect.cpp:51`（`kDragThresholdPx = 5.0`）、`:272-419`（press → `beginPressPending`）、`:438-466`（move 超阈值即 `beginDrag`）、`:493-508`（release 提交）
- **问题描述**：单选模式下按压一条**已选中**的线（`:410-418`）会重置选集并进入待定；随后只要鼠标移动超过 5 像素（约等于手指/触控板的一次抖动）就进入 `Dragging`，release 时几何被整体平移。整个过程没有拖动起手的视觉确认（无 `ClosedHandCursor`、无首帧高亮），虽然可 undo，但用户往往事后才发现线被挪了。5px 在触控板/高 DPI 屏上偏小。
- **严重度**：**中**（静默修改几何，CAD 场景下的高代价误操作）
- **修复建议**：阈值提到 8px；对"已选中的线"再放宽一档；拖动起手时把光标切成 `Qt::ClosedHandCursor` 并让被拖块闪一下高亮，给出"已经开始拖"的确认信号。

### M7. 悬停反馈每次鼠标移动执行 2~3 次全场景命中查询，无缓存无节流

- **位置**：`src/tools/ToolSelect.cpp:1120-1143`（`updateHoverCursor` → `hitBlock` → `m_scene->items()`），同函数末尾 → `refreshOverlapHint`（`:1310`）→ `collectOverlapCandidates`（`:1164`，**再一次** `m_scene->items()`，并对每个候选做一次跨图层的线性查找 `:1210-1211`）；调用点在每次无按钮 `mouseMove`（`:438-466`）
- **问题描述**：鼠标每移动一像素就做两遍完整的场景命中 + 一遍逐块元数据构造。图元数量增长（叠加 M2 的泄漏）时，这是肉眼可见的卡顿来源，且白白烧 CPU。另：`hitBlock` 与 `collectOverlapCandidates` 逻辑高度重复（都遍历 `items()`、都过滤活动层）。
- **严重度**：**中**（性能，随文档规模放大）
- **修复建议**：按 `(scenePos, structureEpoch)` 做单帧缓存；或下沉一个共享 `HitTester`，**一次** `items()` 同时产出"首选块 / 全部重叠候选 / 命中的段"，供三处复用（顺带解决 L2）。

### M8. 角度 HUD 不随视图平移缩放重定位；无效态只有变色没有原因

> ✅ 已落地（2026-12）：①ToolRotate::repositionHud() 挂 CanvasView::zoomFactorChanged + 双滚动条 valueChanged（ToolRotate 非 QObject，须 QObject::connect 限定名 + sender static_cast<CanvasView*>；连接存成员、onDeactivate 断开防悬垂），滚轮缩放/空格平移后 HUD 跟随锚心。②AngleHud::setError(QString)：公式解析失败时把 ExpressionEvaluator::Result::error 显示在单位标签位（⚠ 原因），不再只有边框变红。

- **位置**：`src/tools/ToolRotate.cpp:1321-1353`（`showHud()` 只在锚点确定/切换时 `move()` 一次；全文无对 scrollBar / view transform 变化的连接）；`src/ui/AngleHud.cpp:123-151`（`setValid`）
- **问题描述**：
  1. HUD 是挂在 viewport 上的 QWidget，位置在 `showHud()` 里按当时的 `mapFromScene` 算一次就固定了。打开后滚轮缩放或空格平移视图，HUD 会停在旧屏幕位置，与锚心明显脱节。
  2. 公式解析失败时只有边框和文字变红（`:143-150`），没有任何文字说明原因（"变量不存在" / "表达式语法错误"），用户只看到"我输的东西变红了"。
- **严重度**：**中**
- **修复建议**：`CanvasView` 的 transform / scrollBar 变化时回调一次重定位（或把 HUD 改为跟随锚心的场景内 QGraphicsProxyWidget）；`AngleHud` 增加 `setError(const QString&)`，把解析错误短文显示在单位标签位置。

### M9. 状态栏提示文本过长时硬裁剪，无省略号、无完整文本兜底

- **位置**：`src/app/MainWindow.cpp:551`（创建），`:552`（`sb->addWidget(m_toolHintLabel, 1)`）
- **问题描述**：对该 label 未设置 `setElideMode` / `setMinimumWidth` / `setSizePolicy`（对比 `:591`、`:597` 给坐标和缩放标签都设了最小宽度）。最长的交点提示有 66 个汉字（`:875-877`），1280 宽窗口下可用区域不足，文本会被直接裁掉且无省略号——用户看到的是半句话。
- **严重度**：**中**
- **修复建议**：`setElideMode(Qt::ElideRight)` + `setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed)`，并把完整文本同时 `setToolTip()`，鼠标悬停可看全。

### M10. 智能笔非模态弹窗打开期间画布完全无响应，且无任何提示

> ✅ 已落地（2026-12）：showDialogBlockedFeedback() —— 弹窗（QuickAuxDialog/省道）打开期间画布光标 = ForbiddenCursor + toast「请先完成…」（文案同值守卫防 mouseMove 每帧刷屏）；事件早退路径调用以兜底维持；弹窗 finished / 工具 deactivate 时 clearDialogBlockedCursor() 恢复。

- **位置**：`src/tools/ToolSmartPen.cpp:173-175`（`mousePress` 直接 return）、`:246-247`（`keyPress` 同样 return）
- **问题描述**：快捷辅助点弹窗 / 省道线弹窗是非模态的（设计意图是让用户切到变量面板查公式，`:119-121`）。但弹窗打开期间所有画布输入被静默吞掉：光标不变、无提示条、无 toast。用户切去面板查完公式、回到画布点一下——什么都没发生，第一反应是"工具卡死了"。
- **严重度**：**中**（配合 H2 一起，本项目的"工具无响应"感知问题相当突出）
- **修复建议**：弹窗打开期间把画布光标设为 `Qt::ForbiddenCursor`（或 `WhatsThisCursor`），并在画布顶部常驻一条细提示条"请先完成「快速辅助点」设置"；弹窗关闭时恢复。

---

## 3. 低危问题（Low）

| # | 位置 | 问题 | 建议 |
|---|------|------|------|
| L1 | `ToolIntersection.cpp`(3处) / `ToolCurveEdit.cpp`(3) / `ToolAngleMeasure.cpp`(2) / `ToolBreak` / `ToolRotate` / `ToolSmartPen` / `RotateGizmo` 各 1 | 共 8 处 `if (m_x) { m_scene->removeItem(m_x); delete m_x; m_x = nullptr; }` 清理样板，逐项手写、容易漏 | 基类提供 `ManagedItems` RAII 容器，登记后由 `deactivate` 统一释放（见 P1） |
| L2 | `ToolSelect.cpp:955-971` vs `ToolRotate.cpp:1808-1823` | `hitBlock` 两份近似实现，且一个用 `layersView().activeLayer()`、另一个用 `activeLayer()` —— 规则已经开始漂移 | 合并进共享 `HitTester`（与 M7 一起做） |
| L3 | 9 处 `SnapEngine m_snapEngine`（7 个工具 + `ConnectGesture.h:231` + `LeaderCandidatePicker.h:62`） | ~~`SnapEngine` 实际无状态~~ —— **前提已过时**：2026-09 性能专项为 SnapEngine 增加每块坐标缓存（`SnapEngine.h:170-171` 的 `m_snapBlockCache`/`m_snapSegCache`），已是有状态类；各工具独立实例 = 独立缓存，属合理设计 | ~~改为按半径构造的轻量值对象或共享实例~~ **无需做** |
| L4 | `ToolSelect.h:59` / `ToolSmartPen.h:94` / `ToolCurveEdit.h:41` 等 | 工具名用八进制转义硬编码中文（`"\xe9\x80\x89\xe6\x8b\xa9"`），不可读、不可维护 | 改用 `QStringLiteral(u8"选择")`；并把 `name()` 的返回类型从 `const char*` 改为 `QString` |
| L5 | `ToolManager.cpp:61-103` | `switchTool` 无条件 `make_unique` 重建，即使切的是当前工具 → 连按两次 R 会清空旋转目标与 HUD 内容 | `if (m_activeType == type && m_activeTool) return;`（或提供显式的 `reset()` 入口） |
| L6 | `ToolSelect.cpp:474-477` | 右键菜单守卫 `overlapMenu == nullptr && actCancel == nullptr && actComponent == nullptr && cands.size() < 2` 中，末项与首项等价，冗余 | 删除末项 |
| L7 | `src/ui/AngleHud.cpp:30-38` 与 `:134-142` | 通过 `parentWidget()->parentWidget()` 反向取场景样式，父链一变就静默回退到硬编码色（注释已承认） | 构造时直接传入 `CanvasStyle*`，去掉反向查找 |
| L8 | `src/canvas/CanvasScene.cpp:277-278` | `showToast` 用 `views().first()`，多视图（未来分屏）会画到错误视图 | 传入发起视图，或按 `QCursor::pos()` 所在视图定位

**L 项复核状态（2026-12）**：
- **L1**（清理样板）✅ 随 P1 `ManagedItems` 收口（7 文件 17 处）。
- **L2**（hitBlock 两份实现）✅ 随 P1 `HitTester.h` 合并。
- **L3**（SnapEngine 冗余实例）— **无需做**：2026-09 性能专项给 SnapEngine 加了每块坐标缓存（SnapEngine.h:170-171），"无状态"前提已过时；各工具独立实例 = 独立缓存，属有意设计。
- **L4**（八进制中文名）— 未处理（显示名已迁 `describe().displayName`，`name()` 保留原样）。
- **L5**（重复切换重建）✅ 随 P2 常驻实例池 + 同类型 no-op。
- **L6**（右键守卫冗余末项）✅ 已删（`cands.size() < 2` 与 `overlapMenu == nullptr` 等价）。
- **L7**（AngleHud 父链反查样式）✅ `AngleHud(QWidget*, const CanvasStyle* = nullptr)` 构造直传，两处调用点（ToolRotate/ConnectGesture）已传 `scene->style()`。
- **L8**（showToast 单视图假设）暂缓（未来分屏再按发起视图定位）。 |

---

## 4. 后端架构评估

### 4.1 注册机制：**没有**

`ToolType` 枚举 + `switchTool` 的 8 分支 switch（`ToolManager.cpp:61-103`）是唯一的"注册"。新增一个工具的改动清单（实测统计）：

| 文件 | 需要改动的位置 | 处数 |
|------|----------------|------|
| `src/tools/ToolManager.h` | `ToolType` 枚举加值 | 1 |
| `src/tools/ToolManager.cpp` | `switchTool` 加分支 | 1 |
| `src/app/MainWindow.h` | QAction 成员 + 槽函数声明 | 2 |
| `src/app/MainWindow.cpp` | QAction 创建 / 加入 group / triggered 连接 / 槽实现 / `specs[]` / `onToolChanged` 的 action 映射 / 按钮索引映射 / 提示分支 | 8 |

共 **12 处、跨 4 个文件**，其中 `MainWindow.cpp` 里有 **31 处** `ToolType::` 出现。工具的核心元数据（名字、图标、快捷键、提示、按钮位置）**没有一处存放在工具自己身上**，而是散落在 MainWindow 的 5 个平行结构里手工对齐——这是本次审查最核心的架构欠账。后果：H3（提示少一个分支）这类 bug 不是偶然，而是这个结构的必然产物。

### 4.2 抽象合理性：半好半坏

- **好的一半**：`Tool` 基类的接口面（activate/deactivate + 6 个输入事件 + name）很克制；手势与协作对象已经从工具里剥离成独立类，说明团队已经在做"工具 = 状态机 + 编排"的收敛。
- **坏的一半**：
  1. 基类声明的能力（`ensureHud/releaseHud`、上下文绑定）因成员遮蔽而大面积失效（H4）——**抽象存在但未被使用，比没有抽象更糟**，因为它会误导下一个维护者。
  2. `Tool` 没有"能力/契约"的维度：`MainWindow` 要拿到"这个工具是否阻止右键菜单"只能 `dynamic_cast<ToolRotate*>`（`MainWindow.cpp:124-127`）；`ToolManager` 要"切换前继承选区"也只能 `dynamic_cast<ToolSelect*>`（`ToolManager.cpp:79-87`）。**所有跨工具的差异化行为都靠向下转型**，这是抽象不足最典型的症状。
  3. 工具之间无共享能力层：HUD 3 套实现、命中测试 5 处 `items()`、清理样板 8 处（L1/L2/M1）。

### 4.3 状态管理：分散在三处，靠手工对齐

| 状态归属 | 存放位置 | 备注 |
|----------|----------|------|
| 工具内部交互状态 | 各工具私有成员（`RotateState` / `SelectState` / `Mode` …） | 7 套互不相同的枚举，无统一约定 |
| "哪个工具激活" | `ToolManager::m_activeType` | 唯一集中管理的部分 |
| 工具长什么样 | `MainWindow` 的 5 处平行结构 | 靠人肉对齐（H3/M4 的来源） |

更麻烦的是**隐藏状态**：`ToolRotate::m_selectionConfirmed`（H2）、`ToolSmartPen::Mode`（M5）、`ToolSelect::m_overlapIndex` 都是对行为有决定性影响、却在界面上完全不可见的状态。

### 4.4 生命周期：销毁重建是补丁的温床

每次 `switchTool` 都 `std::move` 销毁旧实例、new 一个新实例（`ToolManager.cpp:105-128`）。好处是状态默认干净；代价是所有"跨工具继承状态"的需求都只能打补丁：选区继承那段（`ToolManager.cpp:75-89`）要在 switch 前先 `dynamic_cast<ToolSelect*>` 抢出选集、new 完之后再 `adoptSelection` 塞回去，注释里还得解释"必须在 activate 之后因为需要 doc/scene"——这正是"每次都重建"这个决策在反噬。同理还有 L5（重复切换丢失状态）。

### 4.5 可测试性

- 现状：13 个测试文件涉及工具，但**全部只能通过 `dynamic_cast<ToolX*>(tm.activeTool())` 拿到具体类型**（`tests/test_rotate_copy.cpp` 有十余处，`tests/test_component.cpp:536/673` 等），没有一个抽象接口可供通用测试使用。
- 直接后果：**无法编写跨工具的横切测试**——比如"所有工具 deactivate 后不得在场景留下图元"（正是 M2 的问题）、"所有 ToolType 都有提示文本"（正是 H3 的问题）。这两类 bug 只能靠人肉 review 发现。
- 现有分层检查（`tools/check_layering.py`）做得好，但只管 include 方向，管不了这类契约。

---

## 5. 改进方案（可渐进式重构）

目标形态：

```
ToolDescriptor（id / 名称 / 图标 / 快捷键 / 提示 / 光标 / 工厂）
        ↓ 注册
   ToolRegistry ──create(id)──> Tool（持有 ToolContext）
        ↓ 遍历 descriptors
   MainWindow 自动生成 action / 按钮 / 提示（删掉 3 个平行 switch）
```

### P0 — 修补（0.5~1 天，零架构风险，建议立即做）

| 项 | 内容 | 收益 |
|----|------|------|
| H1 | `AngleHud::eventFilter` 增加 `ShortcutOverride` accept | 消除输入被快捷键劫持导致的会话丢失 |
| H3 | 提示改为 `ToolType → QString` 静态表 + 完整性断言 | 修复角度测量提示错配，杜绝同类扩散 |
| H2 | gizmo 区分确认态 + HUD caption 提示 + 更新 :871 文案 | 让"卡死"感知消失 |
| M2 | `ToolSelect` 析构中销毁重叠提示图元 | 消除泄漏与累积性卡顿 |
| M9 | 提示 label 加 elide + tooltip | 长文本不再被硬裁 |

改动量约 150 行，**无 API 变更、无签名变更**，可直接合入。

> **✅ P0 已落地（2026-08-29）**，ctest 28/30（2 红 = 既有基线）。
> 落地细节与本报告的差异：
> - **H1**：`AngleHud::eventFilter` 加 ShortcutOverride 分支（过滤器本就装在 `m_edit`+`this` 两处）。回归 = test_rotate_copy `hudShortcutOverrideContainment`（沿用 test_smartpen_aux 的 ShortcutOverride 事件范式）。
> - **H3**：表放在 **tools 层** `cad::tools::toolHintText(ToolType)`（ToolManager.cpp，switch 全覆盖无兜底），比"MainWindow 内 static 表"更利于测试；完整性断言 = 新测试 target **test_tool_hints**（全枚举非空 / 8 提示互异且以各自工具名开头 / 旋转提示必须含「右键或回车确认」）。
> - **H2**：`RotateGizmo::setConfirmed()`（未确认 = 虚线弧 + 空心锚环 + 减淡配色 / 确认 = 实线弧 + 实心锚环）+ `ToolRotate::applySelectionConfirmed(bool)` 统一翻转入口（标志 + gizmo + HUD caption 三同步，翻转点禁止再散写标志位）+ refreshHudText 的「（右键/回车确认）」后缀。回归 = test_rotate_copy `d15ConfirmStateHasVisualAndCaptionHints`。
> - **M2**：`ToolSelect::~ToolSelect()` 中 `delete m_overlapHintBox`（label 是其子项随之释放；QGraphicsItem 析构自行脱离 scene；MainWindow 子对象反序析构保证 tool 先于 scene 亡，安全）。
> - **M9**：报告建议的 `setElideMode` 在 QLabel/ElaText 上**不存在**——落地改为：横向 `QSizePolicy::Ignored`（解除整句宽度撑布局）+ `QFontMetrics::elidedText`（Resize 事件经 MainWindow::eventFilter 驱动重算，同值守卫防递归）+ 全文 tooltip。

### P1 — 抽公共能力层（1~2 天，纯搬移）

> **✅ P1 已落地（2026-08-29）**，ctest 全绿（除既有基线）。落地要点与差异：
> 1. **HudItem 收口 `src/canvas/HudItem.h/.cpp`**（全局命名空间，随 canvas 模块既有约定）：双外观 `Look::ThemeDefault`（跟随 CanvasScene 主题，原智能笔 HUD 视觉）/ `Look::DarkPill`（深胶囊白字 11px，toast 与重叠提示共用）；`moveToPoint/placeAtScene` 均带**屏幕像素偏移**参数（÷zoom 落地，原重叠提示 "+12 场景单位" 缩放漂移即 M1 所指，已治）。迁移 4 处：ToolSmartPen 预览 HUD、Tool::ensureHud（Measure/Intersection/AngleMeasure 共用）、ToolSelect 重叠提示（替换手搭 rect+text 对，M1 缩放补偿到位）、CanvasScene::showToast（同）。AngleHud（QWidget 悬浮框）性质不同，不在本次收口范围（其 M8 问题另案）。
> 2. **HitTester 收口 `src/tools/HitTester.h`**（header-only 自由函数）：`blockHitsAtScene()` 一次 items() 产出全部块命中（堆叠降序 + blockId 去重 + 活动层过滤**唯一规则源**——消除 hitBlock 两份实现里 `layersView().activeLayer()` vs `activeLayer()` 的漂移，L2）；`isBlankSpaceAtScene()` 空白判定（不分层，保留智能笔右键原语义）。迁移 5 处：ToolSelect::hitBlock / hitSegmentAt / collectOverlapCandidates、ToolRotate::hitBlock、ToolSmartPen::isBlankSpace。**M7 的"单帧缓存"以重组实现**：updateHoverCursor 改为单次扫描，光标判定与重叠提示共用一份命中结果（经 `refreshOverlapHint(pos, &precomputed)` 传入）——每帧 2~3 遍 items() 降为 1 遍，且无缓存失效风险。
> 3. **ManagedItems 收口 `src/canvas/ManagedItems.h`**（RAII 登记）：`own(item, &shadow)` 登记时绑定影子指针，`release(item)` 供会话中释放-重建型图元，`clear()` deactivate 统一释放并自动置空影子。迁移 7 文件 17 处样板（比 L1 清单的 8 处多）：ToolBreak/ToolIntersection/ToolAngleMeasure/ToolCurveEdit/ToolMeasure/ToolSmartPen 预览件/ToolRotate(aimRing+选集包围盒)/RotateGizmo。**顺带修掉 ToolCurveEdit 清理块不置空指针的悬空隐患**。

1. `HudItem` 从 `ToolSmartPen.h` 提到 `src/canvas/`（或 `src/ui/`），统一实现 1/zoom 补偿；`showOverlapHint`（M1）、`showToast`（M1）、`ToolAngleMeasure`/`ToolIntersection` 的 HUD 全部改用它。**消灭 3 套 HUD 实现**。
2. 抽出 `HitTester`（首选块 / 全部重叠候选 / 命中的段，一次 `items()` 出结果），替换 `ToolSelect::hitBlock`、`:hitSegmentAt`、`::collectOverlapCandidates`、`ToolRotate::hitBlock`、`ToolSmartPen` 的 `isBlankSpace` 共 5 处（M7 + L2），并加单帧缓存。
3. 基类加 `ManagedItems` RAII 容器：工具把临时图元登记进去，`deactivate` 统一释放，消灭 8 处清理样板（L1），顺带保证 M2 这类泄漏不可能再发生。

改动量约 400~600 行，以搬移为主；29 个 ctest 用例可回归。

### P2 — 统一基类契约 + ToolContext（2~3 天）

1. 删除 5 处成员遮蔽（H4），`activate/deactivate` 改为非虚模板方法 + `onActivate/onDeactivate` 虚钩子——**让"必须调基类"由编译器保证，而不是靠注释**。
2. 引入 `struct ToolContext { CanvasScene*; ParamDocument*; QUndoStack*; ToolHost*; }`，替代现在逐个注入的 `setUndoStack` / `setToolSwitchRequest` / `setEditTargetCallback`（三处 setter 分散在 `ToolManager.cpp:116-124`）。
3. 工具切换从"销毁重建"改为"保留实例 + `reset(ToolContext)`"（L5）：实例常驻后，`ToolManager.cpp:75-89` 那段选区继承补丁可以删掉，改为 `reset` 时传入继承参数。

改动量约 300 行，触碰 8 个工具的 activate/deactivate 签名。**建议先拿 Measure / AngleMeasure / Break 三个小工具做样板，验证模式后再铺开**。

> **✅ P2 已落地（2026-12）**，ctest 28/30（2 红 = 既有基线），分层检查干净。落地细节与本报告的差异：
> - **H4**：Tool::activate/deactivate 改为**非虚**（全量入口 activate(const ToolContext&) + 两参便捷重载 activate(scene, doc) 供测试直驱），派生类只实现 onActivate/onDeactivate 虚钩子；5 处遮蔽删除（ToolSelect/ToolSmartPen/ToolRotate/ToolCurveEdit/ToolBreak 的 m_scene/m_paramDoc 全删），ToolRotate 悬浮角度框改名 m_angleHud（避免遮蔽基类 HudItem* m_hud）。Tool::setUndoStack 保留为 ToolManager::setUndoStack 的活更新通道（不重激活）。
> - **ToolContext**：ToolContext{scene, paramDoc, undoStack, host} + ToolHost 接口（requestToolSwitch / setEditTarget），ToolManager 实现之；Tool::setToolSwitchRequest 与 ToolSelect::setEditTargetCallback 已删，测试改用 ToolHost 桩（test_smartpen_aux 4 处 / test_nav_smoke 1 处）。
> - **L5**：ToolManager 改为常驻实例池（std::map<ToolType, std::unique_ptr<Tool>>），重复点击当前工具 no-op；选区继承改为 switchTool 内先抓 Select 选集再 deactivate（Select 常驻但其 deactivate 清选集）。常驻实例下各工具 onActivate 补齐会话复位（Select 回单选 / SmartPen 回直线 / Rotate 回角度模式 / Intersection 走 resetState / Measure·AngleMeasure 清 snap 残留）。
> - **测试兜底**：H3 完整性断言（test_tool_hints）与既有工具用例全绿；test_nav_smoke / test_smartpen_aux 直驱工具改经 ToolContext+ToolHost 注入。

### P3 — Registry + 元数据驱动 UI（3~5 天）

1. 每个工具提供 `static ToolDescriptor describe()`：`{ id, 显示名, 图标, 快捷键, 提示文本, 默认光标, factory }`；新增 `ToolRegistry::registerTool<ToolX>()`。
2. `MainWindow` 改为遍历 descriptors 生成 QAction、工具坞按钮、状态栏提示 —— **删掉 action 映射 / 按钮索引映射 / 提示分支这 3 个平行 switch**，`ToolType::` 的出现从 31 处降到 ~5 处。
3. `ToolManager::switchTool` 改为 `registry.create(id)`；测试可直接从 registry 构造工具，不需要 `dynamic_cast`（4.5）。

**收益**：新增工具从"改 12 处、跨 4 文件"降到"加 1 对 .cpp/.h + 1 行注册"；工具可按需裁剪/插件化；"每个工具必须有提示/快捷键"这类契约可由单测强制。
**成本**：`setupMenuBar` / `setupToolBar` / `onToolChanged` 三块装配代码需重写（约 250 行）。**建议保留 `ToolType` 枚举作为 descriptor 的 id**，避免一次性改动过大。

> **✅ P3 已落地（2026-12）**，ctest 28/30（2 红 = 既有基线），分层检查干净。落地细节与本报告的差异：
> - **ToolDescriptor 归 tools 层**（`src/tools/ToolRegistry.h`）：{ id, displayName(含 & 加速符), iconName(Phosphor SVG 名), shortcut, hintText, factory }；`ToolType` 枚举随注册表迁入 ToolRegistry.h（ToolManager.h 不再定义，Tool.h 保留前置声明）。工具坞 ElaIconType 字体图标是 app 层视觉偏好，保留在 MainWindow（按枚举序数组映射，非工具元数据）；默认光标未入 descriptor（现状光标由各工具运行期设置，无静态默认）。
> - **MainWindow**：8 个 QAction 成员 → `QHash<ToolType, QAction*> m_toolActions` + 注册序 `m_toolOrder`；8 个 actionX 槽 → 遍历时 lambda（connect 到 `m_toolManager->switchTool(type)`）；onToolChanged 两个平行 switch → map 查找 + `m_toolOrder.indexOf`；菜单/工具坞按钮/图标/提示四处全部遍历 registry（M3 顺带补 tooltip = hint 全文；交点菜单图标统一为 crosshair，消除 refreshToolIcons 与创建时的 funnel 不一致）。
> - **ToolManager**：ensureTool 工厂 → `ToolRegistry::instance().create(id)`（保留 P2 常驻实例池）；`toolHintText(ToolType)` 退化为查 registry 的薄封装（ToolRegistry.cpp）。
> - **测试**：test_tool_hints 改查 registry（契约断言不变：非空 / 自名开头 / 互异 / 旋转含 D15 确认门）；`ToolType::` 在 MainWindow 的出现从 31 处降到 ~10 处（注册序/枚举映射/onToolChanged 少量必要引用）。

### 复核轮补充修复（2026-08-29 晚：对他窗口 P0~P3 落地的复核 + 收尾）

复核结论：**构建 EXIT=0、ctest 28/30（2 红 = 既有基线，数值与 AGENTS.md 一字不差）、
两个守卫脚本 OK** —— 无回归；H1/H2/H3/H4 的回归测试读源码确认是真测试（逐个字母投递
ShortcutOverride / 确认门三态往返）。复核另发现 7 项问题（N1~N7），并顺手补上此前未修的
M4/M5(文案与常驻指示)/M6，全部落地：

| 编号 | 问题 | 处置 |
|------|------|------|
| N1 | `MainWindow.cpp` 工具坞图标用 `std::array<…,8>` + `arr[static_cast<int>(type)]` 枚举序下标：加第 9 个工具 = **越界读 UB 且编译不报错**；且与"新增工具不再改 MainWindow"的承诺矛盾 | 改 `QHash<ToolType,IconName>` 键控查表 `toolDockIcon()`；查不到 = 兜底箭头图标 + `Q_ASSERT_X`/`qWarning`。AGENTS.md 说法已改正 |
| N2 | L5 常驻实例后"重进工具 = 复位"不再由析构免费提供；8 个工具里只有 `ToolBreak::onActivate` 是空的 → hover/弹窗快照跨切换残留 | 补显式复位 |
| N5 | M2 的清理写在 `~ToolSelect()`，但常驻实例下析构只在退出时跑；累积泄漏实际是靠"只有一个实例"消除的，不是这行代码的功劳 | `delete m_overlapHint` 挪到 `onDeactivate()`；并给 `~ToolManager` 加"先 deactivate 激活工具"——否则退出时激活工具的 onDeactivate 从不执行，"清理写在 onDeactivate 就够了"这条约定不成立 |
| N3 | `ToolRotate` 仍有 2 处直写 `m_selectionConfirmed`，绕过 H2 建立的统一翻转入口 | 改走 `applySelectionConfirmed(false)`（其 `updateGizmo`/`refreshHudText` 都有空守卫，两处调用点安全） |
| N4 | H1 的过滤器只装 `m_edit`+`this`，模式切换按钮漏装 | 三个可聚焦子件全装；过滤器改为先判 `watched` 再分流 |
| N6 | `test_tool_hints` 的 `kAllTypes[]` 手写 8 项，新增枚举值既不编译失败也不断言失败（旧注释"编译不过/断言不过"的说法不准确） | 新增 `registryOrderCoversEveryTool`：用 `registry.order().size()` 与 `kExpectedRegistered` 对账 + 双向覆盖断言 |
| N7 | `registerTool` 先 `std::move(d)` 再读 `d.id`（标量成员 move 后值不变，行为对但读的是已移动对象）；无重复注册防护 | 先取 id 再 move；加重复注册断言 + 改 `emplace` |
| M4 | 曲线=pen（与智能笔同图）、角度测量=rotate（与旋转同图） | 新增 `resources/icons/bezier-curve.svg`、`angle.svg`（**新绘资源，需目视确认**）；两处 `iconName` 已换、qrc 已登记 |
| M5 | 智能笔提示未提 W，省道线模式完全不可发现 | 提示带模式（`hintForMode()` 两模式同源，避免文案漂移）+ **运行期覆盖机制**：`ToolHost::setHintOverride`（**非纯虚**，无头单测桩不实现也能编译）→ `ToolManager::hintOverrideChanged` → `MainWindow::onToolHintOverride`，按 W 时状态栏即时改写；切工具自动清除，不跨工具继承。状态栏因此成为模式的常驻指示器 |
| M6 | 拖动阈值单一 5px，"选中即操作"下手抖即静默平移几何 | 分档：press 未选中 = 5px，press **已选中** = 10px（`dragThresholdPxFor()`）；单选模式下 `wasSelected` 必须在覆写 `m_selection` **之前**取 |
| — | `ToolRotate` 3 处 + `FollowerAngle.h` 1 处丢弃 `[[nodiscard]]` 的 `evaluateLengthMm` 返回值（既有警告，此前被 Tool.h 编译失败掩盖） | 显式 `(void)` + 注释说明"求值失败保持 baseline"的兜底语义 |

**仍未处理（需设计决策 / 改动面大，已在本报告标注）**：
- **M8**：角度 HUD 不随视图平移缩放重定位（需 CanvasView 暴露变换回调）；无效态无错误原因文本（需 `AngleHud::setError` + 求值错误分类）。
- **M5 剩余**：报告建议的"预输入条模式 chip"属 UI 增改；本次只做到状态栏常驻文案。
- **M10**：非模态弹窗期间画布静默无响应（需选光标语义 + 常驻提示条，涉及交互设计）。
- **M3 剩余**：`ToolDockStyle` 的"禁用态"分支仍是死代码（全项目无任何地方禁用工具按钮）——需先决定"工具是否会有禁用态"。
- **L6/L7/L8**：冗余条件 / `parentWidget()->parentWidget()` 反向取样式 / `views().first()` 单视图假设。

### 迁移成本总览

| 阶段 | 工作量 | 风险 | 前置依赖 | 可独立交付 |
|------|--------|------|----------|------------|
| P0 | 0.5~1 天 | 极低（无签名变更） | — | 是 |
| P1 | 1~2 天 | 低（搬移为主） | 建议先做 P0 | 是 |
| P2 | 2~3 天 | 中（改 8 个工具签名） | **必须先做 H4** | 可按工具分批 |
| P3 | 3~5 天 | 中（重写装配层） | P2 完成 | 否 |

**推荐节奏**：P0 本周内单独一个 PR；P1 紧随；P2/P3 排在大功能迭代之间，P2 按工具分批合入，每批都保持 ctest 全绿。

---

## 6. 问题速查（按严重度排序）

| 级别 | 编号 | 一句话 | 位置 |
|------|------|--------|------|
| 高 | H1 | 角度 HUD 输入公式被单字母快捷键劫持，工具被切换、会话丢失 | `ToolRotate.cpp:1351` / `AngleHud.cpp:163` |
| 高 | H2 | 旋转确认门无视觉反馈 + 提示文案描述已废弃行为 | `ToolRotate.h:249` / `MainWindow.cpp:871` |
| 高 | H3 | 角度测量复用「选择」的提示文本 | `MainWindow.cpp:863-884` |
| 高 | H4 | 基类上下文成员被 5 个工具遮蔽，基类契约失效 | `Tool.h:84` / `ToolSelect.h:196` 等 5 处 |
| 中 | M1 | 重叠提示与 toast 不补偿缩放 | `ToolSelect.cpp:1357` / `CanvasScene.cpp:294` |
| 中 | M2 | 重叠提示图元不销毁，每次切工具泄漏 2 个 item | `ToolSelect.cpp:1346` |
| 中 | M3 | 工具按钮无 tooltip；禁用态样式是死代码 | `MainWindow.cpp:409` / `ToolDockStyle.h:71` |
| 中 | M4 | 智能笔/曲线、旋转/角度测量菜单图标同图 | `MainWindow.cpp:266/270/277/308` |
| 中 | M5 | 直线/省道线模式无指示且提示未提 W | `ToolSmartPen.h:100` / `MainWindow.cpp:864` |
| 中 | M6 | 5px 拖动阈值 + 无起手确认 → 误触改几何 | `ToolSelect.cpp:51` |
| 中 | M7 | 悬停每次移动做 2~3 次全场景命中查询 | `ToolSelect.cpp:1120` / `:1164` |
| 中 | M8 | HUD 不随视图变换重定位；无效态无原因说明 | `ToolRotate.cpp:1321` / `AngleHud.cpp:123` |
| 中 | M9 | 提示文本过长硬裁剪、无省略号 | `MainWindow.cpp:551` |
| 中 | M10 | 非模态弹窗期间画布静默无响应 | `ToolSmartPen.cpp:173` |
| 低 | L1~L8 | 清理样板 / 命中重复 / SnapEngine 冗余 / 八进制中文 / 重复切换重建 / 冗余条件 / 父链取样式 / 单视图假设 | 见第 3 节 |
