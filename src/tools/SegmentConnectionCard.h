#pragma once

#include <QUuid>

#include "ElaScrollPageArea.h"

class ElaLineEdit;
class ElaCheckBox;
class ElaPushButton;
class ElaText;
class QWidget;

namespace cad::param {
class ParamDocument;
struct Attachment;
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
        Connected,      ///< Free → connected (new attachment).
        Disconnected,   ///< Connection removed (清除 / host checkbox off).
        Retargeted,     ///< Leader point changed.
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
    void onModeToggle();
    void onAngleDirty();

    /// "L3·肩线" human-readable label for the leader reference.
    [[nodiscard]] QString leaderRefLabel(const cad::param::Attachment& att) const;
    /// Non-pin follower attachment of the target block (nullptr when free).
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    /// Sync the connection rows + captions (former refreshAngleCard).
    void refreshCard();

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText* m_titleLabel = nullptr;  ///< Card title (kept in sync with setTitle).

    // Connected-state row: 基准线 label + 指向点 ref (editable).
    QWidget*      m_connRow = nullptr;
    ElaText*      m_lblLeaderRef = nullptr;
    ElaText*      m_lblLayerBadge = nullptr;
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
    ElaCheckBox*    m_chkFollowHost = nullptr;   ///< 跟随宿主 P5 (all lines).
    ElaCheckBox*    m_chkLockConn = nullptr;     ///< 拖动保护 (all lines).
};

} // namespace cad::tools
