#pragma once

#include <QUuid>

#include <optional>

#include "ElaScrollPageArea.h"

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

namespace cad::tools {

class PointRefEdit;

/// "跟随角度 · 连接" card of the line property dialog: the follower
/// connection row (leader ref / re-target / clear), the free-state 位置吸附
/// input, the angle/arc editor with its mode toggle and readouts, plus the
/// 位置吸附 and 拖动保护 checkboxes. Mutations apply LIVE and are reported
/// via changed(); the owning dialog refreshes the scene / aux tab and owns
/// the debounce timer (angleEdited() restarts it, applyAngle() is the
/// debounce sink).
class SegmentConnectionCard : public ElaScrollPageArea
{
    Q_OBJECT

public:
    /// What kind of connection/angle mutation just happened (drives the
    /// dialog-side refresh granularity — mirror of the pre-extraction slots).
    enum class ChangeKind {
        Connected,      ///< Free → connected (new attachment / 仅角度恢复完整连接).
        Disconnected,   ///< Connection removed (清除).
        Retargeted,     ///< Leader point changed.
        AngleOnlyToggled, ///< 拆开/恢复: 位置吸附开关 (位置吸附 checkbox, 角度跟随保留).
        SlideModeChanged, ///< 滑轨模式切换 (全连接/沿线/垂直, 抽屉式滑动).
        AngleIndependentToggled, ///< 角度独立开关 flipped (位置吸附保持, 角度不跟随).
        AngleRefChanged, ///< 角度基准切换 (位置锚点不变, 角度改由另一线段约束).
        ConnectionModeChanged, ///< 连接模式切换 (完整跟随/双基准/仅角度/角度独立).
        LockToggled,    ///< 拖动保护 flipped.
        ModeSwitched,   ///< angle ↔ arc length (geometry-preserving).
        AngleApplied,   ///< Angle/arc value or formula applied.
    };

    explicit SegmentConnectionCard(cad::param::ParamDocument* doc, CanvasScene* scene,
                                   QWidget* parent = nullptr);

    /// Switch the block/segment being edited (dialog target change).
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// Sync the whole card (connection rows + angle editor) from the model.
    void refresh();
    /// Sync ONLY the angle editor (used after aim-card mutations).
    void populateAngleField();
    /// Bridge lines: show the measured world angle read-only (the dialog
    /// mirrors the same treatment for the length editor). No-op otherwise;
    /// refresh() re-enables the editor for normal lines.
    void setBridgeReadOnly(bool bridge);
    /// Apply the angle editor (debounce / Enter / focus-loss): writes the
    /// follower angle/arc length or the free endpoint's Polar angle.
    void applyAngle();

signals:
    /// Model mutated — the dialog refreshes the scene (and, for connection
    /// topology changes, the aux tab's connection list).
    void changed(ChangeKind kind);
    /// Angle text edited — the dialog restarts its debounce timer.
    void angleEdited();

private:
    // ── UI 构建 (SegmentConnectionCardBuild.cpp, 2026-08 拆分) ──
    // 构造函数按行委托给这些 builders；布局装配顺序保留在构造函数内。
    void buildModeRow(QVBoxLayout* angleLayout);
    void buildConnRow(QVBoxLayout* angleLayout);
    [[nodiscard]] QHBoxLayout* buildAngleRow();
    void buildDartRow();
    void buildSlideRow();
    [[nodiscard]] QHBoxLayout* buildConnControls();
    void buildAngleRefRow();
    void buildAimRow();
    void connectSignals();

