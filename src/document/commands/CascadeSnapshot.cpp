#include "document/commands/CascadeSnapshot.h"

#include <algorithm>

#include "parametric/ParamDocument.h"

namespace cad::cmd {

void collectCascadeDependents(const cad::param::ParamDocument& doc,
                              const QSet<QUuid>& cascade,
                              std::vector<cad::param::LinkedVariable>& outLinked,
                              std::vector<cad::param::Block>& outBakedConsumers,
                              std::vector<cad::param::MeasureVariable>& outMeasures)
{
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc.linkedVars())
            if (lv.sourceBlockId == srcId)
                outLinked.push_back(lv);
        for (const QUuid& cid : doc.linkedConsumerBlocks(srcId)) {
            if (cascade.contains(cid)) continue;   // removed & restored anyway
            const bool taken = std::any_of(
                outBakedConsumers.begin(), outBakedConsumers.end(),
                [&cid](const cad::param::Block& b) { return b.id == cid; });
            if (taken) continue;
            if (const auto* cb = doc.findBlock(cid))
                outBakedConsumers.push_back(*cb);
        }
        for (const auto& mv : doc.measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                outMeasures.push_back(mv);
    }
}

} // namespace cad::cmd
