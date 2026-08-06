#pragma once

#include <QDialog>
#include <QUuid>
#include <QTimer>

#include <optional>
#include <vector>

#include "parametric/Attachment.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;
class QTabWidget;
class QDialogButtonBox;
class QPushButton;
class QVBoxLayout;
class QWidget;
class QColor;
class QListWidget;
class QGroupBox;

namespace cad::param {
class ParamDocument;
struct Segment;
struct ParamPoint;
struct MeasureVariable;
class Block;
}

class CanvasScene;

namespace cad::tools {

class AuxPointForm;
class IntersectionForm;
class PointRefEdit;

/// Modeless-style dialog for editing a segment's properties.
/// Changes apply LIVE as the user edits (no need to press a button for preview).
/// "关闭" accepts and closes; "撤销全部" reverts to the state before the dialog opened.
///
/// Page 1 ("属性") card layout:
///   基本信息 → 几何 → 跟随角度·连接 → 终点指向 → 外观 → 起点/终点.
class LinePropertyDialog : public QDialog
{
    Q_OBJECT

public:
    LinePropertyDialog(const QUuid& blockId, const QUuid& segmentId,
                       cad::param::ParamDocument* paramDoc,
                       CanvasScene* scene,
                       QWidget* parent = nullptr,
                       bool isCreation = false);
    ~LinePropertyDialog() override;

    [[nodiscard]] bool confirmed() const { return m_confirmed; }

    /// Switch the dialog to edit a different segment (used by the group tree).
    void setTarget(const QUuid& blockId, const QUuid& segmentId);

private slots:
    void onLiveUpdate();       ///< Apply current widget values to model + re-resolve + refresh.
    void onLengthApply();      ///< Apply length (triggered by debounce / Enter / focus-loss).
    void onLengthDirty();      ///< Mark length field as having pending changes.
    void onAngleApply();       ///< Apply angle (triggered by debounce / Enter / focus-loss).
    void onAngleDirty();       ///< Mark angle field as having pending changes.
    void onDebounceTimeout();  ///< Global 200ms debounce: apply all pending text fields.
    void onColorPick();        ///< Open color picker for segment color.
    void onWeightPresetChanged(int index); ///< Handle weight preset combo change.
    void onAccepted();
    void onRejected();         ///< Revert to snapshot.
    void onPublishLength();    ///< Publish this segment's length as a linked variable.

    // ── 跟随角度·连接 card ──
    void onConnPointResolved(const QUuid& blockId, const QUuid& pointId);
        ///< Re-target the follower attachment to a different leader point.
    void onConnClear();        ///< Detach the follower connection (remove attachment).
    void onConnectToResolved(const QUuid& blockId, const QUuid& pointId);
        ///< Create a new attachment from the free state (connect to a point).
    void onFollowHostToggled(bool on);
        ///< Attach/detach the start point to measure host A (measure lines).
    void onLockConnToggled(bool on);
        ///< Lock/unlock the follower connection (锁定连接: welded, not draggable apart).

    // ── 终点指向 card ──
    void onAimTargetResolved(const QUuid& blockId, const QUuid& pointId);
        ///< Set a custom endpoint-aim target point.
    void onAimOffsetApply();   ///< Apply endpoint-aim offset angle.
    void onAimClear();         ///< Clear the endpoint-aim constraint.
    void onAimHostToggled(bool on);
        ///< Aim at / release measure host B (measure lines).

