#pragma once

#include "document/commands/BlockCommands.h"  // MovePointCommand (撤销提交)
#include "geometry/Vec2.h"
#include "parametric/Block.h"                 // ParamPoint (freePos 快照)
#include "parametric/ParamDocument.h"         // resolveForDrag / invalidateLayer

#include <QUuid>
#include <QSet>
#include <optional>
#include <utility>
#include <algorithm>

class QUndoStack;

namespace cad::tools {

/// 曲线锚点拖动会话 (ToolSelect 手势提炼, 阶段 3 拆分): 只对 *当前已选* 曲线
/// 块的自由端 pass point 起效 —— 按下后把该点拖离原 freePos 到光标的局部
/// 帧坐标, 实时 resolveForDrag 级联重算; 释放时以 MovePointCommand 提交
/// 撤销。CurveAnchor 拖拽点 (SmartPen 参数化点) 不参与, 避免破坏弦链接。
/// 会话与选择解耦: 该控制器只管「拖」本身, 不碰选择集 / 高亮 / 状态机。
class CurveAnchorDragSession
{
public:
    CurveAnchorDragSession(cad::param::ParamDocument* doc, QUndoStack* undo)
        : m_paramDoc(doc), m_undoStack(undo) {}

    /// 当前选择的曲线块里、距 worldPos < 10px 的最近可拖 pass point。
    /// 返回 {blockId, pointId}; 选择为空 / 无非 CurveAnchor 候选 = nullopt。
    /// CurveAnchor 由 SmartPen 参数化驱动, 此处不响应其拖拽 (会转成 Free
    /// 破坏弦链接)。
    [[nodiscard]] std::optional<std::pair<QUuid, QUuid>> hitAt(
        const cad::geo::Vec2& worldPos, double zoom,
        const QSet<QUuid>& selection) const;

    void begin(const QUuid& blockId, const QUuid& pointId);
    [[nodiscard]] bool active() const { return m_dragging; }
    void update(const cad::geo::Vec2& worldPos);
    void end();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    bool            m_dragging = false;
    QUuid           m_blockId;
    QUuid           m_pointId;
    cad::geo::Vec2  m_origPos;  ///< 拖拽前 freePos (MovePointCommand 撤销载荷).
};

} // namespace cad::tools
