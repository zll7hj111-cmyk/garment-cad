#pragma once

#include <QWidget>
#include <QUuid>

class ElaText;
class ElaLineEdit;
class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::app {

/// 圆专属底部属性条 (CIRCLE_TOOL_DESIGN.md §6.1; 2026-12 用户拍板:
/// 圆不再复用线段条带)。
///
/// 字段: 徽标「圆」+ 编号 + 名称 + 半径 R + 直径 D + 周长 C。
/// 半径是唯一权威 (圆段起点 p₀ 的 distance / distanceFormula, D2/D18);
/// 直径/周长双向联动都先换算回半径文本再落**一步** SegmentEditBarCommand,
/// 换算口径与属性面板 CircleGeometrySection 完全同源 (D/C 是派生量, 不落模型)。
class CircleStripBar : public QWidget
{
    Q_OBJECT

public:
    explicit CircleStripBar(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setDoc(cad::param::ParamDocument* doc) { m_paramDoc = doc; }
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// 选中圆区段 (fitKind == Circle): 填值并显示; 非圆/失效返回 false (条带隐藏)。
    bool setTarget(const QUuid& blockId, const QUuid& segmentId);
    void clearTarget();
    /// 模型变化 (resolved / undo indexChanged) 后重填; 不覆盖正在输入的框。
    void refresh();
    /// 悬停 = 只读预览, 锁定 = 可编辑。
    void setEditable(bool editable);

    // ── 绘制会话 (一期补充, CIRCLE_TOOL_DESIGN.md §5.5) ──
    // 圆心已落、半径未定: 徽标「绘制」, 仅半径框可输入, 直径只读联动,
    // 编号/名称/周长让位。条带是纯输入面 —— 落圆语义全在 ToolCircle 会话里。
    /// 进入绘制态 (清空半径/直径, 聚焦留给用户点击, 不抢键盘焦点)。
    void beginSession();
    /// 会话内实时半径 (cm 域) + 锁定态 (锁定 = 输入已定值, 画布不再跟随光标)。
    void updateSessionValues(double radiusCm, bool locked);
    /// 退出绘制态并隐藏 (提交/取消/切模式/切工具都走这里)。
    void endSession();

    [[nodiscard]] bool hasTarget() const { return !m_blockId.isNull() && !m_segmentId.isNull(); }
    [[nodiscard]] bool hasInputFocus() const;
    [[nodiscard]] bool isSession() const { return m_session; }
    [[nodiscard]] bool isRadiusLocked() const { return m_sessionLocked; }
    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] QUuid segmentId() const { return m_segmentId; }

    void applyTheme();

    // ── 控件访问 (测试与宿主) ──
    [[nodiscard]] ElaLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] ElaLineEdit* radiusEdit() const { return m_radiusEdit; }
    [[nodiscard]] ElaLineEdit* diameterEdit() const { return m_diameterEdit; }
    [[nodiscard]] ElaLineEdit* circumferenceEdit() const { return m_circumferenceEdit; }
    [[nodiscard]] QString badgeText() const;
    [[nodiscard]] QString serialText() const;
    /// 绘制会话的「已锁定」chip (测试与宿主; 会话外恒隐藏)。
    [[nodiscard]] ElaText* lockChip() const { return m_lockChip; }

signals:
    void cancelRequested();
    void returnFocusRequested();

    // ── 绘制会话输入 (一期补充) ──
    /// 半径框击键 (全文; cm 域数值或公式求值后的预览值, 每键一报)。
    void sessionRadiusChanged(double radiusCm, bool locked);
    /// Enter: 以当前半径落圆。
    void sessionCommitted();
    /// Esc: 丢弃橡皮筋, 不落圆。
    void sessionCancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void updateHint();
    /// 提交: 名称 + 半径 (数值/公式), 无实际变化时不压空撤销步。
    void applyEdits();
    void onDiameterEdited();
    void onCircumferenceEdited();
    void onRadiusEdited();
    void focusNextField(bool backwards = false);

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack*                m_undoStack = nullptr;

    ElaText*     m_badge = nullptr;
    ElaText*     m_serialLabel = nullptr;
    ElaText*     m_nameLabel = nullptr;
    ElaText*     m_radiusLabel = nullptr;
    ElaText*     m_diameterLabel = nullptr;
    ElaText*     m_circumferenceLabel = nullptr;
    ElaText*     m_radiusUnit = nullptr;
    ElaText*     m_diameterUnit = nullptr;
    ElaText*     m_circumferenceUnit = nullptr;
    ElaText*     m_lockChip = nullptr;
    ElaLineEdit* m_nameEdit = nullptr;
    ElaLineEdit* m_radiusEdit = nullptr;
    ElaLineEdit* m_diameterEdit = nullptr;
    ElaLineEdit* m_circumferenceEdit = nullptr;
    ElaText*     m_hint = nullptr;

    QUuid m_blockId;
    QUuid m_segmentId;
    bool  m_editable = false;
    /// 绘制会话 (圆心已落、半径未定) 与半径锁定态。
    bool  m_session = false;
    bool  m_sessionLocked = false;
    /// Esc 解除锁定时挡住焦点回落触发的 editingFinished 提交。
    bool  m_suppressApply = false;
};

} // namespace cad::app
