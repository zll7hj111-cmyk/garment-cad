# P2 剩余任务 · 分批交接指令

> 用途：剩余三类工作（qWait 条件化 / 门面按域分组 / 句柄化）都太大，一个上下文窗口跑不完。
> 这里按"单个窗口能做完并验证"的粒度切好批次，**每个批次的指令完全自包含**——复制对应那一块
> （「通用前缀」+「本批专属」）粘到新窗口即可开工，不需要新窗口读过本文其他部分。
>
> 生成时间：2026-08-29 | 基线：ctest 29 用例 27 通过 / 2 失败（既有老问题）

---

> ## 状态：本文件已归档，所有批次均已执行完毕（2026-08-29 04:00 前）
>
> A1~A6、B1~B3、C1 已全部执行。**本文件留档仅作过程记录，不要再按它新开批次。**
> 复核结论（2026-08-29 04:30，独立验证：重新构建 + ctest + 两个 check 脚本）：
>
> | 类 | 结论 | 核实方式 |
> |----|------|----------|
> | **A 类（6 批）** | ✅ 收官。全仓 `QTest::qWait(` 150 处**零未判定点位**（真转换 10 处，其余逐点判定后"有据保留"并加注释） | 文件抽查 + ctest 27/29 |
> | **B 类（3 批）** | ✅ 收官。6 个只读视图真实存在（BlockView/AttachmentsView/ComponentsView/MeasurementsView/LayersView/VariablesView），**134 处调用点已迁移**（全仓 42 处实际引用，非空壳） | `ls src/parametric/*View.h` + grep 引用 |
> | **C 类** | ✅ C1 审计后**结案为"不做"**（失效面仅 4 个 bump 点、成员持有 0、违规 0），**C2/C3 按交接规则跳过** | 见 C1 报告结论 |
>
> **复核时发现并修复的一处遗漏**：`LayersView.h` 与 `VariablesView.h` 是 B3 新增的 header-only
> 视图，但漏登记进 CMakeLists（其余 4 个都登记了）。因是 header-only 所以编译能过、不易察觉，
> 但不符项目约定（IDE/clangd 不索引）。已补登记并重新构建通过。
>
> 剩余未做的只有 **Rust 重构试点**。详见 ARCHITECTURE_REVIEW.md 顶部执行状态表。

---

## 批次总表（已全部执行完毕）

| 批次 | 内容 | 规模 | 风险 | 建议顺序 |
|------|------|------|------|----------|
| **A1** | test_rotate_copy.cpp 行 1~850 | ~40 处 | 低 | 1 |
| **A2** | test_rotate_copy.cpp 行 850~1400 | ~40 处 | 低 | 2 |
| **A3** | test_rotate_copy.cpp 行 1400~末尾 | ~40 处 | 低 | 3 |
| **A4** | test_select_wkey.cpp 行 1~900 | ~35 处 | 低 | 4 |
| **A5** | test_select_wkey.cpp 行 900~末尾 | ~34 处 | 低 | 5 |
| **A6** | 其余 10 个文件 | 44 处 | 低 | 6 |
| **B1** | 门面分组：审计 + blocks 域试点 | 设计 + ~25 调用点 | 中 | 7 |
| **B2** | 门面分组：组件/附着/变量公式域 | ~40 调用点 | 中 | 8 |
| **B3** | 门面分组：图层/求解/undo + 收敛 | ~30 调用点 | 中 | 9 |
| **C1** | 句柄化：审计 + BlockRef 设计 | 只出报告，不改代码 | — | 10 |
| **C2** | 句柄化：实现 + 热点改造 | ~30 处 | 高 | 11 |
| **C3** | 句柄化：全量推广 + 断言 | 剩余 | 高 | 12 |

**A 类可以放心做**：只动测试代码，不动产品代码，风险最低，且每批独立可验证。
**B 类先做 B1**——B1 会产出分组方案，B2/B3 照着推广；如果 B1 发现方案不可行，后面就不用做了。
**C1 必须只出报告不改代码**，先看清裸指针的真实使用规模再决定要不要继续。

---

## 【通用前缀】每个新窗口都要先粘贴这段

