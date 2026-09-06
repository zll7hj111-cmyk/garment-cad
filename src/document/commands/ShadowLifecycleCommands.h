#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

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
    QUuid m_shadowId;
    QUuid m_att2Id;                     ///< 跟随线→影子 连接 id (翻旗)。
    cad::param::Attachment m_att1;      ///< 新挂载连接 (verbatim 重放)。
    cad::param::Attachment m_oldAtt2;   ///< 挂载前 Att2 (undo verbatim 还原)。
    QUuid m_oldLastHostBlockId;
    QUuid m_oldLastHostPointId;
    QUuid m_oldLastHostSegmentId;
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
