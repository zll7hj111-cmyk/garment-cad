#include "TriangleUnfold.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cad::geo {
namespace {

using EdgeKey = std::uint64_t;

EdgeKey edgeKey(int a, int b)
{
    const auto lo = static_cast<std::uint32_t>(std::min(a, b));
    const auto hi = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<EdgeKey>(lo) << 32) | hi;
}

bool fail(std::string* error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

struct EdgeOccurrence {
    int face = -1;
    int a = -1;
    int b = -1;
};

struct FacePlacement {
    bool placed = false;
    int component = -1;
    std::array<int, 3> sourceIds{};
    std::array<int, 3> flatIds{};
};

int cornerFor(const FacePlacement& placement, int sourcePoint)
{
    for (int i = 0; i < 3; ++i)
        if (placement.sourceIds[static_cast<size_t>(i)] == sourcePoint)
            return i;
    return -1;
}

double cross(const Vec2& a, const Vec2& b, const Vec2& p)
{
    return (b - a).cross(p - a);
}

bool placeThird(const Vec2& a, const Vec2& b,
                double distanceToA, double distanceToB,
                double sideSign, Vec2& result)
{
    constexpr double kEpsilon = 1e-10;
    const Vec2 chord = b - a;
    const double chordLength = chord.length();
    if (chordLength <= kEpsilon || distanceToA <= kEpsilon || distanceToB <= kEpsilon)
        return false;

    const double along = (distanceToA * distanceToA
                          - distanceToB * distanceToB
                          + chordLength * chordLength)
                         / (2.0 * chordLength);
    double heightSquared = distanceToA * distanceToA - along * along;
    const double tolerance = 1e-8 * std::max({1.0,
                                               distanceToA * distanceToA,
                                               distanceToB * distanceToB});
    if (heightSquared < -tolerance)
        return false;
    heightSquared = std::max(0.0, heightSquared);

    const Vec2 axis = chord / chordLength;
    const Vec2 normal = axis.perpendicular();
    result = a + axis * along + normal * (std::sqrt(heightSquared) * sideSign);
    return true;
}

int addFlatVertex(TriangleUnfoldResult& result, const Vec2& position, int sourcePoint)
{
    result.vertices.push_back({position, sourcePoint});
    return static_cast<int>(result.vertices.size() - 1);
}

bool placeRoot(const TriangleSurface& surface, int faceIndex, int component,
               double scaleToMm, TriangleUnfoldResult& result,
               FacePlacement& placement)
{
    const auto& source = surface.triangles[static_cast<size_t>(faceIndex)].pointIds;
    const auto& p0 = surface.points[static_cast<size_t>(source[0])];
    const auto& p1 = surface.points[static_cast<size_t>(source[1])];
    const auto& p2 = surface.points[static_cast<size_t>(source[2])];

    const double d01 = p0.distanceTo(p1) * scaleToMm;
    const double d02 = p0.distanceTo(p2) * scaleToMm;
    const double d12 = p1.distanceTo(p2) * scaleToMm;

    Vec2 third;
    if (!placeThird({0.0, 0.0}, {d01, 0.0}, d02, d12, 1.0, third))
        return false;

    placement.placed = true;
    placement.component = component;
    placement.sourceIds = source;
    placement.flatIds = {
        addFlatVertex(result, {0.0, 0.0}, source[0]),
        addFlatVertex(result, {d01, 0.0}, source[1]),
        addFlatVertex(result, third, source[2])
    };
    return true;
}

bool placeChild(const TriangleSurface& surface, int childFace,
                const FacePlacement& parentPlacement, int edgeA, int edgeB,
                int component, double scaleToMm,
                TriangleUnfoldResult& result, FacePlacement& placement)
{
    const auto& source = surface.triangles[static_cast<size_t>(childFace)].pointIds;
    const int childA = cornerFor(
        FacePlacement{true, 0, source, {0, 0, 0}}, edgeA);
    const int childB = cornerFor(
        FacePlacement{true, 0, source, {0, 0, 0}}, edgeB);
    if (childA < 0 || childB < 0 || childA == childB)
        return false;

    int childThird = -1;
    for (const int sourcePoint : source) {
        if (sourcePoint != edgeA && sourcePoint != edgeB) {
            childThird = sourcePoint;
            break;
        }
    }
    if (childThird < 0)
        return false;

    const int parentA = cornerFor(parentPlacement, edgeA);
    const int parentB = cornerFor(parentPlacement, edgeB);
    if (parentA < 0 || parentB < 0)
        return false;

    const Vec2 a = result.vertices[static_cast<size_t>(parentPlacement.flatIds[parentA])].position;
    const Vec2 b = result.vertices[static_cast<size_t>(parentPlacement.flatIds[parentB])].position;
    const int parentThirdCorner = 3 - parentA - parentB;
    const Vec2 parentThird = result.vertices[
        static_cast<size_t>(parentPlacement.flatIds[parentThirdCorner])].position;
    const double parentSide = cross(a, b, parentThird);
    const double childToA = surface.points[static_cast<size_t>(childThird)].distanceTo(
        surface.points[static_cast<size_t>(edgeA)]) * scaleToMm;
    const double childToB = surface.points[static_cast<size_t>(childThird)].distanceTo(
        surface.points[static_cast<size_t>(edgeB)]) * scaleToMm;
    const double sideSign = std::abs(parentSide) < 1e-10
        ? 1.0 : (parentSide > 0.0 ? -1.0 : 1.0);

    Vec2 third;
    if (!placeThird(a, b, childToA, childToB, sideSign, third))
        return false;

    placement.placed = true;
    placement.component = component;
    placement.sourceIds = source;
    placement.flatIds = {};
    for (int i = 0; i < 3; ++i) {
        if (source[i] == edgeA)
            placement.flatIds[static_cast<size_t>(i)] = parentPlacement.flatIds[parentA];
        else if (source[i] == edgeB)
            placement.flatIds[static_cast<size_t>(i)] = parentPlacement.flatIds[parentB];
        else
            placement.flatIds[static_cast<size_t>(i)] =
                addFlatVertex(result, third, source[i]);
    }
    return true;
}

} // namespace