```
【项目背景 · 先读】
项目：E:\garment-cad —— WildWind Pattern，参数化服装 CAD 系统（C++23 / Qt6 / MSVC2022 / Ninja Debug）

开工前必读（不要跳过）：
1. E:\garment-cad\AGENTS.md —— 架构铁律、构建验证命令、模块边界、踩坑经验库索引
2. E:\garment-cad\TROUBLESHOOTING.md 第 9 组 —— 上一轮 P2 收口踩过的坑（CMake 块切片丢行、
   \r\r\n 换行导致 Edit 匹配失败、"历史 vs 模型不变量"的判据、bench 类测试不可断言耗时）

环境与工具（这几个坑上一轮都踩过）：
- 构建必须走 PowerShell 工具：`& .\tools\build.bat`。Bash 工具禁止调 cmd.exe（安全策略直接拦截）。
- PowerShell 工具不回显 stdout，日志必须落盘再读：
    & .\tools\build.bat *>&1 | Out-File -FilePath build\w.log -Encoding utf8
  然后读 build\w.log（是 UTF-8，不要 iconv）。
- 跑测试：cd build\out; ctest 2>&1 | Out-File -FilePath ..\w_ctest.log -Encoding utf8
- 静态检查（都应输出 OK）：
    C:\Users\Administrator\.workbuddy\binaries\python\versions\3.13.12\python.exe tools\check_layering.py
    C:\Users\Administrator\.workbuddy\binaries\python\versions\3.13.12\python.exe tools\check_test_fixtures.py
- 源码换行有 40 个文件是 \r\r\n（MSVC 报 C4335）。python 处理时必须先
  .replace('\r\r\n','\n') 再 .replace('\r\n','\n')；Edit 工具对这类文件会匹配失败，改用 python 脚本。

当前基线（动手前先跑一遍确认，别把老问题算到自己头上）：
- ctest 29 个用例，27 通过 / 2 失败。这两个是既有老问题，与本次改动无关：
  · test_serializer::bridgeAuxPointSnappableAndAttachable
  · test_component::dragComponentLeaderCurveFollowStable（31.4798mm）
- 任何"新出现"的失败都要查清再继续。GUI 测试在整批 ctest 下有负载抖动，看到红先单跑复现再判断。

工作区状态：根目录有大量未提交的改动，git checkout 不是保险。批量改源码前先
cp -r 相关目录到 build\backup_本次批次\（build/ 已被 gitignore）。

收尾要求（每批都做）：
- 跑通：构建 + ctest + 两个 check 脚本，结果贴到回复里
- 更新 E:\garment-cad\ARCHITECTURE_REVIEW.md 顶部「执行状态」表对应条目的进度数字
- 追加 E:\garment-cad\.workbuddy\memory\YYYY-MM-DD.md：做了什么、踩了什么坑、下一步
```

---

## A 类：GUI 测试固定等待 → 条件等待（共 6 批）

### 批次 A1

```
【本批任务：A1 —— test_rotate_copy.cpp 前段 qWait 条件化】

范围：tests\test_rotate_copy.cpp 第 1~850 行（从文件开头到 modeSwitchKeepsFormula 之前），
约 40 处 QTest::qWait。不要碰 850 行之后的。

已有工具（tests\TestHelpers.h，本文件已 include，直接用）：
- cad::test::waitUntil(pred, 5000) —— 轮询事件循环直到 pred 为真，超时返回 false
- cad::test::settle()             —— 排空事件循环（5 轮），给"不该发生的事"一个发生的机会
- cad::test::grabStable(widget)   —— 抓帧等到连续两帧一致

逐处判定规则（这是本轮踩过坑后总结的，务必逐处判断，不要批量替换）：
1. 断言"某个值变了 / 某个控件出现了" → waitUntil，谓词就写那个断言本身。
   例：QTest::qWait(20); QVERIFY(dlg->findChild<X*>() != nullptr);
   改成 QVERIFY2(cad::test::waitUntil([&]{ return dlg->findChild<X*>() != nullptr; }), "超时说明");
2. 断言"某个值没变 / 不该受影响" → settle()。这类没有可等的状态，写成 waitUntil(值==新值)
   会直接超时失败（本批次上一轮就犯过这个错）。
3. view.show(); QVERIFY(qWaitForWindowExposed(&view)); 之后的 qWait(80) → **保持原样不要动**。
   它是"过度等待"不是"等不够"：QTest::qWait 不提前返回，80ms 一定等满，机器忙时并不会变短。
   换成瞬时成立的条件反而会暴露被它掩盖的竞态。
4. sendEvent 之后的 qWait(20) → 先判断这个事件的可观测效果是什么。找得到就用 waitUntil；
   确实找不到就保留 qWait 并加一行注释说明"此处无可观测条件，暂留"。宁可留几处也别乱猜。

禁止：
- waitUntil 谓词里不要直接解引用可能为 nullptr 的指针，先判空（谓词会被轮询很多次）。
- 不要把 QVERIFY 写进非 void 的辅助函数（宏失败时是 return;，MSVC 报 C2561/C2280）。

验证：
  cd build\out; .\test_rotate_copy.exe 2>&1 | Out-File -FilePath ..\a1.log -Encoding utf8
要求：全部 PASS。改前改后单跑耗时不应显著变长（条件等待通常更快）。
```

