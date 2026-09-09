# 圆形工具设计（设计稿）

> 状态：**设计稿 + D1-D21 全部拍板（2026-12 用户追认）并已实施**。本文档是「圆形工具」唯一权威设计记录。
> 决策已登记 `DECISIONS.md`（条目「圆形工具（Bézier 拟合圆，2026-12 用户追认 D1-D21）」）；
> D5 两点直径与 D15 周长发布为用户指定本期补做项，均已完成（§21 / §22）。
> 实施记录：§14 M1（引擎三件套 + 最小 ToolCircle）、
> §15 M2（面板圆区段 + D16 消锯齿）、§16 M3（整圆求交 + 条带圆区段）、§17 D14 解除圆约束、
> §18 D8 曲线编辑置灰、§19 D7 圆段打断、§20 D9 圆段作角度基准、§21 D5 两点直径模式、
> §22 D15 周长发布为公式变量、§23 一期补充：圆专属底部条带 `CircleStripBar`（取代 §16.2 三槽复用）、
> §24 一期补充：圆绘制会话条带（§5.5 落地，画圆时条带输半径/直径）、
> §25 一期补充：圆心→接缝半径基准虚线 + 世界角标注（①），并修复可见弧被镜像到 X 轴另一侧（③）、
> §26 一期补充：基准线口径修订（接缝取解析起点、世界角含块旋转、解析弧同基准）+ 标注改挂虚线 +
> 圆心双击开面板 + 旋转枢轴吸附圆心（2026-12 用户实测反馈五项）、
> §27 一期补充：圆段旋转辅助显示（黄色基准虚线 / 黄弧 / 徽标）适配「圆心→接缝」半径方向。
> 文中行号锚点为撰写时（2026-12）源码状态，实现前以现场代码为准。

---

## 1. 背景与目标

制版实践中「圆」是高频构造件，当前工具链没有对应原语：

- **定位标记**：省尖、袋口、纽扣位的参考圆（小半径，辅助线性质）；
- **构造基准**：袖山 / 袖窿参考弧、荷叶摆与圆裙（大摆裙）的展开参考（大半径，常由公式驱动，
  如 `R = waist/(2π)`）；
- **现有缺口**：智能笔只能画直线与过锚点 Bézier 曲线（Catmull-Rom / Hobby 自动切向，
  `src/parametric/BlockCurve.cpp:113`）。徒手四锚逼近圆既不精确也不参数化——改半径要
  重摆全部锚点，公式驱动无从谈起。

**目标**：新增「圆形」工具，产物是**参数化、可公式驱动、可复算**的圆：

1. 两种绘制模式：**圆心 + 半径**（默认）、**两点直径**（W 键切换）；
2. 半径支持公式（cm 域，与全仓公式约定一致），变量变化时圆随公式呼吸且**恒为真圆拟合**；
3. 复用既有曲线全管线（捕捉 / 测量 / 命中 / 弧长），不新增段类型；渲染仅对圆段改用
   解析绘制路径（D16：`tension == 0` → `arcTo`，否则逐跨 `cubicTo`），消除放大锯齿。

非目标（二期见 §12）：三点圆、椭圆、圆弧独立工具、块内内嵌圆、圆心跨块联动。

---

## 2. 术语（全文档统一）

| 术语 | 含义 |
|---|---|
| 圆块（Circle Block） | 一个圆对应一个独立 Block；Block 原点 = 圆心 |
| 圆心点 | Block 内 `constraint=Free`、位于局部原点的点；是圆的平移 / 捕捉 / 联动锚 |
| 半径点（p0） | 弧起点：`constraint=Polar(圆心, r, a₀)`，**半径的唯一权威来源**（`distance` / `distanceFormula`）**兼基准角度权威**（`angle` / `angleFormula`） |
| 终点（p1） | 弧终点：`constraint=Polar(圆心, r, a₁)`。**整圆 = a₁ = a₀ + 360°：位置与 p0 重合，但点 ID 不同**（重合态 p1 隐藏不可选，D12） |
| 基准角度（a₀） | 圆的 0° 方向 = 接缝位置，落在 p0 的 `angle` / `angleFormula`；与线段「角度」同性质、同一条 UI 通路（D18） |
| 象限锚点 | 90°/180°/270° 处 3 个 `CurveAnchor` 曲线点；位置由 `resolveCurveAnchorPoint` 的 circleFit 分支算出（圆心 + r·(cosθ, sinθ)），**不按弦定位**（§4.1.1） |
| 拟合圆（fitted circle） | 用 4 段三次 Bézier 以 κ 因子等距拟合的真圆逼近（§4.2） |
| κ 因子 | 90° 跨的控制臂系数 `4(√2−1)/3 ≈ 0.55228475`；一般跨用 `(4/3)·tan(θ/4)`，κ 是 θ=90° 的特例（§4.2） |
| fitKind | Segment 上的新枚举字段 `enum class FitKind { None, Circle }`（**不是 bool**，见 D21）：标记该曲线段为「圆」，resolve 时切向自动重拟合。下文「circleFit 分支 / circleFit 段」是行为与分支的简称 |
| 弧（部分圆） | 起止点为**两个** `Polar` 点（a₀ / a₁）的圆弧；圆心、半径、拟合方式与整圆完全相同（§4.4） |
| 包角 | `a₁ − a₀`（度）；**本期可编辑**（写 p1 的角度，含 90/180/270/360 预设，D19）；整圆 = 360°。锚点角度 = a₀ + 包角·{0.25, 0.5, 0.75} |
| 圆度 | `Segment::tension` 在圆段上的语义 = 控制臂（控制点偏移）缩放因子 `1 + tension`；**0 = 正圆（默认）**，**−1 = 内接四边形**（控制臂归零，每跨退化成直线弦），范围 `[−1, +0.25]`（D17） |
| 外长（弧长） | 沿曲线的实际长度（= `CurveSpanEntry::arcLengthMm`；整圆即周长，半圆 = πr）；弦长 = `degToChordMm(包角, r)` 另列 |
| 接缝（seam） | 整圆首尾重合处（p0）；此点入/出切向相反，是方向类判定的特例（D9、§7） |
| 解除圆约束 | 显式命令：圆 → 普通曲线（锚点转 `Free`、清 `circleFit`），之后可自由拖控制点（D14；**实施时不动 `autoTangent`**，见 §17.2） |

---

## 3. 决策清单（全部已拍板，2026-12 用户追认；状态见末列）

| # | 决策 | 推荐 | 类型 |
|---|---|---|---|
| D1 | 圆的模型表示：Bézier 拟合圆 vs 新增真 Arc 段类型 | **拟合圆**（理由 §4.3：真 Arc 需在 8 个子系统各开支线；拟合圆径向误差 ≤0.0273%·r，1 m 半径偏差 <0.3 mm，打印不可辨） | 已实施（2026-12，核对完毕） |
| D2 | 参数化载体：圆心 Free 点 + 两端点 p0/p1 = Polar(圆心, r, a₀/a₁)（半径 + 基准角度权威）+ 3 个 CurveAnchor 象限锚，**零新增 PointConstraint** | 采纳（绕开 ParamPoint.h:37-51 的 13 处约束分派登记表；曲线点身份让全仓 19 处 `constraint == CurveAnchor` 判定零改动生效） | 已实施（2026-12，核对完毕） |
| D3 | 真圆性保持：新增 `Segment::fitKind = FitKind::Circle`（枚举，非 bool，D21）；resolve 时对该段重写锚点切向 = κ·r×径向垂向 | 采纳（§4.2） | 已实施（2026-12，核对完毕） |
| D4 | 圆段默认角色 = **辅助线**（Auxiliary，虚线）；一条命令可切内部线 / 轮廓线 | 采纳推荐（圆在制版以构造参考为主）；反对意见：纽扣圆等标记或更适合 Internal | 已实施（2026-12，核对完毕） |
| D5 | 模式集：本期 = 圆心+半径 ↔ 两点直径；三点圆二期 | 采纳 | 已实施（§21，2026-12） |
| D6 | 快捷键 = `O`（C 已被曲线编辑占用，见 TROUBLESHOOTING §0 登记表）；落地前必 grep `setShortcut` 复核 | 采纳 | 已实施（2026-12，核对完毕） |
| D7 | 打断圆段 = **切成两段真圆弧**（各保留 `circleFit` + 圆心 + 半径，包角互补），**不降级为自由曲线**（降级会让锚点跳到弦上 = 立即变形，§4.4） | 采纳（替代初稿的「降级」方案） | 已实施（§19，2026-12） |
| D8 | 曲线编辑工具（ToolCurveEdit）对 circleFit 段**置灰**（拖锚点必然破圆） | 采纳 | 已实施（2026-12，核对完毕） |
| D9 | 圆端点作为连接基准：位置附着（fromPoint pin 到圆周点）允许；**角度跟随不禁止，改为把基准方向修正为圆段切向**（整圆 = 接缝切线、弧 = 弦）。原文理由「`directionAtPoint` 无曲线分支 ⇒ 整圆弦为 0 ⇒ 垃圾基准」只对拖动连接一条路径成立：全仓 9 处 refWorld 有 8 处早已走 `exitDirectionAtPoint`（有曲线切向分支） | 修正基准方向，不禁止（实施偏离原文的「禁止」方案，理由 §20.2） | 已修正（§20，2026-12） |
| D10 | 圆块 `isClosed = false`（圆不是衣片轮廓语义；几何封闭由**两枚端点位置重合**表达）。**实测修正**：原文「`BlockResolve.cpp:38-47` 的闭合约束会把末点拉到首点、令半径归零」不成立——现 `:53-61` 只做 `lastPt->resolvedPos = firstPt->resolvedPos` 纯位置拷贝，不动 Polar 的 `distance`/`angle`；且 `Segment::isDriven` 全仓只有 `Segment.h:75` 声明 + `DocumentSerializer.cpp:201/:236` 序列化，**无求解消费方**。结论不变，真实理由：①圆不是轮廓语义；②弧块（D7 拆分产物）若 `isClosed = true`，末点每帧被拉到首点、弧退化成细条 | 采纳 | 已实施（核对完毕，2026-12） |
| D11 | 象限锚点定位：`resolveCurveAnchorPoint` 新增 circleFit 分支（圆心 + r·(cosθ, sinθ)，θ = a₀ + 包角·interpPercent） | **必须**（§4.1.1）：弦长恒为 0，不改则 3 锚全部塌到圆心、半径归零 | 已实施（2026-12，核对完毕） |
| D12 | 弧的包角权威：由两端点 `Polar.angle`/`angleFormula` 表达（可公式化）——**整圆同样有两个端点，a₁ = a₀ + 360°（位置重合、ID 不同）**，不用 `start == end` 同一 ID、不新增 `arcSweepDeg` 字段 | 采纳（替代初稿的 `arcSweepDeg` 方案，理由见 §4.1：`startPointId == endPointId` 会让 `BlockQuery.cpp:19`、`ToolCurveEditHandles.cpp:61-67`、`LineEndpointSection.cpp:548`、`BlockExtend.cpp:23-24` 等「是起点还是终点」判定同时成立；两枚重合端点则全部天然正确） | 已实施（2026-12，核对完毕） |
| D13 | 半径 / 直径 / 周长三向联动输入：任一项写入都换算成 `p0.distance`（或 `distanceFormula`）；周长 = 2πr 由程序换算，**公式域不引入 π**（表达式求值器无 π 常量，`ExpressionEvaluator.cpp:40-62`） | 采纳 | 已实施（2026-12，核对完毕） |
| D14 | 自由改形 = 显式「**解除圆约束**」命令（圆 → 普通曲线：锚点转 Free、清 circleFit、tension 行标签回「张力」）；圆模式下曲线编辑置灰（D8）。**实施偏离**：原文的「`autoTangent` 恢复」会让形状在解除瞬间跳变（违反测试 6d），故保持原值，理由 §17.2 | 采纳 | 已实施（2026-12，核对完毕） |
| D15 | 周长发布为公式变量：复用既有「发布参数」通路（`LineGeometrySection.cpp:321-326` + `LinkedVariable::fromSegment`），**零新机制**；为使发布值精确等于 2πr，`segmentBaseLength` 对 circleFit 且圆度 0 的段改返解析值 r·θ（§22.2） | 采纳 | 已实施（§22，2026-12） |
| D16 | **圆段绘制不走扁平折线**：circleFit 段的绘制路径在缓存构建时生成解析路径——**tension == 0 用 `QPainterPath::arcTo`**（正圆，误差 0）；**tension ≠ 0 用 `entry.spans` 逐跨 `cubicTo`**（精确 Bézier，含 tension=−1 的直线弦退化）；均局部坐标、每 resolve 建一次，与「每 resolve 一次」纪律不冲突；命中仍用 0.1 mm 扁平折线（误差远小于 8px 拾取带） | 采纳（**这是「锯齿感」的唯一根因与解**：`BlockCurve.cpp:167` 固定 0.1 mm 容差、`CurveMath.cpp:882-921` 递归细分、`CurveItem.h:32` 绘制与命中共用同一折线，放大到 ~300% 即见棱角，§9） | 已实施（2026-12，核对完毕） |
| D17 | **圆度可调**：复用 `Segment::tension`（`Segment.h:49-51`，double、默认 0.0、无公式字段、面板已有「张力」行 `LineGeometrySection.cpp:243-259`）作为圆段的圆度——控制臂（控制点偏移）× (1 + tension)，**0 = 正圆** | 采纳（面板对圆段该行标签改「圆度」、tooltip「0 = 正圆，−1 = 内接四边形」；**夹紧 tension ∈ [−1, +0.25]**：−1 时控制臂为 0 → 每跨退化成直线弦 → 内接四边形（a₀=45° 即轴对齐正方形，边长 r√2）；负超 −1 自交，正超 +0.25 外凸过冲） | 已实施（2026-12，核对完毕） |
| D20 | **「段数」不作为圆的参数**：圆模型恒为 4 跨三次 Bézier（每跨 90°），用户看到的「很多直线」是渲染折线（`BlockCurve.cpp:167` 固定 0.1 mm 容差 → r=100 mm 约 70 段、r=500 mm 约 157 段），D16 改解析绘制后跨数对观感无影响；**正多边形 = 独立工具**（点数随 N 变、半径语义变外接圆半径），**加工离散化 = 导出设置**（三期） | 采纳（讨论结论，见 §4.4、§13 Q16） | 已实施（2026-12，核对完毕） |
| D18 | **圆的基准角度走线段同一条 UI 通路**：a₀ 落在 p0 的 `angle`/`angleFormula`，由 `SegmentAngleCard` 的「自由线角度」分支编辑（`SegmentAngleCard.h:39/:63`：写自由端点 Polar 角，caption「角度(°)」，0~360° 逆时针为正） | 采纳（改 a₀ = 旋转接缝与全部象限锚，圆心不动；与线段体验一致） | 已实施（2026-12，核对完毕） |
| D19 | **包角本期可编辑**（不再留二期）：条带/面板「包角」输入 + 90/180/270/360 预设；写入 = p1 的 `angle`（数值）或 `angleFormula`（组合式 `"<a₀ 表达式> + <包角表达式>"`，求值器支持 `+`）；半圆 = 一次输入 180° | 采纳（包角仍非独立字段：整圆/开口弧同一套端点角度语义，无第二权威，D12） | 已实施（2026-12，核对完毕） |
| D21 | **圆的标记字段用枚举，不用 bool**：`Segment` 现已有 5 个 bool 成员（`isDriven`/`visible`/`showName`/`showLength`/`showOrthoAxis`），而 `tools/check_bool_flags.py` 的阈值是「> 5 即 FAIL（implicit state explosion）」→ 加第 6 个 bool 会让守卫红；`redline_exceptions.json` 头部不变式明写「This list may only shrink over time, never expand」，故**不给 Segment 开豁免**。改用 `enum class FitKind { None, Circle }`（非 bool，守卫不计数，且二期椭圆/圆弧可扩展为 `Ellipse` 等值） | 采纳（M1 第一步实测暴露，见 §4.2） | 已实施（2026-12，核对完毕） |

---

## 4. 核心模型：参数化拟合圆

### 4.1 实体构成（一圆 = 一 Block）

```
Block「圆N」  transform.origin = 圆心世界位置（rigid 拖动即移整个圆）
├─ ParamPoint pC   constraint = Free, freePos = (0,0)          圆心点（可捕捉/可选中）
├─ ParamPoint p0   constraint = Polar(pC, r, a₀)               弧起点（半径 + 基准角度权威）
│                  distance = r (mm)，distanceFormula = 半径公式（cm 域）
│                  angle = a₀，angleFormula = 基准角度公式
├─ ParamPoint p1   constraint = Polar(pC, r, a₀ + 包角)        弧终点（整圆 = a₀ + 360°）
│                  整圆态：位置与 p0 重合 → visible/selectable = false（只作包角权威）
├─ ParamPoint p90  constraint = CurveAnchor, hostSegmentId = 圆周段,
│                  interpPercent = 0.25                        （位置由 circleFit 分支算）
├─ ParamPoint p180 constraint = CurveAnchor, interpPercent = 0.50
├─ ParamPoint p270 constraint = CurveAnchor, interpPercent = 0.75
└─ Segment 圆周    type = Bezier, startPointId = p0, endPointId = p1,
                   passPointIds = [p90, p180, p270], fitKind = FitKind::Circle,
                   role = Auxiliary（默认，D4）
```

**为什么整圆也用两个端点（D12）**：`startPointId == endPointId` 会让全仓「这段的起点还是终点」判定
同时成立（`BlockQuery.cpp:19`、`ToolCurveEditHandles.cpp:61-67`、`LineEndpointSection.cpp:548`、
`BlockExtend.cpp:23-24`、`LeaderCandidatePicker.cpp:59`、`SmartPenEndConfirm.cpp:96`、
`PointRefEdit.cpp:30`）——同一个点既是起点又是终点，端点语义立刻歧义。两枚**位置重合但 ID 不同**
的端点则让上述全部判定天然正确，且包角永远 = `a₁ − a₀`，**打开 / 收拢圆都是纯角度编辑**（无增删点、
无跨数重构）。整圆态 p1 隐藏不可选（`visible = selectable = false`；曲线构建只看 `resolved`，不看这两个
标志，`BlockCurve.cpp:60-117`），视觉上仍是「一个圆、一个接缝手柄」。

**开口弧（包角 < 360°，含打断后）**：把 p1 的角度改成 `a₀ + 包角` 即得，其余完全不变：

```
├─ ParamPoint p0   constraint = Polar(pC, r, a₀)              弧起点（半径权威）
├─ ParamPoint p1   constraint = Polar(pC, r, a₁)              弧终点（a₁ = a₀ + 包角，可选可见）
└─ Segment 弧      type = Bezier, startPointId = p0, endPointId = p1,
                   passPointIds = 3 个 CurveAnchor，
                   角度 = a₀ + 包角·{0.25, 0.5, 0.75}（按包角四等分，见 §4.4）,
                   fitKind = FitKind::Circle
```

要点：

- **半径唯一权威 = p0 的 Polar `distance` / `distanceFormula`**（存储 mm / 公式 cm 域）。
  改半径 = 一条命令改 p0 一个点（§6），**不存在多点同步问题**。象限锚不存半径，
  它们的位置由圆心与 p0 的**实解算位置**推出：`r = |p0.resolvedPos − pC.resolvedPos|`。
- **象限锚必须是 `CurveAnchor`（不是 Polar）**：曲线点身份让全仓 19 处
  `constraint == CurveAnchor` 判定（选中排除 / 曲线编辑 / 打断 / 序列化类型串 /
  渲染标记 / 切线查询 / 复制）零改动生效，`ParamPoint.h:37-51` 登记表不动。
