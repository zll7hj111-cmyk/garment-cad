#pragma once

#include <QUuid>

#include <QWidget>

#include "parametric/Attachment.h"  // const Attachment* 返回值需要完整类型

class ElaLineEdit;
class ElaPushButton;
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

/// 「连接」行组 (2026-12 去卡框化): 连接拓扑 —— 连接行 (连接线段 L#·名 +
/// 连接点 P# + 拆开/重连 双面按钮) / 滑轨行 / 影子偏转行 / 省道行。
/// 2026-xx 用户拍板 (两维独立): 「连接线段」「独立角度」「连接保护」复选框
/// 与「清除」按钮已全部删除 —— 连接语义 = 两个正交维度的 拆开/重连 双面
/// 开关: 「连接点」按钮管位置维度 (拆开 = 位置自由、重连 = 位置回宿主+焊接),
/// 「基准点」按钮管角度维度 (SegmentRefCard: 拆开 = 角度不跟随、重连 = 恢复
/// 角度跟随); 双拆开 = 自由线。连接/重定向仍可输入 P# (或连接线段 L#/名)。
/// 角度/引用/指向点已抽出 (SegmentAngleCard / SegmentRefCard)。统一表单:
/// 无状态区分, 行集恒定, 无连接时按钮禁用 (仍显示)。
/// 纯行组 (无边框/无标题), 嵌入属性页「连接」分区。
class SegmentConnectionCard : public QWidget
{
    Q_OBJECT

public:
    /// 连接突变类型 (驱动对话框侧刷新粒度)。
    enum class ChangeKind {
        Connected,          ///< Free → connected (new attachment).
        Retargeted,         ///< Leader point changed.
        ConnectionModeChanged, ///< 拆开/重连/影子归零 等连接子状态变更.
        SlideModeChanged,   ///< 滑轨模式切换.
        AngleApplied,       ///< 省道 d/β 值应用.
    };

    explicit SegmentConnectionCard(cad::param::ParamDocument* doc, CanvasScene* scene,
                                   QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 整卡与模型同步 (连接行 + 滑轨 + 省道态)。
    void refresh();

signals:
    /// 模型已变更 —— 对话框刷新画布 (与拓扑变更时刷新 aux 连接列表)。
    void changed(ChangeKind kind);

private:
    // ── UI 构建 (SegmentConnectionCardBuild.cpp) ──
    void buildConnRow(QVBoxLayout* angleLayout);
    void buildDartRow();
    void buildSlideRow();
    /// 影子偏转读数行 (§2.6, 2026-08-27): 只读「随组偏转 N°」+ 归零按钮.
    void buildShadowRow();
    void connectSignals();

    // ── refresh (SegmentConnectionCardRefresh.cpp) ──
    void refreshUnifiedState(const cad::param::Attachment* att,
                             cad::param::Block* block, cad::param::Segment* seg);

    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    /// 统一「连接点」编辑入口: 已有连接 → 重定向; 自由线 → 建立连接。
    void onConnPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
    void onSlideModeChanged(int index);
    void onSlideOffsetEdited();
    /// 拆开/重连 双面按钮 (位置维度): 拆开 = 位置自由 (angleOnly)、重连 =
    /// 位置回原宿主+重新焊接; 无连接禁用。
    void onDetachClicked();
    /// 影子偏转「归零」(SetAttachmentBaselineOffsetCommand, 一步 undo).
    void onShadowResetClicked();
    void onLeaderSegEdited();
    /// 省道线参数编辑 (偏移 d / 角度 β, Enter/失焦即应用)。
    void onDartOffsetEdited();
    void onDartAngleEdited();
    /// doc resolved (任一重解): 只刷新省道反算角读数 (同值短路)。
    void onDocResolved();

    /// "L3·肩线" human-readable label for the leader reference.
    [[nodiscard]] QString leaderRefLabel(const cad::param::Attachment& att) const;
    /// 目标块的非 pin 跟随 attachment (自由线时 nullptr)。
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    void refreshCard();
    /// 省道线态 (用户拍板 2026-08): 挂起点 A、偏移点 B (只读)、反算跟随角度
    /// (灰只读) + 偏移 d / 角度 β 可编辑。B 所在线段不显示不修改 (单向挂靠)。
    void showDartState(const cad::param::Block& block, cad::param::Segment* seg);
    /// 反算跟随角度 (线方向 − 偏移点线段方向), 带符号折角显示。
    QString dartFoldAngleText(const cad::param::Block& block) const;

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    // Connected-state row: 基准线 label + 指向点 ref (editable) + 拆开/重连.
    ElaPushButton* m_btnDetach = nullptr;   ///< 拆开/重连 双面按钮 (连接行尾).
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblConnLabel = nullptr;   ///< 行首标签 恒为「连接线段:」.
    ElaText*      m_lblConnSub = nullptr;     ///< 子标签「连接点:」.
    ElaLineEdit*  m_lblLeaderRef = nullptr;   ///< 连接线段名称/ID（可输入 L# 或 P#）。
    ElaText*      m_lblLayerBadge = nullptr;
    ElaText*      m_lblAngleOnlyBadge = nullptr;
    PointRefEdit* m_refConnPoint = nullptr;
    // 滑轨行 (抽屉式滑动, 用户拍板 2026-08): 全连接 / 沿线滑动 / 垂直拉出.
    QWidget*      m_slideRow = nullptr;
    ElaText*      m_lblSlideBadge = nullptr;
    QComboBox*    m_cmbSlideMode = nullptr;
    ElaLineEdit* m_editSlideAlong = nullptr;
    ElaLineEdit* m_editSlidePerp = nullptr;
    // 影子偏转行 (§2.6): 只读读数 + 归零 (offset==0 整行隐藏, 零干扰).
    QWidget*      m_shadowRow = nullptr;
    ElaText*      m_lblShadowValue = nullptr;
    ElaPushButton* m_btnShadowReset = nullptr;
    // 省道线态 rows.
    QWidget*      m_dartRow = nullptr;
    ElaText*      m_dartStartRef = nullptr;
    ElaText*      m_dartRefLabel = nullptr;
    ElaText*      m_dartFoldLabel = nullptr;
    ElaLineEdit*    m_dartOffsetEdit = nullptr;
    ElaLineEdit*    m_dartAngleEdit = nullptr;
};

} // namespace cad::ui