### 批次 A2

```
【本批任务：A2 —— test_rotate_copy.cpp 中段 qWait 条件化】

范围：tests\test_rotate_copy.cpp 第 850~1400 行（modeSwitchKeepsFormula 到 endTargetRotateCopyUndoRedoKeepsOriginalAim 之间），
约 40 处 QTest::qWait。
（若 A1 已完成，本批起点行号会因 A1 的增删而偏移，按函数名定位而不是死磕行号。）

判定规则、工具、禁止项完全同 A1，要点复述：
1. 断言"值变了/控件出现" → waitUntil（谓词 = 该断言）
2. 断言"值没变"         → settle()
3. show + qWaitForWindowExposed 后的 qWait(80) → 保持原样，不动
4. sendEvent 后的 qWait(20) → 能找到可观测条件就改，找不到就留并加注释
5. waitUntil 谓词内先判空；非 void 辅助函数里不放 QVERIFY

验证：cd build\out; .\test_rotate_copy.exe 2>&1 | Out-File -FilePath ..\a2.log -Encoding utf8
要求全 PASS，且耗时不显著变长。
```

### 批次 A3

```
【本批任务：A3 —— test_rotate_copy.cpp 后段 qWait 条件化】

范围：tests\test_rotate_copy.cpp 第 1400 行到文件末尾（midGestureCtrlConvertsToCopy 及之后所有函数），
约 40 处 QTest::qWait。改完本文件应基本清零。

判定规则、工具、禁止项同 A1/A2：
1. "值变了/控件出现" → waitUntil；2. "值没变" → settle()；
3. qWait(80)（show 之后）→ 不动；4. qWait(20)（sendEvent 之后）→ 有条件就改，没条件就留并注释；
5. 谓词先判空，非 void 辅助函数不放 QVERIFY。

验证：cd build\out; .\test_rotate_copy.exe 2>&1 | Out-File -FilePath ..\a3.log -Encoding utf8
另跑一次全量确认没有波及别处：ctest（应仍是 27/29 那两个老失败）。
```

### 批次 A4

```
【本批任务：A4 —— test_select_wkey.cpp 前段 qWait 条件化】

范围：tests\test_select_wkey.cpp 第 1~900 行（wTogglesMultiSelectionThroughFullEventChain 到
unselectedEndpointPressFallsBackToSelect 之间），约 35 处 QTest::qWait。

工具：cad::test::waitUntil / settle / grabStable（在 tests\TestHelpers.h；若本文件未 include 就加上）。
判定规则（同 A 类前几批）：
1. "值变了/控件出现" → waitUntil（谓词 = 该断言）
2. "值没变/不该受影响" → settle()
3. show + qWaitForWindowExposed 后的 qWait(80) → 保持原样，不动
4. sendEvent 后的 qWait(20) → 有条件就改，没条件就留并注释
5. 谓词先判空；非 void 辅助函数不放 QVERIFY

特别注意：本文件有 endpointClickAfterConfirmKeepsSelectionOperable 这个用例曾在整批 ctest 下
偶发失败（单跑全绿）。如果它在本批范围内，优先用 waitUntil 把它稳住——它断言的是拖拽后的
坐标，属于典型的"效果尚未落定就断言"。

验证：cd build\out; .\test_select_wkey.exe 2>&1 | Out-File -FilePath ..\a4.log -Encoding utf8
```

