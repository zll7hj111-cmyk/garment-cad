#include "TrianglePrototypeBlock.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace cad::param {
namespace {

std::uint64_t edgeKey(int a, int b)
{
    const auto lo = static_cast<std::uint32_t>(std::min(a, b));
    const auto hi = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

} // namespace

Block makeTrianglePrototypeBlock(const geo::TriangleUnfoldResult& result,
                                 const QString& name)
{
    Block block;
    block.name = name.isEmpty() ? QStringLiteral("三角原型") : name;
    block.isClosed = false;

    std::vector<QUuid> pointIds;
    pointIds.reserve(result.vertices.size());
    for (size_t i = 0; i < result.vertices.size(); ++i) {
        ParamPoint point;
        point.name = QStringLiteral("T%1").arg(static_cast<qulonglong>(i + 1));
        point.annotation = QStringLiteral("三角原型展开点");
        point.constraint = PointConstraint::Free;
        point.freePos = result.vertices[i].position;
        pointIds.push_back(point.id);
        block.addPoint(std::move(point));
    }

    std::unordered_map<std::uint64_t, size_t> segmentIndex;
    segmentIndex.reserve(result.edges.size());
    for (const auto& edge : result.edges) {
        if (edge.a < 0 || edge.b < 0
            || edge.a >= static_cast<int>(pointIds.size())
            || edge.b >= static_cast<int>(pointIds.size())
            || edge.a == edge.b)
            continue;

        const auto key = edgeKey(edge.a, edge.b);
        const SegmentRole role = (edge.boundary || edge.seam)
            ? SegmentRole::Outline : SegmentRole::Internal;
        const auto it = segmentIndex.find(key);
        if (it != segmentIndex.end()) {
            auto& existing = block.segments[it->second];
            if (role == SegmentRole::Outline)
                existing.role = SegmentRole::Outline;
            continue;
        }

        Segment segment;
        segment.name = role == SegmentRole::Outline
            ? QStringLiteral("三角原型轮廓/省道边")
            : QStringLiteral("三角网内部边");
        segment.role = role;
        segment.startPointId = pointIds[static_cast<size_t>(edge.a)];
        segment.endPointId = pointIds[static_cast<size_t>(edge.b)];
        segmentIndex.emplace(key, block.segments.size());
        block.addSegment(std::move(segment));
    }
    return block;
}

} // namespace cad::param
