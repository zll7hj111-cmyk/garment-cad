#pragma once

#include <QString>

namespace cad::cmd::texts {

/// 命令/动作文案唯一出处 (2026-12 审计 N4 收口)。
/// 本头位于 document 层: document/commands/* 与上层 ui/tools/app 共用同一份
/// QUndoCommand 文案 (菜单项 / 按钮 / 撤销栈文本必须逐字一致)。ui 层不得被
/// 反向包含, 故共享文案只能落在 document 层。
inline const QString kDeleteComponent = QStringLiteral("删除组件");
inline const QString kDissolveComponent = QStringLiteral("解散组件");
inline const QString kReattach = QStringLiteral("重新挂接");
inline const QString kEndConnect = QStringLiteral("终点连接");
inline const QString kNewGroup = QStringLiteral("新建分组");
inline const QString kDissolveGroup = QStringLiteral("解散分组");
inline const QString kNewAuxPoint = QStringLiteral("新建辅助点");
inline const QString kDeleteLinkedVar = QStringLiteral("删除关联参数");
inline const QString kPublishLinkedVar = QStringLiteral("发布关联参数");
inline const QString kDeleteMeasureVar = QStringLiteral("删除测量变量");
inline const QString kDeleteAngleMeasureVar = QStringLiteral("删除角度测量变量");
inline const QString kShadowMount = QStringLiteral("影子挂载");
inline const QString kDisconnect = QStringLiteral("断开连接");
inline const QString kOtherLayers = QStringLiteral("其他图层");
inline const QString kDeletePlacedPoint = QStringLiteral("删除放置点");
inline const QString kDeleteVariable = QStringLiteral("删除变量");
inline const QString kToLine = QStringLiteral("转为直线");
inline const QString kDetachCircle = QStringLiteral("解除圆约束");
inline const QString kBakeToOperationLayer = QStringLiteral("烘焙到操作层");

} // namespace cad::cmd::texts