### 批次 A5

```
【本批任务：A5 —— test_select_wkey.cpp 后段 qWait 条件化】

范围：tests\test_select_wkey.cpp 第 900 行到文件末尾（bodyDragMovesLine 及之后所有函数），
约 34 处 QTest::qWait。改完本文件应基本清零。

判定规则同 A4：waitUntil（值变了）/ settle（值没变）/ qWait(80) 不动 / qWait(20) 有条件才改 /
谓词先判空 / 非 void 辅助函数不放 QVERIFY。

验证：cd build\out; .\test_select_wkey.exe 2>&1 | Out-File -FilePath ..\a5.log -Encoding utf8
再跑全量 ctest 确认仍是 27/29。
```

### 批次 A6

```
【本批任务：A6 —— 其余 10 个测试文件的 qWait 条件化】

范围（共 44 处，逐个文件处理，不要一次全改完再编译，改一个验一个）：
  tests\test_component.cpp        12 处
  tests\test_dialog_tabs.cpp       9 处
  tests\test_aux_layer.cpp         6 处
  tests\test_segment_edit_bar.cpp  3 处
  tests\test_tool_intersection.cpp 4 处
  tests\test_curve_edit.cpp        3 处
  tests\test_realdoc_full.cpp      3 处
  tests\test_canvas_perf.cpp       2 处
  tests\test_hold_show.cpp         1 处
  tests\test_smartpen_aux.cpp      1 处

注意：
- test_dialog_tabs.cpp 和 test_segment_edit_bar.cpp 里已有上一轮改好的 waitUntil/settle 样板，
  照着它们的风格改，保持一致。
- test_realdoc_full.cpp 是靠环境变量 GCAD_DOC 手动跑的（不进 ctest），改动后无法自动验证，
  如果拿不准就保留原样并加注释。
- test_canvas_perf.cpp 涉及抓帧，优先用 grabStable。

判定规则同 A1~A5：waitUntil（值变了）/ settle（值没变）/ qWait(80) 不动 /
qWait(20) 有条件才改 / 谓词先判空 / 非 void 辅助函数不放 QVERIFY。

验证：每个文件改完跑对应 exe；全部改完跑 ctest（应仍是 27/29 那两个老失败）。
然后更新 ARCHITECTURE_REVIEW.md 里 P2-3 条目的剩余数字（改完应远小于 233）。
```

---

## B 类：门面按域分组窄接口（共 3 批）

> **必须先做 B1**。B1 产出分组方案并验证可行性，B2/B3 照着推广。
> **铁律（AGENTS.md）**：ParamDocument 是门面，**所有公共 API 与信号签名必须保持不变**。
> 所以做法是"新增按域分组的窄接口视图，逐步把调用点迁过去"，**不是删旧 API**。
> 旧接口只有在确认零调用者后才收敛，且要单独一批做。

### 批次 B1

```
【本批任务：B1 —— 门面按域分组：审计 + blocks 域试点】

目标：把 ParamDocument 的 114 个 public 方法，按域分组暴露为窄接口（如 doc.blocks().xxx()），
     降低"万能抽屉"的心智负担。本批只做审计 + 一个域的试点，验证方案可行。

第一步：审计（先看清楚，别急着改）
- 读 E:\garment-cad\src\parametric\ParamDocument.h，里面已有 22 个域注释分区
  （// --- Block management --- 等），以此为分组依据。
- 输出一张表：域名 | 方法数 | 主要调用方（src 下哪些模块、多少处）。
  统计命令参考：grep -rn "doc\.<方法名>" src/ tests/ | wc -l
- 在回复里给出这张表，并标出建议的分组方案（哪几个域合成一个窄接口对象）。

第二步：试点 blocks 域
- 按你的方案实现第一个窄接口（建议命名 BlockView 或 BlocksFacade），
  放在 src/parametric/ 下，头文件纳入 CMakeLists 的 gcad_parametric 源列表。
- 窄接口只暴露该域真正需要的方法，且**只读方法优先**（写方法仍走门面，保证校验与信号不绕过）。
- 迁移 10~25 个调用点到窄接口，选调用最集中的那几个文件。
- **旧 API 一个都不要删**，先并存。

约束：
- 不改动任何公共 API 签名或信号签名。
- 窄接口不得持有独立状态（它是门面的视图，不是副本）。
- 不得出现 const_cast；不得绕过带校验的门面方法直改模型（AGENTS.md 明令）。
- 分层：parametric 引擎不依赖任何 UI，改完跑 tools\check_layering.py 确认。

验证：
- 构建通过；ctest 仍是 27/29（那两个老失败）；两个 check 脚本 OK。
- 在回复里明确一句"方案是否可行、是否建议继续 B2"，并给出 B2 应迁移的域清单。
```