    // Aux point tab slots
    void onAuxAdd();           ///< Add a new auxiliary point to the segment.
    void onAuxRemove();        ///< Remove selected auxiliary point.
    void onAuxSelectionChanged(); ///< Update edit fields when selection changes.
    void onAuxLiveUpdate();    ///< Apply aux point field values to model + refresh.

private:
    void buildPage1(QTabWidget* tabs);   ///< "属性" tab (card layout).
    void buildAnchorTab(QTabWidget* tabs); ///< "锚点" tab (curve pass-points).
    void buildPage3(QTabWidget* tabs);   ///< "辅助点" tab (含交点).
    void buildPage4(QTabWidget* tabs);   ///< "辅助点连接" tab.
    void populateFromModel();
    /// Bridge lines (Block::isBridge) have passive length/angle fully determined
    /// by their two host points. Grey out + disable the length/angle editors,
    /// hide the fx indicators, and show a caption hint. No-op for normal lines.
    void applyBridgeReadOnly();
    void applyToModel();
    void refreshScene();
    void refreshActualLengthLabel();  ///< Update the read-only resolved-length label.
    void connectLiveSignals();
    void updateWeightCombo();  ///< Sync weight combo with current model value.
    void refreshAuxList();     ///< Rebuild the aux point list widget.
    void populateAuxFields();  ///< Fill the correct edit form (aux or intersection)
                               ///< from the selected point.
    /// Refresh the direction combo's item labels so they show the CURRENT
    /// start/end endpoint serials+names.
    void refreshAuxDirLabels();

    /// Refresh the anchor-tab edit fields (tangent angle/length/locked/follow)
    /// when the selected anchor row changes.
    void refreshAnchorFields(int row);

    /// Refresh follower-state bookkeeping after the attachment topology may
    /// have changed: re-snapshot the follower attachment (for 撤销全部) and
    /// sync the angle card display.
    void refreshFollowerState();

    /// Rebuild the "辅助点连接" tab content.
    void refreshAuxConnTab();

    /// Attachment where the current block acts as the follower (跟随线), or
    /// nullptr when the block is free.
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    /// Populate the angle field (m_editAngle) from the model: shows the
    /// follower angle for a follower, the world angle for a free block.
    /// Also refreshes the angle card.
    void populateAngleField();

    /// Sync the 跟随角度·连接 card with the model: switch connected/free
    /// sub-rows, update the leader label, target point ref, world-angle hint,
    /// angle-mode button, and the follow-host checkbox (measure lines).
    void refreshAngleCard();

    /// Sync the 终点指向 card with the model: target point ref, offset field,
    /// clear button state, and the aim-host checkbox (measure lines).
    void refreshAimCard();

    /// The measure variable driving this segment's length (lengthFormula ==
    /// refName), or nullptr when the segment is not a measure line.
    [[nodiscard]] const cad::param::MeasureVariable* findBridgeMeasure() const;

    /// Build a human-readable label for a follower attachment's leader reference:
    /// "L3·肩线 @ P2·颈点".
    [[nodiscard]] QString leaderRefLabel(const cad::param::Attachment& att) const;

    /// Canvas highlight management (dialog lifetime = highlight lifetime).
    void applyCanvasHighlight();
    void clearCanvasHighlight();

    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    QTabWidget* m_tabs = nullptr;   ///< Main tab widget (owned by layout).
    bool m_confirmed = false;

    // Snapshot for cancel-revert
    struct Snapshot {
        QString segName;
        bool showName;
        bool showLength;
        bool visible;
        int role;
        QString lengthFormula;
        double distance;
        QString distanceFormula;  ///< End-point distance formula (for revert).
        double angle = 0;         ///< End-point Polar angle (free lines; angle
        QString angleFormula;     ///< editing may also flip the constraint).
        int constraint = 0;       ///< End-point constraint at open time (角度编辑
        QUuid refPointId;         ///< 会把非 Polar 点改写为 Polar, revert 需还原).
        double tension = 0;       ///< Curve tension (曲线张力).
        QString startName, startAnno;
        bool startShowName;
        QString endName, endAnno;
        bool endShowName;
        int lineStyle;
        double weight;
        QColor color;
        std::optional<cad::param::Attachment> followerAtt;  ///< Attachment where this block is the follower.
        QUuid endTargetBlockId;      ///< Endpoint-aim state (for revert).
        QUuid endTargetPointId;
        double endTargetOffset = 0;
        QString endTargetOffsetFormula;
    };
    Snapshot m_snapshot;
    /// True when the dialog was opened right after the smart pen CREATED the
    /// line — 撤销全部 then means 取消线段创建 (delete the line), not revert
    /// the snapshot. False (double-click / rotate entry) = edit state: 撤销
    /// 全部 reverts this session's changes.
    bool m_isCreation = false;

