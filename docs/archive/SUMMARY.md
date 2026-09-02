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
当前产品内无整组旋转入口；重新设计时参考 ROTATE_REDESIGN_DESIGN.md §2.5/§2.6 判定表。

### CLEANUP_REFACTOR_DESIGN.md —— 清理与收口重构任务书（已收尾 2026-08-29）

重复构建残留清理 + 源码重复实现收口。
A1/A3（角度收口）、B4（序列化表驱动）、B5/A5（注释回填）结论已并入 AGENTS.md 开发规范区。
执行前需以源码为准核对行号（代码已漂移）。

### TOOL_SYSTEM_AUDIT.md —— 工具链路审查报告（已收尾 2026-12）

工具链路系统性审查，H/M/L 问题清单全部修复。
核心结论已并入 CONVENTIONS.md（工具元数据表驱动、三件套、生命周期收口、ToolRegistry）。
问题清单不再逐项有效，但「为什么这么设计」的论证仍可查。

---

## 2026-07 批次（docs/archive/2026-07/repowiki/）

### repowiki —— 自动生成知识库（严重过时，2026-07 生成）

内容停留在 2026-07，未反映 2026-08 起全部重构（ContextStrip / 连接卡 / 面板重设计 / 工具系统收口）。
查阅时以源码为准，仅作背景参考；生成工具已不在维护流程中，不建议投入精力更新。
原位置 `.qoder/repowiki/`，整目录迁入归档。

---

## plans 批次（docs/archive/plans/）

### plan-sess_577d56c5-4238-4245-abf5-5705b4540ae4.md —— 删除影子偏转实施计划（已完成）

删除影子偏转功能（shadowAnchorRotDeg + noFollowRotate + 冻结机制）的一次性实施计划。
该功能已删除（DECISIONS.md 第 33 条），计划已完成使命。
原位置 `.zcode/plans/`，迁入归档。