### 批次 B2

```
【本批任务：B2 —— 门面分组：组件 / 附着 / 变量公式 域】

前置：B1 已完成，blocks 域窄接口已落地且验证通过。按 B1 产出的分组方案继续。
（若新窗口没有 B1 结论，先读 E:\garment-cad\ARCHITECTURE_REVIEW.md 顶部状态表和
 .workbuddy\memory\ 里最近几天的日志找回方案。）

本批范围（建议，如 B1 结论不同以 B1 为准）：
- Components 域（ParamDocument.h 约 152 行起）
- Attachment management 域（约 185 行起）
- Variables / Formula variables / Formula groups（约 295~329 行）
- Linked / Measure / Angle measure variables（约 397~425 行）

做法与 B1 试点完全一致：新增窄接口对象 → 迁移调用点 → 旧 API 保留并存。
每完成一个域就编译一次，别攒着。

约束（同 B1）：不动公共 API/信号签名；窄接口不持状态；不绕过校验；分层检查要过。

验证：构建 + ctest（27/29）+ 两个 check 脚本；回复里列出已迁移的域和调用点数。
```

### 批次 B3

```
【本批任务：B3 —— 门面分组：图层 / 求解 / undo 域 + 旧接口收敛】

前置：B1、B2 已完成。

本批范围：
- Canvas layers（约 329 行）+ Layered dirty marking（约 382 行）
- Resolve（约 460 行）+ Serialization support（约 479 行）+ Undo/Redo（约 505 行）
- Readable serials / serial counters（约 281、609 行）

最后做旧接口收敛（单独一步，谨慎）：
- 对每个已被窄接口覆盖的旧方法，grep 全仓确认零调用者后才删除。
- 只要还有调用者（含 tests/）就保留，**不要为了好看强删**。
- 删除前跑一次全量 ctest 作为对照。

验证：构建 + ctest（27/29）+ 两个 check 脚本。
收尾：更新 ARCHITECTURE_REVIEW.md 顶部状态表 P1-2 条目，把"前半段未做"改为已完成，
并写清 114 个 public 方法收敛到了多少个窄接口。
```

---

## C 类：句柄化替代裸指针（共 3 批，风险最高）

> 背景：`findBlock()` / `blockById()` 返回指向 vector 内部的可变裸指针，vector 扩容即悬空。
> 现有护栏（P1-3 短期项，已完成）：`structureEpoch()` 可观察结构代数 + debug 版
> `blockPointerInRange(p)`。目标是让编译器替我们守住"一次一取、禁止跨结构性变更持有"。

### 批次 C1