    // ── refreshCard 分块 (SegmentConnectionCardRefresh.cpp, 2026-08 拆分) ──
    void refreshModeCombo(const cad::param::Attachment* att);
    void refreshConnectedState(const cad::param::Attachment* att,
                               cad::param::Block* block, cad::param::Segment* seg);
    void refreshFreeState(cad::param::Block* block, cad::param::Segment* seg);
    void refreshConnectionToggles(const cad::param::Attachment* att);
    void refreshAngleRefRow(const cad::param::Attachment* att);
    void refreshAimRow(const cad::param::Block* block);

    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    /// 统一「连接点」编辑入口: 已有连接 → 重定向; 自由线 → 建立连接。
    void onConnPointResolved(const QUuid& blockId, const QUuid& pointId);
    /// 模式切到「独立线段」: 快照完整连接配置 (m_modeCache) 后拆除, 切回
    /// 「跟随」时原样恢复 (缓存效果, 无跳变需前景几何未变)。
    void detachWithCache();
    void onClear();
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
    void onFollowHostToggled(bool on);
    void onLockToggled(bool on);
    void onAngleIndependentToggled(bool on);
    void onSlideModeChanged(int index);
    void onSlideOffsetEdited();
    void onModeChanged(int index);
    void onModeToggle();
    void onAngleDirty();

    void onAngleRefPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onLeaderSegEdited();
    void onAngleRefSegEdited();
    void onAngleOnlyClicked();
    void onAimTargetResolved(const QUuid& blockId, const QUuid& pointId);
    void onAimOffsetApply();
    void onClearAim();
    void onClearAngleRef();
    /// Dart-line parameter editors (偏移 d / 角度 β, applied on Enter/focus-loss).
    void onDartOffsetEdited();
    void onDartAngleEdited();
    /// Doc resolved (any re-solve, incl. per-frame drags): refresh the
    /// geometry-dependent readouts (绝对角度 hint) without touching the input.
    void onDocResolved();

    /// "L3·肩线" human-readable label for the leader reference.
    [[nodiscard]] QString leaderRefLabel(const cad::param::Attachment& att) const;
    /// Non-pin follower attachment of the target block (nullptr when free).
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    /// Sync the connection rows + captions (former refreshAngleCard).
    void refreshCard();
    /// 绝对角度 hint (闭合基准: refWorld + 180° − 线夹角, 归一化 [0,360°)).
    /// Same-value short-circuits the label text (per-frame safe).
    void updateWorldAngleLabel(const cad::param::Attachment& att);
    /// 省道线态 (用户拍板 2026-08): 挂起点 A、偏移点 B（只读）、反算跟随角度
    /// (灰只读) + 偏移 d / 角度 β 可编辑。B 所在线段不显示不修改（单向挂靠）。
    void showDartState(const cad::param::Block& block, cad::param::Segment* seg);
    /// 反算跟随角度 (线方向 − 偏移点线段方向), 带符号折角显示。
    QString dartFoldAngleText(const cad::param::Block& block) const;

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    // 最近宿主记忆 (用户拍板 2026-10): 位置吸附取消（断开）时记录最近宿主点,
    // 重新勾选「位置吸附」可一键恢复连接 (outline: back-solve 角度, 无跳变),
    // 无需重新输入 P 编号。setTarget 换目标线时重置并重新播种。
    QUuid m_hostMemBlockId;
    QUuid m_hostMemPointId;
    /// 「跟随 ↔ 独立线段」切换缓存 (用户 2026-12 需求): 切到独立线段时快照
    /// 完整连接配置 (host/角度/弧长/公式/角度基准/滑轨/角度独立/焊接), 切回
    /// 跟随时原样恢复 —— 不再丢失连接设置。setTarget 换线时重置并重新播种。
    std::optional<cad::param::Attachment> m_modeCache;

    ElaText* m_titleLabel = nullptr;  ///< Card title (kept in sync with setTitle).
    QComboBox*    m_cmbMode = nullptr;        ///< 五态连接模式下拉.
    QWidget*      m_modeRow = nullptr;

