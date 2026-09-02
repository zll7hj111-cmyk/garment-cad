#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "document/commands/CommandIds.h"         // central merge-id enum (P0-2)
#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add a block to the document.
class AddBlockCommand : public QUndoCommand
{
public:
    AddBlockCommand(cad::param::ParamDocument* doc, cad::param::Block block,
                    QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;
};

/// Remove a block (and its attachments) from the document.
class RemoveBlockCommand : public QUndoCommand
{
public:
    RemoveBlockCommand(cad::param::ParamDocument* doc, const QUuid& blockId,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;                       ///< Saved block for undo.
    std::vector<cad::param::Block> m_bridges;        ///< Bridges pinned to it (pre-deletion state).
    std::vector<cad::param::Block> m_shadows;        ///< 影子块级联删除的 pre-deletion 快照
                                                     ///< (本体/跟随线被删时随删, DETACH_SHADOW_DESIGN ⑥)。
    std::vector<cad::param::Attachment> m_attachments; ///< Saved attachments for undo.
    std::vector<cad::param::LinkedVariable> m_linked;  ///< Linked vars sourced from the cascade set
                                                       ///< (auto-deleted with the block).
    std::vector<cad::param::MeasureVariable> m_measures; ///< Measure vars referencing the cascade set.
    std::vector<cad::param::Block> m_bakedConsumers;   ///< Blocks whose length formulas referenced
                                                       ///< those linked vars (baked to numbers by redo).
};

/// 影子角度旋转提交 (拆开影子基准 R6/R8): 拆开态旋转改写影子块 transform
/// (冻结克隆自身姿态) 并同步回写跟随线 transform (绕 p3 原地转, Resolver
/// angleOnly 只写 rotation 不碰 origin)。redo/undo 双块 verbatim 还原 +
/// resolveAll —— 撤销后影子角度与跟随线姿态一起回拖前值。
class ShadowRotateCommand : public QUndoCommand
{
public:
    ShadowRotateCommand(cad::param::ParamDocument* doc,
                        const QUuid& shadowId,
                        const cad::param::Transform2D& shadowOld,
                        const cad::param::Transform2D& shadowNew,
                        const QUuid& followerId,
                        const cad::param::Transform2D& followerOld,
                        const cad::param::Transform2D& followerNew,
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_shadowId;
    QUuid m_followerId;
    cad::param::Transform2D m_shadowOld, m_shadowNew;
    cad::param::Transform2D m_followerOld, m_followerNew;
};

/// Move a group of blocks by a delta (supports mergeWith for continuous drag).
class MoveBlockCommand : public QUndoCommand
{
public:
    MoveBlockCommand(cad::param::ParamDocument* doc,
                     const QList<QUuid>& blockIds,
                     const cad::geo::Vec2& delta,
                     QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return static_cast<int>(CommandId::MoveBlock); }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QList<QUuid> m_blockIds;
    cad::geo::Vec2 m_delta;
};

/// Rotate a block to a new transform (supports mergeWith for continuous drag).
/// Also snapshots the endpoint-aim (endTarget) state: rotating a line that
/// aims at a point releases the aim (旋转 = 放弃终点指向), and undo restores
/// both the transform and the aim constraint.
///
/// Also snapshots an optional follower attachment RELEASED by the rotation
/// (旋转 = 放弃跟随): redo() detaches it, undo() restores it. Used when the
/// pivot was moved OFF the attachment point (anchor = the far endpoint).
class RotateBlockCommand : public QUndoCommand
{
public:
    RotateBlockCommand(cad::param::ParamDocument* doc,
                       const QUuid& blockId,
                       const cad::param::Transform2D& oldTf,
                       const cad::param::Transform2D& newTf,
                       const QUuid& oldEndTargetBlock = {},
                       const QUuid& oldEndTargetPoint = {},
                       const QUuid& newEndTargetBlock = {},
                       const QUuid& newEndTargetPoint = {},
                       const QUuid& releasedAttId = {},
                       const cad::param::Attachment& releasedAttBackup = {},
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return static_cast<int>(CommandId::RotateBlock); }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    cad::param::Transform2D m_oldTf;
    cad::param::Transform2D m_newTf;
    QUuid m_oldEndTargetBlock;
    QUuid m_oldEndTargetPoint;
    QUuid m_newEndTargetBlock;
    QUuid m_newEndTargetPoint;
    QUuid m_releasedAttId;                      ///< Attachment detached by redo().
    cad::param::Attachment m_releasedAttBackup; ///< Snapshot restored by undo().
};

/// Add the clones produced by a Ctrl+drag quick copy (快捷复制) as ONE
/// undo step: auto-published bridge-length linked variables first (so their
/// refName resolves), then blocks, then the cloned internal attachments.
class DuplicateBlocksCommand : public QUndoCommand
{
public:
    DuplicateBlocksCommand(cad::param::ParamDocument* doc,
                           cad::param::DuplicateResult result,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::DuplicateResult m_result;
};

/// Rotate-copy (旋转复制, Ctrl+drag in ToolRotate): ONE undo step that
/// creates a clone of the target block and attaches it BACK to the original
/// (follower = clone, leader = original), with the given follower angle
/// measured RELATIVE to the original's current direction.
class RotateCopyCommand : public QUndoCommand
{
public:
    RotateCopyCommand(cad::param::ParamDocument* doc,
                      cad::param::DuplicateResult result,
                      const QUuid& originalBlockId,
                      const QUuid& pivotPointId,
                      const QUuid& clonePivotPointId,
                      const QUuid& leaderSegmentId,
                      double followerAngle,
                      const QString& followerAngleFormula = {},
                      QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::DuplicateResult m_result;
    QUuid m_originalBlockId;     ///< Leader (the rotated original line).
    QUuid m_pivotPointId;        ///< Leader-side attachment point (pivot).
    QUuid m_clonePivotPointId;   ///< Clone-side attachment point.
    QUuid m_leaderSegmentId;     ///< Leader segment whose exit direction is the
                                 ///< follower-angle reference.
    cad::param::Attachment m_att;  ///< Clone→original attachment (id snapshot).
};

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

/// 线段换向 (角度基准视角切换, 用户拍板 2026-08): 交换线段 start/end 身份,
/// 并把"驱动端"Polar 约束搬到另一物理端 (角度 +180 补偿), 换向后几何零跳变、
/// 修改长度/角度变为驱动另一端。物理延长尾巴不动 (extendStart/End 随端点
/// 角色互换); 宿主辅助点仅翻转 interpFromEnd (求解坐标系等价, 位置不变)。
/// v2 放开 + 自动补偿 (世界姿态/位置零跳变):
///   · 基准段消费者 (Polar refSegmentId=本段) —— 角度 +180 / 公式包裹;
///   · 相对交点宿主 (射线角相对段方向) —— interAngle +180 / 公式包裹;
///   · 跟随连接 —— followerAngle +180·k 补偿 (k = 自身 localDir 与角度基准
///     方向的翻转次数, 两次翻转相互抵消; angleIndependent 不驱动旋转不补偿);
///   · 被连接 + 旧档空角度基准点 —— 回填 angleRefPointId = 旧终点 (其出方向
///     = 原 start→end 基准, 精确等价);
///   · 曲线 —— 过点反序 + 切线互换取反 + 弦上锚点 percent→1−p / offset 取反。
/// v1 资格仍拒绝: 桥/省道/终点指向、角度测量引用 (start→end 是测量基准)、
/// 端点被块内其他线段共享、非"锚 Free + 驱动 Polar(ref=另一端)"标准结构、
/// 滑轨模式连接 (局部系快照会镜像)、需补偿的弧长模式连接 (πr 不可参数化表达)。
class ReverseSegmentCommand : public QUndoCommand
{
public:
    /// 资格检查: 可换向返回 true; 否则 reason 带中文原因 (UI 置灰提示)。
    static bool canReverse(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& segmentId,
                           QString* reason = nullptr);

