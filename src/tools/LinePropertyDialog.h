#pragma once

#include "ElaDialog.h"
#include "ElaText.h"
#include <QUuid>
#include <QTimer>

#include <optional>
#include <vector>

#include "parametric/Attachment.h"
#include "SegmentAimCard.h"
#include "SegmentConnectionCard.h"

class ElaLineEdit;
class ElaComboBox;
class ElaCheckBox;
class ElaText;
class ElaDoubleSpinBox;
class ElaTabWidget;
class QDialogButtonBox;
class ElaPushButton;
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
class SegmentAnchorTab;
class SegmentAuxTab;

/// Modeless-style dialog for editing a segment's properties.
/// Changes apply LIVE as the user edits (no need to press a button for preview).
/// "关闭" accepts and closes; "撤销全部" reverts to the state before the dialog opened.
///
/// Page 1 ("属性") card layout:
///   基本信息 → 几何 → 跟随角度·连接 → 终点指向 → 外观 → 起点/终点.
class LinePropertyDialog : public ElaDialog
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

    /// Transient hold-to-show override (N/L keys held on canvas): the four
    /// display toggles are repainted as (model value OR force) WITHOUT writing
    /// the model (snapshot semantics — releasing restores the model's own
    /// flags). Pass both false to return to plain model values.
    void applyHoldOverride(bool forceName, bool forceLength);

protected:
    /// Esc / window-X close = revert to snapshot (same semantics as the
    /// "撤销全部" button — QDialog's default reject() would skip onRejected).
void reject() override;

private slots:
    void onLiveUpdate();       ///< Apply current widget values to model + re-resolve + refresh.
    void onLengthApply();      ///< Apply length (triggered by debounce / Enter / focus-loss).
    void onLengthDirty();      ///< Mark length field as having pending changes.
    void onDebounceTimeout();  ///< Global 200ms debounce: apply all pending text fields.
    void onColorPick();        ///< Open color picker for segment color.
    void onWeightPresetChanged(int index); ///< Handle weight preset combo change.
    void onAccepted();
    void onRejected();         ///< Revert to snapshot.
    void onPublishLength();    ///< Publish this segment's length as a linked variable.

    // ── 跟随角度·连接 card (extracted to SegmentConnectionCard) ──
    void onConnCardChanged(SegmentConnectionCard::ChangeKind kind);
        ///< Scene/aux-tab re-sync after a connection/angle mutation.

    // ── 终点指向 card (extracted to SegmentAimCard) ──
    void onAimCardChanged(SegmentAimCard::ChangeKind kind);
        ///< Scene/angle/follower re-sync after an aim mutation.

private:
    void buildPage1(ElaTabWidget* tabs);   ///< "属性" tab (card layout).
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

    /// The measure variable driving this segment's length (lengthFormula ==
    /// refName), or nullptr when the segment is not a measure line.
    [[nodiscard]] const cad::param::MeasureVariable* findBridgeMeasure() const;

    /// Canvas highlight management (dialog lifetime = highlight lifetime).
    void applyCanvasHighlight();
    void clearCanvasHighlight();

    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    ElaTabWidget* m_tabs = nullptr;   ///< Main tab widget (owned by layout).
    bool m_confirmed = false;

    /// Extracted sub-tabs (anchor / aux-point / point-connection pages).
    SegmentAnchorTab* m_anchorTab = nullptr;
    SegmentAuxTab*    m_auxTab = nullptr;
    SegmentAimCard*   m_aimCard = nullptr;   ///< 终点指向 card.
    SegmentConnectionCard* m_connCard = nullptr;  ///< 跟随角度·连接 card.

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
    ElaText*      m_lblSegId      = nullptr;   ///< Serial shown as subtle grey text.
    ElaLineEdit*    m_editName      = nullptr;
    ElaComboBox*    m_cmbRole       = nullptr;   ///< Segment role: 轮廓线/内部线/辅助线.

    // Page 1 widgets — 几何
    ElaLineEdit*    m_editLength    = nullptr;   ///< Enter/blur to apply; orange border when dirty.
    ElaText*      m_lblFx         = nullptr;   ///< "fx" indicator shown when length is a formula.
    ElaCheckBox*    m_chkShowLength = nullptr;
    ElaText*      m_lblActualLength = nullptr; ///< Read-only resolved length (gray), shown beside 显示长度标注.
    ElaPushButton*  m_btnPublishLen = nullptr;   ///< "发布长度参数" button.
    ElaText*      m_lblArcLength  = nullptr;   ///< Read-only arc length display (curve only).
    ElaLineEdit*    m_editTension   = nullptr;   ///< Curve tension (curve only).
    ElaText*      m_lblTension    = nullptr;   ///< "张力:" label (curve only).
    QWidget*      m_arcRow        = nullptr;   ///< Container for arc-length row (curve only).
    QWidget*      m_tensionRow    = nullptr;   ///< Container for tension row (curve only).
    ElaPushButton*  m_btnConvert    = nullptr;   ///< "转为直线" button (curve only: removes all curve points).

    // Page 1 widgets — 外观
    ElaComboBox*    m_cmbStyle      = nullptr;
    ElaComboBox*    m_cmbWeight     = nullptr;   ///< Preset: 细/中/粗/自定义.
    ElaDoubleSpinBox* m_spinWeight  = nullptr;   ///< Custom weight (visible when 自定义).
    QPushButton*  m_btnColor      = nullptr;   ///< Color swatch button.
    ElaCheckBox*    m_chkVisible    = nullptr;
    ElaCheckBox*    m_chkShowName   = nullptr;

    // Page 1 widgets — 端点
    ElaText*      m_lblStartPtId  = nullptr;
    ElaLineEdit*    m_editStartName = nullptr;
    ElaCheckBox*    m_chkShowStartName = nullptr;
    ElaLineEdit*    m_editStartAnno = nullptr;
    ElaText*      m_lblEndPtId    = nullptr;
    ElaLineEdit*    m_editEndName   = nullptr;
    ElaCheckBox*    m_chkShowEndName = nullptr;
    ElaLineEdit*    m_editEndAnno   = nullptr;

    QColor m_currentColor;  ///< Current segment color.

    QTimer*       m_debounce       = nullptr;   ///< Global 200ms debounce timer for text field auto-apply.
    /// Coalescing guard for refreshScene(): heavy resolveAll + refreshAllBlockItems
    /// runs deferred on the event loop instead of inside focus-loss / click
    /// handlers, so tab clicks are never blocked by a synchronous re-resolve.
    bool          m_refreshScheduled = false;
};

} // namespace cad::tools
