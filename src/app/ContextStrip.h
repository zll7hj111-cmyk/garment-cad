#pragma once

#include <QWidget>
#include <QUuid>

#include "document/commands/BlockCommands.h"   // SegmentEditBarCommand::State

class ElaText;
class ElaLineEdit;
class ElaPushButton;
class QButtonGroup;
class QPushButton;
class QTimer;
class QUndoStack;

namespace cad::param {
class ParamDocument;
struct Attachment;
enum class RotationMode;
}

namespace cad::app {

class CircleStripBar;
class PlacedPointStripBar;

/// 上下文属性条焦点三态 (CONTEXT_STRIP_DESIGN.md §2.1)。
enum class StripFocus {
    Empty,   ///< 无焦点: 条带隐藏.
    Hover,   ///< 悬停预览: 字段只读, 虚线描边.
    Pinned,  ///< 已锁定: 字段可编辑, accent 实线描边.
};

/// 画布下方常驻的"当前关注线段"属性带 (CONTEXT_STRIP_DESIGN.md)。
class ContextStrip : public QWidget
{
    Q_OBJECT

public:
    explicit ContextStrip(cad::param::ParamDocument* paramDoc, QWidget* parent = nullptr);
    ~ContextStrip() override;

    void setUndoStack(QUndoStack* stack);
    /// 焦点回落到画布的落点 (Enter 走完最后一个字段 / Esc 解除锁定时用)。
    void setCanvasView(QWidget* canvasView);

    // ── 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期) ──
    void beginConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                  const QUuid& attachmentId, double initialAngle);
    void endConnectAngleSession();
    void setConnectAngleValid(bool valid);
    [[nodiscard]] bool connectSession() const { return m_connectSession; }

    // ── 旋转工具锚心 (2026-12): 换向按钮在旋转会话内 = 切换锚心 ──
    void setRotateAnchorState(bool active, bool anchorIsEnd, bool canToggle,
                              const QString& reason, double baseAngleDeg = 0.0);

    // ── 焦点上报 (MainWindow 经 ToolHost 转发) ──
    void setHoverTarget(const QUuid& blockId, const QUuid& segmentId);
    void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId,
                         bool grabFocus = false);
    void clearHover();
    void clearPinned();
    void pinCreatedLine(const QUuid& blockId, const QUuid& segmentId,
                        bool grabFocus = true);
    void showStrokePreview(double lenCm, double angleDeg);
    void hideBar();
    void applyTheme();
    void cancelCreation();

    [[nodiscard]] StripFocus focusState() const { return m_focus; }
    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] QUuid segmentId() const { return m_segmentId; }
    [[nodiscard]] bool readOnly() const;

    // ── 控件访问 (测试与宿主) ──
    [[nodiscard]] ElaLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] ElaLineEdit* lengthEdit() const { return m_lenEdit; }
    [[nodiscard]] ElaPushButton* pasteLengthButton() const { return m_btnPasteLen; }
    [[nodiscard]] ElaLineEdit* baseAngleEdit() const { return m_baseAngleEdit; }
    [[nodiscard]] ElaLineEdit* angleEdit() const { return m_angleEdit; }
    [[nodiscard]] ElaPushButton* pasteAngleButton() const { return m_btnPasteAngle; }
    [[nodiscard]] ElaPushButton* reverseButton() const { return m_btnReverse; }
    [[nodiscard]] QPushButton* unitAngleButton() const { return m_btnUnitAngle; }
    [[nodiscard]] QPushButton* unitArcButton() const { return m_btnUnitArc; }
    [[nodiscard]] QPushButton* unitChordButton() const { return m_btnUnitChord; }
    [[nodiscard]] ElaPushButton* posDetachButton() const { return m_btnPosDetach; }
    [[nodiscard]] ElaPushButton* angleDetachButton() const { return m_btnAngleDetach; }
    [[nodiscard]] QString badgeText() const;
    [[nodiscard]] QString basisText() const;
    /// 长度/角度槽标签文案 (线段语义恒为「长度:」/「角度:」)。
    /// 圆区段走专属 CircleStripBar, 不再改写这两个槽 (CIRCLE_TOOL_DESIGN.md §6.1)。
    [[nodiscard]] QString lengthLabelText() const;
    [[nodiscard]] QString angleLabelText() const;

    // ── 圆专属条带 (委托给 CircleStripBar) ──
    [[nodiscard]] CircleStripBar* circleBar() const { return m_circleBar; }

    // ── 圆绘制会话 (一期补充, CIRCLE_TOOL_DESIGN.md §5.5) ──
    /// 圆心已落、半径未定: 隐藏线段条带, 圆条带进绘制态 (不抢键盘焦点)。
    void beginCircleSession();
    void updateCircleSessionValues(double radiusCm, bool locked);
    /// 结束绘制态 (提交/取消/切模式/切工具) 并交还线段条带。
    void endCircleSession();

    // ── 放置点专属模式与会话 (委托给 PlacedPointStripBar) ──
    void setPlacedPointTarget(const QUuid& blockId, const QUuid& pointId);
    void clearPlacedPoint();

    void beginPlacePointSession(const QString& baseSegName = QString());
    void updatePlacePointValues(double distCm, double angleDeg, bool distLocked, bool angleLocked);
    void endPlacePointSession();
    void focusNextPlacedPointField();

    [[nodiscard]] bool isPlacedPointMode() const;
    [[nodiscard]] PlacedPointStripBar* placedPointBar() const { return m_placedPointBar; }