- **锚点不可交互**：3 个象限锚 `selectable = false`（先例：`ParamPoint.h:154`
  「false for anchor points (invisible pivot)」）；圆心与 p0 正常可选。
- **圆心 = Block 局部原点**：`pC.freePos = (0,0)`，于是 `pC.resolvedPos` 恒为局部原点；
  整圆平移走刚体 transform（Block 刚体模型既定原则，零新机制）。
- `showLength` 语义 = 周长（沿曲线弧长，既有 `CurveSpanEntry::arcLengthMm` 直接读）。

### 4.1.1 致命约束：曲线点按「弦」定位（本设计的头号实现陷阱）

`resolveCurveAnchorPoint`（`src/parametric/BlockResolve.cpp:404-441`）把曲线点定位在
**宿主段起终点连线（弦）** 上：`interpPercent` 沿弦、`interpOffsetDist` 垂直偏移；
且 `:418-422` 明确「弦长 < kGeomEps → 锚点坐在起点上」。

- 圆段两端点**位置重合** ⇒ **弦长恒为 0** ⇒ 3 个象限锚全部塌到圆心，
  半径归零、曲线退化。**照抄既有曲线点语义必然失败**。
- 因此必须加 circleFit 分支（D11），**放在弦长判断之前**：

```
if (hostSeg->fitKind == FitKind::Circle) {
    // 起点 p0 必为 Polar(圆心, r, a₀)；终点 p1 必为 Polar(圆心, r, a₁)
    const ParamPoint* center = findPoint(sp->refPointId);
    if (!center || !center->resolved) return false;
    const double r = (sp->resolvedPos - center->resolvedPos).length();
    // 包角恒 = a₁ − a₀；整圆时 a₁ = a₀ + 360°（D12），无闭合特例
    const double a0    = sp->angle;
    const double sweep = ep->angle - a0;
    const double th = degToRad(a0 + sweep * pt.interpPercent);
    pt.resolvedPos = center->resolvedPos + r * (cos th, sin th);
    pt.resolved = true;
    return true;
}
```

- **整圆与开口弧同一分支，无特例**：`sweep = a₁ − a₀`（整圆 = 360°，半圆 = 180°）。
  `interpPercent` 语义统一为「整段转角的分数」——整圆 0.25 → 90°，半圆弧 0.25 → 45°。
  分支对「两端点位置重合」与「锚点不该在弦上」两种情形都成立，故必须放在弦长判断之前。

- 半径从 p0 的**实解算位置**取（不用 `distance` 字段，公式驱动时天然跟随）。
- `interpPercent` 在 circleFit 段上的语义 = 整圈转角分数（0.25 = 90°）。曲线编辑对
  circleFit 段置灰（D8），不会与「沿弦百分比」的拖拽写回冲突；但
  `src/ui/SegmentAnchorTab.cpp:362` 若按百分比显示该字段，圆段需改为显示角度
  （实现时核对，列入 §10）。
- 依赖前提：circleFit 段的 p0 **必须**是 `Polar` 且 `refPointId = 圆心`（p1 同理，且角度 ≥ p0）。
  若被改成其他约束（面板改约束 / 复制降级），分支返回 false → 锚点未解算 → 曲线不构建。
  实现时在圆段的面板里锁死 p0/p1 的约束切换，并在 §11 加降级用例。
- **禁止对 p1.angle 做 [0,360) 归一化**：`resolvePolarPoint`（`BlockResolve.cpp:224-248`）直接
  用 `pt.angle` 参与 `degToRad()`，不做取模，所以 `a₁ = a₀ + 360°` 可靠；一旦某条面板/命令把
  角度取模成 0，`sweep = a₁ − a₀` 就变成 0，整圆立刻退化成零包角（3 个锚塌到 p0）。实现时
  在写角度的命令里显式保留原值，并在 §11 加「360° 往返不丢」用例。

### 4.2 circleFit 切向重拟合（真圆性保持的唯一新引擎逻辑）

**问题**：Bézier 锚点切向是普通 Vec2 字段（ParamPoint.h:130-133），半径被公式驱动变化时
切向量幅不跟随 → 拟合圆塌缩成花瓣。

**方案**：`Segment` 新增序列化字段 `FitKind fitKind = FitKind::None`（枚举而非 bool，**D21**：
`Segment` 已有 5 个 bool，第 6 个会让 `check_bool_flags` 红，且豁免表只减不增；additive key、
老档缺省 `None`，**零迁移、不 bump kFormatVersion**——先例 = `Segment::annotation`，Segment.h:33-35）。
`Block::resolve()` 在**全部点解算完成之后、`rebuildCurveCache()` 之前**，对每个
`fitKind == FitKind::Circle` 的段执行 O(锚点数) 的重拟合：

```
对相邻锚点 (i, i+1)（半径向量角度 θ_i, θ_{i+1}）：
    θ   = |θ_{i+1} − θ_i|                  // 该跨圆心角（整圆 = 90°，弧 = 包角/4）
    κ_i = (4/3)·tan(θ/4)·(1 + tension)      // 控制点偏移系数；θ=90°、tension=0 → 0.55228475（即 κ）
    tangentOut_i    = 3·κ_i·R_i · perp(dir(d_i))        // perp = 与绕行方向一致的 90° 旋转
    tangentIn_{i+1} = 3·κ_i·R_{i+1} · perp(dir(d_{i+1}))
    // 注意 ×3：ParamPoint 存的是 Hermite 切向量，buildBezierSpans 用 ctrl = P ± T/3
    //（CurveMath.cpp:479-481，注释见 :227）。写成 κ·R 会让控制臂只剩 1/3，圆塌成圆角菱形。
    // 整圆：末跨 p270 → p1（p1 与 p0 位置重合），接缝处出/入切向共线反向
```

- **`tension` 在圆段上就是「圆度」（D17）**：`1 + tension` 缩放控制臂——0 = 正圆；
  正 = 外凸（圆润的方）；负 = 内收（圆润的菱形）。锚点位置**不随圆度改变**（仍落在圆周上），
  只有控制臂变，因此半径 / 弧长口径不变（弧长按真实 spans 积分，会随圆度变化，属预期）。
- **控制臂必须按该跨的实际夹角取，不能固定 κ**：弧的每跨夹角 = 包角/4——半圆弧每跨 45°，
  正确臂长 = `(4/3)·tan(11.25°)·r ≈ 0.2652·r`；若沿用 κ = 0.5523·r 会外凸约 **2.08 倍**，
  半圆/小弧立刻变形。κ 只是 θ=90° 的特例，`(4/3)tan(θ/4)` 收口为 `Angle.h` 一个常量函数。
- 绕行方向（顺 / 逆时针）由创建时锚点编号顺序唯一确定；refit 沿用同号 perp，落地时以画布
  Y-flip 后的实际环绕方向定符号（一处常量，收口在 `BlockCurve.cpp`）。
- 重拟合**写回 ParamPoint.tangentIn/tangentOut**：`spansForSegment` 的 memo 指纹
  （`curveAnchorFingerprint`，BlockCurve.cpp:28-44）已把切向纳入 key，形状变化天然触发
  重建，无需另起缓存机制。
- **必须同时置 `autoTangent = false`（`tangentLocked = true`）**：`buildBezierSpans` 的
  逐锚点 `autoTan[i]` 标志（`BlockCurve.cpp:73/81/84` → `:113-114`）为 true 时走 Hobby
  自动切向并**覆盖**写回的切向；`autoTangent` 也进 memo 指纹，改了自然触发重建。
  漏置 = 圆在半径变化时漂成花瓣（§11 有专测）。
- **epoch 纪律**：半径公式驱动的变化本就会在 resolve 中 bump geometryEpoch；refit 自身
  加「新切向与旧值差 > `kGeomEps` → `touchGeometry()`」守卫（架构原则：唯一 bump 入口）。
- 拟合精度：4 段 κ 拟合的径向最大误差 ≈ 0.0273%·r；r = 500 mm 时 < 0.14 mm。

### 4.3 方案对比（为何不做真 Arc，存档备查）

| | Bézier 拟合圆（D1 推荐） | 真 Arc 段类型 |
|---|---|---|
| 段类型 | 复用 `SegmentType::Bezier` | `SegmentType::Arc` 目前只是**保留枚举**（全仓仅序列化表有一行映射，DocumentSerializer.cpp:94，无任何消费方），等于从零实现 |
| Resolver / 曲线缓存 | 零新增求解路径（+1 个 refit 钩子 + `resolveCurveAnchorPoint` 内 1 个 circleFit 分支，§4.1.1） | 新求解路径 + CurveSpanEntry 旁路或重造 |
| 渲染（BlockItem） | 普通曲线仍走 flatLocal；**圆段加一个解析绘制分支**（D16：`arcTo` / `cubicTo`），改动局限在 `BlockGeometryCache` + `CurveItem` | 新绘制分支（arcTo）+ 缓存双轨 |
| 捕捉 / 命中 | SnapEngine 投影、HitTester 曲线通路自动生效 | 各需新写的解析投影与命中分支 |
| 测量 / 辅助点 / 打断 | 弧长表、cumArcLength、aux 分派全复用 | 全部要定义圆版语义 |
| 径向精度 | ≤0.0273%·r（r=1m 差 <0.3mm） | 数学精确 |
| 改动面估算 | ~3 个模块、2 个新文件 | 8+ 子系统（参数化/画布/工具/序列化/测量/打断/附着/UI） |

**结论**：拟合圆以 <1/5 的改动面拿到打印级下不可分辨的精度；若未来出现 DXF 导出或
激光裁床直出等「必须真圆」的需求，再立真 Arc 原语不迟（§12 列为二期备选）。

**被否决的第三条路**：直接给 4 锚点 `autoTangent = true` 走 Hobby 自动切向——过方四点
的 Hobby 样条不是圆，半径公式驱动时形状呼吸漂移，否决。

### 4.4 部分圆：弧、截断、打断不变形

制版里大量需要的是**圆弧**（袖窿弧、领口弧、圆规弧），所以「只要一部分」是一等公民，
而不是事后补丁。

**弧的表示（零新增点字段，D12）**：包角不单独存字段，而是落在两端点身上——
`p0 = Polar(pC, r, a₀)`、`p1 = Polar(pC, r, a₁)`，包角 = `a₁ − a₀`；**整圆 = a₁ = a₀ + 360°**
（两枚位置重合、ID 不同的端点，§4.1）。
好处：角度天然可公式化（`angleFormula`，ParamPoint.h:79），端点位置仍由 Polar 解析，
半径权威仍是起点 p0 的 `distance`；**整圆与弧共用同一套语义**，打开 / 收拢圆 = 只改 p1 角度。

**锚点固定 4 跨**：`passPointIds` 恒为 3 个象限锚，角度 = `a₀ + 包角·{0.25, 0.5, 0.75}`。
- 结构恒定 ⇒ 改包角**不需要增删点**（增删点是结构性变更，得走命令 + 可能触发断链）；
- 每跨 ≤ 90°；控制臂按该跨实际夹角取 `(4/3)·tan(θ/4)·r`（§4.2），θ < 90° 时误差比整圆更小；
- 代价：20° 小弧也带 4 跨（5 个锚），比理论最小（1 跨）多几个点，可忽略。

**截断（改包角）不变形**：只改 `a₁`（数值或公式）→ 锚点按新包角重算角度 → 半径、
圆心、拟合方式全不变。**不存在**「清 circleFit → 锚点按弦定位 → 形状跳变」的路径。
- **本期直接可编辑（D19）**：条带 / 面板「包角」框写 p1 角度（`a₁ = a₀ + 包角`）——
  整圆输入 180° 即得半圆，**一步到位，不需要打断两刀**；预设 90 / 180 / 270 / 360。
- **包角公式**：`p1.angleFormula = "<a₀ 表达式> + <包角表达式>"`（a₀ 无公式时取其数值），
  求值器原生支持 `+`；π 仍需先算成数字（§12）。

**打断（切成两段）不变形**（D7）：在参数 t 处打断 ⇒ 生成一个共享分割点
`pSplit = Polar(pC, r, a₀ + 包角·t)`，原段变 `[a₀, aSplit]`、新段 `[aSplit, a₁]`，
**两段都保留 `fitKind = FitKind::Circle`**、共用圆心、各自持有半径（初始都等于 r），包角互补。
- 实现要点：`BreakExecution.cpp:99/:161/:177` 目前对曲线是「截断 `passPointIds` +
  在中点插一个 `CurveAnchor`」，对 circleFit 段必须走**角度分割**分支：分割点的
  `interpPercent` 按弧参数折算，两段的 `passPointIds` 按新包角重新四等分；
- 打断后两段各自独立（各自半径权威），用户若要让它们重新同步，用同一个变量或
  二期「圆心联动」（§12）。

**为什么不能「降级成普通曲线」**：清掉 `circleFit` 的瞬间，象限锚从
`resolveCurveAnchorPoint` 的弦分支取位置（§4.1.1），会**立刻跳到弦上**——这就是
「打断后圆变形」的根因。所以降级必须是**显式命令**，且在降级时把锚点位置一次性
冻结成不会跳的形式。

**解除圆约束（D14，唯一的「变自由曲线」入口）**：一条命令完成——
`fitKind = FitKind::None`、全部锚点（含两端点）转 `Free` 并写入当前局部坐标、
`autoTangent = true`（回到 Hobby 自动切向）、`tension` 行标签回「张力」。此后就是普通曲线，
曲线编辑工具可自由拖控制点。undo 由命令快照完整还原（既有命令模式）。

**曲率 / 圆度 / 锯齿（三个不同问题，别混）**：

- **曲率** ≡ 1/r：改 r 即改曲率（半径 / 直径 / 周长框）。
- **圆度**（「不那么圆」的形状调节）= `Segment::tension`：控制臂（控制点偏移）× (1 + tension)，
  **0 = 正圆**（D17）。面板该行对圆段标签改「圆度」、tooltip「0 = 正圆，−1 = 内接四边形」。
  往下调 = 内收，`tension = −1` 时控制臂归零 → 每跨退化成**直线弦** → 形状 = 锚点连成的
  **内接四边形**（`CurveMath.cpp:479-481` 的 `ctrl = P ± T/3` 不做归一化，零切向安全，不 NaN）。
  锚点在 0°/90°/180°/270° 时是**菱形**，把**基准角度 a₀ 转到 45°**（D18）就得到**轴对齐正方形**，
  边长 r√2（周长 5.657r，比圆的 6.283r 短约 10%）。往上调 = 外凸，`+0.25` 起过冲；夹紧
  `tension ∈ [−1, +0.25]`。**注意：−1 时它已经不是圆**（锚点处曲率跳变），半径 / 弧长语义仍在。
- **「段数」不是圆的参数**（D20）：模型恒为 4 跨；用户看到的折线段数由 0.1 mm 容差决定
  （r=100 mm 约 70 段），与形状无关，D16 之后对观感也无影响。要真正的 N 边形请用独立
  正多边形工具（二期），要加工用的离散精度请用导出设置（三期）。
- **锯齿感**与形状无关，是**离散化**问题：`BlockCurve.cpp:167` 把曲线一次性扁平成
  0.1 mm 容差折线，`CurveItem.h:32` 让绘制与命中共用同一条折线
  （`BlockGeometryCache.cpp:85-97` 注释自称「below visual resolution at any zoom」，
  实际放大到 ~300% 以上时 0.1 mm ≈ 1 px，圆周就出现多边形棱角）。**圆段改走解析绘制**
  （D16：`tension == 0` → `arcTo`，`tension ≠ 0` → `spans` 逐跨 `cubicTo`，§9），任意缩放恒光滑；
  同一问题对普通曲线依旧存在，属既有全局问题，见 §12 二期「全局曲线离散化」。

---

## 5. 工具交互设计（ToolCircle）

### 5.1 状态机

```
Idle (无起点)
  │ 左键（圆心模式：落圆心；直径模式：落直径端点 A）
  ▼
SetRadius / SetDiameterEnd   ← Esc 或右键空白 → Idle（无任何 undo 残留）
  │ mouseMove：橡皮筋预览 + HUD；mousePress 左键
  ▼
Commit → CircleFactory → push AddCircleCommand（单步 undo）
  → reportPinnedTarget（条带进入圆编辑，§6）→ 回 Idle 可连画
```

实例常驻、`onActivate` 复位全部会话状态（工具生命周期铁律）；右键空白 =
`requestToolSwitch` 回选择工具（智能笔同款手势）。

### 5.2 模式与提示（W 键纪律）

- W 在「圆心-半径 ↔ 两点-直径」间切换；切换后 `event->accept()` 且**正在进行的橡皮筋
  取消回 Idle**（两类手势语义不同，不允许跨模式续命）。
- `ModeIndicator` 三层提示按 {mode, state} 出文案：
  - hint：`圆形[圆心]：点击落圆心，拖动/预输入定半径 | W 切直径` ；
    `圆形[直径]：点击两点定直径 | W 切圆心`；
  - modeName（画布角标，非常驻态）：`圆心` / `直径`；默认态 = 圆心；
  - toast（切换瞬间，L3 1.4s）：`已切换：两点直径`。
- 静态 `describe()` 的 hintText 为 Idle 圆心态文案（`toolHintText` 唯一出处，缺 =
  test_tool_hints 红）。

### 5.3 捕捉

- **圆心 / 直径端点**：`SnapEngine::findSnap` 点捕捉（12px，`kPointSnapRadiusPx`）+
  线身投影捕捉（X 标记与快捷辅助点沿用智能笔通路，`SmartPenStrokeInput`/QuickAuxDialog
  不新增——本期圆心捕捉仅借用位置，**不建立附着语义**（D9 / §7））。
- **过点圆**：SetRadius 态若光标落在既有点的点捕捉半径内，r 锁定为 `|点 − 圆心|`，
  预览显示过点吸附环（圆自然经过该点）——「圆心 + 一点」画法由它免费获得。
- **Shift**：半径步进吸附 0.5 cm（与智能笔 Shift=45° 同级「按住生效的修饰约束」；
  细节数值可后续调整）。
- **活动层过滤**：一律走 `HitTester.h` 唯一规则源，不在工具内复制过滤逻辑。

### 5.4 HUD 与预览视觉规范（对齐 Theme token，禁硬编码色）

| 元素 | 规格 |
|---|---|
| 预览圆 | 1px（zoom 补偿的 hairline）虚线描边，色 `accent` 陶土（`#CC785C` / 暗 `#D97757`）；无填充；**同样走解析绘制（D16；预览恒为正圆 → `arcTo`）**，不跟随落圆后的 0.1 mm 折线 |
| 半径引导线 | 圆心 → 光标 1px 实线，色 `text2`（`#5C5850`） |
| 圆心标记 | 小十字 1.5px，色 `success` 苔绿（`#3E8966`） |
| 过点吸附环 | 吸附点外圈细环 1.5px，色 `success` |
| HUD | 一律 `HudItem`（禁止手搭 QGraphicsItem+TextItem）：`R 12.50 cm`，等宽字体 `kMonospaceFamily`；公式预输入有效时附 `= waist/(2π)` 灰字 |
| 图元登记 | 预览图元一律 `ManagedItems` 登记，deactivate 统一释放 |

### 5.5 预输入与上下文条带会话

比照 PlacePoint 的会话范式（`Tool::placePoint* 转发组`，Tool.h:197-199）：

- **新增转发虚函数组**（仿既有先例，默认 no-op，无头单测桩可编译）：
  `circleRadiusInput(const QString& text, bool locked)` / `circleDiameterInput(const QString&)`
  / `circleCommitted()`；MainWindow → ToolManager::forward* → Tool 同路径。
