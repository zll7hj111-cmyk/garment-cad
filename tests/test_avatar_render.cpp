#include <QtTest/QtTest>

#include "avatar/AvatarRenderMesh.h"
#include "avatar/Mesh3D.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cad::avatar;

namespace {

std::string assetsDir() {
    const char* env = std::getenv("GCAD_ASSETS_DIR");
    if (env && *env) return env;
    return AVATAR_ASSETS_DIR;
}

} // namespace

class TestAvatarRender : public QObject {
    Q_OBJECT

private slots:
    void buildCounts();
    void buildDataSpotCheck();
    void uvParsedFromBaseObj();
    void noUvFallsBackToSharedVertices();
    void boundsKnownFromBaseObj();
    void emptyMeshSafe();
};

void TestAvatarRender::buildCounts()
{
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    AvatarRenderMesh rm;
    rm.build(mesh);
    // base.obj 带 UV：渲染层按三角形角展开（每个面角一个顶点，UV 接缝
    // 处同一几何顶点可有多个纹理坐标）。28044 三角面 → 84132 渲染顶点。
    QCOMPARE(rm.vertexCount(), 28044 * 3);
    QCOMPARE(rm.indexCount(), 28044 * 3);
    QCOMPARE(static_cast<int>(rm.vertices().size()), 28044 * 3);
    QCOMPARE(static_cast<int>(rm.indices().size()), 28044 * 3);
}

void TestAvatarRender::buildDataSpotCheck()
{
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    AvatarRenderMesh rm;
    rm.build(mesh);

    // 与渲染层同源的白名单：断言索引数与"保留面"一一对应
    const auto keep = [](const std::string& g) {
        if (g.empty()) return true;
        if (g == "body" || g == "helper-tongue" || g == "helper-upper-teeth"
            || g == "helper-lower-teeth")
            return true;
        if (g.rfind("helper-l-eye", 0) == 0 || g.rfind("helper-r-eye", 0) == 0)
            return true;
        if (g.rfind("helper-l-eyelashes", 0) == 0
            || g.rfind("helper-r-eyelashes", 0) == 0)
            return true;
        return false;
    };
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < mesh.faces.size(); ++i)
        if (keep(mesh.faceGroups[i])) kept.push_back(i);
    QCOMPARE(static_cast<int>(rm.indices().size()), static_cast<int>(kept.size() * 3));

    // 展开模式下索引连续（0..N-1），位置/法线按顶点索引取，UV 按面角 vt 索引取
    QCOMPARE(static_cast<int>(rm.indices()[0]), 0);
    QCOMPARE(static_cast<int>(rm.indices().back()), rm.vertexCount() - 1);
    for (std::size_t i = 0; i < kept.size(); ++i) {
        const auto& f = mesh.faces[kept[i]];
        for (int j = 0; j < 3; ++j) {
            const RenderVertex& rv = rm.vertices()[3u * i + static_cast<std::size_t>(j)];
            const Vec3& p = mesh.verts[static_cast<std::size_t>(f[j])];
            QVERIFY2(std::fabs(rv.px - p.x) < 1e-4 && std::fabs(rv.py - p.y) < 1e-4
                         && std::fabs(rv.pz - p.z) < 1e-4,
                     "expanded vertex position must match source vertex");
            const int vtIdx = mesh.faceUvs[kept[i]][j];
            if (vtIdx >= 0) {
                QVERIFY2(std::fabs(rv.u - mesh.uvs[static_cast<std::size_t>(vtIdx)].u) < 1e-4
                             && std::fabs(rv.v - mesh.uvs[static_cast<std::size_t>(vtIdx)].v) < 1e-4,
                         "expanded vertex uv must match source vt");
            }
        }
    }
    QVERIFY(rm.vertices()[0].nx != 0.f || rm.vertices()[0].ny != 0.f
            || rm.vertices()[0].nz != 0.f);
}

void TestAvatarRender::uvParsedFromBaseObj()
{
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    QVERIFY2(mesh.hasUvs(), "base.obj must carry UV data after the loader change");
    QCOMPARE(static_cast<int>(mesh.uvs.size()), 21334);
    QCOMPARE(static_cast<int>(mesh.faceUvs.size()),
             static_cast<int>(mesh.faces.size()));
    // 每个面角的 vt 索引都必须落在有效范围
    for (const auto& fuv : mesh.faceUvs)
        for (int j = 0; j < 3; ++j)
            QVERIFY2(fuv[j] >= 0 && fuv[j] < static_cast<int>(mesh.uvs.size()),
                     "face corner vt index out of range");
}

void TestAvatarRender::noUvFallsBackToSharedVertices()
{
    // 手工构造无 UV 网格：渲染层必须退回共享顶点模式
    Mesh3D mesh;
    mesh.verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.faces = {{0, 1, 2}};
    AvatarRenderMesh rm;
    rm.build(mesh);
    QCOMPARE(rm.vertexCount(), 3);
    QCOMPARE(rm.indexCount(), 3);
    QCOMPARE(static_cast<int>(rm.indices()[0]), 0);
    QCOMPARE(static_cast<int>(rm.indices()[2]), 2);
    QVERIFY(!mesh.hasUvs());
}

void TestAvatarRender::boundsKnownFromBaseObj()
{
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    AvatarRenderMesh rm;
    rm.build(mesh);

    // base.obj 已知包围盒：y ∈ [-8.4488, 8.4967]（0.1m 单位），身高 169.455 cm
    const Vec3 c = rm.center();
    QVERIFY2(std::fabs(c.y - 0.024) < 0.02, "center y must sit near hip/waist height");
    QVERIFY2(rm.radius() > 8.0 && rm.radius() < 9.0,
             "bounding sphere radius must cover the ~17-unit body");
    // 地面基准：minY 必须落在脚底高度（地面渲染贴脚摆放的依据）
    QVERIFY2(std::fabs(rm.minY() - (-8.4488)) < 0.02, "minY must sit at feet level");
}

void TestAvatarRender::emptyMeshSafe()
{
    Mesh3D mesh;
    AvatarRenderMesh rm;
    rm.build(mesh);
    QVERIFY(rm.empty());
    QCOMPARE(rm.vertexCount(), 0);
    QCOMPARE(rm.indexCount(), 0);
    QCOMPARE(rm.radius(), 0.0);
}

QTEST_MAIN(TestAvatarRender)
#include "test_avatar_render.moc"