signals:
    void cancelRequested();

    // ── 放置点交互信号 ──
    void placePointDistChanged(double distCm, bool locked);
    void placePointAngleChanged(double angleDeg, bool locked);
    void placePointCommitted();
    void placedPointDeleted(const QUuid& blockId, const QUuid& pointId);

    // ── 旋转会话换向 ──
    void reverseRequested(const QUuid& blockId, const QUuid& segmentId);

    // ── 圆绘制会话输入 (一期补充) ──
    void circleSessionRadiusChanged(double radiusCm, bool locked);
    void circleSessionCommitted();
    void circleSessionCancelled();

    // ── 连接角度会话输入 ──
    void connectAngleTextChanged(const QString& text);
    void connectAngleModeChanged(cad::param::RotationMode mode);
    void connectAngleCommitted();
    void connectAngleCancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // ── 子分卷实现函数 ──
    // Display (src/app/ContextStripDisplay.cpp)
    void refreshFields();
    void refreshChrome();

    // Edit (src/app/ContextStripEdit.cpp)
    void applyName();
    void applyLength();
    void applyAngle();
    void onUnitToggled(bool wantArc);
    void onUnitSelected(cad::param::RotationMode mode);
    void onReverseClicked();
    void onPosDetachClicked();
    void onAngleDetachClicked();
    void onPasteLength();
    void onPasteAngle();
    void commitState(cad::cmd::SegmentEditBarCommand::State st);
    [[nodiscard]] cad::cmd::SegmentEditBarCommand::State snapshotState() const;
    [[nodiscard]] const cad::param::Attachment* findEditAttachment() const;

    // Core (src/app/ContextStrip.cpp)
    void buildUi();
    void setReadOnlyFields(bool readOnly);
    void flushHover();
    /// 圆区段 (fitKind == Circle) 切到专属条带: 隐藏线段条带、显示圆条带。
    /// 非圆返回 false (调用方照常走线段条带)。
    bool routeToCircleBar(const QUuid& blockId, const QUuid& segmentId, bool editable);
    /// 从圆条带切回线段条带 (清目标 + 显示线段条带)。
    void leaveCircleBar();
    [[nodiscard]] bool inputHasFocus() const;
    void returnFocusToCanvas();

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QWidget*    m_canvasView = nullptr;

    StripFocus m_focus = StripFocus::Empty;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_hoverBlock;
    QUuid m_hoverSegment;
    bool  m_creationPinned = false;
    bool  m_strokePreview = false;
    bool  m_connectSession = false;
    QUuid m_connectAttId;
    double m_connectInitialAngle = 0.0;
    int   m_editStartIndex = 0;

    // 旋转工具锚心会话状态打包
    struct RotateAnchorState {
        bool    active = false;
        bool    anchorIsEnd = false;
        bool    canToggle = false;
        QString reason;
        double  baseAngleDeg = 0.0;
    };
    RotateAnchorState m_rotateAnchor;

    ElaText*       m_idLabel = nullptr;
    ElaLineEdit*   m_nameEdit = nullptr;
    ElaText*       m_lenLabel = nullptr;
    ElaLineEdit*   m_lenEdit = nullptr;
    ElaPushButton* m_btnPasteLen = nullptr;
    ElaLineEdit*   m_baseAngleEdit = nullptr;
    ElaText*       m_angleLabel = nullptr;
    ElaLineEdit*   m_angleEdit = nullptr;
    ElaPushButton* m_btnPasteAngle = nullptr;
    QPushButton*   m_btnUnitAngle = nullptr;
    QPushButton*   m_btnUnitArc = nullptr;
    QPushButton*   m_btnUnitChord = nullptr;
    QButtonGroup*  m_unitGroup = nullptr;
    ElaPushButton* m_btnReverse = nullptr;
    ElaPushButton* m_btnBasis = nullptr;
    ElaPushButton* m_btnPosDetach = nullptr;
    ElaPushButton* m_btnAngleDetach = nullptr;
    ElaText*       m_badge = nullptr;
    ElaText*       m_hint = nullptr;
    QTimer*        m_debounce = nullptr;
    QTimer*        m_hoverTimer = nullptr;

    QWidget*             m_segmentBar = nullptr;
    PlacedPointStripBar* m_placedPointBar = nullptr;
    CircleStripBar*      m_circleBar = nullptr;
};

} // namespace cad::app