- **条带「圆会话」**（绘制中）：胶囊输入框 ×1（半径）+ 锁定 chip + 直径联动只读换算
  （formatLength，2 位小数去尾零）：
  - 文本判读 `cad::geo::parseNumberOrFormula`（cm 域），**不装 QDoubleValidator**（会挡公式
    字符，既有规范）；
  - 红边提示走 `setProperty + polish`（每帧禁 setStyleSheet 铁律）；
  - **输入框必装 ShortcutOverride 事件过滤器**——TROUBLESHOOTING §147 的教训：预输入条
    不装过滤器时，键入 `V1`/`R×2` 这类文本会被主窗口 QAction 单字母快捷键抢焦切工具。
    新会话输入框直接复用预输入条同款过滤器；
  - Enter = 以输入值提交（等效第二次点击）；Esc = 取消橡皮筋；半径+锁定后单击即直接落圆
    （智能笔「长+角全预输入一键出线」的同款体验）。
- **创建完成（pinned）**：见 §6。

> **已实施（2026-12）**：见 §24（圆绘制会话条带）。与本节草案的偏差：转发签名
> `circleRadiusInput(double radiusCm, bool locked)`（对齐 `placePointDistInput(double,bool)` 先例）；
> 会话输入复用半径框，无独立 `circleDiameterInput`；仅圆心+半径模式开会话（直径模式单值无法定圆）。

### 5.6 取消与退化守卫

- r < `kGeomEps` 不提交，toast「半径过小」；两直径端点重合同理。
- 直径模式下第二击若吸附到与 A 相同的点 → 视为退化，提示不提交。
- Esc 两次语义：第一次清橡皮筋回 Idle，第二次（Idle 态）= 切回选择工具（与智能笔一致）。

---

## 6. 创建后编辑（条带 pinned 态）

### 6.1 条带「圆区段」（选中圆周段 → `reportPinnedTarget(blockId, segmentId)`）

> **实施注记（2026-12 更新，见 §23）**：圆已改走**专属底部条带** `CircleStripBar`（六槽：
> 圆徽标 / 编号 / 名称 / 半径 R / 直径 D / 周长 C），不再复用线段条带的三槽（M3-b 临时方案，
> 已删除，见 §16.2）。下表描述的是**属性面板**的完整行清单（M2 已落地）；包角与基准角
> 只在面板编辑，条带单行无空间。

比照 `LineGeometrySection` 的显示位（`src/ui/LineGeometrySection.cpp:313-319` 已有
「弧长行 + tension 行 + 发布参数按钮」的曲线变体，圆段在此基础上替换几何行）：

| 行 | 控件 | 语义 / 写回 |
|---|---|---|
| 半径 R | 胶囊输入（cm / 公式） | 写 `p0.distance` / `p0.distanceFormula`（**半径唯一权威**） |
| 直径 D | 胶囊输入（cm / 公式） | 同上，写入前 ÷2；显示 = 2R |
| 周长 C | 胶囊输入（cm / 公式） | 写入前 ÷2π（程序换算，**公式域无 π**，D13）；显示 = 2πR |
| 基准角度 a₀ | 输入（度 / 公式）——**走线段同一条通路**：`SegmentAngleCard` 自由线「角度」分支（caption「角度(°)」，D18） | 写 `p0.angle` / `p0.angleFormula`；改它 = 旋转接缝与全部象限锚，**圆心不动** |
| 包角 | **可编辑**输入（度 / 公式）+ 预设 `90 / 180 / 270 / 360`（D19） | 写 `p1.angle = a₀ + 包角`；整圆 = 360°（半圆 = 一次输入 180°） |
| 圆度 | 输入（数值，夹紧 `[−1, +0.25]`）+ tooltip「0 = 正圆，−1 = 内接四边形」 | 写 `seg.tension`（**0 = 正圆**，−1 = 内接四边形；配 a₀=45° 即轴对齐正方形，D17）；非圆段该行标签回「张力」 |
| 弧长（外长） | 只读 | `block.segmentBaseLength(seg.id)`（既有通路；**circleFit 且圆度 0 时返回解析值 r·θ**：整圆 = 2πr、半圆 = πr，§22.2） |
| 弦长 | 只读 | `degToChordMm(包角, R)`（`Angle.h:114` 既有函数；半圆 = 2R） |
| 发布参数 | 按钮 | 弧长发布为 `LinkedVariable`（**周长变量**，D15，零新机制；**已实施 §22**） |

- 三项长度输入**双向联动**：改任一项 → 一条 `SetCircleRadiusCommand` 原子写 `p0` 一个点，
  另两项刷新显示（无多点同步问题，§4.1）。
- **圆度行复用既有「张力」行**（同一控件、同一字段，仅标签与 tooltip 随圆段切换，D17）；
  延长行对曲线本就不显示（EXTEND_LINE D3）。
- 角色切换（辅助 ↔ 内部 ↔ 轮廓）、线型 / 线色 / 线宽 / 显隐、`showName` / `showLength`
  走既有段属性通路，零新增。

### 6.2 属性面板（`LinePropertyDialog`）字段总表

「圆的可输入项」完整枚举——分四处归属，**没有圆的私有字段**：

| 归属 | 字段 | 出处 |
|---|---|---|
| 段 | 名称 `name`、注释 `annotation`、序列号 `serial`（只读） | `Segment.h:29-35` |
| 段 | 角色 `role`（轮廓/内部/辅助）、线型 `lineStyle`、线色 `color`、线宽 `weight`、`visible`、`showName`、`showLength` | `Segment.h:38/81-87` |
| 段（圆专属） | `fitKind`（枚举，隐藏，实现细节，D21）、`tension`（圆段显示为「圆度」，0 = 正圆，D17） | 本设计 |
| 几何（圆段变体） | 半径 R / 直径 D / 周长 C / **基准角度 a₀** / 包角（可编辑，D19）/ 弧长（派生）/ 弦长（派生） | §6.1 |
| 圆心点 | 名称、注释、`visible`、`showName`、约束类型（Free 等）、`freePos` 坐标（**Free 点无公式**，ParamPoint.h:66；要公式化圆心 = 二期换 Polar/Interpolated 约束，§12） | `ParamPoint.h:60-61/66/154-155` |
| 端点 p0 / p1 | `Polar` 的 `distance` / `distanceFormula`（= 半径权威）、`angle` / `angleFormula`（p0 = **基准角度**，p1 = 终止角；两者即包角，D12/D18） | `ParamPoint.h:76-79` |
| 连接 | 基准线 / 跟随线、跟随角度 / 弧长 / 弦长、吸附与滑轨（**圆段可作角度基准**：整圆 = 接缝切线、弧 = 弦，统一走 `effectiveAngleRefWorld`，D9/§20） | `Attachment.h` |
| 公式 | 半径 / 直径 / 周长 / 起止角均可填公式，引用任意变量（`Variable` / `FormulaVariable` / `LinkedVariable` / `MeasureVariable`） | `ExpressionEvaluator` |

### 6.3 暂不做

- **拖圆周改半径**（选择工具直接拖弧边缩放）：二期（§12），本期仅条带/面板编辑。
- **拖象限锚改形**：圆段曲线编辑置灰（D8）；要自由改形走「解除圆约束」（D14）。

---

## 7. 与既有子系统的交互矩阵

| 子系统 | 对圆的行为 | 改动 |
|---|---|---|
| 渲染（BlockItem/CurveItem） | **circleFit 段的绘制路径改为解析路径**（D16：`tension == 0` 用 `arcTo`、否则 `spans` 逐跨 `cubicTo`，局部坐标、每 resolve 建一次），放大无棱角；命中/包围盒仍走 flatLocal 折线 | BlockGeometryCache 一个分支 + CurveItem 路径来源切换 |
| 捕捉（SnapEngine） | 圆周投影复用 `projectPointOnCurve`/cumArcLength；圆心点普通点捕捉 | 无 |
| 命中（HitTester） | 曲线命中自动生效；活动层 + 非影子规则不变 | 无 |
| 悬停 / 上下文条带普通目标 | hover 圆周段照常上报（`setHoverTarget`） | 无 |
| 测量 | 两点测量照常；周长 = 曲线弧长（M 长按可见） | 无 |
| 辅助点（Interpolated） | 可落在圆周上（percent 沿周长，弧线长分派复用） | 无 |
| **交点** | **现状对整圆失效**：两条求交路径都以「宿主段两端点弦长」判退化（`ResolverIntersection.cpp:113`、`BlockResolve.cpp:338`），整圆两端点位置重合 → 弦长 0 → 直接 `return false`；且「相对角」基准取端点弦方向（圆无定义）。需：circleFit / 闭合曲线跳过弦长守卫（spans 已含几何），圆段的相对角改为绝对角 / 指向点 | 两处守卫 + 角度基准一处 |
| 连接 / 附着 | 位置 pin 到 0° 锚点允许；**角度跟随以圆段为基准同样正确**（D9/§20：基准方向 = 端点切向，整圆取接缝切线、弧取弦；`effectiveAngleRefWorld` 为唯一通路） | ConnectGesture 线级分支统一到 `effectiveAngleRefWorld`（一处） |
| 旋转 / 拆开 / 影子 | 圆块作为普通块参与（刚体）；拆开影子语义照既定 | 无（回归测试覆盖） |
| 打断 | 切成两段**真圆弧**：共享分割点（`Polar`）+ 两段各保留 `circleFit`/圆心/半径，包角互补（§4.4，D7） | BreakAnalysis/BreakExecution 加 circleFit 角度分割分支 |
| 曲线编辑 | circleFit 段置灰（D8，**已实施 2026-12，§18**）；自由改形走「解除圆约束」命令（D14） | ToolCurveEdit 排除 + 1 条新命令 |
| tension | 圆段语义 = **圆度**（D17：控制臂 ×(1+tension)，0 = 正圆、−1 = 内接四边形、夹紧 [−1,+0.25]）；面板标签随段切换 | 面板一行 + refit 公式 |
| 弧长发布 | 复用「发布参数」按钮 → `LinkedVariable`（周长变量，D15，已实施 §22） | `segmentBaseLength` 一处分支 + `CircleGeometrySection` 一行按钮 |
| 端点延长 | 曲线本就不支持延长（EXTEND_LINE D3），圆自然同规 | 无 |
| 删除影响报告 | 圆块删除走标准块计数；核对九项报告无需新增项 | 核对 |
| 曲线点定位 | **唯一求解新代码**：circleFit 分支（§4.1.1） | `resolveCurveAnchorPoint` +12 行 |
| `p0/p1 位置重合`（整圆） | 弦长恒为 0：凡假设段长 >0 的分支（角度跟随基准、OrthoOffset 轴、箭头方向）必须有过零守卫——**实现时全仓 grep 核对**，缺的补 `safeZoomOr` 同款守卫 | 核对清单 |
| isClosed | 圆块 isClosed=false（D10），消费方核对后定稿 | 核对 |

---

## 8. 序列化

- **序列化新增只有一个键**：`Segment` 加 `"fitKind": "None" | "Circle"`（字符串而非 bool，
  见 D21；additive，老档缺省 `"None"`，**不 bump kFormatVersion**，annotation 先例，
  FormatMigration 链路不动；用字符串是为了二期加 `"Ellipse"` 等值时旧档仍可读）。
  半径 / 基准角度 / 包角 / 圆度全部落在既有字段（`ParamPoint.distance/distanceFormula/
  angle/angleFormula`、`Segment.tension`），**零新键**；初稿的 `arcSweepDeg` 已随 D12 取消。
- 老版本程序读新档：`fitKind` 被忽略，圆呈现为带切向的普通过锚曲线（仍近似圆），不拒载、
  不损坏——向后读兼容自然成立；回归 `test_serializer` round-trip。

---

## 9. 性能设计

- 一圆 = 1 曲线段 + 6 点（圆心 + 两端点 + 3 锚）；单帧成本与一条普通四锚曲线相同（circleFit 段
  切向直接给定，`buildBezierSpans` 不走 Hobby 求解，反而比普通曲线**更省**）。
- **解析绘制路径（D16）的成本口径**：在缓存构建时（每 resolve 一次）生成一次
  `QPainterPath`——`tension == 0` 走 `arcTo`（一条闭合圆弧 ≈ 4 段 cubic），`tension ≠ 0`
  走 `entry.spans` 逐跨 `cubicTo`（≤4 段），绘制时不再做扁平折线的逐段 lineTo。相对
  「几百段 lineTo」在圆数量为个位数时更省；若某文档圆数量极大（数十个）实测重绘变慢，
  回退方案 = 圆段单独用 0.01 mm 容差扁平（仍是每 resolve 一次，锯齿降到不可见）。
- refit 钩子 = 每圆每 resolve O(6) 纯算术；不进 PerfProbe 热点统计的增量可忽略。
- 预览零每帧 new：图元 ManagedItems 常驻复用；HUD setText 同值短路（Qt 性能纪律）。
- 刚体拖圆块不触发 curveCacheBuilds（telemetry 断言照转）。

---

## 10. 工具系统接线清单（实现时逐条打勾）

按 AGENTS.md「新增工具 5 步」+ 本设计增量：

1. `ToolType` 枚举加 `Circle`（ToolRegistry.h:16，注册序 = 工具坞序，建议注册在
   PlacePoint 之后）；
2. `ToolCircle` 实现 `onActivate/onDeactivate` + `name()` = "圆形"；
3. `static ToolDescriptor describe()`：displayName `圆形(&O)`、iconName `circle`、
   shortcut `O`（落地前 grep `setShortcut` 复核 O 无 QAction 占用、落地后更新
   TROUBLESHOOTING §0 登记表）、hintText 按 §5.2；
4. `ToolRegistry.cpp` 加 `registerTool<ToolCircle>()`；
5. `MainWindow.cpp toolDockIcon()` 补分支 + `resources/icons/circle.svg`（Phosphor
   Regular 1.5px stroke）入 qrc；
6. 引擎：`Segment::fitKind`（`enum class FitKind { None, Circle }`，**唯一新增字段**、非 bool
   以过 `check_bool_flags`，D21；`arcSweepDeg` 已随 D12 取消）+ 圆块建
   **两枚重合端点** p0/p1（整圆 p1 隐藏）+ `Block::resolve` refit 钩子（touchGeometry 守卫、
   置 `autoTangent = false`、控制臂 ×(1 + tension) 圆度，D17）+
   `resolveCurveAnchorPoint` 的 circleFit 分支（§4.1.1，**不做则圆塌陷**）；
7. `CircleFactory`（tools 层，纯构造，比照 LineFactory）+ 命令组（document/commands，
   各自单步 undo）：`AddCircleCommand`、`SetCircleRadiusCommand`（R/D/C 三向换算）、
   `SetCircleAngleCommand`（基准角度 a₀ / 包角 / 终止角，D18/D19）、`DetachCircleCommand`（D14）；
8. 画布：`BlockGeometryCache` + `CurveItem` 对 circleFit 段改用**解析绘制路径**（D16：
   `tension == 0` → `arcTo`，否则 `spans` 逐跨 `cubicTo`；命中仍用 flatLocal）；
9. 上下文条带 + 属性面板：圆会话字段 + pinned 圆区段（含**包角输入与 90/180/270/360 预设**、
   **圆度行（夹紧 [−1, +0.25]）**、弦长只读）+ Tool 转发虚函数组 + 输入框 ShortcutOverride
   过滤器 + 基准角度接 `SegmentAngleCard` 自由线分支；
10. 守卫：ConnectGesture 圆基准统一到 `effectiveAngleRefWorld`（D9/§20）、ToolCurveEdit 置灰、**打断走 circleFit 角度
    分割分支**（§4.4，不清标志）、circleFit 段锁死 p0/p1 的 Polar 约束切换、
    `SegmentAnchorTab.cpp:362` 圆段按角度而非弦百分比显示 `interpPercent`、
    面板「张力」行对圆段改标签为「圆度」；
11. 文档同步（收尾防过时纪律）：DOCS_INDEX.md 状态翻「已落地」、DECISIONS.md 登记拍板
    条目、TROUBLESHOOTING §0 快捷键表。

**新文件体量预估**（check_file_size 红线内）：`ToolCircle.h/.cpp`（~300 行）、
`CircleFactory.h/.cpp`（~120 行）、`AddCircleCommand.cpp`（并入或单文件 ~100 行）、
`tests/test_circle_tool.cpp` + `tests/test_circle_fit.cpp`。新增 .cpp 一律入对应模块库
源列表（Tools 两文件 → gcad_tools；命令 → gcad_document；测试源只进 tests target，切勿
加项目头文件）。

---

## 11. 测试计划

新增 `test_circle_tool`（工具）与 `test_circle_fit`（引擎）：

1. **创建命令**：AddCircleCommand 后块/点/段数量与连线关系（p0/p1 两枚端点、pass 序、
   整圆时 p1 与 p0 位置重合且隐藏）、undo/redo 全恢复、geometryEpoch 已 bump；
2. **参数化呼吸**：设半径公式 `R = hip/8`，改变量 → resolve → p0 与 3 个象限锚到圆心距
   恒等新 r；
3. **真圆不变式**：对圆周 spans 采样 64 点，各点到圆心距离与 r 偏差 < 0.001·r；
   接缝处（p0）C1 连续（出/入切向共线反向）；
   **象限锚定位回归**：3 个 CurveAnchor 到圆心距离 == r（锁死 §4.1.1 弦塌陷）；
   **autoTangent 陷阱**：若误置 true，Hobby 切向覆盖写回值 → 形状漂移，用例须能捕获；
   **p0 降级**：把 p0 改成非 Polar 后 resolve 不崩、曲线不构建（或按降级策略处理）；
4. **工具手势**：点击-拖动-提交全流程（事件模拟）；过点吸附锁定 r；W 切模式（橡皮筋
   取消回 Idle）；Esc 两级退出无 undo 残留；预输入（数值 / 公式 / 非法文本红边）；
5. **序列化 round-trip**：`fitKind` 往返保持；老档（无此 key）加载 = 普通曲线（`None`）；
6. **守卫**：曲线编辑置灰（D8，判据 §18.3：拖锚 / Shift 删锚 / Ctrl 加点三手势无效且无 undo）、圆段作角度基准方向 = 接缝切线且连接后不跳变（D9，判据 §20.5）、退化半径不提交、tension 行隐藏；
   8 个守卫脚本全绿——`check_bool_flags` 在 `Segment` 上加第 6 个 bool 会直接红（D21），
   故字段是枚举；`check_file_size`（`BlockCurve.cpp`/`BlockResolve.cpp` 若逼近 400 行需拆）；
6b. **弧（部分圆）**：包角 180° 时 3 锚位于 45°/90°/135° 且各到圆心距 == r；改包角后
   圆心与半径不变（形状不跳变）；**包角输入一步得半圆**（不经打断）、预设 90/180/270/360
   生效、包角公式（`a₀ + 180`）生效；
6c. **打断不变形**：整圆打断 → 两段弧，对两段采样 64 点仍落在原圆上（偏差 < 0.001·r），
   两段包角之和 == 360°；
6d. **解除圆约束**：解除瞬间形状冻结（前后采样点一致）、之后锚点可拖动、undo 完整还原；
6e. **周长变量**：发布后公式可引用该变量，值 == 2πr（cm 域；弧长已解析化，§22.2）；
6f. **绘制路径（消锯齿，D16）**：取 `BlockGeometryCache` 中圆段的绘制路径，采样 256 点，
   各点到圆心距离偏差 < 1e-6·r（tension=0 的解析 `arcTo` 路径应为 0；若误走 0.1 mm 折线，
   该用例在 r = 500 mm 时偏差约 0.1 mm，必红）；同时断言命中路径仍来自 flatLocal；
   **tension ≠ 0 时绘制路径来自 spans 的 `cubicTo`**（与 `buildBezierSpans` 采样逐点一致，
   偏差 < 1e-9），**tension = −1 时绘制路径为 4 条直线**（采样点落在正方形/菱形边上）；
