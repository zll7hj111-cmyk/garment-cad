#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QString>

#include "parametric/Attachment.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// 垂直拉出 PerpLeader). Entering a slide mode snapshots the locked-axis
/// coordinate from the current geometry (via ParamDocument), clears angleOnly
/// and unlocks (位置必须可滑动); switching back to None restores the full
/// connection (re-welded). Undo restores the previous mode, lock-axis
/// snapshots, angleOnly and isLocked verbatim.
class SetAttachmentSlideModeCommand : public QUndoCommand
{
public:
    SetAttachmentSlideModeCommand(cad::param::ParamDocument* doc,
                                  const QUuid& attId,
                                  cad::param::SlideMode mode,
                                  QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    cad::param::SlideMode m_newMode;
    cad::param::SlideMode m_oldMode;
    double m_oldAlongMm = 0.0;
    double m_oldPerpMm = 0.0;
    bool m_oldAngleOnly = false;
    bool m_oldLocked = false;
};

/// 拖动保护开关 (焊接语义, 用户拍板 2026-09 补撤销): protected connections
/// are welded — dragging cannot tear them apart, dragging either side moves
/// the whole pair. Undo restores the previous locked state (此前面板直接改
/// 模型、Ctrl+Z 不回退, 与 angleOnly/slide 开关不一致).
class SetAttachmentLockedCommand : public QUndoCommand
{
public:
    SetAttachmentLockedCommand(cad::param::ParamDocument* doc,
                               const QUuid& attId, bool locked,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newLocked;
    bool m_oldLocked;
};

/// 滑轨拖动回写 (抽屉式滑动, 用户拍板 2026-08): records the free-axis
/// coordinate change (old → new) caused by dragging a slide-mode follower,
/// so the drag's undo step restores the pre-drag rail position. Pushed in
/// the SAME macro as the enclosing MoveBlockCommand. Asymmetric resolve:
/// redo() writes the coordinates WITHOUT resolving (the move command's redo
/// settles the rail — resolving first would double-shift the block); undo()
/// DOES resolve (the move command's undo has already settled with the NEW
/// coordinates, so the final restore needs one more settle).
class SetSlideOffsetsCommand : public QUndoCommand
{
public:
    SetSlideOffsetsCommand(cad::param::ParamDocument* doc, const QUuid& attId,
                           double oldAlong, double oldPerp,
                           double newAlong, double newPerp,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    double m_oldAlong, m_oldPerp, m_newAlong, m_newPerp;
};

/// 滑轨偏移面板编辑 (2026-09 审核收口): 摆放区「滑轨」两轴输入 (数值或公式)
/// 与派生模式 (AlongLeader/PerpLeader/None) 一步落盘 —— 此前 onSlideOffsetEdited
/// 直改附件不进 undo, 会话外 Ctrl+Z 撤不掉。redo/undo 均 resolve (独立命令,
/// 与拖动回写 SetSlideOffsetsCommand 的"同宏不 resolve"约定不同)。
class SetAttachmentSlideOffsetsCommand : public QUndoCommand
{
public:
    SetAttachmentSlideOffsetsCommand(cad::param::ParamDocument* doc,
                                     const QUuid& attId,
                                     cad::param::SlideMode newMode,
                                     double newAlongMm, const QString& newAlongFormula,
                                     double newPerpMm, const QString& newPerpFormula,
                                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    cad::param::SlideMode m_newMode;
    double m_newAlongMm = 0.0;
    QString m_newAlongFormula;
    double m_newPerpMm = 0.0;
    QString m_newPerpFormula;
    cad::param::SlideMode m_oldMode = cad::param::SlideMode::None;
    double m_oldAlongMm = 0.0;
    QString m_oldAlongFormula;
    double m_oldPerpMm = 0.0;
    QString m_oldPerpFormula;
};

} // namespace cad::cmd
