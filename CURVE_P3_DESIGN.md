# CURVE_P3_DESIGN —— 曲线系统 P3 改进任务指令

> 本文档是**交给执行 AI 的完整任务书**：背景、设计决策、逐文件修改清单、测试要求、
> 工程规矩、验收标准全部自包含。执行前请完整阅读；涉及「拍板项」的地方已给出默认
> 建议，若用户在场请确认，否则按默认建议执行并在收尾报告中注明。
>
> 任务来源：2026-08-28 曲线系统审查（P0/P1/P2 已修复并入库，见
> `TROUBLESHOOTING.md` 第 4 组「曲线算法四连修」条目）。P3 = 剩余两项增强：
> ①画布上断开切线锁定的快捷键；②Hobby 求解极端转角的过冲限幅。

---

## 0. 执行须知（先读这个）

1. **修 bug / 改行为前，先 grep `TROUBLESHOOTING.md` 相关关键词**（踩坑经验库）。
   改快捷键前**必查第 0 组快捷键登记表**，改完**必更新**该表。
2. 本项目铁律（违反 = 静默故障，详见 `AGENTS.md`）：
   - **禁止 `const_cast`**。模型变更走 ParamDocument 门面方法或 undo 命令。
   - **凡"曲线结构变了但点没动"的路径必须显式 `++block->geometryEpoch`**，
     否则惰性曲线缓存不重建、画布不刷新。
   - **交互函数必须显式接入事件流**（mouseMove/mousePress/keyPress），并用单元
     测试直接驱动事件流验证——只实现不调用 = 功能静默失效。
3. 构建/测试：
   - 构建：`tools\build.bat`（普通 PowerShell 直接跑 cmake 找不到 cl.exe，必须走
     该脚本；Ninja Debug，binaryDir = `build/out`）。
   - 测试：`cd build/out && ctest`。**既有基线 = 22/26 通过，4 红**：
     `test_serializer`（1 例基线）、`test_component`（1 例基线）、
     `test_intersection_update`（依赖活档 `E:/3.gcad` 内容，环境漂移）、
     `test_extend`（同前）。**验收 = 不新增任何红**；GUI 时序抖动用例
     （test_dialog_tabs / test_aux_layer / test_rotate_copy）单测复跑即过不算回归。
   - QtTest 可执行文件若直接跑 stdout 无输出（GUI 子系统），用
     `./test_xxx.exe -o result.txt,txt` 落盘读结果。
4. **文件编码**：所有源文件（.h/.cpp）必须 UTF-8 **with BOM**，否则 MSVC 解析
   失败。编辑工具（含 Edit/Write 类工具）会剥掉 BOM——**每次编辑含中文的源文件
   后必须补 BOM**：
   ```powershell
   $p = '<路径>'; $b = [System.IO.File]::ReadAllBytes($p); if (-not ($b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) { $t = [System.IO.File]::ReadAllText($p, [System.Text.UTF8Encoding]::new($false)); [System.IO.File]::WriteAllText($p, $t, [System.Text.UTF8Encoding]::new($true)) }
   ```
5. 任务收尾：把根因 + 修复验证追加到 `TROUBLESHOOTING.md` 对应主题分组
   （本任务属第 4 组「数据 / 引擎」的曲线条目区）；若触碰了 `AGENTS.md` 声明的
   事实（用例数 / 命令 / 路径 / 约定行为），同步修订 AGENTS.md。

---

## 1. 任务 P3-1：Alt + 拖拽切线手柄 = 断开切线锁定（尖角模式）

### 1.1 现状事实（已核实，行号可能漂移，以函数名为准）

- 曲线锚点有两只切线手柄（入/出），锁定语义在
  `src/tools/ToolCurveEdit.cpp` 的 `dragHandleTo()`：
  **`tangentLocked = true`（默认）= 两手柄方向共线、长度各自独立**
  （拖一只，另一只跟着转保持平滑，但保持自己的长度——"平滑不等长"）。
  `tangentLocked = false` = 两只完全独立（允许尖角）。
- **切换锁定的唯一入口是属性面板**：`src/tools/SegmentAnchorTab.cpp`
  `m_chkTanLocked` 复选框（toggled 直接写 `pt->tangentLocked` + `resolveAll`，
  **不走 undo 栈**——面板路径现状如此，本任务不改它）。