6g. **圆度（D17）**：tension = 0 时各跨控制点偏移 == `(4/3)tan(θ/4)·r`（切向量 == 3×该值）；tension = 0.2 时
   臂 == 1.2 倍、形状仍闭合光滑（相邻跨切向共线）；弧长随圆度单调变化且非零；
   **tension = −1 时控制点偏移为 0、4 跨退化成直线弦**（顶点仍在圆周上，无 NaN）、
   a₀=45° 时得轴对齐正方形且边长 == r√2（周长 5.657r）、a₀=0° 时为菱形；
6h. **基准角度（D18）**：改 a₀ = 90° 后接缝、p0 位置与全部象限锚同步旋转 90°，圆心与半径不变；
   通过 `SegmentAngleCard` 写入路径与自由线段一致；
6i. **360° 往返不丢（§4.1.1）**：整圆 p1.angle == 360°（不是 0°），`sweep == 360°`；
   经序列化 round-trip、面板编辑往返后仍为 360°（任何取模归一化都会让此用例红）；
7. **既有回归跑测面**（按影响面选测铁律）：`test_tool_hints`（describe 兜底）、
   `test_serializer`、`test_curve`、`test_resolver_curve_arc`、`test_block_commands`、
   `test_select_wkey`、`test_context_strip`；收尾交付前全量 ctest 1 次。

GUI 等待一律 `TestHelpers.h` waitUntil/grabStable/settle（禁 qWait 占位）。

---

## 12. 二期扩展（本设计预留，不做承诺）

| 扩展 | 说明 |
|---|---|
| 三点定圆 | 第三种 W 模式；圆心/半径由三点解出后即可复用全部落地物 |
| 拖圆周改半径 | 选择工具拖圆弧：r = 光标到圆心距，release 提交 SetCircleRadiusCommand |
| 块内圆 | 圆作为既有块的 Internal 段（随衣片走）；需块内创建通路，与 Internal 线创建机制一并评估 |
| 圆心联动 | 圆心点改 Interpolated / 桥接，跨块跟随（Polar 骨架已就位，仅换圆心约束） |
| 圆弧独立工具 | 本期「打断圆 = 真圆弧」已覆盖大半需求（§4.4）；独立弧工具 = 两点 + 包角/半径的创建姿态 |
| 正多边形工具 | 「段数 N 可调」的正 N 边形（4 = 正方形、8 = 八边形…）：**独立工具 + 独立实体**，点数随 N 变（结构性命令）、半径语义 = 外接圆半径，不并入圆的参数（D20）。圆度滑杆（D17，−1 = 内接四边形）已能覆盖「圆 ↔ 方」的连续形变 |
| 导出离散化精度 | 绣花机 / 激光切割 / 缝纫轨迹把圆弧离散成折线（按弦高或段数），属**导出设置**而非几何属性（D20）；与「全局曲线离散化」一并评估 |
| 全局曲线离散化 | 圆段已走解析绘制（D16）；**普通曲线仍受 0.1 mm 固定容差扁平折线限制**（`BlockCurve.cpp:167`），放大同样有棱角——二期评估按缩放自适应重扁平或改解析绘制（注意「每 resolve 一次」纪律与每帧成本） |
| π 常量 | 表达式求值器无 π（`ExpressionEvaluator.cpp:40-62`）；若用户要在公式里写 π，需加常量（注意小写标识符留给函数，得用大写或 `PI`） |
| 周长变量发布 | 本期已用「发布参数」按钮落地（D15）；二期做专属周长 kind + 面板卡片，值域 cm |
| 圆心公式化 | `Free` 点无公式（ParamPoint.h:66）；二期把圆心换成 Polar / Interpolated 即可公式驱动 |
| 椭圆 | κ 双轴推广，a/b 两参数 |
| 真 Arc 原语 | DXF 直出 / 裁床级精度需求出现时重立（§4.3 存档） |

---

## 13. 实现问答（逐问速查）

| # | 问题 | 答案 | 详见 |
|---|---|---|---|
| Q1 | 怎么实现？分几层？ | 工具层手势（ToolCircle）+ 工厂/命令（CircleFactory + 4 条命令）+ 引擎（circleFit 字段、refit 钩子、锚点定位分支）+ UI（条带圆区段、面板圆变体），共 10 步接线 | §10 |
| Q2 | 要做一个圆心吗？能连接吗？ | 是：`Free` 点，Block 局部原点。可被连接（位置 pin 到别的点 / 被别的段跟随）；**角度跟随以圆段为基准也成立**（基准方向 = 端点切向：整圆 = 接缝切线、弧 = 弦，D9 已修正） | §4.1、D9、§20 |
| Q3 | 圆有单独的线段面板吗？ | 有：条带「圆区段」+ 属性面板几何行换成半径/直径/周长/起止角；其余走既有段属性通路，**没有圆的私有字段** | §6 |
| Q4 | 可输入项有多少？ | 段 7 项（名称/注释/角色/线型/线色/线宽/显隐+显示开关）、圆几何（R/D/C/基准角度 a₀/包角/圆度/弧长派生/弦长派生）、圆心点 6 项、端点 4 项、连接若干 | §6.2 |
| Q5 | 有周长、直径、半径吗？半圆呢？ | 三项联动输入，改任一项写 `p0.distance`（周长 ÷2π 由程序换算）；**半圆 = 包角框输入 180°，本期一步得到**（预设 90/180/270/360，不需要打断两刀）；弧长（外长）= πr、弦长 = 2r 均只读显示 | §6.1、D13、D19、§4.4 |
| Q6 | 一个圆由几段组成？四根还是两根直线？ | **1 个 Segment**，由 **4 段三次 Bézier 跨**组成（不是直线），共 6 个点：圆心 + 2 端点（整圆位置重合）+ 3 象限锚 | §4.1 |
| Q7 | 弧线能像曲线那样拖控制点改形吗？ | 象限锚是 `CurveAnchor`，但圆模式下曲线编辑**置灰**（拖了就破圆）；要自由改形走「解除圆约束」→ 变普通曲线后可随意拖 | D8、D14、§4.4 |
| Q8 | 只要一部分，怎么保证打断/截断不变形？ | 弧保留圆心 + 半径 + 包角参数（角度落在两端点 Polar 上）；打断 = 切成**两段真圆弧**，绝不静默清 `circleFit`（清了锚点会跳到弦上 = 立即变形） | §4.4、D7、D12 |
| Q9 | 圆的曲率 / 曲线度 / 锯齿能改吗？ | 三件事分开：**曲率** = 1/r（改半径）；**圆度** = `tension`（0 = 正圆，−1 = 内接四边形，D17，面板该行标签改「圆度」）；**锯齿**与形状无关，是离散化问题，圆段改走解析圆弧绘制后消失（D16）；普通曲线的锯齿是既有全局问题（§12） | §4.4、D16、D17、§9 |
| Q10 | 完整的圆能用变量吗？ | 能：半径 / 直径 / 周长 / 基准角度 a₀ / 包角（终止角）都可填公式并引用任意变量；圆心是 `Free` 点、**坐标不支持公式**（二期换 Polar/Interpolated） | §6.2、D13、§12 |
| Q11 | 周长能被别的线引用吗？ | 能：条带「发布参数」按钮把弧长（= 周长）发布为 `LinkedVariable`，进参数表（cm 域），任何 `lengthFormula` 可引用——既有通路，零新机制（**已实施 §22**） | D15、§6.1 |
| Q12 | undo / 性能？ | 所有编辑都是单步命令（创建、改半径、改基准角度/包角、改圆度、解除约束、打断），快照还原；一圆 = 1 段 6 点，refit 每 resolve O(6) 纯算术 | §9、§11 |
| Q13 | 有哪些没问到的坑？ | ①整圆与射线**求交现在会直接失败**（弦长 0 守卫，需两处放行）；②公式里**写不了 π**（求值器无该常量，用 `2π` 需先算成数字或二期加 `PI`）；③整圆态 p1 与 p0 位置重合，必须靠隐藏标志避免两个手柄叠在一起；④跨 0° 接缝的弧会被切成两段；⑤p0/p1 的 Polar 约束必须锁死，否则锚点塌到圆心 | §4.1.1、§7、§12 |
| Q14 | 圆心有「基准角度」吗？和线段一样吗？ | 有，且**和线段同一条通路**：a₀ 落在 p0 的 `angle`/`angleFormula`，由 `SegmentAngleCard` 自由线「角度」分支编辑；改它 = 旋转接缝与全部象限锚，圆心不动 | D18、§6.1 |
| Q15 | 圆会像现有曲线那样有锯齿吗？ | 不会：圆段绘制改走**解析圆弧路径**（D16）。锯齿的真实来源是 0.1 mm 固定容差的扁平折线（`BlockCurve.cpp:167`），放大到 ~300% 即见棱角；普通曲线仍有此问题，列入二期 | D16、§9、§12 |
| Q16 | 圆是「很多直线」组成的吗？能调段数吗（调到 4 就是正方形？） | 你看到的是**渲染折线**（0.1 mm 容差 → r=100 mm 约 70 段），模型恒为 **4 跨三次 Bézier**——「4」已是固定值，不是可调最小值；D16 改解析绘制后跨数对观感无影响，故**不做段数参数**（D20）。要变正方形请调**圆度到 −1**（控制臂归零 → 每跨退化成直线弦 → 内接四边形），再配 a₀=45° 即轴对齐正方形（边长 r√2）；真要 N 边形 → 独立正多边形工具（二期） | D17、D20、§4.4 |
| Q17 | 落地第一步做什么最省、又能看见成功？ | **M1 = 引擎三件套 + 一条最小创建通路**：画布上出现一个参数化真圆（能改半径仍是圆、能存能读），不接工具、不碰条带/面板、不 bump 格式版本。5 个文件约 200 行，见 §14 | §14、§10 |

---

## 14. 第一步（M1）：最小可看见的成功

**目标**：画布上出现一个**参数化真圆**——改半径后仍是圆、保存重开还在。**不接工具、不碰条带/面板、不 bump 格式版本。**

**为什么这就是最小一步**
- 引擎三件套绕不开：没有锚点定位分支（D11），整圆弦长为 0 → 3 个象限锚全塌到圆心，圆根本不存在。
- 渲染**不用改**：`tension == 0` 时 0.1 mm 扁平折线在 100% 缩放下看起来就是圆（`BlockCurve.cpp:167`），
  D16 解析绘制推迟到 M2——它是「放大 300% 才看得见」的改进，不是「圆能不能出现」的前提。
- 入口二选一：**临时 debug 菜单项**（~15 行，验证完删）或**最小 ToolCircle**（~200 行，不丢弃）。
  想要「用鼠标画出来」的手感就选后者；只想先验证引擎就选前者。

**文件清单（计划 5 个；实施落地见 §14.1）**
1. `src/parametric/Segment.h` — `enum class FitKind { None, Circle };` + `FitKind fitKind = FitKind::None;`
   （**不是 bool**，D21：`check_bool_flags` 阈值 5，`Segment` 已占满 5 个）
2. `src/parametric/BlockResolve.cpp` — `resolveCurveAnchorPoint` 加 `fitKind == Circle` 分支
   （圆心 + r·(cosθ, sinθ)，θ = a₀ + 包角·interpPercent；§4.1.1）
3. `src/parametric/BlockCurve.cpp`（或 `Block::resolve`）— refit 钩子：写回切向 `3κR·perp`、置 `autoTangent = false`
4. `src/document/DocumentSerializer.cpp` — `"fitKind"` 读写（additive，老档缺省 `None`，不 bump 版本）
5. `src/tools/CircleFactory.{h,cpp}` + `src/document/commands/CircleCommands.cpp` — 构造 6 点 + `AddCircleCommand`
   （`isClosed = false`、p1 `visible = selectable = false`）

**验收 = 「看得见成功」的定义**
1. 画布上视觉是圆（截图留档）；放大到 ~300% 才可能见折线棱角 —— **M1 接受**
2. 改 `p0.distance`（或重画）→ 圆等比缩放、圆心不动、**仍是圆**（不是花瓣）
3. 保存 → 重开 → 圆还在（`fitKind` round-trip；老档无此键加载为普通曲线）
4. `test_circle_fit` 绿：256 点采样偏差 < 1e-6·r、切向 == 3κR、`sweep == 360°`
5. 8 个守卫脚本全绿（`check_bool_flags` 正是 D21 的来源）

**明确不在 M1**：D16 解析绘制、HUD / 条带 / 属性面板、W 双模式、包角与圆度 UI、
打断、解除圆约束、发布参数、图标（可后补）。

**M1 最容易踩的三个坑**：① refit 必须在全部点解算后、`rebuildCurveCache()` 之前；
② `autoTangent` 必须 `false`，否则 Hobby 覆盖写回的切向；③ p1.angle 禁止归一化（§4.1.1）。

### 14.1 M1 实施记录（2026-12，已落地）

实际改动 9 处（比计划多 2 处，少 1 处）：

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/parametric/Segment.h` | `:30` `enum class FitKind { None, Circle };`；`:49` `FitKind fitKind = FitKind::None;`（插在 `role` 与 `startPointId` 之间） |
| 2 | `src/parametric/BlockResolve.cpp` | `:36` 前调 `syncCircleFitRadius()`（`evaluateExtendValues` 之后、`resolveUnresolved` 之前）；`resolveCurveAnchorPoint` 的 Circle 分支 `:428-449`（**在 :451 弦长退化早退之前**）；`applyCircleFitTangents()` `:483-562`；`syncCircleFitRadius()` `:564-586` |
| 3 | `src/parametric/Block.h` | `:453-461` 两个私有声明 `applyCircleFitTangents()` / `syncCircleFitRadius()` |
| 4 | `src/document/DocumentSerializer.cpp` | 读写 `"fitKind"`（`kFitKindMap` 已随 #5 迁出）；**未 bump `kFormatVersion`（仍 4）** |
| 5 | `src/document/EnumCodec.h` | **新增**（header-only，`namespace cad::param`）：原 `DocumentSerializer.cpp:38-154` 的整段枚举编解码整体抽出，见 §14.3 |
| 6 | `src/tools/CircleFactory.{h,cpp}` | **新增**：`createCircle(center, radiusMm, startAngleDeg = 0)`；6 点 + 3 象限锚；**复用 `cad::cmd::DrawLineCommand` 而非新建 `AddCircleCommand`**（撤销文案 `setText(u8"画圆")`） |
| 7 | `src/tools/ToolCircle.{h,cpp}` | **新增**：O 键；按下吸附取圆心 → 拖动预览 → 松开提交；`kMinCircleRadiusMm = 0.5`；右键/Esc 拖动中取消、否则切 Select |
| 8 | 注册/图标 | `ToolRegistry.h:25` 加 `Circle`（共 10）；`ToolRegistry.cpp` `registerTool<ToolCircle>()`；`MainWindowToolBar.cpp:35` `ElaIconType::Compass`；`test_tool_hints.cpp` 同步 |
| 9 | `tests/test_circle_fit.cpp` + `CMakeLists.txt` | **新增** 8 个 slot；CMake 三处（gcad_tools 源、gcad_document 源加 `EnumCodec.h`、`test_circle_fit` target） |

**验证结果**：`tools\build.bat all` exit 0；`test_circle_fit`（8 slot 全绿）、`test_tool_hints`、
`test_mode_indicator`、`test_serializer`、`test_migration`、`test_resolver_curve_arc` 全绿；
8 个守卫脚本全绿（`check_file_size` 曾红，见 §14.3）。

### 14.2 M1 实测修正（写进代码的三条）

1. **拟合精度权威值**：4×90° 三次 Bézier（κ = (4/3)·tan(π/8) = 0.552284749831，象限接缝切向连续）
   的**最大径向误差 = 2.7253e-4·r**（200001 点扫描，峰值 t ≈ 0.2113）。
   minimax κ = 0.551915024494 可压到 1.9608e-4·r，但**破坏接缝切向连续 → 拒绝**。
   测试阈值取 `< 1.05 × 2.7253e-4 × r`（不要写 1e-6·r，那是双精度算术误差量级，几何上不成立）。
2. **零切向必须写入**（D17 的实际拦路虎）：`applyCircleFitTangents` 的写回守卫若写成
   `autoTangent || dist² > eps`，则 `tension == -1`（零切向）时**永远不写**，内接四边形永远画不出来
   （实测 minR 仍 = r）。正确写法：`if (a0.autoTangent || !(a0.tangentOut == tOut))` —— 零值也写、
   `autoTangent = false`。见 `BlockResolve.cpp:542-556` 注释。
3. **扁平点数**：0.1 mm 绝对容差下 r = 63.5 的 90° 跨约 14 段 → `flatLocal` ≈ 56 点，**不是 256**。
   测试断言改 `flatLocal.size() >= 32`（同时保留了「折线点在圆上」的 `maxRadiusError` 判据）。

### 14.3 顺带修掉的红线：文件体积 ratchet

加 `"fitKind"` 后 `src/document/DocumentSerializer.cpp` 从 987 → 1004 行，撞上
`tools/check_file_size.py` 的「已登记文件行数只减不增」不变式（`redline_exceptions.json`，
与 D21 拒绝第 6 个 bool 是同一个「不许扩张」约束）——**不允许上调基线**。
做法：新建 header-only `src/document/EnumCodec.h`（`enumToStr`/`enumFromStr` 模板 +
`PointConstraint`/`SegmentType`/`SegmentRole`/`LineStyle`/`FitKind` 五张表 + `AdjustMode`），
把原 `:38-154` 整体迁出；`DocumentSerializer.cpp` 加一行 `#include "document/EnumCodec.h"`
→ **888 行**（低于 900 阈值）。因已回到阈值内，**同时退役了该文件在 `redline_exceptions.json`
的条目**（列表只减不增 ✓）。
> 迁移注意：表的第一行是「默认行」，也是未知值/未知字符串的降级目标——新增枚举值必须在这张表里加行，
> 否则静默降级为默认值。

### 14.4 实施期踩到的环境坑（与代码无关，但会伪装成回归）

`tools\test.bat all` 首轮 26 个 GUI 测试 SEGFAULT / 堆损坏（`0xC0000374`），单跑 `test_measure`
崩在 `BlockGeometryCache::rebuild()` 读 `block->segments`。**根因不是本次改动**：这是
`TROUBLESHOOTING.md:97` 记录的「陈旧 object」家族——历史上 `rules.ninja` 的 `msvc_deps_prefix`
乱码，ninja 从未记录任何头文件依赖，改 `Segment.h`（被广泛包含、且本次改动了结构体布局）后
`gcad_canvas` 等 38 个普通 `.cpp` 没有重编，新旧 `Segment` 布局混链 → 堆损坏。
**判据**：`ninja -C build/out-reldeb -t deps` 里 `CMakeFiles/gcad_canvas.dir/src/canvas/BlockGeometryCache.cpp.obj`
等显示 `#deps 0`（健康时只有 `*_autogen/mocs_compilation.cpp.obj` 与 `qrc_*.cpp.obj` 允许为 0）。
**修法**：`cmake --build --preset relwithdebinfo --target clean` + `tools\build.bat all` 全量重建。

---

## 15. 第二步（M2）：面板编辑 + 消锯齿（已实施 2026-12）

