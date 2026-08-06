#pragma once

#include <QUuid>
#include <QSet>
#include <vector>

#include "parametric/Attachment.h"

namespace cad::param {

/// Result of validating a candidate attachment against the existing graph.
enum class AttachmentIssue {
    Ok,                 ///< Attachment can be added safely.
    DuplicateFollower,  ///< The from-block is already the follower of another
                        ///< attachment (each block follows at most one leader).
    Cycle,              ///< Adding the attachment would close a leader cycle
                        ///< (includes the trivial self-attachment case).
};

/// Validate a candidate attachment against the forest invariant documented in
/// Attachment.h: every block is the follower (from-block) of AT MOST one
/// attachment, and the leader links form a forest (no cycles).
///
/// Bridge pins (Attachment::isPin) relax the follower rule: a bridge block is
/// the follower of exactly TWO pin attachments (one per endpoint). Pins never
/// mix with regular attachments on the same follower. A bridge's PINNED
/// endpoints are pure leaves (nothing attaches to them — enforced by
/// ParamDocument), but an AUXILIARY point on a bridge may anchor a regular
/// follower (the Resolver settles bridge followers after the bridge), so pins
/// still cannot participate in leader cycles.
///
/// This is a pure graph check over the attachment list; existence of the
/// referenced blocks must be verified separately by the caller.
[[nodiscard]] inline AttachmentIssue checkAttachment(
    const std::vector<Attachment>& existing, const Attachment& candidate)
{
    // Self-attachment is a trivial cycle.
    if (candidate.fromBlockId == candidate.toBlockId)
        return AttachmentIssue::Cycle;

    // Follower rule: at most one regular attachment, or at most two pins
    // (bridge endpoints). Mixing pins with regular attachments is rejected.
    int pins = 0;
    for (const Attachment& att : existing) {
        if (att.fromBlockId != candidate.fromBlockId) continue;
        if (!candidate.isPin || !att.isPin)
            return AttachmentIssue::DuplicateFollower;
        ++pins;
    }
    if (candidate.isPin && pins >= 2)
        return AttachmentIssue::DuplicateFollower;

    // Cycle check: walk the leader chain upward from the candidate's leader
    // (to-block); reaching the from-block means the new link would close a loop.
    QSet<QUuid> seen;
    QUuid cur = candidate.toBlockId;
    while (!cur.isNull() && !seen.contains(cur)) {
        if (cur == candidate.fromBlockId)
            return AttachmentIssue::Cycle;
        seen.insert(cur);
        QUuid leader;
        for (const Attachment& att : existing)
            if (att.fromBlockId == cur) { leader = att.toBlockId; break; }
        cur = leader;
    }
    return AttachmentIssue::Ok;
}

} // namespace cad::param