double SurfacePoint3D::distanceTo(const SurfacePoint3D& other) const
{
    const double dx = x - other.x;
    const double dy = y - other.y;
    const double dz = z - other.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool unfoldTriangleSurface(const TriangleSurface& surface,
                           const TriangleUnfoldOptions& options,
                           TriangleUnfoldResult& result,
                           std::string* error)
{
    result = {};
    if (!std::isfinite(options.scaleToMm) || options.scaleToMm <= 0.0)
        return fail(error, "TriangleUnfold: scaleToMm must be positive");
    if (surface.triangles.empty())
        return fail(error, "TriangleUnfold: surface has no triangles");

    std::unordered_set<EdgeKey> cutEdges;
    for (const auto& edge : options.cutEdges) {
        if (edge.a < 0 || edge.b < 0
            || edge.a >= static_cast<int>(surface.points.size())
            || edge.b >= static_cast<int>(surface.points.size())
            || edge.a == edge.b)
            return fail(error, "TriangleUnfold: invalid cut edge");
        cutEdges.insert(edgeKey(edge.a, edge.b));
    }

    std::unordered_map<EdgeKey, std::vector<EdgeOccurrence>> occurrences;
    occurrences.reserve(surface.triangles.size() * 3);
    for (int face = 0; face < static_cast<int>(surface.triangles.size()); ++face) {
        const auto& ids = surface.triangles[static_cast<size_t>(face)].pointIds;
        for (const int id : ids) {
            if (id < 0 || id >= static_cast<int>(surface.points.size()))
                return fail(error, "TriangleUnfold: triangle references an invalid point");
        }
        if (ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0])
            return fail(error, "TriangleUnfold: triangle has duplicate points");
        for (int corner = 0; corner < 3; ++corner) {
            const int a = ids[corner];
            const int b = ids[(corner + 1) % 3];
            occurrences[edgeKey(a, b)].push_back({face, a, b});
        }
    }
    for (const auto& [key, refs] : occurrences) {
        (void)key;
        if (refs.size() > 2)
            return fail(error, "TriangleUnfold: non-manifold edge has more than two faces");
    }

    std::vector<FacePlacement> placements(surface.triangles.size());
    std::unordered_set<EdgeKey> unfoldedConnections;
    std::deque<int> queue;
    int component = 0;

    for (int root = 0; root < static_cast<int>(surface.triangles.size()); ++root) {
        if (placements[static_cast<size_t>(root)].placed)
            continue;
        if (!placeRoot(surface, root, component, options.scaleToMm, result,
                       placements[static_cast<size_t>(root)]))
            return fail(error, "TriangleUnfold: degenerate root triangle");
        queue.push_back(root);

        while (!queue.empty()) {
            const int face = queue.front();
            queue.pop_front();
            const auto& ids = surface.triangles[static_cast<size_t>(face)].pointIds;
            for (int corner = 0; corner < 3; ++corner) {
                const int a = ids[corner];
                const int b = ids[(corner + 1) % 3];
                const EdgeKey key = edgeKey(a, b);
                if (cutEdges.contains(key))
                    continue;
                const auto& refs = occurrences.at(key);
                if (refs.size() != 2)
                    continue;
                const EdgeOccurrence* other = nullptr;
                for (const auto& ref : refs)
                    if (ref.face != face) { other = &ref; break; }
                if (!other || placements[static_cast<size_t>(other->face)].placed)
                    continue;
                if (!placeChild(surface, other->face,
                                placements[static_cast<size_t>(face)],
                                a, b, component, options.scaleToMm, result,
                                placements[static_cast<size_t>(other->face)]))
                    return fail(error, "TriangleUnfold: child triangle cannot be placed");
                unfoldedConnections.insert(key);
                queue.push_back(other->face);
            }
        }
        ++component;
    }

    result.triangles.reserve(surface.triangles.size());
    for (int face = 0; face < static_cast<int>(placements.size()); ++face) {
        const auto& placement = placements[static_cast<size_t>(face)];
        result.triangles.push_back({placement.flatIds, face, placement.component});
    }

    for (const auto& [key, refs] : occurrences) {
        (void)key;
        const bool boundary = refs.size() == 1;
        const bool seam = boundary || !unfoldedConnections.contains(key);
        for (const auto& ref : refs) {
            const auto& placement = placements[static_cast<size_t>(ref.face)];
            const int aCorner = cornerFor(placement, ref.a);
            const int bCorner = cornerFor(placement, ref.b);
            if (aCorner < 0 || bCorner < 0)
                return fail(error, "TriangleUnfold: internal placement mapping failed");
            result.edges.push_back({
                placement.flatIds[aCorner], placement.flatIds[bCorner],
                ref.a, ref.b, ref.face, boundary, seam
            });
        }
    }
    return true;
}

} // namespace cad::geo