M1 让圆「画得出、存得下」，M2 让圆「改得动、看得清」：属性面板出现圆专属几何区，
圆度 0 的圆段绘制改走解析圆弧路径。**仍不在 M2**（留 M3+）：HUD/条带圆区段、
W 双模式、打断、解除约束、发布参数、图标。

### 15.1 改动清单（7 处）

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/ui/CircleGeometrySection.{h,cpp}` | **新增**（83 + 429 行）：半径/直径/周长/基准角/包角/圆度 6 行 + 只读「弧长 / 弦长」；包角行尾 90/180/270/360 预设芯片 |
| 2 | `src/ui/LinePropertyDialog.{h,cpp}` | 接线：`seg.fitKind == FitKind::Circle` 时显示圆区，**隐藏** `LineGeometrySection` 与 `SegmentAngleCard`（后者写的是终点 Polar 角度 = 圆段包角，语义冲突） |
| 3 | `src/ui/LineGeometrySection.cpp` | `applyToModel` 对圆段**早退**（隐藏的长度/张力框仍留旧文本，不早退会把 99 cm 写进终点距离、把圆度写回） |
| 4 | `src/ui/LinePropertySession.{h,cpp}` + `src/document/commands/SegmentPropertyCommands.{h,cpp}` | undo 通路补洞：快照与 `Props` 增加起点 `distance`/`angle`(+公式)、终点 `angle`(+公式)、`tension` —— 否则面板改半径/基准角 Ctrl+Z 撤不掉 |
| 5 | `src/canvas/CurveItem.{h,cpp}` + `src/canvas/BlockGeometryCache.cpp` | D16：`CurveItem::Data::paintPath`；圆段 `tension==0` 建解析圆弧（整圆 `addEllipse`、部分圆 `arcTo`），`tension≠0` 按 `entry->spans` 逐跨 `cubicTo`；命中/包围盒仍用 flatLocal |
| 6 | `src/canvas/CurveItem.h:11` | **顺手修既有 bug**：`namespace cad { class CanvasScene; }` → `class CanvasScene;`（见 §15.3） |
| 7 | `tests/test_circle_edit.cpp` + `CMakeLists.txt` | **新增** 11 个 slot + target 注册（链接 gcad_ui/canvas/tools/document/parametric/geometry） |

### 15.2 三条实现事实（写代码时踩出来的）

1. **基准角 a0 与包角必须「联合」应用**。终点角度 = a0 + 包角，两行互相耦合：
   先写 a0 再写包角时，后一步的 `resolvedStartAngleDeg()` 读的是 `resolvedPos`，
   此刻**还没重新解算** → 拿到旧 a0（= 0），把刚写好的终点角度覆盖成 360°
   （整圆从 a0=90 → ep 应为 450° 却掉回 360°）。现在一次性解析两行再落笔。
2. **D/C 换算精度受显示位数限制**。直径/周长输入换算成半径后写回半径输入框，
   而半径框是 2 位小数 cm，所以「周长 10 cm」→ r 只到 ~0.02 mm 精度
   （测试容差从 1e-9·mm 放宽到 0.02 mm，注释写明原因）。
3. **解析绘制路径的测试判据不能用 1e-6·r**。`QPainterPath` 自己的椭圆也是 4 段三次
   近似（≈2.7e-4·r），断言取 `err < 0.05`；`tension = −1` 时按真实 Bézier 画，
   内接四边形径向误差 > 5 mm，正好把「被画成圆」这个回归钉死。

### 15.3 顺手修掉的既有 bug：`CurveItem.h` 里的 `cad::CanvasScene`

`src/canvas/CurveItem.h:11` 原本写 `namespace cad { class CanvasScene; }`，而全仓其余
50 处（含 `src/canvas/CanvasScene.h:26` 的定义）都在**全局命名空间**。头文件里这个
`cad::CanvasScene` 是另一个未定义类型：任何同时包含 `CurveItem.h` 与 `ui/*.h` 的
编译单元，都会把 `LineGeometrySection` / `CircleGeometrySection` 的构造器按
`cad::CanvasScene*` 修饰，与 `gcad_ui.lib` 里 `::CanvasScene*` 版本不匹配 → `LNK2019`。
改回全局声明即解（`CurveItem.cpp` 里的 `qobject_cast<CanvasScene*>` 不受影响，
它 include 的是 `CanvasScene.h` 的全局定义）。

---

## 16. 第三步（M3）：整圆求交 + 条带圆区段（已实施 2026-12）

M2 之后圆还剩两个「看得见、用不了」的洞：① **交点工具在圆周上失效**——两条求交
路径都以「宿主段两端点弦长」判退化，整圆 p0/p1 位置重合 → 弦长 0 → 直接放弃；
② **选中圆周段时条带仍是线段字段**——长度框显示弦长（整圆恒 `0.00`）、角度框显示
终点绝对角而不是包角。M3 补齐这两处。**仍不在 M3**（留 M4+）：W 双模式、
HUD/预输入。**M3 当时不含、后续章节已补齐**：解除圆约束（D14，§17）、
曲线编辑置灰（D8，§18）、打断成真圆弧（D7，§19）。**M3 之后补齐**：两点直径模式
（D5，§21）、圆段作角度基准（D9，§20）、周长发布变量（D15，§22）。**仍留 M4+**：
HUD/预输入、工具图标。

### 16.1 M3-a 整圆求交

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/geometry/Angle.h` | **新增** `inline double circleCcwTangentDeg(const Vec2& point, const Vec2& center)` = `radToDeg(atan2(radial.y, radial.x)) + 90.0`——圆段无端点弦方向，相对角基准改用起点处 CCW 切向 |
| 2 | `src/parametric/BlockResolve.cpp`、`src/parametric/ResolverIntersection.cpp` | 退化守卫 `segLen < kGeomEps` 加 `&& seg->fitKind != FitKind::Circle`（圆靠 `spans` 提供几何，端点重合不代表退化）；相对角基准分支同样换 `circleCcwTangentDeg` |
| 3 | `src/parametric/Block.{h,cpp}` | `applyCircleFitTangents()` 改签名 `[[nodiscard]] bool`（返回是否移动过点）；`resolve()` 改为两轮拟合（见 16.3-1） |
| 4 | `src/geometry/RayCast.cpp` | 匿名命名空间新增 `const CurveHit& nearestAlongRay(const std::vector<CurveHit>&, const Vec2& origin, const Vec2& dir, bool bidirectional)`（键 = `(point−origin)·dir.normalized()`，`bidirectional` 取 `abs`，最小者胜）；`rayCurveSpansLocal` 与 `raySegmentOrCurveIntersect` 曲线分支改用它。`rayCurveIntersect` 契约不变 |
| 5 | `src/tools/ToolIntersection.{h,cpp}` | 新增 `static std::optional<TargetGeometry> targetGeometry(const Block&, const Segment&)`：圆段 `baseAngle = circleCcwTangentDeg(g.w1, block.transform.toWorld(center->resolvedPos))`，并跳过 `segDir` 退化守卫；新增 `showTargetHighlight(...)` |
| 6 | `src/tools/IntersectionToolVisuals.{h,cpp}` | `m_segHighlight` 由 QGraphicsLineItem 改 QGraphicsPathItem + `showCurveHighlight(QPainterPath, bool hover)` |
| 7 | `tests/test_intersection.cpp`、`tests/test_tool_intersection.cpp` | 新增 `makeCircle(radiusMm, startAngleDeg)` + 5 个 slot（同块指向点 → (−10,0)、相对角走起点切向 → (0,10)、世界角、跨块、半径 10→20 命中点跟到 (−20,0)）+ 工具层 `circleHostAimCreatesIntersection` |

**闭环判据**：`ctest -R "test_intersection|test_intersection_update|test_tool_intersection|test_curve|test_circle_fit|test_circle_edit|test_resolver_points|test_resolver_attachment|test_resolver_curve_arc|test_resolver_diag_misc|test_serializer|test_migration"` 全绿。

### 16.2 M3-b 条带圆区段

> **已取代（2026-12，见 §23）**：本节的三槽复用方案已被圆专属条带 `CircleStripBar` 取代，
> `ContextStripDisplay.cpp` / `ContextStripEdit.cpp` 的圆分支与 `test_context_strip.cpp` 的
> 6 个圆 slot 均已删除。下表保留为设计演进记录；§23 之前的字段语义仍然有效。

条带是单行栏，槽位语义随目标切换（不新增控件、不隐藏任何按钮）：

| 槽 | 直线语义 | 圆语义 | 写回 |
|---|---|---|---|
| 长度 | 长度 | **半径 R** | `p0.distance` / `p0.distanceFormula`（半径唯一权威 D2） |
| 基准 | 基准角度（只读） | **基准角 a₀**（只读） | —— 显示 `p0.angleFormula` 优先，否则 `block->circleStartAngleDeg(seg)` |
| 角度 | 终点角度 | **包角** | `p1.angle = a₀ + 包角` / 组合公式 `"(a₀Expr) + (包角Expr)"`（D19，**禁归一化**） |
| 徽标 | 桥线/曲线/自由 | **「圆」** | 判据 `seg->fitKind == FitKind::Circle` |

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/parametric/Block.{h,cpp}` | **新增圆几何访问器**（面板/条带共用权威）：`circleCenterPoint`、`circleRadiusMm`（解算后 `(sp−center).length()`，未解算回退 `sp->distance`）、`circleStartAngleDeg`、`circleSweepDeg`（角度差，`while (d<=1e-9) d+=360; while (d>360) d-=360;`，回退默认 360） |
| 2 | `src/ui/CircleGeometrySection.cpp` | 删掉 4 个匿名 helper（`centerOf`/`resolvedRadiusMm`/`resolvedStartAngleDeg`/`resolvedSweepDeg`）改为委托 Block 方法；文件 429 → 375 行 |
| 3 | `src/document/commands/SegmentPropertyCommands.{h,cpp}` | `SegmentEditBarCommand::State` 增 4 个起点字段（`startDistance`/`startDistanceFormula`/`startAngle`/`startAngleFormula`）；`applyEditStripState` 在 `setOwnerMeasureName` 后写回 `sp`；`State::captureFrom` 同构读取 |
| 4 | `src/app/ContextStrip.{h,cpp}` | 新增 `lengthLabelText()` / `angleLabelText()` 与成员 `m_lenLabel` / `m_angleLabel`；`buildUi()` 的 `addField` lambda 加末参 `ElaText** outLabel` |
| 5 | `src/app/ContextStripDisplay.cpp` | `refreshFields()` 加圆分支（半径 / a₀ / 包角）；`refreshChrome()` 徽标链插「圆」（**在 `isBridge` 之后、`isCurve()` 之前**）、标签与 tooltip 按圆/线两分支都重写 |
| 6 | `src/app/ContextStripEdit.cpp` | `snapshotState()` 补 `sp` 四字段；`applyLength()` 圆分支写 `st.startDistance(+Formula)`；`applyAngle()` 圆分支写 `st.endAngle` 或组合公式 |
| 7 | `tests/test_context_strip.cpp` + `CMakeLists.txt` | 新增 `CircleRef`/`makeCircle` + 6 个 slot（显示、改半径、改包角、包角公式、半径公式、换回直线的标签复原）；`test_context_strip` 链接加 `gcad_tools` |

### 16.3 四条实现事实（写代码时踩出来的）

1. **圆段求交要两轮拟合才稳**。`resolveUnresolved()` 先于 `applyCircleFitTangents()`
   运行，第一轮交点用的是**未拟合**的 `spans`；且已 resolved 的点不会重解。改法：
   `Block::resolve()` 里 `for (int fitPass = 0;; ++fitPass) { resolveUnresolved → 闭合 →
   const bool fitMoved = applyCircleFitTangents(); if (!fitMoved || fitPass >= 2) break;
   for (auto& pt : points) pt.resolved = false; }`——**最后一轮也必须拟合**，别写成
   `if (fitPass >= 2 || !applyCircleFitTangents())`（短路会把该做的拟合跳过）。
2. **射线穿闭合圆有两次命中，`hits[0]` 不一定是最近的**。`geo::rayCurveIntersect`
   按**全局 span 参数**排序，闭合圆被穿过时 span 2（−r,0）排在 span 0（+r,0）之前
   → 调用方拿到远侧交点。所以按「沿射线投影距离」重挑，而不是改 `rayCurveIntersect`
   的排序契约（它的调用方依赖参数序）。
3. **`Segment::isCurve()` 对圆段为 true**（= `type == SegmentType::Bezier && !passPointIds.empty()`；
   `CircleFactory` 把 3 个象限锚压进了 `passPointIds`）。所以徽标链只判 `isCurve()` 会显示
   「曲线」——必须在它**之前**插 `fitKind == Circle`。
4. **公式模式下 `p1.angle` 数值字段刻意不落笔**。`BlockResolve.cpp:251-255` 只把
   `angleFormula` 求值结果用于算 `resolvedPos`，**不回写 `pt.angle`**——这是既有约定
   （面板 `applyToModel` 同样如此）。因此条带 `applyAngle()` 的公式分支只写
   `st.endAngleFormula`，不覆盖 `st.endAngle`；测试判据也必须断言**解算后的几何**
   （`block->circleSweepDeg(seg) ≈ 180`）或公式串，不能断言 `ep->angle`。

**取舍记录**：包角 `90/180/270/360` 预设只在属性面板（M2 已有）；条带单行无空间，
仅保留输入框。基准角在条带沿用直线惯例只读（改它走面板）。

### 16.4 M3 闭环判据

- `tests/test_context_strip.cpp` 36 slot 全绿（含 6 个圆 slot）；`test_circle_edit` /
  `test_circle_fit` / `test_intersection*` / `test_curve*` / `test_resolver_*` /
  `test_serializer` / `test_migration` / `test_dialog_tabs_*` 全绿。
- 八守卫脚本（`check_layering` / `check_hardcoded_colors` / `check_test_fixtures` /
  `check_file_size` / `check_header_classification` / `check_bool_flags` /
  `check_test_split` / `check_inline_units`）全绿。
- `tools\build.bat all` + 串行 `tools\test.bat all` 全绿。

---

## 17. 第三步补遗：解除圆约束（D14）

对应决策 D14（§2.2 / §4.4）、交互矩阵 §12 末行、命令清单 §13、测试计划 6d。

### 17.1 语义

一条命令把 `FitKind::Circle` 段冻结成**普通曲线**，形状逐点不变：

1. `doc->resolveAll()` 先跑（`freePos` 要写**当前**解算位置，拟合切向也只有解算过才是当前值）。
2. 本段 `circlePointIds(seg)` = 起点 + `passPointIds` + 终点，逐个 `freezePoint()`：
   `if (pt.resolved) pt.freePos = pt.resolvedPos;` → `constraint = Free` →
   清 Polar 专属字段（`refPointId` / `refSegmentId` / `distance = 0` / `angle = 0` /
   `distanceFormula` / `angleFormula`）。
3. `seg->fitKind = FitKind::None` → `doc->touchAndResolve(blockId)`。

此后段仍是 `SegmentType::Bezier` + 3 个 `passPointIds`，所以 `isCurve()` 仍为 true，
锚点 Tab / 曲线编辑 / 附着定位全部照常工作；`role` 保持 `Auxiliary`。

### 17.2 与 D14 原文的一处有意偏离：**不置 `autoTangent = true`**

D14 原文要求「`autoTangent = true`（回到 Hobby 自动切向）」，但 §11 测试 6d 要求
「解除瞬间形状冻结（前后采样点一致）」——两者不可兼得，取 6d。

依据：`Block::applyCircleFitTangents()`（`BlockResolve.cpp:516-596`）把拟合切向写入
`tangentOut` / `tangentIn` 并置 `autoTangent = false`；`BlockCurve.cpp:73-84` 收集三者交给
`CurveMath.cpp:435-470`：**任一 auto → `solveC2Tangents` 重算**，否则用存储值。因此置
`autoTangent = true` 会让形状在解除瞬间跳变。实现改为**保持 `autoTangent` / `tangentIn` /
`tangentOut` / `tangentLocked` 原值**（拟合切向本来就是当前形状的切向），
理由写在 `CircleCommands.h` 头注释里。

### 17.3 改动表

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/document/CommandTexts.h` | 新增 `inline const QString kDetachCircle = QStringLiteral("解除圆约束");`（紧跟 `kToLine`） |
| 2 | **新文件** `src/document/commands/CircleCommands.{h,cpp}` | `detachCircleInPlace()` + `DetachCircleCommand`。为什么新文件：`CurveCommands.cpp` 已 339 行、`commands/` 阈值 400 行，加进来会越线 |
| 3 | `CMakeLists.txt` | `CircleCommands.h/.cpp` 进 `gcad_document`；新增 `test_circle_detach` 目标 |
| 4 | `src/ui/CircleGeometrySection.{h,cpp}` | 新增「解除圆约束」按钮（`detachButton()` 访问器 + `detachRequested()` 信号 + `onDetach()`）；有 undoStack 走命令，否则走 `detachCircleInPlace()` |
| 5 | `src/ui/LinePropertyDialog.cpp` | 接 `detachRequested` → `populateFromModel()`：`fitKind` 变 `None` 后圆区隐藏、`LineGeometrySection` 与角度卡恢复 |
| 6 | `tests/test_circle_detach.cpp` + `tests/test_circle_edit.cpp` | 新测试 4 slot；`test_circle_edit` 补 `detachButtonDetachesAndSignals()`（按钮 → 信号 1 次 + 恰好压 1 条命令 + undo 回圆） |

### 17.4 四条实现事实

1. **圆心点不动也不删**。它本来就是 `Free`（`freePos = (0,0)` 局部），块内可能有其它段引用它；
   命令只冻结本段的起点 / 终点 / 象限锚。
2. **象限锚的 `hostSegmentId` / `interpPercent` 不清**。`ToolCurveEditAnchor.cpp:289`
   `deleteCurvePoint()` 靠 `pt->hostSegmentId` 找宿主段，清掉会让「删除曲线点」失效；
   而 `BlockQuery.cpp:104-105` 的 CurveAnchor 弧长分支按 `constraint` 判断，
   转 `Free` 后自然不再走。
3. **清 Polar 专属字段**（`refPointId` / `refSegmentId` / `distance` / `angle` / 两个公式）。
   `Free` 下这些字段全部惰性，留着会误导 UI 与后续序列化。undo 用完整点快照还原，
   所以清掉不会丢信息。
4. **自由拖动已具备**，无需新代码：`CurveAnchorDragSession.cpp:32-45` 的 `hitAt()` 只跳过
   `constraint == CurveAnchor` 的分段锚，转 `Free` 后即可拾取，`:71-72` 写 `freePos`
   并保持 `constraint = Free`。

### 17.5 闭环判据（测试 6d）

- `detachFreezesShapeAndConstraints`：整圆 r=30 / a₀=25 解除前后采样
  `4*(kSamplesPerSpan+1)` 点，`maxDelta < 1e-9`；`fitKind = None`、`type` 仍 Bezier、
  `passPointIds` 仍 3 个；端点/锚 `constraint = Free` 且 `freePos ≈ resolvedPos`；
  `refPointId.isNull()`、公式空；圆心不动不删。
- `detachKeepsTangentsAndMakesAnchorsDraggable`：解除前 `!autoTangent` 且
  `tangentOut.length() > 0`；解除后 `tangentOut` 差 `< 1e-12`、`autoTangent` 仍 false；
  改 `freePos` + `touchAndResolve` 后 `resolvedPos ≈ target`、`maxDelta > 1.0`。
- `undoRestoresCircleExactly`：还原 Polar / `refPointId` / `distance` / `angle`，
  `ep->angle == 450.0`（**不归一化**）、锚回 `CurveAnchor`、形状差 `< 1e-9`、redo 可再解除。
- `detachOnPlainCurveIsNoOp`：对普通曲线重复解除安全 no-op。
- `test_circle_edit::detachButtonDetachesAndSignals`：按钮点击 → 信号 1 次 +
  `undoStack` 恰好 +1 条命令 → `fitKind = None` → undo 回 `Circle` / `Polar`。

---

## 18. 曲线编辑对圆段置灰（D8，已实施 2026-12）

### 18.1 语义

圆段的点全部由 `Block::applyCircleFitTangents()` 在每次 resolve 时重写 —— 拖锚 / 删锚 /
加点都会当场破圆，而圆形的唯一权威入口是「解除圆约束」（D14）后再自由编辑。因此
ToolCurveEdit 对 `Segment::fitKind == FitKind::Circle` 的段整体置灰，并 toast 提示
`ui::str::kCircleCurveLockedHint`（"圆段不可编辑曲线点，请先「解除圆约束」"）。

范围只含 ToolCurveEdit。ToolSelect 的 `CurveAnchorDragSession`
（`src/tools/CurveAnchorDragSession.cpp:32-45`）只遍历 `passPointIds` 且跳过
`constraint == CurveAnchor` 的点 —— 圆段的 3 个象限锚正是 CurveAnchor，本就不可拾取；
两端点为 Polar，全仓仅 `CurveAnchorDragSession` 一个拖拽会话（`src/tools` grep
`DragSession`）→ ToolSelect 不会破坏圆，无需改动。

### 18.2 七处守卫

| 手势 | 入口 | 守卫 |
|---|---|---|
| 拖锚 / Shift 删锚 / 显示切向手柄 | `src/tools/ToolCurveEdit.cpp::mousePress` 步骤 2（点捕捉分支） | `belongsToCircleSegment(*blk, *pt)` → `hideCurvePointPreview()` + `hideHandles()` + `notifyCircleLocked()` + return |
| Ctrl+click 加点 | `src/tools/ToolCurveEdit.cpp::mousePress` 步骤 3（段捕捉分支） | `seg->fitKind == Circle` → 隐藏预览 + toast，不调 `placeCurvePoint` |
| Ctrl 悬停预览点 | `src/tools/ToolCurveEditAnchor.cpp::updateCurvePointPreview` | 圆段 → `hideCurvePointPreview()` |
| 加点（底层） | `src/tools/ToolCurveEditAnchor.cpp::placeCurvePoint` | 圆段 → 返回空 QUuid |
| 拖锚（底层） | `src/tools/ToolCurveEditAnchor.cpp::startAnchorDrag` | `belongsToCircleSegment` → return |
| 删锚（底层，含 Delete 键路径） | `src/tools/ToolCurveEditAnchor.cpp::deleteCurvePoint` | `belongsToCircleSegment` → return |
| 切向手柄 | `src/tools/ToolCurveEditHandles.cpp::showHandles` / `updateHandleGraphics` | 圆段 → `hideHandles()` |

`belongsToCircleSegment(const cad::param::Block&, const cad::param::ParamPoint&)`
（`ToolCurveEdit.h` 私有静态，实现在 `ToolCurveEditAnchor.cpp`）：遍历块内段，命中
`fitKind == Circle` 且点 id ∈ {`startPointId`, `endPointId`, `passPointIds`} 即 true。
底层函数各自重复守卫（不只依赖 mousePress），因为 `placeCurvePoint` /
`deleteCurvePoint` 也可由按键等其它路径进入。

### 18.3 闭环判据（`tests/test_curve_edit.cpp::circleSegmentsAreLocked`）

r=40 / a₀=0 整圆（象限锚 90°/180°/270°，45° 圆身处距最近点 30.6mm > 12px 吸附半径），
真实事件流：
- 拖 90° 象限锚 → `interpPercent` 与 `resolvedPos` 不变、`undoStack.count()` 不变；
- Shift+点象限锚 → 点仍在、`passPointIds.size() == 3`、命令数不变；
- Ctrl+点 45° 圆身 → 仍 3 个锚、命令数不变；
- 末尾 `circleRadiusMm ≈ 40`、`circleSweepDeg ≈ 360`、`tension == 0`。

改形路径由 D14 覆盖（§17），两者合起来构成「要么是圆、要么是自由曲线」的二态闭环。

---

## 19. 圆段打断（D7，已实施 2026-12）

### 19.1 语义

圆段（整圆或圆弧）在参数 t 处打断 ⇒ 一分为二，**两段都保持 `fitKind = Circle`**：

- 共享分割点：世界位置 = `Polar(圆心, r, a₀ + 包角·t)`，前段终点与后段起点是**两个不同块的
  两个点**（位置相同），后段块只平移（`transform.rotation` 与原块相同 ⇒ `rotToLocal = 0`）。
- 两段共用圆心、各自持半径（初始都 = r，公式半径一起复制）、包角互补：raw 域两段之和 ==
  原 raw 包角（`ep->angle − sp->angle`，不归一化）。
- 每段仍按 D20 保持 3 个象限锚（前段按新包角重新四等分，后段 0.25/0.5/0.75）。

### 19.2 为什么必须走角度分割分支

两条既有守卫会把整圆打断直接卡死：

1. `gatherBreakGeometry`（`src/document/commands/BreakAnalysis.cpp`）以
   `st.segLenMm < kGeomEps` 判退化段 —— 整圆两端点重合 ⇒ 弦长 0 ⇒ 必然拒绝（与 M3-a
   求交同源的退化守卫，§16.1 第 2 行已在解算侧豁免圆）。
2. `evaluateBreakPosition`（`BreakEvaluate.cpp`）随后用 `localDir / st.segLenMm` 定位
   断点 ⇒ 除零。

且圆上断点的位置只能用**圆周角**描述，弦长参数化对整圆无意义。因此 `BreakState` 加
`isCircle` 标记 + 一组 `circle*` 字段，流水线四个阶段各加**显式圆分支**，并把
`st.isCurve` 一律置 false 以绕开全部 Hobby / 弦长路径（与 §16.3 的圆解算分支同策略）。

### 19.3 改动表

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/document/commands/BreakState.h` | 尾部追加 `bool isCircle = false; QUuid circleCenterId; double circleRadiusMm; QString circleRadiusFormula; double circleSplitAngleDeg; double circleFrontSweepDeg; double circleBackSweepGeomDeg; double circleBackSweepRawDeg; std::vector<QUuid> circleAnchorIds; QHash<QUuid,double> circleAnchorOffsetDeg; QHash<QUuid,double> circleAuxOffsetDeg;` |
| 2 | `src/document/commands/BreakAnalysis.cpp::gatherBreakGeometry` | 端点 resolved 检查之后插入圆分支：半径 = `v0.length()`（< `kGeomEps` 拒绝）、几何角 `a0Geom = atan2(v0)`、`sweepGeom = normalizeRad(atan2(v1) − a0Geom)`（≤eps 则 +2π）、`frontSweep = normalizeRad(atan2(vb) − a0Geom)`（<0 则 +2π）、**`1e-6 < frontSweep < sweepGeom − 1e-6` 才允许**、raw 包角 `sweepRaw = ep->angle − sp->angle`（≤1e-9 则 +360）、`circleBackSweepRawDeg = sweepRaw·(sweepGeom−frontSweep)/sweepGeom`、`mode = BreakMode::Freeze`、`refDeltaRad = 0`、登记锚点/辅助点的角向偏移并排序 |
| 3 | `src/document/commands/BreakEvaluate.cpp::evaluateBreakPosition` | 顶部圆早退（前/后段距离 = r、公式清空、`breakWorld = transform.toWorld(auxPt->resolvedPos)`），**必须早于 `localDir / st.segLenMm`** |
| 4 | `src/document/commands/BreakEvaluate.cpp::redistributeAuxPoints` | 顶部圆分支：按 `circleAuxOffsetDeg` 与前包角比较分配前/后段，后段点复制并按 `(off − frontSweep)/circleBackSweepGeomDeg` 折算 `interpPercent`（清公式、`interpFromEnd = false`） |
| 5 | `src/document/commands/BreakExecution.cpp::modifyFrontBlock` | isCircle 优先分支：断点转绕圆心 Polar（`refPointId = circleCenterId`、`distance = r`、`angle = circleSplitAngleDeg`）；`lengthFormula` 清空且**不发布长度变量**；isCurve 块之后按 `frontSweep·{0.25,0.5,0.75}` 重排前段 3 锚（就近复用未占用原锚，不足新建 CurveAnchor），最后 `passPointIds = anchorIds` + `touchGeometry()` |
| 6 | `src/document/commands/BreakFinish.cpp` | 新增匿名命名空间 `buildCircleBackBlock(...)`：后段块 origin = `st.breakWorld`、rotation 复制；圆心 Free 于 local 域；起点 Polar（`angle = radToDeg(atan2(toSplit))`）、终点 Polar（`+ circleBackSweepRawDeg`）、`fitKind = Circle`、3 个 CurveAnchor；`buildBackBlock` 开头 `if (st.isCircle) return buildCircleBackBlock(...)` |
| 7 | `CMakeLists.txt` | 注册 `test_break_circle`（`tests/test_break_circle.cpp` + `gcad_document gcad_parametric gcad_tools Qt6::Core/Gui/Test`） |

### 19.4 五条实现事实（写圆打断代码必读）

1. **分割角写几何值**：`circleSplitAngleDeg = radToDeg(atan2(vb))`，不是 raw 域值。圆分支
   解算用 `a0Rad = atan2(v0)` 作起点角、`sweepDeg = ep->angle − sp->angle` 作包角 ——
   只依赖两者之差，所以基准角非 0 或跨越 ±180° 由 `+360` 规则自动回绕
   （`circleBreakOnRotatedBaseAngleKeepsRadius` 覆盖）。
2. **半径权威 = 起点 Polar 的 `distance`/`distanceFormula`**（D2）：前段起点不变 ⇒ 自动继承；
   后段两端点都写 `distance = r` 并复制 `distanceFormula`，随后 `syncCircleFitRadius()`
   把起点镜像到终点 ⇒ 公式半径继续联动。
3. **`refDeltaRad = 0`**：圆上切向连续，断点处的附件（followerAngle / 弧长 / 弦长）无需角度
   补偿；后段 `transform.rotation` 复制原块（`rotToLocal = 0`）。
4. **前段锚点复用是就近匹配**（目标 `frontSweep·{0.25,0.5,0.75}`，在未占用的原锚中取角向
   偏移最接近者），不保证原锚留在原半段 —— 锚点位置完全由 `interpPercent` 决定、切向由
   `applyCircleFitTangents()` 重算，形状不受影响。
5. **拒绝路径是静默空操作**：断点落在端点或绘制弧之外（例如与整圆求交得到、但位于所绘圆弧
   之外的交点）时 `gatherBreakGeometry` 返回 false，`redo()` 直接 return。注意
   `BreakSegmentCommand::isValid()`（`src/document/commands/BreakCommands.cpp:29-31`）
   只反映 ctor 里的 `canBreak`，**不反映 gather 失败** ⇒ 测试要断言「redo 后文档不变」，
   不能断言 `!isValid()`。

### 19.5 闭环判据（`tests/test_break_circle.cpp`，6 slot）

- `circleBreakAtAnchorProducesTwoCircleHalves`：r=50 断在 50% 锚（180°）⇒ 2 块、2 个
  Circle 段、各 180°、半径 50、径向偏差 < 0.001·r（0.05mm）、包角和 360。
- `circleBreakAtInterpolatedSplitsByAngle`：r=40 在 t=0.3 处断 ⇒ 108°/252°（容差 0.2°）。
- `circleBreakOnRotatedBaseAngleKeepsRadius`：r=20 / a₀=30° 断在 50% 锚 ⇒ 各 180°、
  径向偏差 < 0.001·r（验证 atan2 回绕）。
- `partialArcBreakComplementsSweep`：整圆改 `ep->angle = 120` 后断 ⇒ 各 60°、和 120°。
- `circleBreakUndoRestoresSingleCircle`：undo 后 1 块、`fitKind = Circle`、包角 360、半径 50、
  3 锚 `interpPercent` 0.25/0.5/0.75、`ep->angle − sp->angle == 360`（不归一化）。
- `circleBreakAtSeamIsRejected`：断在接缝（t=0）⇒ redo 后文档不变（仍 1 块、包角 360）。

---

## 20. 第三步补遗：圆段作角度基准（D9，已实施 2026-12）

### 20.1 语义

连接 / 跟随的**角度基准方向**对圆宿主同样是良定义的，不需要禁止：

- **整圆宿主**：两端点位置重合 ⇒ 母线弦长为 0 ⇒ 基准 = **接缝（起点 p0）的出口切向**，
  与块 `transform.rotation` 无关；
- **弧宿主**：两端点不重合 ⇒ 基准 = **母线弦**（`start → end` 世界向量），与直线宿主同一公式；
- **直线宿主**：`exitDirectionAtPoint` 本身就返回弦方向，弦覆盖取同一值 ⇒ **存量行为逐位不变**。

「连接不改变几何」对圆宿主同样成立：反算 `followerAngle` 与解析器
（`src/parametric/ResolverAttachment.cpp:37`）读的是**同一个** `effectiveAngleRefWorld`，
首次 `resolveAll()` 不再跳线。圆块旋转时基准（接缝切向）随块旋转 ⇒ 跟随线保持相对角
（切线跟随能力保留）。

### 20.2 与 D9 原文的一处有意偏离：不禁止，改为修正基准方向

D9 初稿写的是「**禁止**圆端点作角度跟随基准」，理由是「`directionAtPoint` 无曲线分支 ⇒
整圆弦长为 0 ⇒ 垃圾基准」。实施改为「**修正基准方向**」，三条理由：

1. **前提只对 `directionAtPoint` 成立**：解析器与全仓绝大多数消费方走的是
   `exitDirectionAtPoint`，它有**曲线端点切向分支**（`src/parametric/BlockQuery.cpp:57-77`
   扫描重载、`:158-163` 偏好段重载）：终点取末跨 t=1 切向、起点取首跨 t=0 切向取反 ⇒
   整圆得到接缝切向，是有意义的基准，不是垃圾值。
2. **只剩一个漏网分支**：按 D9 决策行的核查，全仓 9 处 refWorld 计算里 8 处早已走
   `exitDirectionAtPoint`，只剩拖动连接的线级分支（`src/tools/ConnectGestureAttach.cpp`）
   还在手写「块 rotation + 弦覆盖」——它恰恰是唯一没有切向基的那一处（即审计台账
   TOOL-P1-17，`docs/archive/2026-09/ledger_7.8_tool.md:28`）。禁止圆基准等于把「跟随线
   沿切线」这一有用能力一起砍掉，而修好这一处即可与解析器同源。
3. **零新分支**：弦覆盖自带 `distanceTo > kGeomEpsLoose` 守卫
   （`src/parametric/ParamDocumentAttachments.cpp:647`）——整圆距离 ≈ 0 自动跳过覆盖 ⇒
   切向基准自然保留；弧不跳过 ⇒ 弦基准自然生效。圆 / 弧 / 直线的三分行为全部由既有守卫
   导出，无新增判断。

### 20.3 改动表

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/tools/ConnectGestureAttach.cpp:111-117` | 删手写「`toBlk->transform.rotation` + 母线弦覆盖」旧公式，改 `const double refWorld = cad::param::effectiveAngleRefWorld(m_paramDoc, att);`（此刻 `att.angleRef*` 为空 ⇒ 等价于「宿主出口切向 + 母线弦覆盖」） |
| 2 | `tests/test_circle_attach.cpp` | **新增** 4 slot：整圆基准 = 接缝切向且不跳线 / 旋转圆块跟随线随转 / 弧宿主基准 = 弦 / 直线宿主存量行为不变 |
| 3 | `CMakeLists.txt:744-749` | 注册 `test_circle_attach`（源 + `${GCAD_SRC}` + `gcad_document gcad_parametric gcad_tools gcad_canvas Qt6::Core/Gui/Test`） |

### 20.4 三条实现事实

1. **基准的基是 `exitDirectionAtPoint`，不是 `directionAtPoint`**（`src/parametric/FollowerAngle.h:131`
   声明 / `src/parametric/ParamDocumentAttachments.cpp:631-651` 实现）——前者有曲线切向分支、
   后者只有弦；写消费方代码时别退回 `directionAtPoint`。
2. **整圆态 p1 隐藏不影响基准**：基准只取 `att.toPointId`（= p0 接缝），p1 的
   `visible/selectable = false` 与角度基准无关。
3. **测试不能直接用 `directionAtPoint` 断言世界方向**：它返回的是局部弦方向，块旋转时不变
   —— 断言「连接不改变几何」必须看 `transform.rotation + directionAtPoint()` 的世界方向
   （`tests/test_circle_attach.cpp:90-95` 的 `worldDirDeg`）。

### 20.5 闭环判据（`tests/test_circle_attach.cpp`，4 slot，实测 1/1 Passed 0.19s）

- `fullCircleLeaderKeepsFollowerGeometry`：r=40、接缝 (240,0)，拖动连接后 ①位置吸附在圆周上
  ②`effectiveAngleRefWorld` ≠ 块 rotation（差 > 1°，锁死旧公式的退化）③反算角与
  `backSolveFollowerAngle(rotation, directionAtPoint, refWorld)` 同源（< 1e-9）④跟随线世界
  方向不变（旧公式在此跳 90°）。
- `fullCircleRotationRotatesFollower`：圆块旋转 30° ⇒ 位置仍钉在旋转后的接缝（< 1e-6 mm）、
  跟随线世界方向同步转 30°（< 1e-6°）。
- `partialArcLeaderUsesChord`：整圆改 `ep->angle = 120` 的弧宿主 ⇒ 基准 == 两端点弦方向
  （< 1e-6°）且连接后不跳线。
- `straightLineLeaderStillUsesChord`：竖直宿主（rotation = 90°）⇒ 基准 == 90°（< 1e-6°），
  存量直线行为不变。

---

## 21. 第四步补遗：两点直径模式（D5，已实施 2026-12）

### 21.1 交互

圆心模式与直径模式共用 `O` 工具，**W 键切换**（工具模式切换统一 W 键、禁 Tab）。两种模式的
提交时机**不同**，这是本设计唯一的实现难点：

| 模式 | 手势 | 提交时机 | 预览 |
|---|---|---|---|
| 圆心 + 半径（默认） | 按下定圆心 → 拖动 → **松开**提交 | `mouseRelease` | 半径圆 + 圆心点 |
| 两点直径 | 按下定第一点 → 移动 → **第二次按下**定第二点提交 | `mousePress`（第二次） | 直径圆 + 圆心点 + 两点连线 |

- 直径 = 两击点距离、圆心 = 两击点中点、半径 = 直径 / 2；直径 < 2·`kMinCircleRadiusMm`
  （0.5 mm）时**不建块**（防退化成点）。
- 切模式**强制重置手势**（`resetToIdle()`）：跨模式续命会把刚落的圆心当成直径首点。
- Esc 取消进行中的手势（两种模式一致）；右键退出工具走既有 `requestToolSwitch(Select)`。
- 捕捉沿用圆心模式的 `SnapEngine::findSnap`（§5.3），两种模式零差异。

### 21.2 改动表

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/tools/ToolCircle.h` | namespace 级 `enum class CircleMode { CenterRadius, TwoPointDiameter }`；私有 `enum class Session { Idle, Dragging, Armed }`（直径模式第二次**按下**即提交，故 `Armed` ≠ `Dragging`）；`mode()` 只读访问器 + 静态 `modeIndicatorFor(CircleMode, bool gestureActive)` |
| 2 | `src/tools/ToolCircle.cpp` | `mousePress` 直径模式 Idle → 记 `m_anchor` + `Armed`，否则取距离建块；`mouseRelease` 仅圆心模式提交（`m_session != Session::Dragging` 直接 return）；`keyPress` 先拦 `Qt::Key_W` → `toggleMode()`；`updatePreview` 按模式算圆心/半径，直径模式额外画 anchor→cursor 实线 |
| 3 | `tests/test_circle_tool.cpp` | **新增** 8 slot（见 21.4），`CMakeLists.txt` 注册为 ctest #30 |

### 21.3 三条实现事实

1. **`ModeIndicator` 是模式文案唯一出处**：`modeIndicatorFor(mode, gestureActive)` 出
   `modeName`（「圆心」/「直径」）/`detail`/`wAction`（「W 切直径」/「W 切圆心」）/`toast`
   /`isDefault`，`describe()` 的 `hintText` 取 `modeIndicatorFor(CenterRadius, false).hint("画圆")`
   ——静态与运行期同源，`test_tool_hints.cpp` 的 startsWith 断言照旧过。
2. **无头场景下 `modeBadgeText()` 返回空**（`src/canvas/CanvasScene.cpp:362`：`views().isEmpty()`
   时无角标可锚定）。测试**不能**用角标断言模式，必须断言 `ToolHost::setHintOverride` 收到的
   L1 状态栏文案（含 `[直径]` + 「W 切圆心」）；角标路径由 `tests/test_mode_indicator.cpp`
   （带 `QGraphicsView`）覆盖。
3. **文案与 §5.2 的示例措辞不完全一致**：落地文案是「画圆: 半径 %1 cm，松开完成 (Esc 取消)」
   与「画圆[直径]: 直径 %1 cm，点选第二点完成 (Esc 取消)」——§5.2 是设计期草案措辞，
   以 `ToolCircle.cpp` 为准。

### 21.4 闭环判据（`tests/test_circle_tool.cpp`，8 slot，实测 1/1 Passed）

- `centerRadiusDragCreatesCircle`：圆心模式 press(0,0) + move(30,40) + release ⇒ r = 50、
  圆心 (0,0)、接缝 (50,0)、undo +1；
- `bareClickCreatesNoCircle`：只按不拖 ⇒ 不建块；
- `staticHintTextMatchesRuntimeDefault`：静态 `describe().hintText` 与运行期默认态同源；
- `wTogglesDiameterMode`：W 切换后 L1 文案含「[直径]」+「W 切圆心」；
- `twoPointDiameterCreatesCircle`：A(10,10)、B(10,70) ⇒ 圆心 (10,40)、r = 30、undo +1；
- `diameterSamePointCreatesNothing`：同点两击 ⇒ 不建块；
- `wToggleCancelsInFlightGesture`：直径模式第一击后按 W ⇒ 手势取消且不建块；
- `escapeCancelsInFlightGesture`：Esc 取消进行中手势，两种模式均无 undo 残留。

---

## 22. 第四步补遗：周长发布为公式变量（D15，已实施 2026-12）

### 22.1 语义

圆段的「弧长（外长）」行 = `block.segmentBaseLength(seg.id)`；圆度 0 时它就是**周长**
（整圆 2πr、半圆 πr、一般弧 r·θ）。面板新增「发布参数」按钮把它发布成只读 `LinkedVariable`
（`refName = "L" + serial`、`name = 段名/序号 + "长"`），进参数表 **cm 域**，任何
`lengthFormula` 都能引用——**零新机制**（复用 `LineGeometrySection` 的发布通路与
`LinkedVariable::fromSegment`）。

### 22.2 一处必须修的精度缺口：弧长解析化

`segmentBaseLength` 的曲线分支原本返回 `entry->arcLengthMm`（`src/parametric/BlockCurve.cpp:168`，
Gauss-Legendre 对 4 跨三次 Bézier 的 `|B'(t)|` 数值积分）。而 κ = (4/3)·tan(π/8)
= 0.5522847498307936 的 4 跨拟合圆**不是**真圆：整圆数值弧长 = 6.284066792296·r，
2πr = 6.283185307180·r，相对偏差 **+1.4029e-4**（r = 100 mm 差 0.088 mm）。后果两处，
都是用户可见的：

1. 面板「弧长」行显示 **62.84**、「周长」行显示 **62.83**（`Units::formatCm` 2 位小数 cm）
   ——同一个圆两个周长；
2. 发布的周长变量值 ≠ 2πr，违反验收判据 §11 6e「值 == 2πr（cm 域）」。

修法（`src/parametric/BlockExtend.cpp` 的 `segmentBaseLength` 曲线分支，判据与 D16 渲染一致）：

```cpp
if (seg->fitKind == FitKind::Circle && std::abs(seg->tension) <= 1e-12) {
    const double r = circleRadiusMm(*seg);
    if (r > cad::geo::kGeomEps)
        return geo::degToArcMm(circleSweepDeg(*seg), r);   // r·θ，整圆 = 2πr
}
// 否则回落 entry->arcLengthMm（数值弧长）
```

圆度 ≠ 0 的形状确实不再是圆（D17），继续用数值弧长。**不动** `LinkedVariable` /
`MeasurementStore`：每次 resolve 重算 `segmentEffectiveLength` 并
`publishParameter(refName, mmToCm(len))`，圆段自身编辑走 `skipAuxSource = false` 通路必然刷新。

### 22.3 改动表

| # | 文件 | 内容 |
|---|------|------|
| 1 | `src/parametric/BlockExtend.cpp` | `segmentBaseLength` 曲线分支加 circleFit + 圆度 0 的解析分支（含 `#include "geometry/Angle.h"`） |
| 2 | `src/ui/CircleGeometrySection.{h,cpp}` | 新增「发布参数」按钮（tooltip 说明整圆 = 2πR）+ `onPublishLength()`（guard `findLinkedBySource` → `resolveAll` → `LinkedVariable::fromSegment` → `AddLinkedCommand`/`addLinked` → `resolveAll` → 按钮「已发布」+ 禁用）；`publishButton()` 只读访问器供测试 |
| 3 | `tests/test_circle_edit.cpp` | **新增** 2 slot（见 22.4） |
| 4 | `src/document/commands/LinkedVariableCommands.h`（**新增**）+ `VariableCommands.h` + `src/ui/CircleGeometrySection.cpp` | 3 个 linked 命令类（`AddLinkedCommand`/`RemoveLinkedCommand`/`SetLinkedCommand`）从 `VariableCommands.h` 拆到子域头，`VariableCommands.h` 转 include 它；`CircleGeometrySection.cpp` 只 include 子域头。原因：`tools/check_header_classification.py` 对 `document/commands/` 下的头设「fan-in > 15 且命令类 > 5 ⇒ FAIL」门槛，`VariableCommands.h` 原 fan-in 恰好 15（含自身 .cpp），新增任一 includer 即红；拆出子域头既满足门槛又保留 `VariableCommands.h` 对所有既有 includer 的透明性 |

### 22.4 闭环判据（`tests/test_circle_edit.cpp`，2 slot，实测 Totals: 16 passed）

- `circleArcLengthIsAnalytic`：r = 100 整圆 `segmentBaseLength` 与 2πr 差 < 1e-9、
  `formatCm` == `"62.83"`（与「周长」行同一个数）；包角 180° 后 == πr（差 < 1e-9）；
  圆度 0.2 后回落数值弧长（有限、> 0、与 πr 差 > 1e-9）。
- `publishCircumferenceVariableIsExactAndReferenceable`：初态按钮文案「发布关联参数」+ 可用；
  点击后 undo +1、`findLinkedBySource` 命中、`value` 与 2πr（r = 50 mm）差 < 1e-9、`refName`
  以 `"L"` 开头；按钮变「已发布」且禁用、再点不增 undo；`FormulaVariable` 引用该 refName 后
  `valid` 且 `mmToCm(baseValue)` == 2πr/2（cm 域）。

### 22.5 二期（不变）

专属周长 kind + 面板卡片（§12）；本期刻意不新增字段，值域换算仍在程序侧。

---

## 23. 一期补充：圆专属底部条带（CircleStripBar，已实施 2026-12）

### 23.1 动机（用户拍板）

> 用户原话（2026-12）：「对一期的内容做一些补充和完善。如：圆应该有自己的底部状态栏，
> 不应该复用线段的状态」。

M3-b 的三槽复用（§16.2）是临时落地：长度槽借位显示半径、基准槽借位显示 a₀、角度槽借位
显示包角，徽标「圆」——语义拥挤，且包角预设放不进单行。现改为**独立子栏**，与
`src/app/PlacedPointStripBar` 同构（选中圆时 `m_segmentBar->hide()`，圆条带 `show()`）。
字段集由用户拍板：**圆徽标 + 编号 + 名称 + 半径 R + 直径 D + 周长 C**；范围只做圆条带。

### 23.2 字段与语义

| 槽 | 控件 | 语义 / 写回 |
|---|---|---|
| 圆 | `ElaText` 徽标 | 恒「圆」 |
| 编号 | `ElaText` 只读 | `Serial::tag(seg.serial)` |
| 名称 | 胶囊输入 | `seg.name`（一步 `SegmentEditBarCommand`） |
| 半径 R | 胶囊输入（cm / 公式） | **唯一权威** = `p0.distance` / `p0.distanceFormula` |
| 直径 D | 胶囊输入（cm / 公式） | 写入前 ÷2（数值）/ `"(%1)/2"`（公式）；显示 = 2R |
| 周长 C | 胶囊输入（cm / 公式） | 写入前 ÷2π（数值）/ `"(%1)/(%2)"`（公式，分母 17 位有效数字）；显示 = 2πR |

- 直径 / 周长是**派生量，不落模型**；换算口径与属性面板
  `src/ui/CircleGeometrySection.cpp:310-346` 逐字同源。
- 数值换算经 `Units::formatCm`（2 位小数去尾零）——无理换算（如 C = 10 cm）会有
  ≤ 0.05 mm 的显示量化误差，与面板一致。
- 半径是唯一落笔点：D/C 编辑先改写半径框文本再走同一条 `applyEdits()`，因此一次编辑
  只压**一步**撤销（仅真变化才 push，避免焦点离开压空步）。
- 包角与基准角仍在属性面板（条带单行无空间，§16.2 取舍不变）；弧长 / 弦长 / 发布参数
  在面板几何行（§22）。

### 23.3 交互

- 选中圆 → `ContextStrip::routeToCircleBar(blockId, segmentId, /*editable=*/true)`；
  悬停 → `editable = false`（四框全只读）。
- 条带内 Tab / Backtab 在四个输入框间循环；Enter 提交并交还画布焦点；Esc 解除锁定且
  **不落笔**（`m_suppressApply` 挡住焦点回落触发的 `editingFinished`）。
- 换选直线 → `leaveCircleBar()`：圆条带 `clearTarget()` + 隐藏，线段条带 `show()`。
  （修复：`setPinnedTarget` / `flushHover` 的非圆路径原先漏调，圆条带会残留且线段条带
  保持隐藏。）

### 23.4 改动表

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/app/CircleStripBar.{h,cpp}`（**新增**） | 独立子栏：六槽 + `eventFilter`（ShortcutOverride / Enter / Esc / Tab）+ 一步撤销提交 |
| 2 | `src/app/ContextStrip.{h,cpp}` | `circleBar()` 访问器 + `routeToCircleBar()` / `leaveCircleBar()`；`setPinnedTarget` / `flushHover` / `hideBar` / `clearPlacedPoint` / `setPlacedPointTarget` / `beginPlacePointSession` / `showStrokePreview` 接线；`inputHasFocus` / `setUndoStack` / `applyTheme` / 刷新信号委托 |
| 3 | `src/app/ContextStripDisplay.cpp` | 删除圆分支（半径槽 / 基准槽 / 包角槽 / 徽标「圆」/「半径:」「包角:」标签与圆 tooltip），标签恒「长度:」/「角度:」 |
| 4 | `src/app/ContextStripEdit.cpp` | 删除 `applyLength` / `applyAngle` 的圆分支（不可达死代码） |
| 5 | `CMakeLists.txt` | `gcad_app` 源列表 + `test_circle_strip` 注册 |
| 6 | `tests/test_context_strip.cpp` | 6 个圆 slot + `CircleRef` / `makeCircle` 助手迁出（1467 → 1277 行） |
| 7 | `tests/test_circle_strip.cpp`（**新增**） | 10 slot（见 23.5） |

