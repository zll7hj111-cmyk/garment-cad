---
kind: error_handling
name: 基于结果结构体的表达式求错与UI提示错误处理
category: error_handling
scope:
    - '**'
source_files:
    - src/parametric/ExpressionEvaluator.h
    - src/parametric/ExpressionEvaluator.cpp
    - src/parametric/FormulaVariable.h
    - src/ui/FormulaCard.h
    - src/ui/FormulaCard.cpp
    - src/ui/VariablePanel.cpp
    - tests/test_expression.cpp
---

该仓库采用轻量级的「结果结构体」模式进行错误处理，集中在参数化公式求值链路中，未使用异常、哨兵错误或全局错误码。核心设计如下：

1. **ExpressionEvaluator::Result 结构体**（`src/parametric/ExpressionEvaluator.h`）
   - 每个求值操作返回 `Result { bool ok; double value; QString error; }`，调用方通过检查 `ok` 字段判断成功与否，并通过 `error` 获取人类可读的错误描述。
   - 内部解析器通过私有 `fail(message)` 方法设置 `m_ok=false` 和 `m_error`，避免抛出异常或中断控制流。

2. **FormulaVariable 中的错误传播**（`src/parametric/Formul aVariable.h`）
   - `FormulaVariable` 结构体包含 `bool valid` 和 `QString error` 字段，用于缓存最近一次求值的错误信息，供 UI 层展示。

3. **UI 层的错误展示**（`src/ui/FormulaCard.cpp`、`src/ui/VariablePanel.cpp`）
   - `FormulaCard::setResult(ok, valueCm, error)` 将错误信息作为 tooltip 显示在数值标签上，用户可悬停查看具体错误原因。
   - `VariablePanel` 在变量求值失败时清空或填充 `f.error`，并传递给卡片组件。

4. **测试覆盖**（`tests/test_expression.cpp`）
   - 针对除零、未知变量、空表达式等错误场景编写了 QtTest 用例，断言 `r.ok` 为 false 且 `r.error` 非空。

5. **GUI 级错误**（`src/app/MainWindow.cpp`）
   - 仅使用 `QMessageBox::about` 显示“关于”对话框，未见其他业务错误弹窗。

**约定与约束**：
- 所有表达式求值必须通过 `ExpressionEvaluator::evaluate` 返回的 `Result` 结构体传递状态，禁止使用 C++ 异常或 `qWarning`/`qFatal` 等日志宏报告业务错误。
- 错误消息统一使用 `QStringLiteral` 中文描述，便于直接展示给用户。
- UI 层不自行构造错误信息，仅透传底层 `Result.error` 内容。
- 条件引擎注释明确说明“条件未应用时可防止级联调整错误”，体现错误隔离的设计意图。