# SUMMARY.md —— 归档化石摘要

> 本文件是「归档瘦身」手术的产物（2026-09-02）。**严禁删除任何文字**——原文件完整移入
> `docs/archive/` 对应年份目录，本文件只保留每份化石的 3-5 句结论摘要，供快速回顾。
> 归档原则：已下线功能 / 已废弃方案 / 已完成任务书 / 过时自动生成知识库，一律入档不销毁。
> 归档后同步更新 `DOCS_INDEX.md` 状态列。

---

## 2026-08 批次（docs/archive/2026-08/）

### CONNECTION_REDESIGN_DESIGN.md —— 连接卡重设计（已废弃 2026-08-31）

曾按技术视角（AttachEnd / LandMode / ConnMode 枚举）设计连接卡重做，用户否决方向。
现行方案 = 两维独立（位置维 angleOnly / 角度维 angleIndependent），见 DECISIONS.md「连接卡片」条目。
文中「连接名下是 5 套机制」的源码考古结论仍有效，重设计连接 UI 前可参考。

### COMPONENT_ROTATE_DESIGN.md —— 组件整组旋转（已废弃 2026-08-29）

W 键组件整组旋转模态已删除（执行 ROTATE_REDESIGN_DESIGN.md D1 拍板：整组模式被多选旋转覆盖）。
选集旋转路径（RotateBlocksCommand / AimRelease / DartRelease）同批删除。
**2026-09-04 选集旋转已重新设计回归**（MarqueeGesture 框选 + adoptSelection + RotateBlocksCommand，产品现有整组旋转入口），本摘要记录的是 2026-08-29 删除时刻的历史状态；重新设计时参考 ROTATE_REDESIGN_DESIGN.md §2.5/§2.6 判定表。

### CLEANUP_REFACTOR_DESIGN.md —— 清理与收口重构任务书（已收尾 2026-08-29）

重复构建残留清理 + 源码重复实现收口。
A1/A3（角度收口）、B4（序列化表驱动）、B5/A5（注释回填）结论已并入 AGENTS.md 开发规范区。
执行前需以源码为准核对行号（代码已漂移）。

### TOOL_SYSTEM_AUDIT.md —— 工具链路审查报告（已收尾 2026-12）

工具链路系统性审查，H/M/L 问题清单全部修复。
核心结论已并入 CONVENTIONS.md（工具元数据表驱动、三件套、生命周期收口、ToolRegistry）。
问题清单不再逐项有效，但「为什么这么设计」的论证仍可查。

---

## 2026-09 批次（docs/archive/2026-09/）

### CURVE_P3_DESIGN.md —— 曲线系统 P3 改进任务书（已闭环）

2026-08-28 派发给执行 AI 的自包含任务书：①画布上断开切线锁定的快捷键；②Hobby 求解极端转角的过冲限幅。
任务书内的构建 / 测试命令（`build/out`、基线 22/26 红 4）已过时，**执行前须以当前 AGENTS.md 为准**（`tools\build.bat` + `build/out-reldeb`，基线红 0）。
保留价值：P0/P1/P2 曲线审查的问题背景与 P3 验收口径。

### DART_LINE_REMOVAL_REPORT.md —— 省道线功能彻底下线工程报告（已落地 2026-09）

旧版「省道线（Dart Line）」被「正交拐角偏置（OrthoOffset）」+「端点跟随开度模式（ChordLength）」完全覆盖后彻底剥离，清理死码与孤岛逻辑 600+ 行。
含跨层清理清单、旧档兼容策略与验证记录，是「如何干净下线一个跨层功能」的完整范例。
文中 `42/42` 等历史计数已过时，现状以 `ctest -N` 为准。

---

## 2026-12 批次（docs/archive/2026-12/）

### DETOUR_AUDIT_REPORT.md —— 全项目「拐弯路径实现」审计报告（10 候选全部闭环）

2026-12 全量扫描 `src/` 291 个源文件，按模块边界 5 路并行审计，只报带 `file:line` 证据的候选。
10 个修复候选（绕门面丢信号 / 冗余复算 / 隐藏点丢弃 / 死码 / 同构复制 / 手搭图元 / 命令分叉等）**全部闭环**，架构七守卫 100%。
三条硬红线实测为 0：`const_cast` 0 处、手动 `++geometryEpoch` 0 处、`RawModelAccess` 无违规绕行——是「架构纪律体检」的基线记录。

---

## 2026-07 批次（docs/archive/2026-07/repowiki/）

### repowiki —— 自动生成知识库（严重过时，2026-07 生成）

内容停留在 2026-07，未反映 2026-08 起全部重构（ContextStrip / 连接卡 / 面板重设计 / 工具系统收口）。
查阅时以源码为准，仅作背景参考；生成工具已不在维护流程中，不建议投入精力更新。
原位置 `.qoder/repowiki/`，整目录迁入归档。

---

## plans 批次（docs/archive/plans/）

### DETACH_SHADOW_PLAN.md —— 拆开影子基准实施计划（已落地 2026-09-03）

「拆开 = 复制隐藏影子基准线」语义的任务分解与验收标准；权威语义见 `docs/design/DETACH_SHADOW_DESIGN.md`。
文内测试文件名（`test_commands` / `test_select_wkey` / `test_dialog_tabs` 等）与用例号为拆分前状态，现行清单见 `CONVENTIONS.md`。

### FILE_SPLIT_PLAN_V2.md —— 文件职责治理第二轮计划（已被 V3 取代）

提出「单文件 2500 行硬顶」并把 `test_commands.cpp`(3399) / `test_rotate_copy.cpp`(3373) / `test_resolver.cpp`(1688) 列为拆分对象。
目标已由 `FILE_SPLIT_PLAN_V3.md` 全量落地（2026-09，HEAD `e2fe4e7`）；文内行数、文件名为当时快照。

### FILE_SPLIT_PLAN_V3.md —— 文件职责治理第三轮实施计划（已全量落地 2026-09）

批次 0~4 + 全局验收 100% 通过，是「智能体任务书」写作范例（现状证据 / 任务分解 / 验收口径）。
文内 ctest 计数与 `test_*.cpp` 名称为拆分前状态（现行 = 61 用例、`test_resolver_*` 等）。

### plan-sess_577d56c5-4238-4245-abf5-5705b4540ae4.md —— 删除影子偏转实施计划（已完成）

删除影子偏转功能（shadowAnchorRotDeg + noFollowRotate + 冻结机制）的一次性实施计划。
该功能已删除（见 DECISIONS.md「拆开保留角度」条目——影子偏转字段随该决策删除），计划已完成使命。
原位置 `.zcode/plans/`，迁入归档。