- 画布手柄拖拽链路：`ToolCurveEdit::mousePress` → `handleHitTest()` →
  `beginHandleDrag(which)` →（mouseMove）`dragHandleTo()` →（mouseRelease）
  `endHandleDrag()` → 推 `cad::cmd::SetCurveTangentCommand`。
- **`SetCurveTangentCommand` 目前只快照 tangentIn/tangentOut/autoTangent**
  （`src/document/commands/BlockCommands.h` ~492 行类定义、
  `BlockCommands.cpp` ~1185-1220 redo/undo），**不含 tangentLocked**——
  不解锁直接拖拽没问题，但本任务要在拖拽中改锁定态，必须扩展快照，否则
  undo 无法恢复锁定标志。
- 修饰键占用：工具内 **Ctrl+点击 = 放置曲线点、Shift+点击 = 删除曲线点**；
  **Alt 修饰键全 `src/` 零使用**（已 grep 确认），QAction 层仅 Alt+F4（系统级）。
  Windows 上 Alt 单按会激活菜单栏，但 **Alt+鼠标拖拽安全**（按钮按下期间菜单不激活）。

### 1.2 设计决策（拍板项，附默认建议）

| # | 决策点 | 默认建议 |
|---|--------|----------|
| D1 | 触发方式 | **Alt + 拖拽手柄**：`beginHandleDrag` 时按住 Alt → 本次拖拽以断锁模式进行，并将 `pt->tangentLocked` 持久置 false（拖完仍断锁，与 Illustrator「转换锚点」语义一致）。不做"仅本次拖拽临时断锁、松手回锁"（反直觉）。 |
| D2 | 回锁入口 | 沿用面板复选框（已有）。**不**新增画布回锁手势（避免过度设计）。 |
| D3 | Alt 在拖拽中途按下/松开 | 以**拖拽开始时**的修饰键为准，拖拽中不响应 Alt 变化（简单、无状态跳变）。 |
| D4 | 视觉反馈 | 可选：断锁拖拽时两只手柄圆点描边换色（如橙）提示"尖角模式"。**可做可不做**；做则只改 `updateHandleGraphics()` 的颜色选择，不加新 item。 |
| D5 | 端点镜像手柄 | 曲线端点只有一只相邻手柄（另一只镜像，见 `anchorTangents()` 端点镜像逻辑），Alt 断锁对端点语义 = 与非端点一致：置 `tangentLocked=false`。不特殊处理。 |

### 1.3 修改清单（按序执行）

**Step 1：扩展 undo 命令快照**（先行，测试依赖它）

- `src/document/commands/BlockCommands.h` `SetCurveTangentCommand`：
  构造函数末尾追加 `bool oldLocked, bool newLocked` 两参数；私有成员追加
  `bool m_oldLocked = true; bool m_newLocked = true;`；更新类注释
  （"Stores the before/after tangentIn/tangentOut/autoTangent" → 加 tangentLocked）。
- `src/document/commands/BlockCommands.cpp` 构造函数/redo/undo：
  redo 末尾 `pt->tangentLocked = m_newLocked;`（放在 `++geometryEpoch` 之前）；
  undo 对称。构造初始化列表接收两新值。
- 唯一调用点 `ToolCurveEdit::endHandleDrag()` 同步传新值（见 Step 3）。

**Step 2：ToolCurveEdit 状态与签名**

- `src/tools/ToolCurveEdit.h`：
  - `beginHandleDrag(int which)` → `beginHandleDrag(int which, Qt::KeyboardModifiers mods)`。
  - 私有成员追加 `bool m_handleOldLocked = true;`。
- `src/tools/ToolCurveEdit.cpp`：
  - `mousePress` 中调用处改为 `beginHandleDrag(h, event->modifiers());`。

**Step 3：拖拽逻辑**

- `beginHandleDrag`：
  ```cpp
  m_handleOldTanIn  = pt->tangentIn;
  m_handleOldTanOut = pt->tangentOut;
  m_handleOldAuto   = pt->autoTangent;
  m_handleOldLocked = pt->tangentLocked;   // 新增快照
  // ... 既有 autoTangent 物化逻辑保持不变 ...
  // 新增：Alt = 本次以尖角模式拖拽（持久断锁，D1）
  if (mods & Qt::AltModifier)
      pt->tangentLocked = false;
  ```
- `dragHandleTo`：**不改**。它内部两个 `if (pt->tangentLocked)` 分支在断锁后
  自然跳过对侧同步——这正是 Alt 断锁要的效果。
