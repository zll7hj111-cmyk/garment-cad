#pragma once

#include "ElaDialog.h"
#include "ElaText.h"
#include <QUuid>
#include <QTimer>

#include <optional>
#include <vector>

#include "parametric/Attachment.h"
#include "ui/SegmentAngleCard.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentRefCard.h"
#include "ui/LinePropertySession.h"

class QKeyEvent;
class ElaLineEdit;
class ElaComboBox;
class ElaText;
class ElaDoubleSpinBox;
class ElaTabWidget;
class QDialogButtonBox;
class ElaPushButton;
class QPushButton;
class QButtonGroup;
class QComboBox;
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

namespace cad::ui {

class AuxPointForm;
class IntersectionForm;
class NoteButton;      ///< 便利贴注释按钮 (可复用控件, 见 ui/NoteButton.h).
class PointRefEdit;
class LineEndpointSection;
class LineAppearanceSection;
class LineGeometrySection;
class SegmentAnchorTab;
class SegmentAuxTab;

/// Modeless-style dialog for editing a segment's properties.
/// Changes apply LIVE as the user edits (no need to press a button for preview).
/// "关闭" accepts and closes; "撤销全部" reverts to the state before the dialog opened.
///
/// Page 1 ("属性") — 2026-12 去卡框化重设计 (单列 inspector, 用户拍板):
///   分区 (黄竖条标题 + hairline 分隔, 无卡框): 基本 → 几何 (长度/角度/曲线/
///   延长) → 外观 (线型/粗细分段预览 + 颜色 chip + 显示 toggle chips) →
///   端点 (起/终双列微卡) → 连接 (拓扑 + 引用行组).
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

    /// Target of this dialog (block/segment under edit) — used by tests to
    /// verify that a double-click picked the intended line.
    [[nodiscard]] QUuid targetBlockId() const { return m_blockId; }
    [[nodiscard]] QUuid targetSegmentId() const { return m_segmentId; }

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
    /// Enter = 只提交当前输入框 (editingFinished), 不关闭对话框。
    /// ElaLineEdit 的回车会传播到 QDialog (default button click / accept), 导致
    /// 任意输入框按回车整个属性对话框被悄悄关掉 (用户 2026-12: 滑轨输入回车后
    /// "没有生效的痕迹"——对话框关闭, 输入看似被丢弃)。
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onLiveUpdate();       ///< Apply current widget values to model + re-resolve + refresh.
    void onAccepted();
    void onRejected();         ///< Revert to snapshot.
    void onDebounceTimeout();  ///< Debounce timer for angle/aux live apply.
    /// 刷新滑轨行状态。
    void refreshSlideRow();

    // 便利贴注释 (NoteButton, 2026-12): 三处注释统一走「live 写模型、不自行
    // push」—— 本对话框是会话制, onAccepted 会把「打开快照 → 确认状态」推成
    // 一条 SetLinePropertiesCommand, 注释随之一起进入撤销链。
    void onSegNoteEdited(const QString& text);    ///< 段注释 (Segment::annotation).
    void onDirectionArrowClicked();
    void refreshDirectionArrow();
    void refreshEndpointExtends();

    // ── 连接 card (extracted to SegmentConnectionCard) ──
    void onConnCardChanged();
        ///< Scene/联动摘要 re-sync after a connection mutation.
    // ── 引用线段 card (extracted to SegmentRefCard) ──
    void onRefCardChanged();
        ///< Scene/angle re-sync after reference/aim mutations.

private:
    void buildPage1(ElaTabWidget* tabs);   ///< "属性" tab (card layout).
    void populateFromModel();
    /// Bridge lines (Block::isBridge) have passive length/angle fully determined
    /// by their two host points. Grey out + disable the length/angle editors,
    /// hide the fx indicators, and show a caption hint. No-op for normal lines.
    void applyBridgeReadOnly();
    /// 刷新长度模式 chips (自动/指定) 的选中与启用态。
    void refreshLengthMode();
    void applyToModel();
    void refreshScene();
    void refreshActualLengthLabel();  ///< Update the read-only resolved-length label.
    /// 端点双列微卡的只读连接行 (方案 A, 2026-xx): 本端点的跟随/挂载/指向。
    void refreshEndpointConnRows();
    /// 连接分区标题右侧状态 badge (未连接/已连接 L#·名/桥接线/终点指向…)。
    void refreshConnHint();
    void connectLiveSignals();
    void updateWeightControls();  ///< Sync weight segmented buttons + spin with model value.
    void updateWindowTitle();     ///< 标题 = 线条属性 - 名称/编号 (ctor 与 setTarget 共用).

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
    SegmentConnectionCard* m_connCard = nullptr;  ///< 连接线段 card (拓扑).
    SegmentRefCard*   m_refCard = nullptr;        ///< 引用线段 card (角度基准 + 指向点).
    SegmentAngleCard* m_angleCard = nullptr;      ///< 角度 card (行组, 嵌入几何卡).

    LinePropertySession m_session;
    /// True when the dialog was opened right after the smart pen CREATED the
    /// line — 撤销全部 then means 取消线段创建 (delete the line), not revert
    /// the snapshot. False (double-click / rotate entry) = edit state: 撤销
    /// 全部 reverts this session's changes.
    bool m_isCreation = false;

    // Page 1 widgets — 基本信息
    ElaText*      m_lblSegId      = nullptr;   ///< Serial shown as subtle grey text.
    ElaLineEdit*    m_editName      = nullptr;
    NoteButton*     m_noteSeg       = nullptr;   ///< 段注释便利贴 (名称输入框右侧).
    ElaComboBox*    m_cmbRole       = nullptr;   ///< Segment role: 轮廓线/内部线/辅助线.

    // Page 1 widgets — 外观 (extracted to LineAppearanceSection)
    LineAppearanceSection* m_appearanceSection = nullptr;

    // Page 1 widgets — 几何 (extracted to LineGeometrySection)
    LineGeometrySection*   m_geometrySection   = nullptr;

    // Page 1 widgets — 端点 (extracted to LineEndpointSection)
    LineEndpointSection* m_endpointSection = nullptr;

    // Page 1 widgets — 连接
    ElaText*      m_lblConnHint   = nullptr;   ///< 分区标题右侧状态 (未连接/已连接 L#·名).

    QTimer*       m_debounce       = nullptr;
    /// Coalescing guard for refreshScene(): heavy resolveAll + refreshAllBlockItems
    /// runs deferred on the event loop instead of inside focus-loss / click
    /// handlers, so tab clicks are never blocked by a synchronous re-resolve.
    bool          m_refreshScheduled = false;
};

} // namespace cad::ui
