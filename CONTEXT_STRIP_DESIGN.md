# 上下文属性条 CONTEXT_STRIP_DESIGN.md（一期+二期+三期，用户拍板 2026-08-30）

> 状态：**一期（2026-12）+ 二期（2026-12）+ 三期（2026-12）全部完成**。本文档是一期~三期的权威设计记录。
> 起因：用户要求删掉旋转工具的浮动 HUD，但**不是简单复用底部条**——而是把底部编辑带
> 升级成"全能状态栏"（上下文属性条），复用到选择 / 智能笔 / 旋转三个工具。
> 二期（2026-12）把连接手势的角度输入并进条带，`AngleHud` 整体退场（tools 层再无 QWidget）。
> 三期（2026-12）：测量/角度测量/交点/打断 四工具只读悬停接入完成；补充信息区按用户拍板
> **维持现状不扩展**（§2.3 的"坐标/图层/延长量/张力留给属性面板"终局生效）。

---

## 1. 要解决的问题

底部原来有两条互斥显示的 bar，各自只服务一个场景：

| 旧组件 | 服务场景 | 问题 |
|---|---|---|
| `SegmentEditBar` | 智能笔创建后编辑 / 选择工具选中线段 | 只有"创建后"和"选中后"两个入口，其余时候空着 |
| `SmartPenPreInputBar` | 智能笔预输入（值被下一条线消费后清空） | 一次性消费语义，用户评价"不好用" |
| `AngleHud`（浮动） | 旋转角度 / 连接手势角度 | 第三套输入实现，焦点与画布博弈、需随视图重定位 |

三套并存 = 同一件事（改一个角度）有三处入口、三种交互、三种 undo 路径。

## 2. 目标形态

**一条常驻条带 + 一个焦点概念**：条带永远显示"当前关注的线段"，焦点由工具上报。

### 2.1 焦点三态

```
Empty ──悬停线段──> Hover ──点击选中──> Pinned
  ↑                  │                    │
  └──移出────────────┘      Esc/点空白/删除─┘
```

| 态 | 视觉 | 字段 | 进入 |
|---|---|---|---|
| Empty | 条带隐藏 | — | 工具切换 / 焦点线段被删 |
| Hover | 虚线描边 + 次要底色 | **只读**（`setReadOnly(true)`） | 光标悬停线段 |
| Pinned | accent 实线描边 | 可编辑 | 点击选中 / 创建完成 |

**优先级：Pinned > Hover。** 锁定后鼠标划过别的线**不抢显示**，否则点中一条准备改角度、
手一抖内容就被覆盖。智能笔**从不 Pinned**（点击 = 画线），永远由 Hover 驱动 —— 正合
「智能笔就是悬停显示切换就好了」。

### 2.2 上报通道（依赖倒置，不破分层）

`ToolHost`（tools 层，`src/tools/Tool.h`）新增**两个非虚方法**——与 `setHintOverride`
同款，无头单测桩不实现也能编译：

```cpp
/// 悬停候选 (null = 移出). 节流由条带负责, 工具可直接每帧上报.
virtual void setHoverTarget(const QUuid& blockId, const QUuid& segmentId) {}
/// 锁定焦点 (null = 解除锁定).
virtual void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId) {}
```

`ToolManager` 实现并转发，`MainWindow` 接住后驱动 `ContextStrip`。tools 层不认识 app 层。

### 2.3 字段集（一期）

```
[L12] 名称[____] 长度[__] cm  角度[__] [°|⌒]  换向  P3→P1  跟随 L5        Esc 解除锁定
```

| 控件 | 宽度×高 | 行为 |
|---|---|---|
| 编号徽章 | 自适应 ×35 | `Serial::tag(seg->serial)`，等宽 |
| 名称 | 150×35 | 立即应用（无 debounce） |
| 长度 | 110×35 | 200ms debounce + Enter/失焦立即应用 |
| 角度 | 100×35 | 同上；桥线段只读 |
| 单位切换 | 2×38×35 分段 | ° ↔ ⌒（写 `attachment.rotationMode`）；自由线无 attachment 时禁用 |
| 换向 | 58×35 | `ReverseSegmentCommand`，用 `canReverse()` 预判置灰 + 中文原因 tooltip；**旋转会话内转义为「切换锚心」**（2026-12，转发给 ToolRotate，不 push 命令） |
| 基准读数 | 110×35 只读 | `P# → P#`（起点→终点，显示真实串号 tag，2026-12 起与属性对话框同口径，换向后翻转；旋转会话内锚心端在前） |
| 状态徽标 | 自适应 | 跟随 L# / 自由 / 曲线 / 桥线 —— 一眼看出这条线受谁驱动 |
| 右端提示 | 自适应 | Esc 解除锁定 · Enter 确认 |

