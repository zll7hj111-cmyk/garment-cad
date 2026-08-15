#pragma once

#include <array>
#include <string>
#include <vector>

#include "geometry/Vec2.h"

namespace cad::geo {

/// A 3D point used by the triangle sloper generator.
/// The caller owns the unit convention; TriangleUnfoldOptions::scaleToMm
/// converts the input distances to the 2D paper-space millimetres.
struct SurfacePoint3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] double distanceTo(const SurfacePoint3D& other) const;
};

/// One triangle in the authored low-poly torso surface.
struct SurfaceTriangle {
    std::array<int, 3> pointIds{};
};

struct TriangleSurface {
    std::vector<SurfacePoint3D> points;
    std::vector<SurfaceTriangle> triangles;
};

/// An edge that is deliberately cut before unfolding.
struct TriangleEdge {
    int a = -1;
    int b = -1;
};

struct TriangleUnfoldOptions {
    /// Output scale. MakeHuman's current coordinate unit is 0.1 m, so a
    /// MakeHuman surface normally uses 100.0 here to produce millimetres.
    double scaleToMm = 1.0;

    /// Optional authored seams. Non-tree adjacency edges are also exposed as
    /// seams automatically because a curved surface cannot close in the plane.
    std::vector<TriangleEdge> cutEdges;
};

struct FlatVertex {
    Vec2 position;
    int sourcePoint = -1;
};

struct FlatTriangle {
    std::array<int, 3> vertexIds{};
    int sourceFace = -1;
    int component = -1;
};

/// One occurrence of a source edge in a flattened triangle.
/// A shared edge has two occurrences; a cut edge gets two independent pairs
/// of flat vertex ids and is therefore rendered as two pattern boundaries.
struct FlatEdge {
    int a = -1;
    int b = -1;
    int sourceA = -1;
    int sourceB = -1;
    int face = -1;
    bool boundary = false;
    bool seam = false;
};

struct TriangleUnfoldResult {
    std::vector<FlatVertex> vertices;
    std::vector<FlatTriangle> triangles;
    std::vector<FlatEdge> edges;
};

/// Flatten an authored triangle surface by preserving every triangle edge.
///
/// A breadth-first spanning forest places each child triangle on the opposite
/// side of its shared edge from its parent. Edges not selected by the forest
/// are seams: the resulting gap is the geometric information that a classic
/// dart-based sloper loses. Explicit cutEdges force a seam even when the
/// topology could otherwise be traversed.
///
/// Returns false for invalid topology, degenerate triangles, or impossible
/// triangle side lengths. On failure, error receives a short diagnostic.
bool unfoldTriangleSurface(const TriangleSurface& surface,
                           const TriangleUnfoldOptions& options,
                           TriangleUnfoldResult& result,
                           std::string* error = nullptr);

} // namespace cad::geo