### 23.5 闭环判据（`tests/test_circle_strip.cpp`，10 slot，实测 Totals: 11 passed）

- `pinnedCircleRoutesToCircleBarAndFillsSixFields`：50 mm 圆 → 徽标「圆」/ 编号 = `Serial::tag` /
  R `"5"` / D `"10"` / C `"31.42"`；四框可编辑。
- `hoverPreviewIsReadOnly`：悬停节流到期进 Hover → 四框只读、R `"5"`；`clearHover()` 后
  `hasTarget()` false。
- `radiusEditResizesCircle`：R 7.5 → `circleRadiusMm` 75、`sp->distance` 75、圆心不动、
  包角仍 360、D `"15"` / C `"47.12"`。
- `radiusFormulaWritesStartDistanceFormula`：`3+2` → `distanceFormula == "3+2"`、distance 50。
- `diameterEditBackCalculatesRadius`：D `"12"` → R 60 mm、半径框 `"6"`、undo count == 1。
- `circumferenceEditBackCalculatesRadius`：C `"10"` → 半径 = `formatCm(cmToMm(10)/(2π))` 回读。
- `nameEditApplies`：写入 `seg.name`。
- `escUnpinsWithoutWriting`：半径框输入 9 后 Esc → 解除锁定且半径仍 50。
- `switchingToLineLeavesCircleBar`：换选直线 → 圆条带退场、线段条带 R `"10"`。
- `undoRestoresPreviousRadius`：R 7.5 落笔 undo count 1 → undo 回 50 mm、半径框 `"5"`。

