#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"
#include "document/commands/CommandIds.h"  // central merge-id enum (P0-2)

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add an attachment between two blocks.
class AddAttachmentCommand : public QUndoCommand
{
public:
    AddAttachmentCommand(cad::param::ParamDocument* doc,
                         cad::param::Attachment att,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;
};

/// Remove an attachment by ID.
class RemoveAttachmentCommand : public QUndoCommand
{
public:
    RemoveAttachmentCommand(cad::param::ParamDocument* doc,
                            const QUuid& attId,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;  ///< Saved for undo.
    /// Removing a bridge pin releases the bridge (it becomes an independent
    /// segment) — snapshot its pre-removal state + attachments for undo.
    cad::param::Block m_bridge;
    std::vector<cad::param::Attachment> m_bridgeAtts;
    bool m_hasBridge = false;
};

/// 拆开保留角度 (detach position, keep angle): 影子基准语义 (用户拍板 2026-xx,
/// DETACH_SHADOW_DESIGN.md —— 翻案旧「拆开=活引用」)。redo(angleOnly=true):
///   · 基准是普通线 + 非降级 → 影子换代 ②: 复制本体 exit 段为隐藏影子块
///     (isShadow), Att2 原地换代指向影子 (angleOnly=true, offset 原样 R2);
///   · 基准已是影子 → 再拆开 ④: 删除 Att1 (挂载关系), 影子冻结当前方向;
///   · 降级场景 (本体为桥线/省道/组件成员/多段块/曲线段) → 旧 angleOnly
///     行为逐位保持 (无影子, 活引用)。
/// redo(angleOnly=false): 基准是影子 → 挂回本体 ⑤: 删影子 + Att2 还原到
///   本体 (活引用恢复, 重新焊接); 基准非影子 → 旧恢复语义。
/// Undo 全部一步回到动作前状态 (影子块/连接 verbatim 快照)。
class SetAttachmentAngleOnlyCommand : public QUndoCommand
{
public:
    SetAttachmentAngleOnlyCommand(cad::param::ParamDocument* doc,
                                  const QUuid& attId, bool angleOnly,
                                  const QUuid& explicitToPoint = QUuid(),
                                  const QUuid& explicitToSegment = QUuid(),
                                  QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    enum class Mode { Legacy, FreshDetach, ReDetach, ReconnectMaster };
    Mode m_mode = Mode::Legacy;

    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newAngleOnly;
    bool m_oldAngleOnly;
    bool m_oldLocked;  ///< 拖动保护 snapshot (拆开时清除).
    cad::param::SlideMode m_oldSlideMode;  ///< 拆开时清除滑轨 (互斥).
    /// 影子换代 verbatim 快照: 拆开前/后连接态 + 影子块 (redo 添加 / undo 删除)。
    cad::param::Attachment m_oldAtt;
    cad::param::Attachment m_newAtt;
    cad::param::Attachment m_oldAtt1;   ///< 挂载关系 Att1 (④删除/⑤删除, undo 还原)。
    bool m_hasAtt1 = false;
    cad::param::Block m_shadow;         ///< 影子块 verbatim (FreshDetach/ReconnectMaster)。
    bool m_hasShadow = false;
    QUuid m_explicitToPoint;            ///< ⑤ 显式落点 (挂载路由拖回本体)。
    QUuid m_explicitToSegment;
};

/// 影子挂载 (拆开影子线段, DETACH_SHADOW_DESIGN.md §7.4 状态③): 跟随线
/// (基准=影子) 拖到新宿主线上 → Att1 = 影子→宿主 (Δ 反算保向, 挂载瞬间影子/
/// 跟随线世界方向不变) + Att2 恢复位置钉点并重新焊接 —— 形成 L3→影子→L2
/// 双连接链 (R3: 宿主旋转链式带动跟随线)。undo 一步回到挂载前拆开态。
class ShadowMountCommand : public QUndoCommand
{
public:
    ShadowMountCommand(cad::param::ParamDocument* doc,
                       const QUuid& shadowId,
                       const QUuid& toBlockId,
                       const QUuid& toPointId,
                       const QUuid& toSegmentId,
                       QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_att2Id;                     ///< 跟随线→影子 连接 id (翻旗)。
    cad::param::Attachment m_att1;      ///< 新挂载连接 (verbatim 重放)。
    cad::param::Attachment m_oldAtt2;   ///< 挂载前 Att2 (undo verbatim 还原)。
    bool m_valid = false;
};

/// 清除影子 (面板「清除影子」入口, DETACH_SHADOW_DESIGN.md §7.3): 删除 Att2
/// (跟随线失去角度基准转纯自由线) 与 Att1 (若挂载) 及影子块本身。undo 一步
/// verbatim 还原影子块 + 全部连接。
class RemoveShadowCommand : public QUndoCommand
{
public:
    RemoveShadowCommand(cad::param::ParamDocument* doc,
                        const QUuid& shadowId,
                        QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_shadowId;
    cad::param::Block m_shadow;                 ///< 影子块 verbatim (undo 还原)。
    std::vector<cad::param::Attachment> m_atts; ///< Att1/Att2 verbatim。
    bool m_valid = false;
};

/// 位置吸附保持、角度独立 (用户新需求 2026): toggles an attachment between
/// normal angle-following and independent-angle mode. In independent mode the
/// from-point stays pinned to the leader, but the follower's own rotation is
/// preserved. Turning OFF back-solves the follower angle from the current world
/// direction so there is no visual jump when angle-following resumes.
class SetAttachmentAngleIndependentCommand : public QUndoCommand
{
public:
    SetAttachmentAngleIndependentCommand(cad::param::ParamDocument* doc,
                                         const QUuid& attId, bool angleIndependent,
                                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    bool m_newIndependent;
    bool m_oldIndependent;
    bool m_oldAngleOnly = false;
    cad::param::SlideMode m_oldSlideMode = cad::param::SlideMode::None;
    bool m_oldLocked = false;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
};


/// 位置锚点与角度基准分离 (用户需求 2026): sets a separate angle-reference
/// block/segment on an attachment. The follower's position stays pinned to
/// toBlock, but its followerAngle is measured against the chosen angle-ref
/// segment instead of the position leader. Pass null to restore the default
/// (angle follows the position leader). Changing the ref back-solves the
/// current world direction into followerAngle so there is no visual jump.
class SetAttachmentAngleRefCommand : public QUndoCommand
{
public:
    SetAttachmentAngleRefCommand(cad::param::ParamDocument* doc,
                                 const QUuid& attId,
                                 const QUuid& newRefBlockId,
                                 const QUuid& newRefSegmentId,
                                 const QUuid& newRefPointId = QUuid(),
                                 const QUuid& newRef2BlockId = QUuid(),
                                 const QUuid& newRef2PointId = QUuid(),
                                 QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newRefBlockId;
    QUuid m_newRefSegmentId;
    QUuid m_newRefPointId;
    QUuid m_newRef2BlockId;
    QUuid m_newRef2PointId;
    QUuid m_oldRefBlockId;
    QUuid m_oldRefSegmentId;
    QUuid m_oldRefPointId;
    QUuid m_oldRef2BlockId;
    QUuid m_oldRef2PointId;
    bool m_oldAngleIndependent = false;
    bool m_oldAngleOnly = false;
    cad::param::SlideMode m_oldSlideMode = cad::param::SlideMode::None;
    bool m_oldLocked = false;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
};

/// 重新挂接 (用户需求 2026): 解焊后把跟随线拖到新的位置宿主 A, 同时保留
/// 原角度基准 B —— 形成“位置挂 A、角度跟 B”的双基准连接。
/// 若原连接没有独立角度基准, 自动把旧位置宿主设为新的角度基准 (B=旧A)。
/// Undo 恢复旧 attachment 与拖前 transform。
class ReattachAttachmentCommand : public QUndoCommand
{
public:
    ReattachAttachmentCommand(cad::param::ParamDocument* doc,
                              const QUuid& attId,
                              const QUuid& newToBlockId,
                              const QUuid& newToPointId,
                              const QUuid& newToSegmentId,
                              QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newToBlockId;
    QUuid m_newToPointId;
    QUuid m_newToSegmentId;
    cad::param::Attachment m_oldAtt;
    bool m_hasOldAtt = false;
    cad::geo::Vec2 m_oldOrigin;
    double m_oldRotation = 0.0;
};

/// 设置对齐点 (2026-09 设计修正): 本线段的哪个端点钉在目标点上 —— 只改
/// Attachment::fromPointId (同块点互换), 并按当前基准方向反算 followerAngle
/// 保零跳变。**与换向 (start/end 身份) 完全无关**: fromPointId 由连接语义
/// 决定, 不随 ReverseSegmentCommand 翻转。undo 恢复旧 fromPointId 与角度。
class SetAlignPointCommand : public QUndoCommand
{
public:
    SetAlignPointCommand(cad::param::ParamDocument* doc,
                         const QUuid& attId,
                         const QUuid& newFromPointId,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    QUuid m_newFromPointId;
    QUuid m_oldFromPointId;
    double m_oldFollowerAngle = 0.0;
    QString m_oldFollowerFormula;
    cad::param::RotationMode m_oldRotationMode = cad::param::RotationMode::Angle;
    double m_oldArcLength = 0.0;
    QString m_oldArcFormula;
};

/// 仅角度线拖端点重挂 (用户报告 2026-12: 使用了引用线段但无连接线段的线,
/// 拖动端点无吸附反应): a line in angle-only mode (位置自由、角度随基准线)
/// is reconnected onto a new leader — 旧角度基准保留为独立角度基准 (双基准),
/// 位置挂到新端点, 恢复完整连接并重新焊接. redo re-applies the reattached
/// state (含 HUD 角度调整后的新态), undo restores the old angle-only
/// attachment + pre-drag transform — the WHOLE reattach is ONE undo step
/// (与普通连接的"建立连接"宏单步撤销体验一致).
class ReconnectAttachmentCommand : public QUndoCommand
{
public:
    ReconnectAttachmentCommand(cad::param::ParamDocument* doc,
                               const QUuid& attId,
                               const cad::param::Attachment& newAtt,
                               const cad::param::Attachment& oldAtt,
                               const cad::geo::Vec2& oldOrigin,
                               double oldRotation,
                               QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    cad::param::Attachment m_newAtt;  ///< 重挂后状态 (含 HUD 角度调整).
    cad::param::Attachment m_oldAtt;  ///< 重挂前仅角度态 (undo 恢复).
    cad::geo::Vec2 m_oldOrigin;       ///< 拖前 transform (undo 恢复).
    double m_oldRotation = 0.0;
};


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

/// Set the follower angle (followerAngle) and/or arc-length rotation state
/// of an attachment. Supports both angle and arc-length modes.
class SetFollowerAngleCommand : public QUndoCommand
{
public:
    SetFollowerAngleCommand(cad::param::ParamDocument* doc,
                          const QUuid& attId, double newAngle,
                          const QString& newFormula = QString(),
                          cad::param::RotationMode newMode = cad::param::RotationMode::Angle,
                          double newArcLength = 0.0,
                          const QString& newArcFormula = QString(),
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return static_cast<int>(CommandId::SetFollowerAngle); }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    double m_oldAngle;
    double m_newAngle;
    QString m_oldFormula;
    QString m_newFormula;
    cad::param::RotationMode m_oldMode;
    cad::param::RotationMode m_newMode;
    double m_oldArcLength;
    double m_newArcLength;
    QString m_oldArcFormula;
    QString m_newArcFormula;
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