    // Page 1 widgets — 基本信息
    QLabel*       m_lblSegId      = nullptr;   ///< Serial shown as subtle grey text.
    QLineEdit*    m_editName      = nullptr;
    QComboBox*    m_cmbRole       = nullptr;   ///< Segment role: 轮廓线/内部线/辅助线.

    // Page 1 widgets — 几何
    QLineEdit*    m_editLength    = nullptr;   ///< Enter/blur to apply; orange border when dirty.
    QLabel*       m_lblFx         = nullptr;   ///< "fx" indicator shown when length is a formula.
    QCheckBox*    m_chkShowLength = nullptr;
    QLabel*       m_lblActualLength = nullptr; ///< Read-only resolved length (gray), shown beside 显示长度标注.
    QPushButton*  m_btnPublishLen = nullptr;   ///< "发布长度参数" button.
    QLabel*       m_lblArcLength  = nullptr;   ///< Read-only arc length display (curve only).
    QLineEdit*    m_editTension   = nullptr;   ///< Curve tension (curve only).
    QLabel*       m_lblTension    = nullptr;   ///< "张力:" label (curve only).
    QWidget*      m_arcRow        = nullptr;   ///< Container for arc-length row (curve only).
    QWidget*      m_tensionRow    = nullptr;   ///< Container for tension row (curve only).
    QPushButton*  m_btnConvert    = nullptr;   ///< "转为直线" button (curve only: removes all curve points).

    // Page "锚点" widgets (curve only)
    QWidget*      m_anchorTab     = nullptr;   ///< Tab page (hidden for lines).
    QListWidget*  m_anchorList    = nullptr;   ///< List of ALL anchors (start + curve pts + end).
    QComboBox*    m_cmbTanMode    = nullptr;   ///< Auto/Manual tangent mode.
    QPushButton*  m_btnResetTan   = nullptr;   ///< Reset to auto tangent.
    QLabel*       m_lblTanInfo    = nullptr;   ///< Tangent info display.
    // --- Anchor tangent editing (new) ---
    QDoubleSpinBox* m_spinTanAngleIn  = nullptr; ///< Tangent-in angle relative to chord (deg).
    QDoubleSpinBox* m_spinTanLenIn    = nullptr; ///< Tangent-in handle length (cm).
    QDoubleSpinBox* m_spinTanAngleOut = nullptr; ///< Tangent-out angle relative to chord (deg).
    QDoubleSpinBox* m_spinTanLenOut   = nullptr; ///< Tangent-out handle length (cm).
    QCheckBox*    m_chkTanLocked  = nullptr;   ///< Smooth (collinear) / Corner (independent).
    QLabel*       m_lblFollowInfo = nullptr;   ///< Follow connection display (curve pts only).
    QPushButton*  m_btnReleaseFollow = nullptr; ///< Release (释放) the follow connection.
    std::vector<QUuid> m_anchorPointIds;       ///< Ordered point IDs matching list rows.

