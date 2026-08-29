#pragma once

#include <QUuid>

#include <optional>

#include <QWidget>

#include "parametric/Attachment.h"  // std::optional<Attachment> 需要完整类型

class ElaLineEdit;
class ElaCheckBox;
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
/// 连接点 P# + 拆开 + 清除) / 「连接线段」复选框 (唯一连接开关) + 独立角度☐ +
/// 滑轨行; 省道线态由本卡接管 (showDartState)。角度/引用/指向点已抽出
/// (SegmentAngleCard / SegmentRefCard)。统一表单: 无状态区分, 行集恒定,
/// 无连接时 清除/拆开/独立角度/滑轨 只是禁用 (仍显示)。
/// 纯行组 (无边框/无标题), 嵌入属性页「连接」分区。
class SegmentConnectionCard : public QWidget
{
    Q_OBJECT

public:
    /// 连接突变类型 (驱动对话框侧刷新粒度)。
    enum class ChangeKind {
        Connected,          ///< Free → connected (new attachment / 仅角度恢复完整连接).
        Disconnected,       ///< Connection removed (清除).
        Retargeted,         ///< Leader point changed.
        AngleOnlyToggled,   ///< 拆开/恢复.
        SlideModeChanged,   ///< 滑轨模式切换.
        AngleIndependentToggled, ///< 角度独立开关 flipped.
        ConnectionModeChanged, ///< 连接子状态变更 (拆开等).
        LockToggled,        ///< 拖动保护 flipped.
        AngleApplied,       ///< 角度/弧长值应用 (保留枚举, 现由 SegmentAngleCard 发出).
    };

    explicit SegmentConnectionCard(cad::param::ParamDocument* doc, CanvasScene* scene,
                                   QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 整卡与模型同步 (连接行 + 开关 + 滑轨 + 省道态)。
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
    [[nodiscard]] QHBoxLayout* buildConnControls();
    void connectSignals();

    // ── refresh (SegmentConnectionCardRefresh.cpp) ──
    void refreshUnifiedState(const cad::param::Attachment* att,
                             cad::param::Block* block, cad::param::Segment* seg);
    void refreshConnectionToggles(const cad::param::Attachment* att);

    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    /// 统一「连接点」编辑入口: 已有连接 → 重定向; 自由线 → 建立连接。
    void onConnPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onClear();
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
    void onFollowHostToggled(bool on);
    void onLockToggled(bool on);
    void onAngleIndependentToggled(bool on);
    void onSlideModeChanged(int index);
    void onSlideOffsetEdited();
    void onAngleOnlyClicked();
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

    // 最近宿主记忆 (用户拍板 2026-10): 断开时记录最近宿主点, 重新勾选一键恢复.
    QUuid m_hostMemBlockId;
    QUuid m_hostMemPointId;
    /// 断开记忆快照 (用户 2026-12): 取消勾选/清除时快照完整连接配置, 重连
    /// 「连接线段」时优先原样恢复; 快照失效 (宿主已删/校验失败) 丢弃退回记忆宿主新建.
    std::optional<cad::param::Attachment> m_modeCache;

    // Connected-state row: 基准线 label + 指向点 ref (editable) + 拆开/清除.
    ElaPushButton* m_btnAngleOnly = nullptr;  ///< 快速进入「仅角度」的按钮（快拆，连接行尾）。
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblConnLabel = nullptr;   ///< 行首标签 恒为「连接线段:」.
    ElaText*      m_lblConnSub = nullptr;     ///< 子标签「连接点:」.
    ElaLineEdit*  m_lblLeaderRef = nullptr;   ///< 连接线段名称/ID（可输入 L# 或 P#）。
    ElaText*      m_lblLayerBadge = nullptr;
    ElaText*      m_lblAngleOnlyBadge = nullptr;
    PointRefEdit* m_refConnPoint = nullptr;
    ElaPushButton*  m_btnClearConn = nullptr;
    // 滑轨行 (抽屉式滑动, 用户拍板 2026-08): 全连接 / 沿线滑动 / 垂直拉出.
    QWidget*      m_slideRow = nullptr;
    ElaText*      m_lblSlideBadge = nullptr;
    QComboBox*    m_cmbSlideMode = nullptr;
    ElaLineEdit* m_editSlideAlong = nullptr;
    ElaLineEdit* m_editSlidePerp = nullptr;
    // 连接控制组: 连接线段开关 (唯一连接开关) / 连接保护 (隐藏) / 独立角度.
    ElaCheckBox*    m_chkFollowHost = nullptr;
    ElaCheckBox*    m_chkLockConn = nullptr;
    ElaCheckBox*    m_chkAngleIndependent = nullptr;
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
