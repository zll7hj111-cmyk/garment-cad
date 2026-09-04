#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "geometry/Vec2.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add a curve pass-point (曲线点) to a segment. The point is a CurveAnchor
/// constraint; the segment is promoted to a Bézier curve (type + passPointIds).
/// Undo removes the point and restores the segment's previous type (straight
/// line when this was the only curve point).
class AddCurvePointCommand : public QUndoCommand
{
public:
    AddCurvePointCommand(cad::param::ParamDocument* doc,
                         const QUuid& blockId, const QUuid& segmentId,
                         cad::param::ParamPoint pt,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamPoint m_pt;
    cad::param::SegmentType m_oldType;
};

/// Remove a curve pass-point from a segment. When the last curve point is
/// removed the segment reverts to a straight line. Undo restores the point,
/// its position in passPointIds and the segment type.
class RemoveCurvePointCommand : public QUndoCommand
{
public:
    RemoveCurvePointCommand(cad::param::ParamDocument* doc,
                            const QUuid& blockId, const QUuid& segmentId,
                            const QUuid& pointId,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_pointId;
    cad::param::ParamPoint m_pt;      ///< Saved copy of the removed point.
    int m_index = 0;                  ///< Position within passPointIds.
    cad::param::SegmentType m_oldType;
};

/// Move a curve anchor by (percent, offset) along its host chord. Pushed once
/// per drag stroke (on mouse release) — each stroke is its own undo step.
/// Also records the anchor's follow connection before/after the stroke: a drag
/// detaches an existing follow on its first move and may snap-connect a new
/// one on release, so undo must restore the complete connection state.
class MoveCurveAnchorCommand : public QUndoCommand
{
public:
    MoveCurveAnchorCommand(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& pointId,
                           double oldPercent, double oldOffset,
                           double newPercent, double newOffset,
                           const QUuid& oldFollowBlockId,
                           const QUuid& oldFollowPointId,
                           const cad::geo::Vec2& oldFollowOffset,
                           const QUuid& newFollowBlockId,
                           const QUuid& newFollowPointId,
                           const cad::geo::Vec2& newFollowOffset,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    double m_oldPercent, m_oldOffset;
    double m_newPercent, m_newOffset;
    QUuid m_oldFollowBlockId;
    QUuid m_oldFollowPointId;
    cad::geo::Vec2 m_oldFollowOffset;
    QUuid m_newFollowBlockId;
    QUuid m_newFollowPointId;
    cad::geo::Vec2 m_newFollowOffset;
};

/// Edit a curve anchor's tangent handles (manual Bézier handles). Stores the
/// before/after tangentIn/tangentOut/autoTangent AND tangentLocked so a handle
/// drag undoes cleanly — an Alt+drag (尖角模式) flips tangentLocked to false
/// persistently inside the drag, so undo must restore the pre-drag lock state.
class SetCurveTangentCommand : public QUndoCommand
{
public:
    SetCurveTangentCommand(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& pointId,
                           const cad::geo::Vec2& oldTanIn, const cad::geo::Vec2& oldTanOut, bool oldAuto,
                           const cad::geo::Vec2& newTanIn, const cad::geo::Vec2& newTanOut, bool newAuto,
                           bool oldLocked, bool newLocked,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    cad::geo::Vec2 m_oldTanIn, m_oldTanOut;
    cad::geo::Vec2 m_newTanIn, m_newTanOut;
    bool m_oldAuto;
    bool m_newAuto;
    bool m_oldLocked = true;
    bool m_newLocked = true;
};

/// P0-3 (ARCHITECTURE_REVIEW): 释放曲线锚点的跟随连接。此前的写路径
/// (SegmentAnchorTab 的「释放」按钮) 直改 pt->followBlockId/PointId/followOffset
/// + resolveAll(), 完全绕过 undo —— 命令化后撤销可恢复跟随关系。
class ReleaseCurveFollowCommand : public QUndoCommand
{
public:
    ReleaseCurveFollowCommand(cad::param::ParamDocument* doc,
                              const QUuid& blockId, const QUuid& pointId,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    QUuid m_oldFollowBlockId;
    QUuid m_oldFollowPointId;
    cad::geo::Vec2 m_oldFollowOffset;
};

} // namespace cad::cmd
