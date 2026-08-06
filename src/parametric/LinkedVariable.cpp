#include "LinkedVariable.h"

#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"

namespace cad::param {

LinkedVariable LinkedVariable::fromSegment(const Block& blk, const Segment& seg)
{
    LinkedVariable lv;
    lv.sourceBlockId   = blk.id;
    lv.sourceSegmentId = seg.id;
    lv.refName = QStringLiteral("L") + seg.serial;
    lv.name = seg.name.isEmpty()
        ? seg.serial + QStringLiteral("长")
        : seg.name + QStringLiteral("长");

    const auto* sp = blk.findPoint(seg.startPointId);
    const auto* ep = blk.findPoint(seg.endPointId);
    if (sp && ep && sp->resolved && ep->resolved)
        lv.value = sp->resolvedPos.distanceTo(ep->resolvedPos);

    return lv;
}

} // namespace cad::param
