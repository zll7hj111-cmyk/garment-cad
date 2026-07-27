#pragma once

#include <QUuid>
#include <QHash>
#include <QString>
#include <QList>
#include <vector>

#include "parametric/Condition.h"

namespace cad::param {

class Block;
struct Attachment;

/// Resolves the full dependency chain across all Blocks and Attachments.
/// Usage: call resolveAll() after any parameter change.
class Resolver
{
public:
    /// Resolve all blocks and propagate attachments.
    /// @param blocks      All blocks in the document.
    /// @param attachments All inter-block attachments.
    /// @param params      Variable name→value map (cm) for formula evaluation.
    /// @param conditioned formulaName→conditions for standalone-condition
    ///                    semantics (see ConditionEngine). May be empty.
    static void resolveAll(std::vector<Block>& blocks,
                           const std::vector<Attachment>& attachments,
                           const QHash<QString, double>& params = {},
                           const QHash<QString, QList<Condition>>& conditioned = {});

private:
    /// Process a single attachment: position and rotate the from-block
    /// so that its from-point coincides with the to-block's to-point.
    static void applyAttachment(Block& from, const Attachment& att,
                                const Block& to);
};

} // namespace cad::param
