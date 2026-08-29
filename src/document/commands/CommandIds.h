#pragma once

#include <QtGlobal>

namespace cad::cmd {

/// Central, single-definition-point command merge IDs.
///
/// QUndoStack calls `top->mergeWith(other)` whenever the incoming command's
/// `id()` equals the stack top's `id()` (both != -1). mergeWith() then casts
/// the other command to its own type, so **two commands sharing an id is
/// undefined behaviour** (the mismatch used to be caught only by luck: the
/// RotateBlockCommand / SetVariableValueCommand collision at 1003 could cast
/// a variable command to a block command and read garbage).
///
/// Every command that overrides QUndoCommand::id() MUST draw its id from this
/// enum — never hardcode a literal. Keep ids globally unique.
enum class CommandId : int {
    /// MoveBlockCommand (continuous drag merge).
    MoveBlock = 1001,
    /// SetFollowerAngleCommand (continuous angle-drag merge).
    SetFollowerAngle = 1002,
    /// RotateBlockCommand (continuous rotation-drag merge).
    RotateBlock = 1003,
    /// SetVariableValueCommand (continuous slider/spinbox merge). Was 1003
    /// (collided with RotateBlockCommand) — renumbered to 1004.
    SetVariableValue = 1004,
};

} // namespace cad::cmd