#pragma once

#include <QUndoCommand>
#include <QUuid>

#include "parametric/Block.h"
#include "parametric/Attachment.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Break a segment at an auxiliary (Interpolated) point that lies exactly on
/// the segment (no offset). The original block keeps the front portion; a NEW
/// block is created for the back portion, connected via an Attachment with
/// follower angle 180° (straight continuation; 闭合基准 2026-08: 0° = 折叠).
///
/// Formula splitting:
///   front = "(orig)*percent" [+constant]
///   back  = "(orig)*(1-percent)" [-constant]
///
/// Undo restores the original block snapshot and removes the new block.
class BreakSegmentCommand : public QUndoCommand
{
public:
    BreakSegmentCommand(cad::param::ParamDocument* doc,
                        const QUuid& blockId,
                        const QUuid& segmentId,
                        const QUuid& auxPointId,
                        QUndoCommand* parent = nullptr);

    void redo() override;
    void undo() override;

    /// Returns true if the break preconditions are met (aux point is
    /// Interpolated, no offset, on a Line segment, not a bridge).
    [[nodiscard]] bool isValid() const { return m_valid; }

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_auxPointId;
    bool  m_valid = false;

    // Undo state
    cad::param::Block m_origBlockSnapshot;
    std::vector<cad::param::Attachment> m_removedAttachments;
    QUuid m_newBlockId;

    // Auto-published front-length measurement (RefChain/Freeze break): the
    // back formula becomes "orig − M_front" so the total length stays exactly
    // conserved while the front half moves dynamically.
    QUuid m_publishedLinkedId;   ///< Newly created variable (empty = reused an
                                 ///< existing publication of this segment).
    QString m_publishedRefName;  ///< refName used by the back formula.
};

} // namespace cad::cmd
