#pragma once

#include <QString>

namespace cad::ui::str {

/// UI 文案唯一出处 (2026-12 审计 N4 收口)。
/// 只收「ui/app/tools 内重复」的文案; 与 document/parametric 层共享的命令文案
/// 在 src/document/CommandTexts.h (下层不可反向包含 ui)。
inline const QString kPendingInput = QStringLiteral("待输入");
inline const QString kName = QStringLiteral("名称");
inline const QString kVariable = QStringLiteral("变量");
inline const QString kLayer = QStringLiteral("图层");
inline const QString kAutoMeasureReadOnly = QStringLiteral("自动测量，不可编辑");
inline const QString kManualOverride = QStringLiteral("实际覆盖");
inline const QString kOverrideInProgress = QStringLiteral("实际覆盖中");
inline const QString kOverrideActiveTip = QStringLiteral("当前填入了实际覆盖值，公式计算已被覆盖值临时接管");
inline const QString kEvalOk = QStringLiteral("求值正常");
inline const QString kStatusOk = QStringLiteral("状态正常");
inline const QString kFormulaSyncedTip = QStringLiteral("公式求值成功，当前结果处于有效同步状态");
inline const QString kFormulaEmptyHint = QStringLiteral("公式表达式为空，请输入公式后自动求值");
inline const QString kInvalidExpression = QStringLiteral("表达式无效或引用的变量不存在");
inline const QString kEndConnectPoint = QStringLiteral("终点连接点");
inline const QString kConnectionPoint = QStringLiteral("连接点");
inline const QString kStartConnect = QStringLiteral("起点连接");
inline const QString kLinkCurrentLine = QStringLiteral("链接当前线");
inline const QString kActiveLayer = QStringLiteral("当前活动图层");
inline const QString kClearBasis = QStringLiteral("清除基准");
inline const QString kAngleBasis = QStringLiteral("角度基准");
inline const QString kIndependentAngle = QStringLiteral("独立角度");
inline const QString kClearCustomBasisTip = QStringLiteral("清空自定义基准，角度跟随所连线段的方向。");
inline const QString kReverseSegment = QStringLiteral("线段换向");
inline const QString kDetachPositionLink = QStringLiteral("拆开位置连接");
inline const QString kReconnectPosition = QStringLiteral("重新连接位置");
inline const QString kToggleAnchorCenter = QStringLiteral("切换锚心");
inline const QString kFillIn = QStringLiteral("填入");
inline const QString kPasteClipboard = QStringLiteral("填入剪贴板");
inline const QString kPasteClearsInputTip = QStringLiteral("清空输入框并粘贴剪切板内容");
inline const QString kPasteFormulaOrValue = QStringLiteral("粘贴公式/数值");
inline const QString kNoteButtonTip = QStringLiteral("点击打开便签，添加详细工艺或测量说明");
inline const QString kAddNote = QStringLiteral("附加注释");
inline const QString kDoubleClickRename = QStringLiteral("双击设置名称");
inline const QString kSegmentStats = QStringLiteral("线段统计");
inline const QString kSegmentNumber = QStringLiteral("线段编号");
inline const QString kLayerSegmentCountFmt = QStringLiteral("当前图层包含 %1 条线段");
inline const QString kConnectionTopologyDiag = QStringLiteral("连接拓扑诊断");
inline const QString kSwapInOut = QStringLiteral("调换进/出");
inline const QString kAlignPoint = QStringLiteral("对齐点");
inline const QString kComponentFmt = QStringLiteral("组件 %1");
inline const QString kStartToEndTip = QStringLiteral("起点 → 终点。换向后修改长度/角度将驱动对端。");

} // namespace cad::ui::str
