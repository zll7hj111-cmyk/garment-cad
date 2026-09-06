#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"

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
/// Removing a bridge pin releases the bridge (the model layer converts it to
/// an independent segment) — snapshot the pristine block + all attachments
/// for undo.
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
    cad::param::Block m_bridge;
    std::vector<cad::param::Attachment> m_bridgeAtts;
    bool m_hasBridge = false;
};

/// 拆开保留角度 (detach position, keep angle): 影子基准语义
/// (DETACH_SHADOW_DESIGN.md)。redo(angleOnly=true):
///   · 基准是普通线 + 非降级 → 影子换代 ②: 复制本体 exit 段为隐藏影子块
///     (isShadow), Att2 原地换代指向影子 (angleOnly=true, offset 原样 R2);
///   · 基准已是影子 → 再拆开 ④: 删除 Att1 (挂载关系), 影子冻结当前方向;
///   · 降级场景 (本体为桥线/省道/组件成员/多段块/曲线段) → 旧 angleOnly
///     行为逐位保持 (无影子, 活引用)。
/// redo(angleOnly=false): 基准是影子 → 挂回本体 ⑤: 删影子 + Att2 还原到
///   本体 (活引用恢复, 重新焊接); 基准非影子 → 旧恢复语义。
///   @p forceMaster (面板显式重定向到本体用): 跳过重连缓存自动选路
///   (ReconnectMounted), 强制走 ReconnectMaster —— 用户明确选了本体落点,
///   不允许被 lastHost 缓存改道到其他宿主。
/// Undo 全部一步回到动作前状态 (影子块/连接 verbatim 快照)。
class SetAttachmentAngleOnlyCommand : public QUndoCommand
{
public:
    SetAttachmentAngleOnlyCommand(cad::param::ParamDocument* doc,
                                  const QUuid& attId, bool angleOnly,
                                  const QUuid& explicitToPoint = QUuid(),
                                  const QUuid& explicitToSegment = QUuid(),
                                  bool forceMaster = false,
                                  QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    enum class Mode { Legacy, FreshDetach, ReDetach, ReconnectMaster, ReconnectMounted };
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
    cad::param::Attachment m_newAtt1;   ///< ReconnectMounted 新挂载 Att1 (redo 添加/undo 删除)。
    cad::param::Block m_shadow;
    bool m_hasShadow = false;
    QUuid m_explicitToPoint;            ///< ⑤ 显式落点 (挂载路由拖回本体)。
    QUuid m_explicitToSegment;
    bool m_forceMaster = false;         ///< ⑤ 跳过重连缓存, 强制挂回本体。
    QUuid m_oldShadowLastHostBlockId;
    QUuid m_oldShadowLastHostPointId;
    QUuid m_oldShadowLastHostSegmentId;
};

} // namespace cad::cmd

#include "document/commands/ShadowLifecycleCommands.h"