**补充信息刻意只加徽标 + 基准读数**——坐标、图层、延长量、张力留给右侧属性面板。
条带一旦超过 7 个控件，窄窗口就要折叠，"全能"会变成"全挤"。

## 3. 各工具映射（一期）

| 工具 | 悬停上报 | 点击 | 锁定 | 条带行为 |
|---|---|---|---|---|
| 选择 | ✅ | 选中线段 | ✅ | 三项可编辑 + °/⌒ + 换向 |
| 智能笔 | ✅ | 画线（**不锁定**） | ❌ | 只读预览；画线中显示**正在画的那条线**（沿用 `showPreview` 语义）；画完自动锁定新线（保留"创建后编辑"） |
| 旋转 | ✅ | 选中 = 旋转目标 | ✅ | 角度格随拖动实时刷新；**不再弹 AngleHud**；确认门与复制态读数走状态栏 L1 |
| 测量/角度测量/交点/打断 | ✅（三期，2026-12） | 各自功能 | ❌ | 只读（统一"扫过即看"） |

## 4. 四个拍板（用户 2026-08-30 确认）

1. **旋转复制（Ctrl+拖动）的相对角**：复制态条带角度**置灰不显示**（相对角与跟随角/绝对角
   是三套语义，塞同一个框必然出错）；读数走状态栏 L1；松手后副本即普通线段，条带恢复正常。
2. **悬停节流 80ms**：只取窗口内最后一个候选，且要求 hover 稳定一帧，避免扫过一堆线时条带狂闪。
3. **补充信息**先加：状态徽标 + 角度基准读数。其余留给属性面板。
4. **智能笔画线中**：显示**正在画的那条线**的实时长度/角度，不是悬停到的线（注意力在自己手上）。

**实现铁律**：任一输入框获得焦点时，**忽略 hover 更新**，且 `refreshFields()` 跳过聚焦字段。
否则正在敲角度、鼠标微动一下内容就被覆盖。

## 5. 复用清单（不重复造轮子）

| 能力 | 来源 |
|---|---|
| 名称/长度/角度应用 + 200ms debounce + Enter/Tab 流转 | 迁自 `SegmentEditBar.cpp` |
| 一步 undo | `cad::cmd::SegmentEditBarCommand`（`BlockCommands.h`），State 结构原样复用 |
| 换向 + 资格检查 | `cad::cmd::ReverseSegmentCommand::canReverse()`（返回中文原因） |
| 拖动实时刷新 | `ParamDocument::resolved` 信号 —— `resolveForDrag` 末尾照样 emit（`ParamDocumentResolver.cpp:271 → :667`），**不需要额外转发，也不必每帧刷状态栏** |
| 输入包含（吞掉工具单字母快捷键） | `eventFilter` 里 `ShortcutOverride` accept，迁自 `SegmentEditBar.cpp:424` |
| 悬停候选 | 各工具已有的 `m_hoverSnap` / `m_hoverSeg`（`SegmentSnapResult`） |

## 6. 分期

- **一期（已交付，2026-12）**：`ContextStrip` 落地（合并两条 bar、删预输入条）+ `ToolHost` 双通道 +
  三态机 + 选择/智能笔/旋转接入 + °/⌒ + 换向 + 徽标 + 基准读数。
  一期结束时**旋转工具已不再弹 HUD**（`AngleHud` 只剩 `ConnectGesture` 一个宿主）。
  旧 `SegmentEditBar` / `SmartPenPreInputBar` 类已删除（测试迁入 `test_context_strip`）。
