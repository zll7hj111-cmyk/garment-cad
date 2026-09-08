#pragma once

#include <QDoubleSpinBox>
#include <QString>

namespace cad::ui {

/// 数值输入框统一规格 (2026-12 审计 UI-P1-8 / 工单 N5 落地).
///
/// 同一物理量必须用同一「精度 + 后缀 + 步长」: 此前 cm 长度在
/// ConditionDialog / VariableCard / SegmentAnchorTab 各自写死 (2 位 + " cm"),
/// 角度 1 位 + "°", 线宽 1 位无后缀 —— 全部散落在调用点, 改一处漏一处。
/// 这里收口为 NumericFieldSpec。**范围不在此表**: 范围是领域约束
/// (切线长度不得为负、变量值可正可负), 由调用方按语义单独 setRange。
///
/// 自由文本卡片输入框 (ElaLineEdit) **刻意不装 QDoubleValidator**:
/// 它们同时接受公式 (parseNumberOrFormula / parseAngleText), 校验器会直接
/// 挡掉公式字符 —— 见 docs/DECISIONS.md「卡片输入框不装校验器」。
struct NumericFieldSpec
{
    int decimals = 2;         ///< 小数位
    const char* suffix = "";  ///< UTF-8 后缀 (裸字节, 不依赖执行字符集)
    double step = 0.5;        ///< 单步
};

/// cm 长度: 2 位小数 + " cm"。
inline constexpr NumericFieldSpec kLengthCmSpec{2, " cm", 0.5};

/// 角度 (deg): 1 位小数 + "°" (U+00B0 写成裸 UTF-8 字节, 避免执行字符集差异)。
inline constexpr NumericFieldSpec kAngleDegSpec{1, "\xC2\xB0", 1.0};

/// 线宽 (px): 1 位小数, 无后缀。
inline constexpr NumericFieldSpec kWeightPxSpec{1, "", 0.2};

/// 应用规格 (精度/后缀/步长), 不改范围与当前值。
inline void applyNumericSpec(QDoubleSpinBox* spin, const NumericFieldSpec& spec)
{
    spin->setDecimals(spec.decimals);
    spin->setSuffix(QString::fromUtf8(spec.suffix));
    spin->setSingleStep(spec.step);
}

} // namespace cad::ui
