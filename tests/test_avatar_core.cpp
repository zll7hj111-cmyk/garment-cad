#include <QtTest/QtTest>

#include "avatar/AvatarModel.h"
#include "avatar/JsonReader.h"
#include "avatar/MeasureSystem.h"
#include "avatar/Mesh3D.h"

#include <cstdlib>
#include <map>
#include <string>
#include <unordered_set>

using namespace cad::avatar;

namespace {

/// 资产根目录：优先环境变量 GCAD_ASSETS_DIR，否则编译期默认。
std::string assetsDir() {
    const char* env = std::getenv("GCAD_ASSETS_DIR");
    if (env && *env) return env;
    return AVATAR_ASSETS_DIR;
}

std::string goldenPath(const std::string& name) {
    return assetsDir() + "/tests/golden/" + name + ".json";
}

double maxCoordError(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    double worst = 0.0;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const double d = a[i].distanceTo(b[i]);
        if (d > worst) worst = d;
    }
    return worst;
}

/// 读取 golden 的 seed_coords（嵌套 [[x,y,z],...]）为 Vec3 列表。
std::vector<Vec3> readSeedCoords(const JsonValue& doc) {
    const JsonValue* coords = doc.find("seed_coords");
    if (!coords) throw std::runtime_error("golden: no seed_coords");
    const auto& arr = coords->asArray();
    std::vector<Vec3> out;
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto& p = v.asArray();
        if (p.size() < 3) throw std::runtime_error("golden: bad coord entry");
        out.emplace_back(p[0].asNumber(), p[1].asNumber(), p[2].asNumber());
    }
    return out;
}

/// 读取 golden 的 spec.targets（key -> weight）。
std::vector<TargetWeight> readSpecTargets(const JsonValue& doc) {
    const JsonValue* spec = doc.find("spec");
    if (!spec) return {};
    const JsonValue* targets = spec->find("targets");
    if (!targets) return {};
    std::vector<TargetWeight> out;
    for (const auto& [k, v] : targets->asObject())
        out.push_back({k, v.asNumber()});
    return out;
}

} // namespace

class TestAvatarCore : public QObject {
    Q_OBJECT

private slots:
    void baseObjLoads();
    void goldenDefaultEqualsBaseObj();
    void goldenSpecsMatchVertexForVertex();
    void goldenMeasuresMatch();
    void measureSystemLoads();
    void filteredMeshKeepsBodyIntact();
    void remapChainsRehitsExcludedToBody();
    void geodesicDistanceMatchesTape();
};

void TestAvatarCore::baseObjLoads() {
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    QCOMPARE(mesh.vertexCount(), 19158);
    QCOMPARE(mesh.faceCount(), 36972); // base.obj 为四边形网格（18486 面），加载时扇形三角化
    QVERIFY(mesh.normals.empty()); // 加载后法线未计算
}

void TestAvatarCore::goldenDefaultEqualsBaseObj() {
    // default 样本无任何目标：seed coords 必须等于 base.obj 原始坐标，
    // 即 OBJ 解析 + reset 语义正确。
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    const JsonValue doc = parseJsonFile(goldenPath("default"));
    const std::vector<Vec3> golden = readSeedCoords(doc);
    const double err = maxCoordError(golden, mesh.verts);
    // MH 内部以 float32 存坐标，golden 输出为 float32 量化值（~5e-7 m 噪声）。
    // double 精度引擎与 float32 基准的差异在 1e-6 m 内即视为一致。
    QCOMPARE(golden.size(), static_cast<size_t>(mesh.vertexCount()));
    QVERIFY2(err < 1e-6,
             (std::string("default golden must equal base.obj coordinates, err=") +
              std::to_string(err)).c_str());
}