### 23.6 与 §16.2 的关系

§16.2 的三槽复用实现已被本节**取代**（相关代码已删除）；M3-b 的圆几何访问器
（`circleRadiusMm` / `circleSweepDeg` 等）与 §6.2 面板字段表不变。

---

## 24. 一期补充：圆绘制会话条带（已实施 2026-12）

### 24.1 动机

§5.5 描述的「预输入条」在代码中从未存在（`Tool::setPreInput` 全仓零生产调用者，仅测试引用），
故照**唯一活先例** `PlacePoint` 会话范式落地：`PlacedPointStripBar::beginSession` /
`Tool::placePoint* 转发组`（`src/tools/Tool.h:197-199`）。范围（用户经选项确认）：
**画圆过程中底部条带直接输半径 / 直径**，替代原来只有 hint 文字。

### 24.2 交互

| 阶段 | 条带 | 画布 |
|---|---|---|
| 圆心模式按下 | `beginSession()`：徽标「绘制」+ 半径框（唯一可编辑）+ 直径框（只读联动）；编号 / 名称 / 周长隐藏 | `Session::Dragging`，橡皮筋预览 |
| 拖动 | `updateSessionValues(r, locked=false)` 实时回显半径与 2r | 半径 = 光标距离 |
| 条带输入半径 | 数值或公式求值成功 → `sessionRadiusChanged(v, true)` → **锁定** chip 显示「已锁定」 | 半径锁定值，光标不再改半径 |
| Enter（画布 / 条带内） | `sessionCommitted()` | 有效半径 ≥ `kMinCircleRadiusMm` 则落圆 |
| 第二次点击 | — | 同样落圆（点击-点击画法） |
| Esc | `sessionCancelled()` | 会话关闭、不落圆 |
| 松开鼠标但半径 < 0.5 mm | 会话**保留**（等条带输入） | 不再 `resetToIdle` |

- 半径是唯一权威输入；直径只读联动 = 2R，条带内不提供直径写入（避免双权威）。
- 会话开始**不抢键盘焦点**（`beginSession()` 不 `setFocus`）：W 切模式 / Esc 取消 / Enter 提交
  仍由画布 `ToolCircle::keyPress` 收；用户点框才进入文本输入。
- 输入判读与放置点同口径：`cad::geo::parseNumberOrFormula`（cm 域，不装 `QDoubleValidator`）；
  数值或公式求值成功都算 locked=true，空文本回 locked=false，公式不 ok 不 emit。
- 条带回声不覆盖正在输入的文本：`updateSessionValues` 仅在 `!locked && !radiusEdit->hasFocus()`
  时写半径框（公式 `3+2` 不会被回声 `5` 改写），直径恒按有效半径联动。
- 输入框复用 `CircleStripBar` 既有 `eventFilter`：`ShortcutOverride` accept（防单字母快捷键抢焦）、
  Enter → `sessionCommitted()` + `returnFocusRequested()`、Esc → `sessionCancelled()` + 同上、
  Tab / Backtab 会话内聚焦半径框。

