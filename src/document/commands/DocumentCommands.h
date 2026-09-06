#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QList>

#include <optional>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"
#include "document/commands/BlockLifecycleCommands.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Composite: draw a line = add block (with 2 points + 1 segment) + optional attachment.
/// Use beginMacro/endMacro via the parent QUndoStack.
class DrawLineCommand : public QUndoCommand
{
public:
    DrawLineCommand(cad::param::ParamDocument* doc,
                    cad::param::Block block,
                    std::optional<cad::param::Attachment> att = std::nullopt,
                    QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;
    std::optional<cad::param::Attachment> m_att;
};

/// Composite: delete a block + all its attachments + shadow cascade.
/// Alias to RemoveBlockCommand for full shadow and baked-consumer cascade support.
using DeleteBlockCommand = RemoveBlockCommand;

/// Composite: draw a measured line (new bridge model) = add a free block whose
/// length formula references a fresh MeasureVariable (two-point distance).
/// By default the line also follows host A (attachment) and aims at host B
/// (endTarget, stored on the block itself) — 构造线默认跟随.
/// The line remains fully editable afterwards (rotate / resize / drag).
class DrawMeasureLineCommand : public QUndoCommand
{
public:
    DrawMeasureLineCommand(cad::param::ParamDocument* doc,
                           cad::param::Block block,
                           cad::param::MeasureVariable mv,
                           std::optional<cad::param::Attachment> followAtt = std::nullopt,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_block;
    cad::param::MeasureVariable m_mv;
    std::optional<cad::param::Attachment> m_followAtt;  ///< Default start-follow.
    /// True only when redo()'s addAttachment actually succeeded — undo must
    /// remove EXACTLY what was established (快照对称: never detach an edge
    /// that was rejected).
    bool m_attAdded = false;
};

/// Composite: bake a measure line onto a working layer (烘焙到操作层).
///
/// Semantics = COPY, not move (烘焙语义=复制而非移动): the source measurement
/// line stays on its layer as the measurement owner, while a NEW free line is
/// created on the target working layer at the source's CURRENT world pose.
/// The copy's length formula references the source's MeasureVariable refName
/// (活链接 — owner stays the source line), the angle is the source's current
/// world angle, and NO attachments are created (free line).
class BakeMeasureCopyCommand : public QUndoCommand
{
public:
    BakeMeasureCopyCommand(cad::param::ParamDocument* doc,
                           const QUuid& sourceMeasureBlockId,
                           const QUuid& targetLayerId,
                           QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

    /// The id of the baked copy (stable across undo/redo — built in the ctor).
    [[nodiscard]] QUuid newBlockId() const { return m_newBlock.id; }
    /// False when the constructor rejected the request (source gone / not a
    /// measure line / target not a working layer) — redo/undo are no-ops.
    [[nodiscard]] bool isValid() const { return m_valid; }

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Block m_newBlock;  ///< The baked copy, fully built in the ctor.
    bool m_valid = false;
};

} // namespace cad::cmd