void TestAvatarCore::goldenSpecsMatchVertexForVertex() {
    const Mesh3D base = loadObjFile(assetsDir() + "/base.obj");
    AvatarModel model(base, assetsDir() + "/targets");
    const char* names[] = {"waist_up", "bust_up", "hips_up", "thinner",
                           "taller", "hourglass", "extreme"};
    for (const char* name : names) {
        const JsonValue doc = parseJsonFile(goldenPath(name));
        const std::vector<TargetWeight> spec = readSpecTargets(doc);
        QVERIFY2(!spec.empty(), "spec must not be empty");
        model.applyTargets(spec);
        const std::vector<Vec3> golden = readSeedCoords(doc);
        const double err = maxCoordError(golden, model.mesh().verts);
        QVERIFY2(err < 1e-6,
                 (std::string(name) + ": vertex error too large: " +
                  std::to_string(err)).c_str());
    }
}

void TestAvatarCore::goldenMeasuresMatch() {
    // 用 golden 的种子坐标反推测量值，与基准一致。
    // 围度 = 拟合截面平面（Newell 法向量）凸包周长；距离 = 首末欧氏距离。
    // 基准值在 assets/avatar/tests/golden/*.json 的 measures_cm 字段。
    const char* names[] = {"default", "waist_up", "bust_up", "hips_up",
                           "thinner", "taller", "hourglass", "extreme"};
    for (const char* name : names) {
        const JsonValue doc = parseJsonFile(goldenPath(name));
        const JsonValue* m = doc.find("measures_cm");
        QVERIFY(m);
        const std::vector<Vec3> coords = readSeedCoords(doc);
        MeasureSystem ms;
        ms.loadChains(assetsDir() + "/measurement_chains.json");
        const std::map<std::string, double> computed = ms.computeAll(coords);
        // 容差 5e-3 cm（0.05 mm）：基准值写回为 6 位有效数字，舍入误差上限。
        for (const auto& [k, v] : m->asObject()) {
            const double got = computed.count(k) ? computed.at(k) : 0.0;
            QVERIFY2(std::abs(got - v.asNumber()) < 5e-3,
                     (std::string(name) + ": measure mismatch on " + k).c_str());
        }
    }
}

void TestAvatarCore::measureSystemLoads() {
    MeasureSystem ms;
    ms.loadChains(assetsDir() + "/measurement_chains.json");
    const auto keys = ms.measureKeys();
    QCOMPARE(static_cast<int>(keys.size()), 23);
    QVERIFY(ms.hasChain("measure/measure-waist-circ-decr|incr"));
    QVERIFY(ms.hasChain("measure/measure-bust-circ-decr|incr"));
    QVERIFY(ms.hasChain("custom/neck-to-bust"));
    QVERIFY(ms.hasChain("custom/bust-to-bust"));
    QVERIFY(ms.hasChain("custom/shoulder-width"));
    QVERIFY(!ms.hasChain("no-such-measure"));
    QCOMPARE(ms.computeOne("no-such-measure", {}), 0.0);
}

void TestAvatarCore::filteredMeshKeepsBodyIntact() {
    const std::unordered_set<std::string> exclude = {
        "helper-tights", "helper-skirt", "helper-hair", "helper-genital"};
    const Mesh3D full = loadObjFile(assetsDir() + "/base.obj");
    const Mesh3D filtered = loadObjFile(assetsDir() + "/base.obj", exclude);

    QCOMPARE(filtered.vertexCount(), 15136);
    QVERIFY(filtered.vertexRemap.size() == static_cast<size_t>(full.vertexCount()));

    for (int i = 0; i < 13380; ++i) {
        QVERIFY(filtered.vertexRemap[static_cast<size_t>(i)] == i);
        QVERIFY(filtered.verts[static_cast<size_t>(i)].distanceTo(full.verts[static_cast<size_t>(i)]) < 1e-12);
    }

    QVERIFY(filtered.vertexRemap[15128] == -1);
    QVERIFY(filtered.vertexRemap[15328] == -1);
    QVERIFY(filtered.vertexRemap[18002] == -1);
    QVERIFY(filtered.vertexRemap[18722] == -1);
    QVERIFY(filtered.vertexRemap[19149] == -1);

    QVERIFY(filtered.vertexRemap[13622] >= 0);
    QVERIFY(filtered.vertexRemap[19150] >= 0);
}

