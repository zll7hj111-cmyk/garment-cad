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
    void onLengthApply();      ///< Apply length (triggered by debounce / Enter / focus-loss).
    void onLengthDirty();      ///< Mark length field as having pending changes.
    void onDebounceTimeout();  ///< Global 200ms debounce: apply all pending text fields.
    void onColorPick();        ///< Open color picker for segment color.
    void onAccepted();
    void onRejected();         ///< Revert to snapshot.
    void onPublishLength();    ///< Publish this segment's length as a linked variable.
    /// 长度模式 (2026-xx §6.3): 自动/指定 切换 (仅非双端连接时可改)。
    void onLengthModeChanged(bool autoMode);
    /// 滑轨 (2026-xx §3 从连接卡拆到摆放分区)。
    void onSlideModeChanged(int index);
    void onSlideOffsetEdited();
    /// 端点组内连接 (2026-xx §3): 起点/终点「连接到」+ 拆开/重连。
    void onStartConnectResolved(const QUuid& blockId, const QUuid& pointId);
    void onStartDetachClicked();
    void onEndConnectResolved(const QUuid& blockId, const QUuid& pointId);
    void onEndDetachClicked();
    /// 刷新滑轨行状态。
    void refreshSlideRow();

    // 便利贴注释 (NoteButton, 2026-12): 三处注释统一走「live 写模型、不自行
    // push」—— 本对话框是会话制, onAccepted 会把「打开快照 → 确认状态」推成
    // 一条 SetLinePropertiesCommand, 注释随之一起进入撤销链。
    void onSegNoteEdited(const QString& text);    ///< 段注释 (Segment::annotation).
    void onStartNoteEdited(const QString& text);  ///< 起点注释 (ParamPoint::annotation).
    void onEndNoteEdited(const QString& text);    ///< 终点注释.
    /// 朝向箭头点击 (2026-xx, §6.1): 翻转 P1/P2 身份 (ReverseSegmentCommand).
    void onDirectionArrowClicked();
    /// 刷新朝向箭头: 显示/隐藏 (换向资格) + 箭头方向 (P1→P2 世界朝向).
    void refreshDirectionArrow();
    /// 端点延长量编辑 (2026-xx, §6.2): 上/下槽各一栏, SetSegmentExtendCommand
    /// (按槽内点的当前角色写 extendStart/End)。
    void onStartExtendEdited();
    void onEndExtendEdited();
    /// 刷新端点延长量输入 (置灰 + 回填数值/公式)。
    void refreshEndpointExtends();
    /// 单端延长量应用 (数值/公式解析 + 命令)。isTop = 上槽 (P1 物理点)。
    void applyEndpointExtend(ElaLineEdit* edit, bool isTop);
    /// 单端延长置灰原因 (空 = 允许)。pointId = 物理点, 由调用方按槽内点传入。
    [[nodiscard]] QString endpointExtendDisableReason(
        const cad::param::Block& block, const cad::param::Segment& seg,
        const QUuid& pointId) const;

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

    // Snapshot for cancel-revert
    struct Snapshot {
        QString segName;
        QString segAnnotation;    ///< 段便利贴注释 (NoteButton, 撤销时还原).
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
        /// 长度模式 (2026-xx §6.3): 自动/指定 —— 2026-09 审核收口, 撤销全部
        /// 需还原打开时的模式。
        bool lengthAuto = false;
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
    NoteButton*     m_noteSeg       = nullptr;   ///< 段注释便利贴 (名称输入框右侧).
    ElaComboBox*    m_cmbRole       = nullptr;   ///< Segment role: 轮廓线/内部线/辅助线.

    // Page 1 widgets — 几何
    ElaLineEdit*    m_editLength    = nullptr;   ///< Enter/blur to apply; orange border when dirty.
    ElaText*      m_lblFx         = nullptr;   ///< "fx" indicator shown when length is a formula.
    QPushButton*    m_btnLenAuto    = nullptr;   ///< 长度模式: 自动 (两端钉死).
    QPushButton*    m_btnLenSpec    = nullptr;   ///< 长度模式: 指定 (起点钉死 + 长度).
    class QButtonGroup* m_lenGroup   = nullptr;
    QPushButton*    m_chkShowLength = nullptr;   ///< 显示 chip (外观区, checkable).
    ElaText*      m_lblActualLength = nullptr; ///< Read-only resolved length (dim mono), 长度行内.
    QPushButton*  m_btnPublishLen = nullptr;   ///< "发布长度参数" button (类型下拉框正下方, 与之左缘/同宽对齐).
    // 滑轨行 (2026-xx §3 从连接卡拆到摆放分区).
    QWidget*      m_slideRow       = nullptr;
    ElaText*      m_lblSlideBadge  = nullptr;
    class QComboBox* m_cmbSlideMode = nullptr;
    ElaLineEdit*  m_editSlideAlong = nullptr;
    ElaLineEdit*  m_editSlidePerp  = nullptr;
    ElaText*      m_lblArcLength  = nullptr;   ///< Read-only arc length display (curve only).
    ElaLineEdit*    m_editTension   = nullptr;   ///< Curve tension (curve only).
    QWidget*      m_arcRow        = nullptr;   ///< Container for arc-length row (curve only).
    QWidget*      m_tensionRow    = nullptr;   ///< Container for tension row (curve only, 含转为直线).
    QPushButton*  m_btnConvert    = nullptr;   ///< "转为直线" button (curve only: removes all curve points).

    // Page 1 widgets — 外观 (2026-12: 分段预览按钮 + toggle chips)
    QPushButton*    m_styleBtns[3]  = {nullptr, nullptr, nullptr};  ///< 线型: 实线/虚线/点线 (图标分段).
    QButtonGroup*   m_styleGroup    = nullptr;
    QPushButton*    m_weightBtns[3] = {nullptr, nullptr, nullptr};  ///< 粗细: 细/中/粗 (分段; 无选中=自定义).
    QButtonGroup*   m_weightGroup   = nullptr;
    ElaDoubleSpinBox* m_spinWeight  = nullptr;   ///< 粗细数值 (真值来源, 常量可见).
    QPushButton*  m_btnColor      = nullptr;   ///< Color swatch button.
    ElaText*      m_lblColorHex   = nullptr;   ///< 颜色 hex 读数 (mono dim).
    QPushButton*    m_chkVisible    = nullptr;   ///< 显示 chip: 可见.
    QPushButton*    m_chkShowName   = nullptr;   ///< 显示 chip: 名称.

    // Page 1 widgets — 端点 (双列微卡, objectName startPointCard/endPointCard)
    // 2026-08 用户再拍板: 槽位绑定**物理点** (创建序) —— 上槽恒 = 先建的点
    // (P1), 下槽恒 = 后建点 (P2), 换向只交换"进出"，不交换面板位置。
    // 控件名沿用 Start/End (上/下槽), 角色语义见 m_topIsStart。
    ElaText*      m_lblStartPtId  = nullptr;
    ElaLineEdit*    m_editStartName = nullptr;
    QPushButton*    m_chkShowStartName = nullptr;  ///< 显示 chip.
    NoteButton*     m_noteStart     = nullptr;   ///< 起点注释便利贴 (原「备注」长输入框).
    /// 只读连接行 (方案 A, 2026-xx): 「跟随/挂载 …」; 双卡同构保证等高。
    ElaText*      m_lblStartConn  = nullptr;
    ElaText*      m_lblEndPtId    = nullptr;
    ElaLineEdit*    m_editEndName   = nullptr;
    QPushButton*    m_chkShowEndName = nullptr;    ///< 显示 chip.
    NoteButton*     m_noteEnd       = nullptr;   ///< 终点注释便利贴 (原「备注」长输入框).
    ElaText*      m_lblEndConn    = nullptr;
    /// 朝向箭头 (2026-xx, §6.1): 两个端点组之间的换向按钮, 点击翻转 P1/P2 身份.
    QPushButton*  m_btnDirectionArrow = nullptr;
    /// 端点延长量输入 (2026-xx, §6.2): 起/终各一栏, 数值或公式 cm (≥0).
    ElaLineEdit*  m_editStartExtend = nullptr;
    ElaLineEdit*  m_editEndExtend   = nullptr;
    // 端点组内连接控件 (2026-xx §3): 连接到 + 拆开/重连。
    class PointRefEdit* m_refStartConnect = nullptr;
    QPushButton* m_btnStartDetach = nullptr;
    class PointRefEdit* m_refEndConnect = nullptr;
    QPushButton* m_btnEndDetach = nullptr;

    // Page 1 widgets — 连接
    ElaText*      m_lblConnHint   = nullptr;   ///< 分区标题右侧状态 (未连接/已连接 L#·名).

    QColor m_currentColor;  ///< Current segment color.

    // 端点槽位语义 (2026-08 用户再拍板, 取代 2026-08-31 的"槽位随角色"): 槽位
    // 绑定**物理点**—— 上槽恒 = 先建的点 (P1), 下槽 = 后建点 (P2); 换向
    // (进出互换) 不改变点在面板上的位置, 唯一变化 = 朝向箭头翻面 + 各槽的
    // 延长量/连接行改按该点**当前角色** (start/end) 取值与写回。
    // m_topIsStart = 上槽点当前是否为模型 start 端 (每次 populate 刷新)。
    bool m_topIsStart = true;

    QTimer*       m_debounce       = nullptr;   ///< Global 200ms debounce timer for text field auto-apply.
    /// Coalescing guard for refreshScene(): heavy resolveAll + refreshAllBlockItems
    /// runs deferred on the event loop instead of inside focus-loss / click
    /// handlers, so tab clicks are never blocked by a synchronous re-resolve.
    bool          m_refreshScheduled = false;
};

} // namespace cad::ui
