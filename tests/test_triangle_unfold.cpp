#include <QtTest>

#include <cmath>

#include "geometry/TriangleUnfold.h"

using cad::geo::FlatEdge;
using cad::geo::SurfacePoint3D;
using cad::geo::SurfaceTriangle;
using cad::geo::TriangleSurface;
using cad::geo::TriangleUnfoldOptions;
using cad::geo::TriangleUnfoldResult;
using cad::geo::unfoldTriangleSurface;

namespace {

double flatDistance(const TriangleUnfoldResult& result, int a, int b)
{
    return result.vertices[static_cast<size_t>(a)].position.distanceTo(
        result.vertices[static_cast<size_t>(b)].position);
}

double surfaceDistance(const TriangleSurface& surface, int a, int b)
{
    return surface.points[static_cast<size_t>(a)].distanceTo(
        surface.points[static_cast<size_t>(b)]);
}

const FlatEdge* findSourceEdge(const TriangleUnfoldResult& result, int a, int b)
{
    for (const auto& edge : result.edges) {
        if ((edge.sourceA == a && edge.sourceB == b)
            || (edge.sourceA == b && edge.sourceB == a))
            return &edge;
    }
    return nullptr;
}

} // namespace

class TestTriangleUnfold : public QObject
{
    Q_OBJECT

private slots:
    void flatSquarePreservesEverySide();
    void curvedSurfaceExposesAutomaticSeam();
    void explicitCutDuplicatesSharedEdge();
};

void TestTriangleUnfold::flatSquarePreservesEverySide()
{
    TriangleSurface surface{
        {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0},
         {2.0, 2.0, 0.0}, {0.0, 2.0, 0.0}},
        {{{0, 1, 2}, {0, 2, 3}}}
    };
    TriangleUnfoldResult result;
    QVERIFY2(unfoldTriangleSurface(surface, {}, result), "flat square must unfold");
    QCOMPARE(static_cast<int>(result.triangles.size()), 2);
    QCOMPARE(static_cast<int>(result.vertices.size()), 4);

    for (const auto& triangle : result.triangles) {
        for (int corner = 0; corner < 3; ++corner) {
            const int a = triangle.vertexIds[static_cast<size_t>(corner)];
            const int b = triangle.vertexIds[static_cast<size_t>((corner + 1) % 3)];
            const int sourceA = result.vertices[static_cast<size_t>(a)].sourcePoint;
            const int sourceB = result.vertices[static_cast<size_t>(b)].sourcePoint;
            QVERIFY(std::abs(flatDistance(result, a, b)
                             - surfaceDistance(surface, sourceA, sourceB)) < 1e-9);
        }
    }

    const FlatEdge* diagonal = findSourceEdge(result, 0, 2);
    QVERIFY(diagonal != nullptr);
    QVERIFY(!diagonal->seam);
}

void TestTriangleUnfold::curvedSurfaceExposesAutomaticSeam()
{
    // A tetrahedron has cycles in its face graph. A spanning tree can unfold
    // three connections, but the closing edge must become a dart/seam.
    TriangleSurface surface{
        {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {1.0, 2.0, 0.0},
         {1.0, 1.0, 1.8}},
        {{{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}}}
    };
    TriangleUnfoldResult result;
    QVERIFY2(unfoldTriangleSurface(surface, {}, result), "tetrahedron must unfold");

    bool hasSeam = false;
    for (const auto& edge : result.edges)
        hasSeam = hasSeam || edge.seam;
    QVERIFY(hasSeam);
    for (const auto& triangle : result.triangles) {
        for (int corner = 0; corner < 3; ++corner) {
            const int a = triangle.vertexIds[static_cast<size_t>(corner)];
            const int b = triangle.vertexIds[static_cast<size_t>((corner + 1) % 3)];
            const int sourceA = result.vertices[static_cast<size_t>(a)].sourcePoint;
            const int sourceB = result.vertices[static_cast<size_t>(b)].sourcePoint;
            QVERIFY(std::abs(flatDistance(result, a, b)
                             - surfaceDistance(surface, sourceA, sourceB)) < 1e-9);
        }
    }
}

void TestTriangleUnfold::explicitCutDuplicatesSharedEdge()
{
    TriangleSurface surface{
        {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0},
         {2.0, 2.0, 0.0}, {0.0, 2.0, 0.0}},
        {{{0, 1, 2}, {0, 2, 3}}}
    };
    TriangleUnfoldOptions options;
    options.cutEdges.push_back({0, 2});
    TriangleUnfoldResult result;
    QVERIFY2(unfoldTriangleSurface(surface, options, result), "explicit cut must unfold");
    QCOMPARE(static_cast<int>(result.vertices.size()), 6);

    int seamCount = 0;
    for (const auto& edge : result.edges)
        if (edge.seam && ((edge.sourceA == 0 && edge.sourceB == 2)
                          || (edge.sourceA == 2 && edge.sourceB == 0)))
            ++seamCount;
    QCOMPARE(seamCount, 2);
}

QTEST_MAIN(TestTriangleUnfold)
#include "test_triangle_unfold.moc"
