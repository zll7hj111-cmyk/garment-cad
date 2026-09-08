#pragma once

// 跨选集连接策略（2026-12 审计 TOOL-P0-8 / TOOL-P1-32 / U10 收口）
//
// 「跨选集连接」= attachment 恰好一端在本次操作的对象集合内、另一端在集合外。
// 此前判定散落三处且 pin 处理相反（TOOL-P0-8）：
//   · 移动 src/tools/SelectDragController.cpp:49 —— 无条件断开（含 pin）
//   · 旋转 src/tools/MultiRotateSession.cpp:59   —— !isPin 才释放
//   · 复制 src/parametric/Duplicate.cpp Pass 3/4 —— 按块裁剪（不是逐条判定）
// 本头是移动/旋转的唯一判定来源；执行方式（忽略解算 / angleOnly / 提交时删除）
// 仍由各工具决定。
//
// 语义依据：
//   · 移动（用户拍板 2026-09「拖动不跟随，直接断开」，DECISIONS.md:22）：
//     拖动即断，pin 也断 —— 桥接线被拖动时销钉必须跟着断开，否则销钉会把端点
//     拉回宿主，桥接线根本动不了。
//   · 旋转：桥接线永不进入旋转选集（MultiRotateSession::adoptSelection 排除
//     isBridge），故 pin 的跨边界方向恒为 toIn=true（宿主在集合内）。释放 pin
//     会让桥接线在旋转期间脱离宿主、提交后断裂，必须保持被动拉伸。
//   · 复制：跨边界处置是「按块」的（Duplicate.cpp Pass 3 只克隆两端都在集合内
//     的连接；Pass 4 按桥接线两侧 keptPins==2 决定副本保留还是冻结释放），
//     单条连接无法判定，故本函数对 Duplicate 恒返回 false。契约见
//     src/parametric/Duplicate.h:40-50。
//
// 注：U10 工单原签名含 isBridge —— 桥接线的差别已在「选集采纳」阶段（旋转排除
// 桥接线）与本函数的 pin 分支里表达，逐条判定不需要 isBridge，故省略。

namespace cad::param {

/// 触发跨选集判定的操作类型。
enum class CrossSelectionOp {
    Move,       ///< 拖动（SelectDragController）
    Rotate,     ///< 旋转（MultiRotateSession / RotateSession）
    Duplicate,  ///< 复制（Duplicate，见文件头说明）
};

/// 恰好一端在选集中（另一端在集合外）。
[[nodiscard]] inline bool crossesSelectionBoundary(bool fromIn, bool toIn)
{
    return fromIn != toIn;
}

/// 该连接是否因跨选集而脱离本次操作的解算。
/// @param fromIn 跟随端（Attachment::fromBlockId）是否在选集中
/// @param toIn   基准端（Attachment::toBlockId）是否在选集中
/// @param isPin  桥接销钉连接（Attachment::isPin）
[[nodiscard]] inline bool isReleasedAcrossSelection(bool fromIn, bool toIn,
                                                    bool isPin, CrossSelectionOp op)
{
    if (!crossesSelectionBoundary(fromIn, toIn)) return false;
    switch (op) {
    case CrossSelectionOp::Move:      return true;   // 拖动即断（含 pin）
    case CrossSelectionOp::Rotate:    return !isPin; // 桥接 pin 保持被动拉伸
    case CrossSelectionOp::Duplicate: return false;  // 按块裁剪，见文件头
    }
    return false;
}

}  // namespace cad::param