    // Page 1 widgets — 跟随角度·连接 card
    QGroupBox*    m_grpAngle      = nullptr;   ///< Card container (title adapts to state).
    QWidget*      m_connRow       = nullptr;   ///< Connected-state row: leader label + target ref.
    QLabel*       m_lblLeaderRef  = nullptr;   ///< "基准线: L1·肩线" (read-only).
    QLabel*       m_lblLayerBadge = nullptr;   ///< Cross-layer badge "→ 操作层1" (leader layer name); hidden for same-layer connections.
    PointRefEdit* m_refConnPoint  = nullptr;   ///< Target point on the leader (editable).
    QPushButton*  m_btnClearConn  = nullptr;   ///< Detach the connection (remove attachment).
    QWidget*      m_freeConnRow   = nullptr;   ///< Free-state row: "连接至" input.
    PointRefEdit* m_refConnectTo  = nullptr;   ///< Point input to establish a connection.
    QPushButton*  m_btnAngleMode  = nullptr;   ///< Toggle: ∠ (angle) / ⌒ (arc length) for follower.
    QLabel*       m_lblAngleCaption = nullptr; ///< Row caption: "绝对角度(°):" / "跟随角度(°):" / "弧长(cm):".
    QLineEdit*    m_editAngle     = nullptr;   ///< World angle / follower angle / arc length.
    QLabel*       m_lblFxAngle    = nullptr;   ///< "fx" indicator for angle/arc formula.
    QLabel*       m_lblWorldAngle = nullptr;   ///< Read-only "= 绝对角度 xx°" hint (follower only).
    QCheckBox*    m_chkFollowHost = nullptr;   ///< "跟随宿主 P5" (all lines).
    QCheckBox*    m_chkLockConn   = nullptr;   ///< "锁定连接" (all lines, disabled when free).

    // Page 1 widgets — 终点指向 card
    PointRefEdit* m_refAimTarget  = nullptr;   ///< Aim target point (editable).
    QLineEdit*    m_editAimOffset = nullptr;   ///< Offset angle (°) from exact aim.
    QPushButton*  m_btnClearAim   = nullptr;   ///< Clear the aim constraint.
    QCheckBox*    m_chkAimHost    = nullptr;   ///< "终点指向宿主 P8" (all lines).

    // Page 1 widgets — 外观
    QComboBox*    m_cmbStyle      = nullptr;
    QComboBox*    m_cmbWeight     = nullptr;   ///< Preset: 细/中/粗/自定义.
    QDoubleSpinBox* m_spinWeight  = nullptr;   ///< Custom weight (visible when 自定义).
    QPushButton*  m_btnColor      = nullptr;   ///< Color swatch button.
    QCheckBox*    m_chkVisible    = nullptr;
    QCheckBox*    m_chkShowName   = nullptr;

    // Page 1 widgets — 端点
    QLabel*       m_lblStartPtId  = nullptr;
    QLineEdit*    m_editStartName = nullptr;
    QCheckBox*    m_chkShowStartName = nullptr;
    QLineEdit*    m_editStartAnno = nullptr;
    QLabel*       m_lblEndPtId    = nullptr;
    QLineEdit*    m_editEndName   = nullptr;
    QCheckBox*    m_chkShowEndName = nullptr;
    QLineEdit*    m_editEndAnno   = nullptr;

    QColor m_currentColor;  ///< Current segment color.

    // Page 3 ("辅助点") widgets — 辅助点与交点共用一个列表，按类型切换表单
    QListWidget*  m_auxList       = nullptr;   ///< List of aux + intersection points.
    AuxPointForm* m_auxForm       = nullptr;   ///< Form for Interpolated points.
    IntersectionForm* m_ixForm    = nullptr;   ///< Form for Intersection points.
    QTimer*       m_debounce       = nullptr;   ///< Global 200ms debounce timer for text field auto-apply.
    QUuid         m_currentAuxId;               ///< Currently selected point (stable across refreshes).

    // Page 4 ("辅助点连接") widgets
    QVBoxLayout*  m_auxConnLayout = nullptr;   ///< Container for aux connection cards.

    // Snapshot for aux points (revert support)
    struct AuxSnapshot {
        QUuid pointId;
        double percent;
        QString percentFormula;
        double constant;
        QString constantFormula;
        bool fromEnd;
        bool showName;
        QString name;
    };
    std::vector<AuxSnapshot> m_auxSnapshots;  ///< Initial state of aux points for revert.
    std::vector<QUuid> m_auxAddedIds;         ///< Aux points added during this dialog session (remove on revert).
};

} // namespace cad::tools
