#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QString>
#include <QColor>

#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; class Block; }

namespace cad::cmd {

/// Set a segment's visual/semantic properties.
class SetSegmentPropertyCommand : public QUndoCommand
{
public:
    struct Props {
        QString name;
        /// 便利贴注释 (NoteButton, 2026-12): 纯备忘, 不参与求解 —— 但仍随本
        /// 命令一起收进 undo, 与其它段属性同样一步撤销。
        QString annotation;
        cad::param::SegmentRole role;
        cad::param::LineStyle lineStyle;
        QColor color;
        double weight;
        bool visible;
        bool showName;
        bool showLength;
        QString lengthFormula;
    };

    SetSegmentPropertyCommand(cad::param::ParamDocument* doc,
                              const QUuid& blockId, const QUuid& segmentId,
                              const Props& newProps,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    Props m_oldProps;
    Props m_newProps;
};

/// 端点延长量 (延长线, EXTEND_LINE_DESIGN.md): 设置线段起/终点的延长量
/// (数值 mm / 公式 cm 域, >=0)。原参数化/公式一律保留 —— 延长量是叠加参数。
/// redo/undo 都显式 touchGeometry() (本体不变但可视尾巴变 → 画布重绘铁律; P1-1
/// 起 epoch 是 Block 私有字段, 唯一 bump 入口 = Block::touchGeometry()) +
/// resolveAll (跟随线/交点/测量联动)。
class SetSegmentExtendCommand : public QUndoCommand
{
public:
    struct Values {
        double startMm = 0.0;
        QString startFormula;
        double endMm = 0.0;
        QString endFormula;
    };

    SetSegmentExtendCommand(cad::param::ParamDocument* doc,
                            const QUuid& blockId, const QUuid& segmentId,
                            const Values& newValues,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    static bool apply(cad::param::Segment* s, const Values& v);

    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    Values m_oldValues;
    Values m_newValues;
};

/// P0-3 (ARCHITECTURE_REVIEW): LinePropertyDialog 主表单会话命令。对话框
/// live-apply 直写 seg/端点字段靠打开时快照兜底 —— 关闭确认后这些修改不进
/// undo 栈（Ctrl+Z 撤不掉）。按 SegmentEditBarCommand 同款「live 编辑 +
/// 确认时推一步命令」模式收口: oldProps = 打开时快照, newProps = 确认时模型
/// 状态; undo 恢复打开状态, redo 重放确认状态。
class SetLinePropertiesCommand : public QUndoCommand
{
public:
    struct Props {
        QString name;
        /// 段便利贴注释 (NoteButton, 2026-12): 纯备忘, 不参与求解 —— 但随本
        /// 会话一起收进 undo (与其它段属性同样一步撤销)。
        QString annotation;
        cad::param::SegmentRole role = cad::param::SegmentRole::Outline;
        bool showName = true;
        bool showLength = true;
        bool visible = true;
        QColor color;
        cad::param::LineStyle lineStyle = cad::param::LineStyle::Solid;
        double weight = 0.0;
        QString lengthFormula;
        double distance = 0.0;         ///< End-point distance (polar), mm.
        QString distanceFormula;       ///< End-point distance formula.
        QString startName, startAnno;
        bool startShowName = false;
        QString endName, endAnno;
        bool endShowName = false;
        /// 长度模式 (2026-xx §6.3): 自动/指定 —— 块级字段 (Block::lengthAuto),
        /// 2026-09 审核收口: 此前对话框内切换直写模型不进 undo, 撤销全部
        /// 与 Ctrl+Z 都回不滚。
        bool lengthAuto = false;
        bool operator==(const Props& o) const;
        bool operator!=(const Props& o) const { return !(*this == o); }
    };

    SetLinePropertiesCommand(cad::param::ParamDocument* doc,
                             const QUuid& blockId, const QUuid& segmentId,
                             Props oldProps, Props newProps,
                             QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    static bool apply(cad::param::ParamDocument* doc,
                      cad::param::Block* b, cad::param::Segment* s,
                      const Props& p);
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    Props m_oldProps;
    Props m_newProps;
};

/// Snapshot of the status-bar edit strip's mutable state (SegmentEditBar):
/// the segment's name / length fields, the endpoint's Polar / measure fields
/// and the follower attachment's angle / arc state. The strip edits the model
/// LIVE and pushes ONE command per commit, so undo/redo AND the undo stack's
/// dirty flag stay consistent (without this, edits after a save were silently
/// lost on close — the stack stayed clean).
class SegmentEditBarCommand : public QUndoCommand
{
public:
    struct State {
        QString segName;
        QString lengthFormula;
        QString endDistanceFormula;
        double endDistance = 0.0;
        double endAngle = 0.0;
        QString endAngleFormula;
        int endConstraint = 0;
        QUuid endRefPointId;
        // Follower attachment (null id = free line).
        QUuid attId;
        double followerAngle = 0.0;
        QString followerAngleFormula;
        double arcLength = 0.0;
        QString arcLengthFormula;
        int rotationMode = 0;
    };

    /// @p newState Target state; the OLD state is captured from the model at
    /// construction (push() then applies newState via redo()).
    SegmentEditBarCommand(cad::param::ParamDocument* doc,
                          const QUuid& blockId, const QUuid& segmentId,
                          State newState, QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    State m_oldState;
    State m_newState;
};

} // namespace cad::cmd
