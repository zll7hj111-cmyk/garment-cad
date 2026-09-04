#pragma once

#include "geometry/Vec2.h"
#include "parametric/Attachment.h"  // Attachment (拖拆快照) / SlideMode
#include "tools/SelectState.h"      // SelectState (状态回调载荷)
#include "tools/SnapEngine.h"       // 拖拽释放时的重挂吸附 (SnapEngine)

#include <QUuid>
#include <QList>
#include <QHash>
#include <QSet>
#include <functional>
#include <utility>

class QUndoStack;
namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// 选集拖动控制器 (ToolSelect 手势提炼, 阶段 3 拆分): 负责按下后超过阈值
/// 才开始的「整体平移选择集」会话 ——
///   · begin:      捕获拖动起点、成员原点快照、方向感知拆除名单、滑轨坐标快照;
///   · update:     每帧按 delta 移动 + 实时 resolveForDrag (跟随线级联随动);
///   · end:        复原 → 经 undo 栈重放 (移动 + 拆/保留角度 + 滑轨坐标回写),
///                 拖动释放点在吸附半径内时尝试「位置重挂 + 角度基准保留」
///                 (影子基准拓扑: 挂回本体 = 删影子; 挂到其他线 = 影子挂载链);
///                 返回 true = 已收尾为单击 (选择集保留, 由调用方继续管理);
///   · cancelDrag: 不动 undo 栈地把几何弹回拖前快照并重新求解 (Esc 中止)。
/// 状态转换/选择集清空由回调交还 ToolSelect (与 ConnectGesture 同款骨架)。
class SelectDragController
{
public:
    using StateFn = std::function<void(SelectState)>;

    SelectDragController(cad::param::ParamDocument* doc, QUndoStack* undo,
                         StateFn stateFn)
        : m_paramDoc(doc), m_undoStack(undo), m_stateFn(std::move(stateFn)) {}

    /// 开始拖动: 快照拖动集 (锁焊闭包 + 组件闭包) 与原点。
    void begin(const cad::geo::Vec2& pos, const QSet<QUuid>& selection);
    /// 每帧: 应用 delta + live resolve (跟随级联实时可见)。
    void update(const cad::geo::Vec2& pos);
    /// 释放: 复原 → undo 重放; 纯单击 (位移≈0) 不推命令。返回 true = 已
    /// 收尾为单击 (选择集保留); false = 命令已提交, 调用方应
    /// clearSelectionAndIdle (2026-09 取消确认基准: 移动后清选回 Idle)。
    [[nodiscard]] bool end(const cad::geo::Vec2& pos, double zoom);
    /// Esc 中止: 快照复原 + 求解, 回到 Selecting (ToolSelect 继续刷新画布)。
    void cancelDrag();

    /// 设置拖拽起始时的画布缩放 (供 end() 内的 tryReattachOnDragEnd 吸附半径
    /// 换算)。ToolSelect 在 mouseMove 转 Drag 时注入。
    void setZoom(double zoom) { m_zoom = zoom; }

private:
    /// 拖动释放时若满足「解焊后拖到新位置点」, 执行位置重挂 + 角度基准保留。
    /// 仅单 follow 线 + 单拆名单场合生效 (否则 false, 走普通移动命令)。
    [[nodiscard]] bool tryReattachOnDragEnd(const cad::geo::Vec2& pos);

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    StateFn m_stateFn;
    double m_zoom = 1.0;             ///< begin() 时由 ToolSelect 注入.

    cad::geo::Vec2 m_startPos;
    QList<QUuid>   m_blockIds;
    QHash<QUuid, cad::geo::Vec2> m_origins;
    QList<QUuid>   m_detachedAttachments;     ///< Cross-boundary attachments to break.
    /// 滑轨模式 (抽屉式滑动): attachmentId → pre-drag (slideAlongMm, slidePerpMm)
    /// snapshot for slide attachments whose FOLLOWER is inside the drag set.
    QHash<QUuid, std::pair<double, double>> m_oldSlideOffsets;
    SnapEngine m_snapEngine;  ///< 拖拽释放时检测「重新挂接」目标点.
};

} // namespace cad::tools
