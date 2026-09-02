#pragma once

#include <QUuid>

#include <QPushButton>
#include <QWidget>

#include "parametric/Attachment.h"  // const Attachment* 返回值需要完整类型

class ElaLineEdit;
class ElaText;
class QComboBox;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

namespace cad::param {
class ParamDocument;
struct Attachment;
class Block;
struct Segment;
}

class CanvasScene;

namespace cad::ui {

class PointRefEdit;

/// 「连接」行组 (2026-12 去卡框化): 连接拓扑 —— 起点连接行 (连接线段 L#·名 +
/// 连接点 P# + 拆开/重连 双面按钮) / 终点连接行 (同形, 引擎载体 = endTarget
/// 终点指向 + 偏移)。
/// 2026-xx 用户拍板 (每端完整连接): 一条线段两个端点各有一行连接 —— 起点 =
/// Attachment (位置+角度跟随), 终点 = endTarget 指向 (Resolver Step 7, 桥接
/// 落点模式自动发布 M_xxx 驱动长度 → 终点精确落点); 双端都连上 = 桥接线
/// (角度/基准线/滑轨全部失效, 互斥由 SegmentRefCard 隐藏 + 本卡禁用表达)。
/// 2026-xx 用户拍板 (两维独立): 「连接线段」「独立角度」「连接保护」复选框
/// 与「清除」按钮已全部删除 —— 连接语义 = 两个正交维度的 拆开/重连 双面
/// 开关: 「连接点」按钮管位置维度 (拆开 = 位置自由、重连 = 位置回宿主+焊接),
/// 「基准点」按钮管角度维度 (SegmentRefCard: 拆开 = 角度不跟随、重连 = 恢复
/// 角度跟随); 双拆开 = 自由线。连接/重定向仍可输入 P# (或连接线段 L#/名)。
/// 角度/引用/指向点已抽出 (SegmentAngleCard / SegmentRefCard)。统一表单:
/// 无状态区分, 行集恒定, 无连接时按钮禁用 (仍显示)。
/// 2026-08 死代码清理: ChangeKind 枚举删除 (旧 aux 连接列表消费者已删,
/// 对话框侧刷新粒度不再区分突变类型 —— 任何变更走同一套全量联动刷新)。
/// 纯行组 (无边框/无标题), 嵌入属性页「连接」分区。
class SegmentConnectionCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentConnectionCard(cad::param::ParamDocument* doc, CanvasScene* scene,
                                   QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 整卡与模型同步 (连接行 + 终点行)。
    void refresh();

signals:
    /// 模型已变更 —— 对话框刷新画布与联动摘要行。
    void changed();

private:
    // ── UI 构建 (SegmentConnectionCardBuild.cpp) ──
    void buildConnRow(QVBoxLayout* lay);
    /// 终点连接行组 (2026-xx 每端完整连接): caption + [连接线段][连接点][偏移]
    /// [拆开/重连]。
    void buildEndRow(QVBoxLayout* lay);
    void connectSignals();

    // ── refresh (SegmentConnectionCardRefresh.cpp) ──
    void refreshUnifiedState(const cad::param::Attachment* att,
                             cad::param::Block* block);
    /// 终点连接行刷新 (endTarget 状态 + 拆开/重连记忆)。
    void refreshEndRow(const cad::param::Block* block);

    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    /// 统一「连接点」编辑入口: 已有连接 → 重定向; 自由线 → 建立连接。
    void onConnPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
    /// 拆开/重连 双面按钮 (位置维度): 拆开 = 位置自由 (angleOnly)、重连 =
    /// 位置回原宿主+重新焊接; 无连接禁用。
    void onDetachClicked();
    /// 「连接线段」框 (PointRefEdit) 解析成功: L#/名称 → 线段起点 或 P# → 该点。
    /// 已有连接 = 重定向; 自由线 = 预填到「连接点」框 (回车第二次才建立)。
    void onLeaderSegResolved(const QUuid& blockId, const QUuid& pointId);

    // ── 终点连接行 actions (2026-xx 每端完整连接) ──
    /// 终点「连接点」统一入口: 已有指向 → 重定向; 自由 → 建立。
    void onEndConnPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onEndLeaderSegResolved(const QUuid& blockId, const QUuid& pointId);
    /// 偏移(°) 应用 (SetEndTargetCommand, 一步 undo)。
    void onEndOffsetEdited();
    /// 终点拆开/重连 双面按钮: 拆开 = 清终点指向 (记忆目标)、重连 = 恢复。
    void onEndDetachClicked();
    /// 目标块的非 pin 跟随 attachment (自由线时 nullptr)。
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    void refreshCard();

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    // Connected-state row: 基准线 label + 指向点 ref (editable) + 拆开/重连.
    QPushButton* m_btnDetach = nullptr;   ///< 拆开/重连 双面按钮 (连接行尾).
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblConnLabel = nullptr;   ///< 行首标签 恒为「连接线段:」.
    ElaText*      m_lblConnSub = nullptr;     ///< 子标签「连接点:」.
    PointRefEdit* m_refLeaderSeg = nullptr;   ///< 连接线段框 (PointRefEdit 统一解析, 2026-08).
    PointRefEdit* m_refConnPoint = nullptr;
    // ── 终点连接行 (2026-xx 每端完整连接) ──
    QWidget*      m_endRow = nullptr;
    ElaText*      m_lblEndConnLabel = nullptr;   ///< 副标签「连接线段」.
    PointRefEdit* m_refEndLeaderSeg = nullptr;   ///< 终点连接线段框 (PointRefEdit 统一解析).
    ElaText*      m_lblEndConnSub = nullptr;     ///< 子标签「连接点」.
    PointRefEdit* m_refEndPoint = nullptr;       ///< 终点目标点 (endTarget).
    ElaLineEdit*  m_editEndOffset = nullptr;     ///< 指向偏移 (°).
    QPushButton*  m_btnEndDetach = nullptr;      ///< 终点拆开/重连 双面按钮.
    // 终点拆开记忆 (拆开前快照, 重连恢复; 宿主删除则丢弃)。
    QUuid m_endMemBlock;
    QUuid m_endMemPoint;
    double m_endMemOffset = 0.0;
};

} // namespace cad::ui
