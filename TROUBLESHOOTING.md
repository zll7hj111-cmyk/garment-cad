# TROUBLESHOOTING —— 踩坑经验库

按需查阅（不注入会话）：修改 bug 前先 grep 本文件相关关键词；修复了本清单之外的 bug 后，把根因 + 修复追加到对应主题分组。

## 0. 快捷键登记表（改快捷键前必查，改完必更新）

### 菜单/QAction 层（src/app/MainWindow.cpp，主窗口级，画布聚焦时也拦截）
| 键 | 功能 | 位置 |
|----|------|------|
| Ctrl+N/O/S / Ctrl+Shift+S / Alt+F4 | 新建/打开/保存/另存/退出 | 160-184 |
| Ctrl+Z / Ctrl+Y | 撤销/重做 | 190/194 |
| V | 选择工具 | 205 |
| L | 智能笔工具 | 212 |
| C | 曲线编辑工具 | 219 |
| R | 旋转工具 | 226 |
| B | 打断工具 | 233 |
| I | 交点工具 | 240 |
| A | 角度测量工具 | 254 |
| H | 切换辅助层 | 263 |
| Ctrl+D | 切换主题 | 276 |
| ~~M~~ | ~~测量工具~~ —— **已移除（2026-08），M 让给画布长按显示长度** | 原 247 |

### 画布长按（src/canvas/CanvasView.cpp:228-263，在工具分发前拦截）
| 键 | 功能 |
|----|------|
| N | 按住显示所有名称（线段名/点名称/曲线名），松开快照恢复 |
| M | 按住显示所有长度（线段/曲线弧长），松开快照恢复 |

### 工具内部按键
| 键 | 功能 | 位置 |
|----|------|------|
| W | 选择工具：单选/多选切换（Tab 禁用） | ToolSelect 等 |
| G | 选择工具：分组 | ToolSelect.cpp:572 |
| X | 旋转工具：旋转模式切换 | ToolRotate.cpp:253 |
| Escape / Delete / Backspace | 取消/删除 | 各工具 |
| Shift | 修饰键（多选/正交等） | 各工具 |

> 注意：QAction 快捷键（WindowShortcut）在 ShortcutOverride 阶段先于 widget keyPressEvent 生效——**画布内工具按键如果与 QAction 快捷键同名，QAction 先抢**（本次 L 冲突教训：MainWindow.cpp:212 智能笔 L 曾吞掉画布长按 L）。新增画布按键前先 grep `setShortcut` 确认 QAction 层无同名键；新增 QAction 快捷键前先查本表。

## 1. 构建与工具链

- **clangd 索引陈旧**：新增/删除源文件或改 CMakeLists.txt 后不重跑编译数据库刷新，clangd 会报大量"不可能的错误"（Expected unqualified-id、构造函数不匹配、No member named xxx、QWidget is not a direct base...）——构建其实通过，属索引噪音，勿误判回归。刷新命令见 AGENTS.md「构建命令」。
- **clangd 索引与真实构建源码不同步**：build/clangd/_deps 的 ElaWidgetTools 是旧副本时，clangd 会对项目代码报 Ela 头不匹配（如 ElaText 构造函数 not viable）。刷新时必须带 `-DFETCHCONTENT_SOURCE_DIR_ELAWIDGETOOLS=build/out/_deps/elawidgettools-src`（patch 版），保证索引与真实构建一致。
- **PowerShell 5.1 嵌套引号**：`cmd /c ""...&&..."` 报 `'C:\Program' 不是内部或外部命令`——改走临时 .bat（先 call vcvars64.bat 再 cmake）。
- **LNK2005 stale object**：头文件大改后增量构建报 already defined → touch 源文件强制重编译。
- **陈旧 AUTOMOC**：MSVC 出现"不可能的错误"（单文件 cl 编译过、磁盘内容正确但 MSBuild 报 C2653/C2027）→ 删该目标 `.dir` 与 `*_autogen` 目录重建。预防：IDE 打开状态下不要用脚本交替改写同一文件。
- **新增 .cpp 漏加 target 源列表**：CMake 无自动扫描，LNK2019 且找不到原因时检查所有引用该文件的 target（AGENTS.md 验证命令节）。
- **变量面板性能**：折叠组点击延迟 = O(N²) 布局冻结 + 双击判定 timer，已根治；测量页提取踩过 AUTOMOC 陷阱。

## 2. ElaWidgetTools（第三方库，PATCH_COMMAND 维护）