```
【本批任务：C1 —— 裸指针审计 + BlockRef 设计】（只出报告，不改产品代码）

背景：src\parametric\ParamDocument.h:87/88 的 findBlock（const/非 const 重载）与 :149 的
blockById 返回可变裸指针 Block*，指向 m_blocks 内部。任何结构性变更（addBlock/removeBlock/
clear）都可能使其失效。中期方案是句柄化：BlockRef = id + generation。

本批只做调查与设计，输出一份报告贴在回复里，**不要修改 src/ 下的产品代码**：

1. 统计裸指针使用规模
   - grep -rn "findBlock\|blockById" src/ tests/ 分别统计出现次数与文件数。
   - 重点识别"跨结构性变更持有"的高危模式：把 Block* 存进局部变量/成员变量后，
     中间又调用了 addBlock/removeBlock/clear/addAttachment 等，再使用这个指针。
   - 列出高危点 TOP 10（文件:行号 + 一句话说明为什么危险）。

2. 设计 BlockRef
   - 结构建议：{ QUuid id; uint32_t generation; }，轻量可拷贝。
   - 需要回答：generation 存在哪（Block 内？还是并发表？）、resolve 时如何校验失效、
     与现有 structureEpoch() 的关系（是否取代它）、对性能敏感路径（Resolver 热循环）的影响。
   - 给出过渡方案：能否让 findBlock 继续返回 Block*（保证不破坏现有 500+ 调用点），
     同时新增 blockRef() 返回句柄，逐步迁移？

3. 给出结论与工作量估算
   - 句柄化是否值得做（对比现有护栏 + 代码实际情况——上一轮勘察发现热点代码其实已普遍
     遵守"变更后重新 findBlock"的写法，真句柄的收益主要在于让编译器守住这条纪律）。
   - 如果值得：列出 C2 应先改造的热点清单。
   - 如果不值得：明确说"建议结案为不做"，并说明现有护栏是否足够。

报告写完后追加到 E:\garment-cad\.workbuddy\memory\YYYY-MM-DD.md，并更新
ARCHITECTURE_REVIEW.md 顶部 P1-3 条目。
```

### 批次 C2

```
【本批任务：C2 —— BlockRef 实现 + 热点改造】

前置：C1 报告结论为"值得做"。若 C1 结论是不做，本批直接跳过并在文档里结案。

范围：按 C1 列出的热点清单改造（建议 20~30 处，不要一次全量）。

实施要点：
- 新增 BlockRef（按 C1 设计），放在 src/parametric/ 下，头文件纳入 CMakeLists 的
  gcad_parametric 源列表。
- 采用 C1 确定的过渡方案（推荐：保留 findBlock 返回 Block*，新增 blockRef() 返回句柄，
  热点逐步迁移），避免一次性打断 500+ 调用点。
- 改造热点时逐个验证，改一个编译一次。

约束：
- 不改动任何公共 API 签名（findBlock 仍返回 Block*）。
- 不得 const_cast；不得绕过门面直改模型。
- 分层：parametric 不依赖 UI，改完跑 tools\check_layering.py。

验证：构建 + ctest（27/29）+ 两个 check 脚本。
回复里列出已改造的热点数与剩余数。
```

### 批次 C3

```
【本批任务：C3 —— 句柄化全量推广 + debug 断言】

前置：C2 已完成，热点已验证。

范围：把 C2 未覆盖的调用点迁到句柄；在 debug 构建下加断言。

实施要点：
- 逐个文件迁移，每完成一个文件编译一次。
- debug 断言：在 BlockRef::get() 等解引用入口校验 generation，失配时 qWarning 并断言，
  release 下编译为零成本（参照现有 blockPointerInRange 的做法）。
- 迁移完成后评估：findBlock 返回 Block* 的旧路径是否还有调用者。若有，保留并存并加注释
  说明"遗留路径，新代码请用 blockRef()"；不要强删。

验证：构建（Debug）+ ctest（27/29）+ 两个 check 脚本。
额外跑一次 Release 构建确认断言确实零成本：
  & .\tools\build.bat release *>&1 | Out-File -FilePath build\rel.log -Encoding utf8

收尾：更新 ARCHITECTURE_REVIEW.md 顶部 P1-3 条目（中期项从"未做"改为已完成），
并更新 AGENTS.md「架构原则」里关于裸指针的约定（把"一次一取"的注释约定改成句柄的实际做法）。
```

---

## 通用收尾检查清单（每批做完都要过一遍）

- [ ] `& .\tools\build.bat` 构建通过（日志里没有 `error C` / `error LNK` / `FAILED:`）
- [ ] `cd build\out; ctest` 结果仍是 27 通过 / 2 失败，且失败的是那两个老面孔
- [ ] `python tools\check_layering.py` 输出 `layering OK`
- [ ] `python tools\check_test_fixtures.py` 输出 `test fixtures OK`
- [ ] `ARCHITECTURE_REVIEW.md` 顶部执行状态表的进度数字已更新
- [ ] `.workbuddy\memory\YYYY-MM-DD.md` 已追加（做了什么 / 踩了什么坑 / 下一步）
- [ ] 若踩到新坑，追加到 `TROUBLESHOOTING.md` 对应主题分组