    // Connected-state row: 基准线 label + 指向点 ref (editable).
    ElaPushButton* m_btnAngleOnly = nullptr;  ///< 快速进入「仅角度」的按钮（快拆）。
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblConnLabel = nullptr;   ///< 行首标签: 连接态「连接线段:」/ 自由态「位置吸附:」.
    ElaText*      m_lblConnSub = nullptr;     ///< 子标签「连接点:」(连接态显示; 自由态隐藏).
    ElaLineEdit*  m_lblLeaderRef = nullptr;   ///< 连接线段名称/ID（可输入 L# 或 P#）。
    ElaText*      m_lblLayerBadge = nullptr;
    ElaText*      m_lblAngleOnlyBadge = nullptr;  ///< "仅角度 · 位置自由" (拆开保留角度).
    PointRefEdit* m_refConnPoint = nullptr;
    ElaPushButton*  m_btnClearConn = nullptr;
    // Angle value row: [mode] [caption] [fx] [input] [world-angle readout].
    ElaPushButton*  m_btnAngleMode = nullptr;
    ElaText*      m_lblAngleCaption = nullptr;
    ElaText*      m_lblFxAngle = nullptr;
    ElaLineEdit*    m_editAngle = nullptr;
    ElaText*      m_lblFollowValue = nullptr;  ///< Formula current-value readout.
    ElaText*      m_lblWorldAngle = nullptr;   ///< "= 绝对角度 xx°" hint.
    // 滑轨模式 row (抽屉式滑动, 用户拍板 2026-08): 全连接 / 沿线滑动 /
    // 垂直拉出 selector + 状态 badge. Visible only while connected.
    QWidget*      m_slideRow = nullptr;
    ElaText*      m_lblSlideBadge = nullptr;   ///< "沿线滑动" / "垂直拉出" badge.
    QComboBox*    m_cmbSlideMode = nullptr;
    ElaCheckBox*    m_chkFollowHost = nullptr;   ///< 位置吸附 P5 (all lines).

    ElaLineEdit* m_editSlideAlong = nullptr;   ///< 沿基准线方向偏移 (mm).
    ElaLineEdit* m_editSlidePerp = nullptr;    ///< 垂直基准线方向偏移 (mm).
    ElaCheckBox*    m_chkLockConn = nullptr;     ///< 拖动保护 (all lines).
    ElaCheckBox*    m_chkAngleIndependent = nullptr; ///< 角度独立：位置吸附保持、不跟随基准角度.
    // 省道线态 row (用户拍板 2026-08): 起点 A / 偏移点 B (只读) + 反算角度 (灰)
    ElaLineEdit*  m_lblAngleRefSeg = nullptr;   ///< 角度基准线段名称/ID（可输入 L# 或 P#）。
    /// 角度基准分离 (用户需求 2026): 选择另一条线段的端点作为“角度基准点”,
    /// 本线位置仍吸附在 toBlock, 但角度改为相对这条线段方向计算。
    QWidget*      m_angleRefRow = nullptr;
    PointRefEdit* m_angleRefPoint = nullptr;
    ElaPushButton* m_btnClearAngleRef = nullptr;
    // 指向（终点指向）并入连接卡片：新增指向点 + 偏移 + 清除。
    QWidget*      m_aimRow = nullptr;
    PointRefEdit* m_refAimPoint = nullptr;
    ElaLineEdit*  m_editAimOffset = nullptr;
    ElaPushButton* m_btnClearAim = nullptr;
    // + 偏移 d / 角度 β 输入。只有 block->isDart() 时可见。
    QWidget*      m_dartRow = nullptr;
    ElaText*      m_dartStartRef = nullptr;   ///< "起点 A: P3".
    ElaText*      m_dartRefLabel = nullptr;   ///< "偏移点 B: L5·肩线" (只读).
    ElaText*      m_dartFoldLabel = nullptr;  ///< "跟隨角度（反算）: 23.5°" (灰只读).
    ElaLineEdit*    m_dartOffsetEdit = nullptr;  ///< 偏移 d (mm 数值或 cm 公式).
    ElaLineEdit*    m_dartAngleEdit = nullptr;   ///< 角度 β (度, 相对线段).
};

} // namespace cad::tools
