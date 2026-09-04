#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QString>

#include "parametric/ParamPoint.h"
#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; class Block; }

namespace cad::cmd {

/// 终点连接 (每端完整连接, 2026-xx): 设置线段「终点连接」= 终点指向
/// (Block::endTargetBlockId/PointId/Offset) —— 终点连接的引擎载体, Resolver
/// Step 7 旋转指向目标点 (配长度公式则终点精确落点)。redo/undo 都 resolveAll
/// (旋转被驱动 → 跟随线/交点/测量联动)。拆开/重连/偏移编辑共用本命令。
class SetEndTargetCommand : public QUndoCommand
{
public:
    SetEndTargetCommand(cad::param::ParamDocument* doc,
                        const QUuid& blockId,
                        const QUuid& targetBlockId,
                        const QUuid& targetPointId,
                        double offsetDeg,
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

    /// 写 endTarget 四字段 (SetEndTargetCommand 与 ConnectEndCommand 共用)。
    static void apply(cad::param::Block* b, const QUuid& tb, const QUuid& tp,
                      double off, const QString& offFormula);

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_newBlock, m_oldBlock;
    QUuid m_newPoint, m_oldPoint;
    double m_newOffset = 0.0, m_oldOffset = 0.0;
    QString m_newOffsetFormula, m_oldOffsetFormula;
};

/// 终点连接一步 undo (桥接落点, 2026-xx): 写 endTarget 指向 + 桥接落点时自动
/// 发布测量变量 M_xxx 驱动长度 (仅在线段无长度公式时) —— 终点精确落在目标点
/// 上, 与 SmartPen 桥接创建 (LineFactory::createBridgeLine) 同语义。undo 全量
/// 回滚 (恢复指向/长度公式, 删除本次新增的测量)。
class ConnectEndCommand : public QUndoCommand
{
public:
    ConnectEndCommand(cad::param::ParamDocument* doc,
                      const QUuid& blockId, const QUuid& segmentId,
                      const QUuid& targetBlockId, const QUuid& targetPointId,
                      double offsetDeg, bool bridgeLand,
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId, m_segmentId;
    QUuid m_targetBlockId, m_targetPointId;
    double m_offsetDeg = 0.0;
    bool m_bridgeLand = false;
    // 旧态 (构造时快照, undo 恢复)。
    QUuid m_oldBlock, m_oldPoint;
    double m_oldOffset = 0.0;
    QString m_oldOffsetFormula;
    QString m_oldLengthFormula;
    QString m_oldEndDistFormula;   ///< 终点 Polar 距离公式 (undo 恢复).
    // 新增测量 (redo 创建, undo 删除; redo 幂等重建)。
    QUuid m_addedMeasureId;
    // 既有归属测量 (重定向时更新其目标点, undo 恢复)。
    QUuid m_ownerMeasureId;
    QUuid m_oldMeasureBlockB;
    QUuid m_oldMeasurePointB;
};

/// Add an auxiliary (Interpolated) point to a host segment.
/// Pushed as its OWN undo step, deliberately NOT merged with the line that
/// borrows the point: the aux point belongs to the host segment (辅助点归
/// 宿主线段所有), so undoing / deleting the borrowing line must never
/// remove it — the first Ctrl+Z removes the line, the second removes the point.
class AddAuxPointCommand : public QUndoCommand
{
public:
    AddAuxPointCommand(cad::param::ParamDocument* doc,
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
};

/// Move a single parametric point (curve anchor drag).
/// Stores old/new freePos; on redo applies new, on undo restores old.
class MovePointCommand : public QUndoCommand
{
public:
    MovePointCommand(cad::param::ParamDocument* doc,
                     const QUuid& blockId, const QUuid& pointId,
                     const cad::geo::Vec2& oldPos, const cad::geo::Vec2& newPos,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_pointId;
    cad::geo::Vec2 m_oldPos;
    cad::geo::Vec2 m_newPos;
};

} // namespace cad::cmd