- **二期（已交付，2026-12）**：`ConnectGesture` 的角度输入并入条带（连接角度会话），
  `AngleHud` 类已删除（src/ui/AngleHud.h/.cpp + CMakeLists 源列表），tools 层再无 QWidget。
  通道（依赖倒置四条）：①手势→条带 = `ConnectGesture` 注入 `m_beginAngleSession` 回调 →
  `Tool::reportConnectAngleSession` → `ToolHost::setConnectAngleSession`（ToolManager emit
  `connectAngleSessionChanged`）→ MainWindow → `ContextStrip::beginConnectAngleSession(blockId,
  segId, attId, initialAngle)`，**全 null = 会话结束**；②合法性 = `ToolHost::setConnectAngleValidity`
  → 条带角度框红边（`angleInvalid` 属性 + Theme QSS）；③条带→手势 = ContextStrip 四信号
  （`connectAngleTextChanged/ModeChanged/Committed/Cancelled`）→ ToolManager `forward*` →
  `Tool` 虚钩子（默认 no-op）→ ToolSelect override → ConnectGesture 的四个 public 输入口
  （onAngleTextChanged/onAngleModeChanged/commitAngle/cancelAngle）。条带是**纯输入面**：
  会话内名称/长度只读、角度可编辑，击键实时预览（resolveForDrag 种子同旧 HUD）、° / ⌒
  几何保持切换（公式驱动拒绝）、Enter/Esc 经信号回报收尾（整步 undo 宏不变）。会话内
  条带绝不 push 命令（连接在 finalize 前是飞行中附件）。详见 `TROUBLESHOOTING.md`
  「上下文属性条二期」条目。
- **三期（已交付，2026-12）**：测量（ToolMeasure）/ 角度测量（ToolAngleMeasure）/
  交点（ToolIntersection）/ 打断（ToolBreak）四个工具在各自 hover 更新处上报
  `reportHoverTarget`（悬停点所在线段经 `exitSegmentAtPoint` 解析；线身吸附直接取
  segmentId；交点工具 SelectLine 态报目标线段、SelectPoint 态报点所在线段、AimAngle/
  BorrowAim 态报空；打断工具断点优先、线身兜底；移出一律报空）。条带 Hover 态已有
  只读显示（一期），本次只是接入上报 → 四工具统一"扫过即看"。附带修一处焦点归一化：
  `hideBar()` 对 Hover 态同样清焦点（"Hover" 语义 = 条带可见的只读预览，隐藏后不得
  残留悬停焦点）。测试：test_measure +2、test_tool_intersection +1、test_break +1、
  test_context_strip +1（hoverShowsReadOnlyPreview，waitUntil 等 80ms 节流）。
- **补充信息区扩展（用户拍板 2026-12：维持现状不扩展）**：一期 §2.3 的"坐标/图层/
  延长量/张力留给右侧属性面板 + 条带超 7 控件窄窗口折叠"警告即为终局 —— 徽标
  （跟随/自由/桥线/曲线）+ 角度基准读数（P1→P2）不再加字段。三期的"补充信息区
  扩展"条目就此结案。

## 7. 验收标准

- 构建 0 error；ctest 对照既有基线 2 红（`test_serializer::bridgeAuxPointSnappableAndAttachable`、
  `test_component::dragComponentLeaderCurveFollowStable` 31.4798mm），无新增红。
- `check_layering.py` / `check_test_fixtures.py` 均 OK。
- 手工验收：选择工具点线段 → 条带锁定可编辑；智能笔扫过线段 → 只读切换、点击画线不抢焦点；
  旋转工具拖线段 → 条带角度实时变、画布上不再出现浮动输入框。
- **二期追加**：连接手势拖到目标 → 条带进入会话（跟随线段名称/长度只读 + 角度可编辑、提示
  「Enter 确认 · Esc 取消」），画布上不再出现浮动 AngleHud；击键实时预览、°/⌒ 切换、
  Enter 收尾整步 undo；Esc 保留连接回退初值；组件级连接与仅角度重挂同样进会话。
- **三期追加**：测量/角度测量/交点/打断 四工具扫过线段 → 条带只读预览（Hover 态），
  移出即收起；四个工具点击各自功能、从不锁定（§3 表格语义）。
