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
    cad::param::Block m_shadow;
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

} // namespace cad::cmd