    ReverseSegmentCommand(cad::param::ParamDocument* doc,
                          const QUuid& blockId, const QUuid& segmentId,
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    /// 端点约束快照 (换向只动这两个点的驱动结构; 曲线时含切线)。
    struct PointSnapshot {
        cad::param::PointConstraint constraint{};
        cad::geo::Vec2  freePos;
        QUuid  refPointId;
        double distance = 0.0;
        double angle    = 0.0;
        QUuid  refSegmentId;
        QString distanceFormula;
        QString angleFormula;
        bool   interpFromEnd = false;
        geo::Vec2 tangentIn;    ///< 原存储切线 (undo 恢复; 直线恒零向量, 无害)。
        geo::Vec2 tangentOut;
        bool   autoTangent = true;  ///< 原自动切线标志 (冻结后恢复)。
    };

    /// v2: 方向基准消费者快照 (Polar refSegmentId / 相对交点, +180 补偿)。
    struct ConsumerSnapshot {
        QUuid pointId;
        double angle = 0.0;
        QString angleFormula;
        double interAngle = 0.0;
        QString interAngleFormula;
    };

    /// v2: 曲线过点快照。Hobby 自动切线求解不保证换序对称 (实测弧长漂移),
    /// 换向 = 从曲线缓存捕获"同解"有效切线 → 镜像后冻结为手动 (与
    /// BreakSegmentCommand 打断冻结同范式), undo 恢复原 autoTangent/切线。
    struct CurveAnchorSnapshot {
        QUuid pointId;
        bool autoTangent = true;      ///< 原标志 (undo 恢复)。
        geo::Vec2 tangentIn;          ///< 原存储切线 (undo 恢复)。
        geo::Vec2 tangentOut;
        geo::Vec2 effTangentIn;       ///< 换向前解算有效切线 (冻结源)。
        geo::Vec2 effTangentOut;
        double interpPercent = 0.0;
        double interpOffsetDist = 0.0;
    };

    /// v2: 连接补偿快照 (跟随角 +180·k / 旧档角度基准点回填 oldEnd)。
    struct AttachmentSnapshot {
        QUuid attId;
        bool compensateAngle = false;  ///< k 为奇数且角度被驱动 → followerAngle +180
        bool backfill = false;         ///< 旧档空角度基准点 → 回填旧终点 id
        double followerAngle = 0.0;
        QString followerAngleFormula;
        QUuid angleRefPointId;         ///< 原值 (可能为空, undo 恢复空 = 旧档语义)
    };

    void applyState(bool reversed);

    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_oldStartId;
    QUuid m_oldEndId;
    PointSnapshot m_oldStart;
    PointSnapshot m_oldEnd;
    std::vector<std::pair<QUuid, bool>> m_auxFromEnd;  ///< 宿主辅助点 (id, 原 interpFromEnd).
    std::vector<QUuid> m_passPoints;                   ///< 曲线过点原序 (undo 恢复).
    std::vector<ConsumerSnapshot> m_consumers;
    std::vector<CurveAnchorSnapshot> m_curveAnchors;
    bool m_curveCacheValid = false;   ///< 曲线缓存捕获成功 (span 数 = 过点数+1)。
    geo::Vec2 m_effStartTangentOut;   ///< 曲线端点同解有效切线 (缓存捕获, 冻结源)。
    geo::Vec2 m_effEndTangentIn;
    std::vector<geo::Vec2> m_innerEffIn;   ///< 内部锚点同解有效切线 (过点序)。
    std::vector<geo::Vec2> m_innerEffOut;
    std::vector<AttachmentSnapshot> m_attComp;
    double m_extendStartMm = 0.0;
    QString m_extendStartFormula;
    double m_extendEndMm = 0.0;
    QString m_extendEndFormula;
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