- `endHandleDrag`：推命令时带锁定新旧值：
  ```cpp
  m_undoStack->push(new cad::cmd::SetCurveTangentCommand(
      m_paramDoc, m_handleBlockId, m_handlePointId,
      m_handleOldTanIn, m_handleOldTanOut, m_handleOldAuto,
      pt->tangentIn, pt->tangentOut, pt->autoTangent,
      m_handleOldLocked, pt->tangentLocked));
  ```
- `cancelHandleDrag`：追加 `pt->tangentLocked = m_handleOldLocked;`。

**Step 4（可选，D4）**：断锁提示色——`updateHandleGraphics()` 里
`pt->tangentLocked == false` 时手柄圆点/线用橙色（如 `QColor(0xE6,0x8A,0x00)`）
替代青色。

**Step 5：登记表**——`TROUBLESHOOTING.md` 第 0 组「工具内部按键」表追加：
`| Alt(拖拽时) | 曲线编辑工具：拖切线手柄断开锁定（尖角模式，持久；回锁走属性面板复选框） | ToolCurveEdit.cpp beginHandleDrag |`

### 1.4 测试要求（必须新增，驱动真实事件流）

惯例参照 `tests/test_select_wkey.cpp`：`CanvasScene scene(&doc)` +
`cad::tools::ToolManager tm(&scene)` + 真实 view + `sendMouse` lambda 发送
`QMouseEvent`（带修饰键）经 viewport 走完整事件链。

新增用例（放 `tests/test_select_wkey.cpp` 或新建 `tests/test_curve_edit.cpp`——
若新建，target 只需加入 `CMakeLists.txt` 测试区并链接库，**切勿把项目头文件加进
测试源列表**，会 LNK2005；ctest 用例总数 26→27 需同步修订 `AGENTS.md` 验证命令
段的"ctest 共 26 个用例"表述）：

1. `altDragHandleBreaksLock`：构造块（直线 + Ctrl 放两个曲线点成曲线）→
   点锚点显示手柄 → **Alt+Press** 出手柄 → Move → Release。断言：
   ①`pt->tangentLocked == false`；②被拖手柄切线已更新、**对侧切线保持
   断锁前值**（未被共线同步）；③`autoTangent == false`（首次手动物化）。
2. `plainDragKeepsLock`：同样路径**不按 Alt** → 断言 `tangentLocked` 不变、
   对侧切线 = 同方向保长度（既有共线语义不回归）。
3. `altDragUndoRestoresLock`：用例 1 后 `doc.undoStack()->undo()` → 断言
   tangentLocked 回 true、两手柄切线回拖前值；`redo()` 复原断锁态。
   （注意：连接手势/工具的 undo 栈是 **ParamDocument 内部栈**
   `doc.undoStack()`，不是 MainWindow 的栈——见 TROUBLESHOOTING 既有条目。）

既有 `test_curve` 29 用例、`test_commands` 46 用例必须保持全绿。

---

## 2. 任务 P3-2：Hobby 求解极端转角过冲限幅（overshoot clamp）

### 2.1 现状事实（已核实）

- 自动切线生产路径 = Hobby 样条：`src/geometry/CurveMath.cpp`
  `solveHobbyTangents()`。方向来自加权角度三对角系统，**长度来自
  `hobbyVelocity(theta, phi) * chordLen / tau`**（匿名命名空间
  `hobbyVelocity`，Hobby 1986 经典公式）。
- 锚点（CurveAnchor）弦向定位 `interpPercent` **允许超出 [0,1] 外推**
  （`src/parametric/ParamPoint.h` 注释明确 "can exceed [0,1] for
  extrapolation"）。外推/近折返锚点序使相邻弦接近 ±180° 折返，此时
  `hobbyVelocity` 分子中 `(cosθ − cosφ)` 项放大、分母趋小，**手柄长度可达
  弦长数倍 → 曲线打圈/自交**。Seamly2D 同源实现亦无防护。
- 正常服装曲线的手柄/弦长比典型值 0.3~0.7（Hobby 速度公式输出区间），
  限幅阈值只要 ≥1.5 就不会影响正常形状。

### 2.2 设计决策（拍板项，附默认建议）