### 24.3 改动表

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/tools/Tool.h` | `ToolHost::setCircleSession(bool)`(:96) / `updateCircleSession(double,bool)`(:98)；`Tool::circleRadiusInput(double,bool)` / `circleCommitted()` / `circleCancelled()`(:211-212)；helper `reportCircleSession`(:279-281) / `reportCircleValues`(:284-286) |
| 2 | `src/tools/ToolManager.{h,cpp}` | 两个 `ToolHost` override → `emit circleSessionChanged/Updated`；`forwardCircleRadius/Commit/Cancel` → 活动工具 |
| 3 | `src/app/CircleStripBar.{h,cpp}` | 会话态：`beginSession/updateSessionValues/endSession/isSession/isRadiusLocked/lockChip` + 三信号；`onRadiusEdited`（`ConditionEngine::evaluate`）；锁定 chip（objectName `stripCircleLock`） |
| 4 | `src/app/ContextStrip.{h,cpp}` | `beginCircleSession/updateCircleSessionValues/endCircleSession` + 三透传信号；会话期隐藏线段条带；`leaveCircleBar` 收口为 `endCircleSession` |
| 5 | `src/app/MainWindow.cpp` | `ToolManager::circleSession*` ↔ `ContextStrip` ↔ `forwardCircle*` 双向接线（含 `updateEditBand`） |
| 6 | `src/tools/ToolCircle.{h,cpp}` | 会话状态机：`effectiveRadiusMm()` / `commitCurrentCircle()` / `m_radiusLocked` / `m_lockedRadiusMm` / `m_lastCursor` / `m_sessionReported`；`resetToIdle()` 统一关会话 |
| 7 | `tests/test_circle_tool.cpp` | `HostStub` 记录会话上报；新增 6 slot（见 24.4） |
| 8 | `tests/test_circle_strip.cpp` | 新增 5 slot（见 24.4） |

### 24.4 闭环判据（实测）

`tests/test_circle_tool.cpp`（ctest #30，Totals: 14 passed）：
`centerPressOpensSessionAndReportsRadius` / `stripRadiusLocksPreviewAndEnterCommits` /
`secondClickCommitsSession` / `stripCancelClosesSessionWithoutCircle` /
`stripCommitUsesLockedRadius` / `diameterModeIgnoresStripInput`；
`bareClickCreatesNoCircle` 更名为 `bareClickKeepsSessionOpen`（行为变更：裸点击后无圆但会话保持，
`modeIndicator().modeName == "拖半径"`、`sessionStarts == 1`，Esc 才 `sessionEnds == 1`）。

`tests/test_circle_strip.cpp`（ctest #31，Totals: 16 passed）：
`sessionShowsRadiusAndDiameterOnly` / `sessionValuesDriveDiameterAndLockChip` /
`sessionRadiusInputEmitsLockedValues` / `sessionEnterCommitsEscCancels` /
`endingSessionRestoresCircleFields`。

### 24.5 有意偏差

1. 转发签名用 `double radiusCm + bool locked`，非 §5.5 草案的 `const QString& text` ——
   对齐既有 `placePointDistInput(double, bool)` 先例，解析（数值 / 公式）留在条带侧。
2. 不新增 `circleDiameterInput`：会话内直径只读，直径写入只在创建后条带（§23）。
3. **仅圆心+半径模式开会话**：直径模式只有一个中间值（首点已定），条带输入无法决定圆，
   故直径模式仍纯点击。
4. §5.5 的「红边提示 `setProperty + polish`」本期未启用（非法输入走「不 emit / 保持上次值」，
   与放置点一致）；后续如需再补。

---

## 25. 一期补充：圆心→接缝半径基准与可见弧镜像修复（已实施 2026-12）

### 25.1 ① 动机

圆段作角度基准（D9 / §20）的基准方向，就是「圆心→接缝（起点 p0）」这条半径；
旋转接缝（§6.1）也是绕圆心转它。但画布上没有任何东西指出它 —— 用户看不出
`a₀` 到底指哪儿，也看不出「世界 0°」与圆的关系。故补一条**半径基准虚线 + 世界角标注**。

### 25.2 ① 交互与口径

| 项 | 口径 |
|---|---|
| 触发 | 悬停该曲线 **或** 该块被选中/锁定（`toolSelected`/`toolLocked`）；灰显层、隐藏曲线、非活动层不画 |
| 线 | 圆心→接缝 1.0 px 虚线（cosmetic，不随缩放变粗）+ 圆心小十字（±2.0） |
| 色 | `st.gizmoAccentColor`（#FB8C00，与旋转量角器同一强调色） |
| 标注 | 半径中点圆角徽标（`gizmoBadgeBg`/`gizmoBadgeFg`，与旋转度数徽标同语言），文本 = `a₀` 一位小数 + `°` |
| 角度口径 | `toDisplayDeg(startAngleDeg, AngleDisplayRole::WorldDirection)` → `[0,360)` |
| 世界角→场景 | `(cx + r·cos a₀, cy − r·sin a₀)`（y 取负，与 `TransientOverlay` 姿态虚线同一约定） |
| 包围盒 | `CurveItem::boundingRect()` 在 `guide.valid` 时并进圆心 —— 窄弧（如包角 10°）的折线包围盒不含圆心，不并进来基准线就画到盒外 |
| 不画 | 非圆曲线、圆度 ≠ 0 的变形圆、退化半径（`rMm ≤ kGeomEps`） |

> 修订（§26，2026-12）：`startAngleDeg` 已换成 `worldAngleDeg`（= `a₀` + 块旋转），
> 接缝坐标改由 `guide.seam` 直接给出（不再用角度反算），标注也从「半径中点圆角
> 徽标」改为「沿虚线挂载的文字」——下表为当时口径，最新口径见 §26.3。

### 25.3 ① 改动表

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/canvas/CurveItem.h` | `Data` 末尾嵌套 `struct CircleGuide { bool valid=false; QPointF center; double radius=0.0; double startAngleDeg=0.0; }` + 成员 `guide`；访问器 `circleGuide()`；前置声明 `class CanvasStyle;`；私有 `drawCircleGuide(QPainter*, const CanvasStyle&) const` |
| 2 | `src/canvas/CurveItem.cpp` | `boundingRect()` 并入圆心；`paint()` 在方向箭头后按触发条件调 `drawCircleGuide()`；`drawCircleGuide()` 画虚线 + 圆心十字 + 度数徽标（`<QFontMetricsF>`、`geometry/Angle.h`） |
| 3 | `src/canvas/BlockGeometryCache.cpp` | 解析圆分支内 `const bool analyticCircle = rMm > cad::geo::kGeomEps && std::abs(seg.tension) <= 1e-12;`，成立则填 `guide.{valid,center,radius,startAngleDeg}`（与 `paintPath` 同源 → 基准方向恒等于可见弧起点方向）；`CurveItem::Data{...}` 末尾追加 `, guide` |
| 4 | `tests/test_circle_edit.cpp` | 新增 `circleGuideFollowsSeamAndSweep`（见 25.5） |

### 25.4 ③ 排查与修复：可见弧被镜像到 X 轴另一侧

用户报告三条症状（2026-12）：①「粉色点在线段之外」（曲线锚点看着不在弧上）；
②「只有粉色点才能打开面板」（点弧身没反应）；③「拖动圆留下残影」（显示刷新不对）。

**根因（单一缺陷）**：`src/canvas/BlockGeometryCache.cpp` 解析圆的绘制路径把**世界角取负**：

```cpp
paintPath.arcMoveTo(box, -a0Deg);
paintPath.arcTo(box, -a0Deg, -sweepDeg);   // ← 双重翻转
```

Qt 的弧角约定（`arcTo` 正角 = 视觉逆时针）与**世界系（Y 向上）**一致 ——
`box` 中心已 `toLocal()` 到场景系（y-down），Y 翻转已由 Qt 自身的角度约定承担，
再取负即**双重翻转**。整圆对称看不出来；半圆/部分包角则把**可见弧镜像到 X 轴另一侧**：

- 命中与包围盒用折线 `path`（世界几何，正确），而用户点的是**看得见的**镜像弧 →
  不在拾取带内 → 只有粉色点（按世界几何解算，落在正确弧上）能点中 → 症状 ①+②；
- 镜像弧越出 `boundingRect()`（由折线 `path` 算出）最多 2r，配合
  `CanvasView` 的 `MinimalViewportUpdate` 留下未重绘像素 → 症状 ③。

**证据链**（`tests/test_circle_edit.cpp`，2026-12）：

- `circleAnchorsStayOnPaintedArc()`：3 个 CurveAnchor 到 `paintPath` 的最短距离 ——
  修复前半圆时锚点 `(35.355, −35.355)` 到可见弧 **38.2683 = 2·50·sin 22.5°**
  （即到「另一半」的距离）；修复后全场景 ≤ 0.025（720 点采样）。
- `doubleClickOnCircleArcOpensPropertyDialog()`：真实输入链路（`CanvasView` +
  `ToolManager` + 合成 press/release/dblclick）下弧身 / 锚点 / 接缝双击都开面板；
  仅圆心不开（到弧 50 mm ≫ 8 px 容差，属预期）。
- 交叉验证：`TransientOverlay` 的角度扇区 gizmo 直接喂世界角
  `arcPath.arcTo(rect, sceneStartDeg, sweepDeg)` 且显示正确；全仓 `arcTo|arcMoveTo`
  仅 3 处（另 1 处 `CompoundChip` 是圆角装饰，与几何无关）→ 取负缺陷仅此一处。

**修复**：直接用世界角 —— `arcMoveTo(box, a0Deg); arcTo(box, a0Deg, sweepDeg);`。

### 25.5 测试

`tests/test_circle_edit.cpp`（ctest #17，Totals: 19 passed）本项相关 slot：

- `circleAnchorsStayOnPaintedArc`（③ 回归：整圆 / 数值包角 / 公式包角 / 公式后改数值 / 改半径）
- `doubleClickOnCircleArcOpensPropertyDialog`（③ 回归：真实输入链路）
- `circleGuideFollowsSeamAndSweep`（①：a₀=0 数据、a₀=90 接缝落 `(0,−50)`、窄弧包围盒含圆心、变形圆不画）

### 25.6 有意偏差

1. 基准线**悬停/选中时**显示，不是常显 —— 避免多圆图纸被徽标淹没；若要常显，
   去掉 `CurveItem::paint` 里的 `(m_hovered || m_owner->toolSelected() || m_owner->toolLocked())`
   条件即可。
2. 标注只写 `a₀` 一位小数，不带「a₀=」前缀 —— 与旋转量角器度数徽标同语言。

---

## 26. 一期补充：基准线口径修订 + 圆心交互（已实施 2026-12）

### 26.1 用户报告（原话）

> 1. 圆的角度不是从圆心延伸到外圆的点吗？
> 2. 悬停在圆时，圆的角度太显眼了。我觉得如果要显示。可以挂载在虚线上而不是一个大方块
> 3. 圆的角度是恒定的世界角度，这个是不对的。它是以世界角度为基准。
> 4. 圆的虚线是从圆心延伸到➡️的外圆点。并且跟随角度移动。
> 5. 当前圆心好像是圆的附属对象？目前好像只有移动属性，双击圆心是打不开面板的。
>    以及旋转工具的选择旋转中心也捕捉不到圆心。

### 26.2 根因（四条，均已在源码定位）

| 症状 | 根因 |
|---|---|
| 1 / 3 / 4 | `guide` 存的是**块内** `a₀`，但缓存坐标系已烘入块旋转（`BlockGeometryCache::toLocal` 施加 `cosR/sinR`），而标注号称「世界角」却直接输出局部值 → 块一转，虚线不再指外圆点、标注恒定不变。同一处解析弧也喂了局部 `a₀`（`paintPath.arcMoveTo(box, a0Deg)`）→ 块旋转后解析弧与折线/命中/锚点错开整整一个旋转角。 |
| 2 | 角度标注画的是 `gizmoBadgeBg` 深色圆角块，`kPadX=5.0` / `kPadY=2.0` 是**场景单位（mm）**，而字号只有 9pt（≈3.2 mm 高）→ 底框比文字宽出 10 mm 以上，视觉上就是「一个大方块」。 |
| 5a | 圆心是独立 Free 点（`CircleFactory` 里 `ptCenter` 与段端点分离；段端点是绕它的 Polar 接缝点），双击路径 `ToolSelect::mouseDoubleClick` 的「段命中」分支用**到弧距离**判定（圆心到弧 = 半径 50 mm ≫ 8 px 容差），圆心永远打不开面板。 |
| 5b | 旋转枢轴吸附 `RotateInputTracker::updateHoverSnap` 只遍历 `{seg.startPointId, seg.endPointId}`，圆心既不是起点也不是终点 → 永远吸不到。 |

### 26.3 口径（修订 §25.2）

| 项 | 新口径 |
|---|---|
| 虚线端点 | `guide.seam = toLocal(sp->resolvedPos)` —— 取**解析出的起点**，不再用角度反算 → 恒落在外圆点上 |
| 世界角 | `guide.worldAngleDeg = a₀ + radToDeg(block->transform.rotation)`（缓存系里的角 = 世界角） |
| 解析弧 | `arcMoveTo(box, a0FrameDeg); arcTo(box, a0FrameDeg, sweepDeg);` —— 与虚线/标注同一角度基准 |
| 标注 | 文字沿半径方向挂在虚线上（半径 62% 处、法线抬 `fm.height()/2 + 2`），随半径方向旋转、超 ±90° 翻正；`canvasBackground` / `gizmoBadgeBg` 3 px cosmetic 描边 + `gizmoAccentColor` 填充，**不再画圆角底块** |
| 圆心双击 | 命中点是圆段 `Block::circleCenterPoint(seg)` 时打开该段的 `LinePropertyDialog` |
| 旋转枢轴 | 候选 = 所有 `resolved && selectable && visible` 的点（含圆心）；段端点仍在候选内 |
| 提示文案 | `RotateHintTexts`：「点击端点/圆心或画布任意位置」 |

### 26.4 改动表

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/canvas/CurveItem.h` | `CircleGuide`：`startAngleDeg` → `worldAngleDeg`，新增 `QPointF seam`（注释说明含块旋转） |
| 2 | `src/canvas/BlockGeometryCache.cpp` | 新增 `a0FrameDeg = a0Deg + radToDeg(block->transform.rotation)`；解析弧改喂 `a0FrameDeg`；`guide.seam = toLocal(sp->resolvedPos)`、`guide.worldAngleDeg = a0FrameDeg` |
| 3 | `src/canvas/CurveItem.cpp` | `drawCircleGuide()` 重写：虚线 `center→seam`、圆心十字保留、标注改挂虚线（描边无底块） |
| 4 | `src/tools/ToolSelectActions.cpp` | `mouseDoubleClick()` 命中点后按 `circleCenterPoint` 识别圆心 → 开 `LinePropertyDialog(blockId, segId)` |
| 5 | `src/tools/RotateInputTracker.cpp` | `updateHoverSnap()` 候选从「段端点」扩为「所有可捕捉点」 |
| 6 | `src/tools/RotateInputTracker.h` | 类注释同步 |
| 7 | `src/tools/RotateHintTexts.cpp` | 定锚提示改「点击端点/圆心或画布任意位置」 |
| 8 | `tests/test_circle_edit.cpp` | `circleGuideFollowsSeamAndSweep` 改用 `seam` / `worldAngleDeg` 并加**块旋转 90°** 用例；`doubleClickOnCircleArcOpensPropertyDialog` 的圆心双击从「记录行为」改为**必须开面板** |
| 9 | `tests/test_rotate_pivot_multi.cpp` | 新增 `pivotSnapFindsCircleCenter` |

### 26.5 测试

- `test_circle_edit`：**20 passed**。新增断言：块旋转 90° 后 `worldAngleDeg = 180°`、
  虚线方向（`atan2(-dy, dx)`）与标注角一致、虚线长度 = 半径、接缝 = `(50, 0)` 起步；
  圆心双击必须开面板（`doubleClickOnCircleArcOpensPropertyDialog`）。
- `test_rotate_pivot_multi`：**6 passed**。`pivotSnapFindsCircleCenter`：圆心吸附（偏 2 mm）、
  接缝端点吸附不回归、远离任何点不吸附。

### 26.6 有意偏差

1. 「圆心只能拖拽」保持 —— 圆心是自由点，拖动移动整圆是预期行为，不是缺陷。
2. 标注角度 = `a₀` + 块旋转，即**世界方向角**；不提供「块内 `a₀`」显示切换
   （面板里仍按块内 `a₀` 编辑，二者相差一个块旋转）。

---

## 27. 一期补充：圆段旋转辅助显示适配（已实施 2026-12）

### 27.1 用户报告（原话）

> 圆的旋转辅助显示好像没有做单独的适配。比如旋转的黄色虚线、灰色虚线，黄圈效果

### 27.2 根因

自由块的「姿态角」全链路取**弦向 start→end**（`RotateSession::currentAngleDeg` /
`applyAngleDeg` / `originalWorldRotDeg`、`RotateAimSnap::freeAimTip`、
`RotateCopyGesture::createCopyFromRotated` 内联重算）。整圆 `a₁ = a₀ + 360°`，两端点**同为接缝点** ⇒
`w2 - w1 = (0, 0)` ⇒ `atan2(0,0) = 0` ⇒ 姿态恒 0°：黄色基准虚线压在灰色世界 0° 虚线上、
黄弧 sweep 恒 0（看不到「黄圈」）、徽标恒 0°。

灰色虚线是**世界 0° 参考线**（`TransientOverlay::showRotateGizmo` 内 `kWorldZeroRad = 0.0`），
本身与块的姿态无关，无需为圆适配；要改的是「块当前姿态方向」。

### 27.3 口径

| 项 | 口径 |
|---|---|
| 圆段姿态方向 | 「圆心 → 接缝（起点）」的半径方向 = 块内 `a₀`（`Block::circleStartAngleDeg`），旋转不变 |
| 其它段 | 保持 start→end 弦向 |
| 未解析回退 | `0.0`（与旧行为一致） |
| 收口位置 | 单一 helper `cad::tools::localPoseDirRad(blk, seg)`，四处消费点全部改用它 |

该方向与 §26 圆心虚线标注同源（同取 `a₀`），因此「旋转时的黄色基准虚线」与「悬停时的
圆心→接缝虚线」在视觉上始终重合。

### 27.4 改动表

| # | 文件 | 改动 |
|---|---|---|
| 1 | `src/tools/RotateDragMath.h` | 新增 `[[nodiscard]] double localPoseDirRad(const cad::param::Block&, const cad::param::Segment&);`；前置声明扩为 `class Block; class ParamDocument; struct Segment;` |
| 2 | `src/tools/RotateDragMath.cpp` | 定义 `localPoseDirRad`：`seg.fitKind == FitKind::Circle` → `degToRad(blk.circleStartAngleDeg(seg))`；否则 `(ep->resolvedPos - sp->resolvedPos).angle()` |
| 3 | `src/tools/RotateSession.cpp` | `applyAngleDeg` / `currentAngleDeg` / `originalWorldRotDeg` 三处自由分支改用 helper（`baseWorldDeg = radToDeg(baseTf.rotation) + radToDeg(localPoseDirRad(...))`） |
| 4 | `src/tools/RotateAimSnap.cpp` | `freeAimTip()`：`tip.curDirRad = blk->transform.rotation + localPoseDirRad(...)`；`endpointAtAngle()` 圆段 `segLen = max(segLen, 2.0 * blk->circleRadiusMm(seg))`（弦长 0 会让瞄准端塌到枢轴，取过接缝的直径另一端） |
| 5 | `src/tools/RotateCopyGesture.cpp` | `createCopyFromRotated()` 内联弦向重算替换为 `o.originalWorldRotDeg()`（与 `begin()` 同源，圆段不再算出 0） |
| 6 | `tests/test_rotate_copy_flow.cpp` | 新增 `gizmoCircleUsesRadiusDirection` |

`TransientOverlay::showRotateGizmo` / `RotateGizmo` 本身**无需改动**：它们只消费传入的
`startPoseRad` / `currentPoseRad`，且全部 `ItemIgnoresTransformations`（像素单位）。

### 27.5 测试

`tests/test_rotate_copy_flow.cpp::gizmoCircleUsesRadiusDirection`（圆 r=50、接缝在 (0,50) ⇒
半径方向世界 90°，与退化的 0° 可区分；旋转工具在弧上 315° 处按下并拖到 −45°）：

- 就绪态 `startPoseRad == currentPoseRad == π/2`（旧实现为 0）；
- 拖动中 `startPoseRad` 仍为 π/2（黄虚线冻结）、`currentPoseRad ≈ π/4`、`isArcEmpty() == false`（黄圈出现）；
- 状态提示读数「基准: 90° · 角度: 45°」（与画布徽标同源，`updateGizmo → updateStatusHint`）；
- 提交后 `transform.rotation ≈ −45°` 且块内 `a₀` 仍为 90°（旋转量只算一次）。

结果：`test_rotate_copy_flow` **8 passed**；防回归 `test_rotate_anchor` 10、
`test_rotate_copy_endtarget` 10、`test_rotate_copy_semantics` 14、`test_rotate_copy_shadow` 4、
`test_rotate_pivot_multi` 6、`test_rotate_angle_domain` 10、`test_rotate_strip` 6、
`test_rotate_d15_gate` 7、`test_circle_edit` 20 全绿。

### 27.6 有意偏差

1. 灰虚线保持**世界 0°**，不为圆改成半径方向（它是「世界基准」，不是块姿态）。
2. 圆段锚心仍取接缝（`startPointId`），与 §26「旋转枢轴可吸圆心」是两件事：
   枢轴吸附决定**绕谁转**，姿态方向决定**从哪条基准线量角**。

---

## 附：与既有约定的对齐自查

- 单位：内部 mm / 条带与公式 cm 域，格式化走 `Units::formatLength` ✓
- 像素容差：点捕捉 12px / 线身 8px 取自 InteractionTolerances.h/CanvasStyle ✓
- 角度/几何常量：`kGeomEps`、`degToRad` 等取自 Epsilon.h / Angle.h，κ 常数收口 Angle.h ✓
- 文案：命令/提示文案落 `CommandTexts.h` / `UiStrings.h` 唯一出处 ✓
- 分层：tools 不含 QWidget；条带改动归 app/ui ✓（check_layering 把关）
- 命令：文案取 `CommandTexts.h`、快照式 undo（构造时整点拷贝）✓（D14 同构）
