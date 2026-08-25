#pragma once

namespace cad::tools {

/// Interaction state machine for the selection tool (ETCAD-inspired).
enum class SelectState {
    Idle,           ///< No selection, no active interaction.
    Selecting,      ///< Selection set non-empty; selected = operation-ready.
                    ///< (2026-09 取消确认基准: 原 Confirmed 状态由
                    ///< ToolSelect::setState 归一化为本态 — 选中即就绪,
                    ///< 按住线身移动 = 拖动, 无需右键确认.)
    Confirmed,      ///< 保留枚举 (ConnectGesture/CopyDragController 仍会发
                    ///< 该值), ToolSelect::setState 统一归一化为 Selecting.
    Marquee,        ///< Left-drag on empty space: drawing selection rectangle.
    Dragging,       ///< Moving selection (anchored at the press point).
    CopyDragging,   ///< Ctrl+drag on a segment: dragging freshly cloned copies.
    Connecting,     ///< Dragging from an endpoint to establish a new connection.
    ConfirmTarget,  ///< Multiple overlapping target points: click a candidate
                    ///< segment (whose endpoint lies on the connection spot) to
                    ///< confirm the leader; Esc/blank cancels.
    ConfirmSource,  ///< Multiple overlapping SOURCE points: click a candidate
                    ///< member segment to choose which member endpoint starts
                    ///< the connection; Esc/blank cancels.
    AngleInput,     ///< Connection made; HUD active for construction-angle entry.
};

/// Selection behaviour mode (W toggles within the select tool).
enum class SelectionMode {
    Multi,   ///< Click toggles blocks in/out of the selection; marquee supported.
    Single,  ///< Click selects exactly ONE block (replaces previous); no marquee.
};

} // namespace cad::tools
