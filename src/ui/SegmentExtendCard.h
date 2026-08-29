#pragma once

#include <QUuid>

#include <QWidget>

class ElaLineEdit;
class ElaText;
class QWidget;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

class CanvasScene;

namespace cad::ui {

/// 「延长」行组 (端点延长线, EXTEND_LINE_DESIGN.md; 2026-12 去卡框化):
/// 起/终两个端点各一栏延长量 (数值 mm / 公式 cm 域, ≥0, 默认 0) +
/// 「原长 / 延长量 / 实际长」只读行。编辑即提交 (SetSegmentExtendCommand,
/// undo 一步), 公式/数值均保留原有参数化 —— 延长量是叠加参数。
/// 纯行组 (无边框/无标题), 由属性页「几何」分区嵌入并提供分区标题。
///
/// 置灰规则:
///   · 整卡灰   —— 曲线段 / 桥接线 / 省道线;
///   · 单端灰   —— 粘死端 (本线自身作为跟随线的吸附点) / 同块角点 (同 Block
///                 多段共用的端点) / 组件暴露端点。
class SegmentExtendCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentExtendCard(cad::param::ParamDocument* doc, CanvasScene* scene,
                               QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 从模型全量刷新 (置灰判定 + 输入框 + 只读行)。
    void refresh();

signals:
    /// 模型已变更 —— 对话框刷新画布/跟随线。
    void changed();

private:
    void onStartEdited();
    void onEndEdited();
    /// 解释单个输入框: 空 = 0; 纯数字 = mm (负值拒绝); 其他 = 公式 (cm 域)。
    void applyEdited(ElaLineEdit* edit, bool isStart);
    void updateReadout(const cad::param::Block& block, const cad::param::Segment& seg);
    /// 单端置灰原因 (空 = 允许)。整卡置灰由 refresh() 单独判定。
    [[nodiscard]] QString endDisableReason(const cad::param::Block& block,
                                           const cad::param::Segment& seg,
                                           bool forStart) const;

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText* m_lblHint = nullptr;       ///< 整卡置灰原因 (空 = 隐藏)。
    ElaText* m_lblStart = nullptr;      ///< 起端标签: 起点延长(P#).
    ElaText* m_lblEnd = nullptr;        ///< 终端标签: 终点延长(P#).
    QWidget* m_startRow = nullptr;
    ElaLineEdit* m_startEdit = nullptr;
    ElaText* m_lblStartValue = nullptr; ///< 已求值 当前值 (只读)。
    QWidget* m_endRow = nullptr;
    ElaLineEdit* m_endEdit = nullptr;
    ElaText* m_lblEndValue = nullptr;
    ElaText* m_lblReadout = nullptr;    ///< 原长 ｜ 延长 ｜ 实际。
};

} // namespace cad::ui
