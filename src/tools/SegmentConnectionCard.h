#pragma once

#include <QUuid>

#include "ElaScrollPageArea.h"

class ElaLineEdit;
class ElaCheckBox;
class ElaPushButton;
class ElaText;
class QComboBox;
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
/// connection row (leader ref / re-target / clear), the free-state 跟随宿主
/// input, the angle/arc editor with its mode toggle and readouts, plus the
/// 跟随宿主 and 拖动保护 checkboxes. Mutations apply LIVE and are reported
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
        AngleOnlyToggled, ///< 拆开/恢复: 位置吸附开关 (跟随宿主 checkbox, 角度跟随保留).
        SlideModeChanged, ///< 滑轨模式切换 (全连接/沿线/垂直, 抽屉式滑动).
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
    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    void onClear();
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
    void onFollowHostToggled(bool on);
    void onLockToggled(bool on);
    void onSlideModeChanged(int index);
    void onModeToggle();
    void onAngleDirty();
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

    ElaText* m_titleLabel = nullptr;  ///< Card title (kept in sync with setTitle).

    // Connected-state row: 基准线 label + 指向点 ref (editable).
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblLeaderRef = nullptr;
    ElaText*      m_lblLayerBadge = nullptr;
    ElaText*      m_lblAngleOnlyBadge = nullptr;  ///< "仅角度 · 位置自由" (拆开保留角度).
    PointRefEdit* m_refConnPoint = nullptr;
    ElaPushButton*  m_btnClearConn = nullptr;
    // Free-state row: 跟随宿主 input (typed P number establishes the follow).
    QWidget*      m_freeConnRow = nullptr;
    PointRefEdit* m_refConnectTo = nullptr;
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
    ElaCheckBox*    m_chkFollowHost = nullptr;   ///< 跟随宿主 P5 (all lines).
    ElaCheckBox*    m_chkLockConn = nullptr;     ///< 拖动保护 (all lines).
    // 省道线态 row (用户拍板 2026-08): 起点 A / 偏移点 B (只读) + 反算角度 (灰)
    // + 偏移 d / 角度 β 输入。只有 block->isDart() 时可见。
    QWidget*      m_dartRow = nullptr;
    ElaText*      m_dartStartRef = nullptr;   ///< "起点 A: P3".
    ElaText*      m_dartRefLabel = nullptr;   ///< "偏移点 B: L5·肩线" (只读).
    ElaText*      m_dartFoldLabel = nullptr;  ///< "跟隨角度（反算）: 23.5°" (灰只读).
    ElaLineEdit*    m_dartOffsetEdit = nullptr;  ///< 偏移 d (mm 数值或 cm 公式).
    ElaLineEdit*    m_dartAngleEdit = nullptr;   ///< 角度 β (度, 相对线段).
};

} // namespace cad::tools