- **库被 patch 过**：`third_party/elawidgettools_qt69_patch.cmake` 共 3 个 Part（均幂等）——改库行为一律走该脚本，不要直接改 build 产物或绕开 ElaMsgBox.h 里的依赖假设。
  - Part 1：QChar 单 token 包装 char16_t（Qt 6.9+ 编译兼容）。
  - Part 2：ElaContentDialog 三按钮信号改同步 emit（修 use-after-free）；`setMiddleButtonText` 空文本自动隐藏按钮（消除默认英文 "minimum"）。
  - Part 3：ElaMenu 移除 400ms 滑入动画。
- **弹窗「是/取消」闪退**（已修复，2026-08）：根因 = ElaContentDialog 按钮回调先同步 accept/reject（exec 返回 + 调用方 delete dlg）再 `QTimer::singleShot(0)` 延迟 emit → 信号落在已释放 this 上（UAF）。中间按钮安全（无关闭调用）。修复 = 同步 emit。`src/ui/ElaMsgBox.h` 依赖此语义。
- **菜单"悬停看不见文字"**（已修复，2026-08）：根因 = ElaMenu showEvent 抓静态图 + 400ms 滑入动画，动画期间 paintEvent 画滑动的抓图（实测 20ms 帧内容 0 像素、100ms 才 55%、250ms 后完整），且 hover 高亮被动画拦截。修复 = Part 3 移除动画。实证方法：临时探针程序（grab 各时间点帧统计非背景像素数）见 `C:\Users\Administrator\AppData\Local\Temp\opencode\menu_probe\`。
- **重建流程**（github.com:443 不可达，无代理）：删 `build/out/_deps/elawidgettools-{build,subbuild}` → configure 加 `-DFETCHCONTENT_SOURCE_DIR_ELAWIDGETOOLS=E:\garment-cad\build\out\_deps\elawidgettools-src`（SOURCE_DIR 模式下 PATCH_COMMAND 仍执行，幂等可重复跑）。
- **静态库 + dllimport**：库以 `add_definitions(-DELAWIDGETTOOLS_LIBRARY)` 目录级编译（dllexport），静态库中 dllexport 不生成 `__imp_` 别名；主程序以 dllimport 语义链接产生 LNK4217 警告属正常；**probe 程序 LNK2019 缺 __imp_ 符号时，编译加 `-DELAWIDGETTOOLS_LIBRARY` 即可**。
- **patch 脚本自测假阴性**：patchtest 目录里的 patch.cmake 副本若从 third_party 复制后源文件再被改，测的是旧脚本——重测前必须重新复制；`cmake -P` 的 file(GLOB) 相对当前工作目录，workdir 必须设对，否则 glob 匹配不到、静默不替换。

## 3. Qt / UI 渲染

- **QTreeWidget stylesheet 需 ::indicator**：连接图标/勾选框不渲染时检查；列宽 0 需容纳分支箭头。
- **LayerPanel refresh 必须 QueuedConnection**（否则 use-after-free）。
- **每帧同步槽禁止 setStyleSheet**（re-polish 数毫秒级），只在离散状态翻转时调用；setText 做同值短路；批量操作禁用布局避免 O(N²)。
- **Debug 构建性能波动 30%+**：断言用宽松边界（如 ≤2.0x），勿用严格排序。
- **`QGraphicsScene::render` 裁剪到 item boundingRect**：画在 boundingRect 外的 label（如 `mid + (4,-4)`）会被剪掉，像素级渲染断言误报「label 没画」；必须用 QGraphicsView::grab()（真实 view 路径不裁剪）做画布像素验证（tests/test_hold_show.cpp 的教训）。
- **cache 里的预格式化文本与开关绑定**：BlockItem rebuildCache 的 lengthText 曾只在 showLength=true 时格式化 → hold-to-show 强制显示（L 键）拿不到文本；需要强制显示的内容必须无条件预格式化（BlockItem.cpp 直线/曲线两处）。
- **AngleHud 输入文字灰得看不清（2026-08 修复）**：根因 = setValid() 的 `qobject_cast<QGraphicsView*>(parentWidget())` 必然失败——AngleHud 的 parent 就是 viewport（纯 QWidget），必须 `parentWidget()->parentWidget()` 才到 CanvasView；cast 失败静默走 fallback 白底浅灰字（(255,255,255,245)/(232,234,237)），亮暗主题都看不清。同文件构造函数里用的是 viewport->parentWidget() 而 setValid 里用的是 parentWidget()——两处 cast 不一致的教训：HUD/悬浮控件取主题一律从构造时解析一次 CanvasScene 存成员。

## 4. 数据 / 引擎

- **addAttachment 拒绝跨层**（isAuxBlock 不一致）：测试构造桥 pin 前必须先设工作层。
- **新增 undo 命令**：记得 geometryEpoch 递增与快照字段完整性；删除宿主时关联变量自动删除、引用副本固化回数值。
- **角度/弧长闭合基准（2026-08 定稿 v3：恒等映射 + 连接显示带符号折角）**：followerAngle 0° = 两线**折叠重叠**、90° = 垂直、180° = 沿 leader 出口直行延续（用户拍板"以闭合为基准角度"，角度/弧长同一基准）。Resolver 权威公式：`newRotation = refWorld + π − angleRad`（Resolver.cpp:547-574；角度模式 angleRad = followerAngle·π/180，弧长模式 = arcMm/radius，radius≤1e-9 时 0）。**弧长↔角度恒等映射（2026-08 拍板）：弧长角 = 线夹角本身，任何 fmod(180−x,360) 反转都删净**（v1 的"显示反转 6 处"规则作废）。换算/显示同步点（改一处必须全改，漏一处 = 静默反向）：①ConnectGesture.cpp onAngleModeChanged 双向恒等；②ToolRotate.cpp applyAngleDeg/currentAngleDeg/onHudModeChanged 弧长分支恒等；③SegmentConnectionCard.cpp refreshCard 弧长分支 + onModeToggle 双向恒等；④SegmentAuxTab.cpp makeCard 恒等；⑤LineFactory.cpp:188-191/314（创建/桥接世界角→α 存储 = fmod(180−(世界角−refWorld),360)，与 Resolver 自洽，**保持反转**）⑥BreakCommands.cpp:964（打断 back = 180 直行，保持）。**显示方向（2026-08 v3 拍板）**：连接线显示 = **带符号折角**（signedFoldDeg(α) ∈ [−180,+180]，折叠 0 / 开平 ±180 / 垂直 ±90，符号 = 折向；角度、弧长（= 折角·πr/180）通用）；自由线显示 = 世界角+锚心偏移 [0,360) 逆时针为正（AutoCAD 惯例，水平基线 0° = 朝右）；复制显示 = 相对角 [0,360) 逆时针为正（0 = 与原线重叠）；智能笔 HUD 附着 = 带符号折角、自由 = 世界角。辅助函数 signedFoldDeg/alphaFromSignedFold 在 ToolRotate.cpp / SegmentConnectionCard.cpp / ConnectGesture.cpp 各自匿名命名空间。**连接线输入规则（v3 拍板）**：输入 ±180 = 带符号折角（符号含折向），>180 视为原始 α；HUD/卡片/智能笔的输入出口必须过 alphaFromSignedFold 再写存储；模式切换换算用**存储 α**（负数折角 180 输入会静默翻转，onHudModeChanged 已用 alphaFromSignedFold 包裹）。**拖动方向（v3 拍板）**：connected 线跟光标（target = 起始 α − 光标增量，WYSIWYG 表盘式：逆时针拖折角增大、过开平回落）；free/copy target = m_dragAngle0 + 光标增量（逆时针为正）。updateRotation **必须先判 copy 再判 connected**：复制中 connected 原线仍挂接，漏判会走连接线 α−δ 分支导致符号错（2026-08 回归一次：lockedFollowerRotateCopyWorks 副本角 45° 变成 −45°）。**旋转工具黄弧（updateGizmo）画真实世界方向**：connected = refWorld + π − α、free = 显示角、copy = **原线世界方向基准 + 内弧归一化**（refRad = connected ? refWorld+π−α : 原线首段方向+锚心偏移；span 归一化到 (−π,π]，弧永远 ≤180° 内弧，画在原线与克隆线之间 —— 旧代码从水平 0° 线性扫到克隆世界角，整圈 + 外弧到 180°，用户报告 HUD 整圈/外弧）。**复制相对角基准（2026-08 修复）**：relToWorldRad = `m_attachExitRad + (baseOffsetDeg + relDeg)·π/180`（世界角 = 挂接点出口方向 + 基准偏移 + 相对角，与 Resolver 恒等），旧基准 `m_copyRefWorldRad − (anchorIsEnd?0:π)` 使自由线副本方向差 180°、连接线差 α —— 黄弧端点、终点瞄准吸附全部偏离；begin/convert 里曾经的 refLineDeg 翻转（180−currentAngleDeg）与 m_copyRefWorldRad 赋值已整体删除。终点瞄准吸附（ToolRotate.cpp endpointAtAngle/checkEndpointAimSnap）：connected 必须用闭合基准 `世界方向 = refWorld + π − α`（旧代码 refWorld+α 只在 90° 碰巧正确，且比较线方向导致永吸附失败），回算显示角 = signedFoldDeg((refWorld+π−dirToP)·180/π)。卡片世界角提示 = fmod(refWorldDeg + 180 − constDeg, 360)（角度分支旧代码差 180°，弧长分支曾靠双重错误抵消；提示用存储 α 直接算，无需折向信息）。backSolveFollowerAngle（FollowerAngle.h）存储域不变。历史教训：本约定 2026-08 被反转过四次（0=0° → 0=180° → 0=0° → 0=180°）且 v1 曾把显示 180− 误当恒等，HUD 换算与 Resolver 起点基准必须一致，改前先问用户。测试：test_resolver 三弧长测试（arcLengthZeroMeansFoldBack0 / arcLengthHalfCircleMeansStraight180 / arcLengthQuarterTurnMatchesAngle90）；test_rotate_copy 弧长 3 圈 ≡ 0° 折叠（HUD 显示 0，再输入 270 → 存储 α=270 显示 −90）与自由线显示 = 45°（逆时针）。
- **旋转复制锚心语义（方案 B，2026-08 用户拍板）**：副本相对角 0° 恒 = 与原线**重叠**（AutoCAD 风格），HUD 显示"旋转角度" = 绕锚心角，起点/终点锚心一致（2026-08 修复：旧实现"锚心偏移 0/180°"只对水平自由线碰巧正确，斜线/连接线差 180°−α —— 用户报告"复制以 180° 创建"）。实现：RotateCopyGesture 存 `m_baseOffsetDeg = 原线世界朝向 − 挂接点出口世界方向`（原线世界朝向 = ToolRotate::originalWorldRotDeg()：连接线 = refWorld+π−α、自由线 = 首段世界方向+终点锚心 180；挂接点出口 = `rotation + exitDirectionAtPoint(锚心, leaderSegment)` = 克隆 Resolver 的 refWorld，存 `m_attachExitRad`）+ 相对角；**存储翻转 followerAngle = 180 − (baseOffset + relDeg)**，commit 同翻；显示层 2026-08 v3 起逆时针为正（显示 = 相对角本身，无镜像）：currentRelativeAngle = fmod(180 − baseOffset − followerAngle, 360)，convert/applyAngle 直接换算，不再 fmod(360−deg,360)；relToWorldRad/worldRadToRel 公式基准 = **m_attachExitRad**（2026-08 修复，旧 m_copyRefWorldRad−π 基准已废，begin 时 refLineDeg 翻转已删；begin 用 live 姿态、convert 用 m_baseTf 姿态，两处出口方向分别计算）。几何推论：起点锚心拖 +90° → 副本绝对 +90°（原线朝向 + 相对角）。测试：test_rotate_copy 六处复制角度断言按此更新（ctrlDrag 90 / diagonal 45→135 / consecutive 90+180 / dropsAim 135 / idleKeepsAim 90 / endAnchor 135）。
- **已连接线段禁止切换锚心（2026-08 用户拍板）**：任一端点有 attachment 时 X 键/点击端点切换一律拒绝（ToolRotate.cpp toggleAnchor + mousePress 锚心点击分支），保持 Connected 模式 = 编辑跟随角，跟随绝不断开；要换锚心必须先断开连接（旋转 = 放弃跟随的语义只对自由线开放）。测试：connectedLineXAnchorSwitchBlocked。
- **弧长/角度即时切换三坑（2026-08）**：①切换后必须 `refreshCard()+populateAngleField()` 全量刷新，只刷输入框 → caption/∠⌒按钮/世界角提示不更新（用户报告"切换到弧长没有任何文字变化"）；②切换前先把未应用输入 applyAngle() 落盘、非纯数值输入拒绝切换，否则公式信息被清成数值；③AngleHud/ConnectGesture/ToolRotate 三处模式换算必须与 Resolver 恒等映射一致（漏一处 = 静默反向）。
- 约束类型分派点登记表 / 删除影响报告计数等规范见 AGENTS.md「开发规范」。

## 5. 测试

- **test_canvas_perf 的 singleCurveFrame 在 Debug 下段错误为存量问题**（与换肤类改动无关，疑似环境/软渲染），用基线对照法排查，勿误判回归。
- **ElaContentDialog 探针构造需要非空 parent**（源码 `_maskWidget->setFixedSize(parent->size())` 解引用 parent，nullptr 崩溃）；探针必须 `ElaApplication::getInstance()->init()`（ElaAwesome 字体 + 阴影 helpers）。
- **cmd 直跑 exe 的 `& echo %ERRORLEVEL%` 对 GUI 子系统程序不可信**（cmd 不等待，读到残留值）；PowerShell 用 `Start-Process -Wait -PassThru` 拿真实退出码；QTest 详情用 `-o <file>` 落盘（stdout/stderr 重定向经常抓不到 QTest 输出）。
- **1.gcad（P612 回归文档）位于 build/out/Debug/，易被用户保存操作覆盖**：p612 测试对「文件存在但内容不符」降级为 QSKIP（缺 P489/P560/P612 点即跳过），勿当回归失败。