void TestAvatarCore::remapChainsRehitsExcludedToBody() {
    // 肩宽链 [1602, 18891, 8274] 的中间点 18891 落在被排除的 helper-hair（C7 位置）。
    // remapChains 应把它重新命中到最近的保留（body）顶点，而不是剔除让链退化。
    const std::unordered_set<std::string> exclude = {
        "helper-tights", "helper-skirt", "helper-hair", "helper-genital"};
    const Mesh3D full = loadObjFile(assetsDir() + "/base.obj");
    const Mesh3D filtered = loadObjFile(assetsDir() + "/base.obj", exclude);

    MeasureSystem ms;
    ms.loadChains(assetsDir() + "/measurement_chains.json");
    ms.remapChains(filtered.vertexRemap, full.verts);

    const auto& chain = ms.chains().at("custom/shoulder-width");
    QCOMPARE(static_cast<int>(chain.size()), 3); // 仍三点：左肩 -> C7 -> 右肩

    // 首末端点映射后应仍指向身体左右肩（原 1602 / 8274 的两侧）。
    QCOMPARE(chain[0], filtered.vertexRemap[1602]);
    QCOMPARE(chain[2], filtered.vertexRemap[8274]);

    // 中间点应命中身体，而非被剔除（原 18891 是 hair，remap=-1，应被最近邻替换）。
    QVERIFY(chain[1] >= 0 && chain[1] < filtered.vertexCount());
    const Vec3 mid = filtered.verts[static_cast<size_t>(chain[1])];
    // C7 特征：正中（|x|小）、颈后（z<0）、y 约 5.5（肩高附近）。
    QVERIFY(std::fabs(mid.x) < 0.5);
    QVERIFY(mid.z < 0.0);
    QVERIFY(mid.y > 5.0 && mid.y < 5.8);
}

void TestAvatarCore::geodesicDistanceMatchesTape() {
    // 测地线（贴体表最短路）应 > 直线：绕开身体凸起（如颈到胸绕开胸部）。
    // 用完整 base.obj（body 顶点 0~13379 连通），骨骼/五官为孤立块不影响 body 测地。
    const Mesh3D mesh = loadObjFile(assetsDir() + "/base.obj");
    struct Case { const char* name; int src; int dst; double straightCm; };
    // 直线值取自实测（0.1m 单位 x10 = cm）
    const Case cases[] = {
        {"napetowaist", 1491, 4181, 36.60},
        {"waisttohip",  4121, 4341, 19.89},
        {"necktoBust",  7478, 8456, 24.33},
        {"upperleg",    10970, 11230, 37.99},
        {"lowerleg",    11225, 12820, 45.96},
        {"upperarm",    8274, 10037, 23.55},
    };
    for (const auto& c : cases) {
        const double geo = geodesicDistance(mesh, c.src, c.dst);
        QVERIFY2(geo > 0.0, c.name);
        const double geoCm = geo * 10.0;
        // 测地线必须 >= 直线（贴体表绕凸起，只可能更长或相等）
        QVERIFY2(geoCm >= c.straightCm - 1e-6,
                 (std::string(c.name) + ": geodesic shorter than straight").c_str());
        // 且不能比直线长出太多（合理上限，避免图建错导致绕远路）。这里给宽松 1.5x。
        QVERIFY2(geoCm < c.straightCm * 1.5,
                 (std::string(c.name) + ": geodesic implausibly long").c_str());
        // 路径应至少返回 2 点
        const std::vector<int> path = geodesicPath(mesh, c.src, c.dst);
        QVERIFY2(path.size() >= 2, c.name);
        QCOMPARE(path.front(), c.src);
        QCOMPARE(path.back(), c.dst);
    }
}


QTEST_MAIN(TestAvatarCore)
#include "test_avatar_core.moc"