| # | 决策点 | 默认建议 |
|---|--------|----------|
| D6 | 限幅对象 | **手柄长度限幅**（方案 A），不限转角（方案 B 会扭动角度系统解出的方向，破坏 C1 视觉）。 |
| D7 | 阈值 | `kMaxHandleRatio = 2.0`（手柄长 ≤ 2×对应弦长，除法在 tau 之后），常量放 `CurveMath.cpp` 匿名命名空间并注释"可调、只拦失控形态"。 |
| D8 | 手动切线 | **只限 AUTO 点**；用户手动拖出的长手柄是显式意图，不动（与 D 系列"用户输入优先"原则一致）。 |
| D9 | 形状变化披露 | 限幅只影响触发阈值的极端曲线；既有正常文档形状**逐位不变**（阈值远高于正常速度输出）。收尾报告需列出被触及的测试数据验证这一点。 |

### 2.3 修改清单

- `src/geometry/CurveMath.cpp`：
  - 匿名命名空间追加：
    ```cpp
    /// Handle-length clamp (P3-2): at near-180° chord fold-backs (e.g.
    /// anchors extrapolated past a curve endpoint) the Hobby velocity
    /// formula emits handles several times the chord length and the curve
    /// loops. Normal garment curves sit at ratio 0.3~0.7, so 2.0 only
    /// catches runaway shapes. Manual tangents are never clamped.
    constexpr double kMaxHandleRatio = 2.0;
    ```
  - `solveHobbyTangents()` 手柄长度循环（现有 `cOut`/`cIn` 计算处）：
    auto 分支计算后追加
    `cOut = std::min(cOut, chordLen[k] * kMaxHandleRatio);`（cIn 对称）。
    manual 分支（`manualTanOut[k].length()`）保持不变。
- `src/geometry/CurveMath.h`：`solveHobbyTangents` 注释补一句限幅说明。
- **不要**改 `hobbyVelocity` 本身（它是纯公式，多处语义依赖）。

### 2.4 测试要求

`tests/test_curve.cpp` 新增（文件为纯 ASCII，注释用英文）：

1. `hobbyFoldbackHandlesClamped`：构造近折返锚点序（如
   `{(0,0), (100,0), (50,2), (150,0)}` 或显式构造出 >2× 弦长的速度输出
   的点位）→ `buildBezierSpans(..., Hobby)` → 遍历所有 span 断言
   `(ctrl1-p0).length() <= 2*chord + 1e-9` 与 `(p3-ctrl2).length()` 对称。
   **先打印修复前实测值注释在旁**（执行时先跑一遍无限幅版本确认该用例
   在旧代码下确实超限，再开限幅——防止写一个永远通过的假用例）。
2. `hobbyNormalShapeUnclamped`：既有 `hobbyOvershootVsC2` 的领口点位
   `{(0,0),(40,50),(80,-20),(140,30)}`，断言所有手柄比 < 2.0（即正常形状
   不触发限幅），且与无限幅参考（直接调 `solveHobbyTangents` 数学重算或
   硬编码修复前数值）逐位一致。
3. 既有 Hobby 全部用例（hobbyInterpolatesPoints / EndpointChordDirection /
   TangentContinuity / ManualRespected / TensionEffect / OvershootVsC2）
   必须保持绿。

---

## 3. 收尾清单（DoD，逐项打钩）

- [ ] 构建通过（`tools\build.bat`，exit=0）
- [ ] `ctest` 不新增红（基线 22/26；若新建 test_curve_edit 则 23/27 并修订
      AGENTS.md 用例数）
- [ ] 新测试全部通过且**在旧代码下确实失败**（至少各验证一次）
- [ ] 编辑过的含中文源文件全部有 BOM（用第 0 节脚本逐文件检查）
- [ ] `TROUBLESHOOTING.md`：第 0 组快捷键表 +Alt 条目；第 4 组追加本次两项
      修复条目（根因 + 设计决策编号 + 验证结果）
- [ ] `AGENTS.md` 事实核对：用例数 / 交互约定（若新增 Alt 语义可在「开发规范」
      的 W 键条目旁补一句）按需修订
- [ ] 未引入 `const_cast`；模型变更均走命令/门面
- [ ] 收尾报告列出：改动文件清单、D1-D9 拍板项实际取值、测试证据

## 4. 明确不做（防止范围蔓延）

- 不改面板 `SegmentAnchorTab` 的直写模型路径（无 undo 是既有现状，单列任务）。
- 不做曲率梳（curvature comb）可视化——那是更大的"特别好"级特性，另立设计。
- 不做锚点外推（percent 超出 [0,1]）的交互限制——限幅只兜形状底线。
- 不改 `hobbyVelocity` 公式与 C2 求解路径。
