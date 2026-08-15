#include "MeasureSystem.h"

#include "JsonReader.h"
#include "Mesh3D.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cad::avatar {

namespace {

/// 收集链中合法顶点坐标（越界跳过），返回按链序的坐标集。
std::vector<Vec3> collectChainCoords(const std::vector<int>& chain,
                                     const std::vector<Vec3>& coords) {
    std::vector<Vec3> pts;
    pts.reserve(chain.size());
    for (const int idx : chain) {
        if (idx < 0 || idx >= static_cast<int>(coords.size())) continue;
        pts.push_back(coords[idx]);
    }
    return pts;
}

/// 2D 点（凸包计算用）。
struct P2 {
    double x = 0.0;
    double y = 0.0;
};

/// 2D 凸包（Andrew's monotone chain），返回绕序凸包顶点（不含重复末点）。
/// 共线边上的中间点被剔除（软尺拉紧语义：凹处绷直）。
std::vector<P2> convexHull2D(std::vector<P2> pts) {
    std::sort(pts.begin(), pts.end(), [](const P2& a, const P2& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const P2& a, const P2& b) {
                  return a.x == b.x && a.y == b.y;
              }),
              pts.end());
    if (pts.size() <= 2) return pts;

    const auto cross = [](const P2& o, const P2& a, const P2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    std::vector<P2> hull(pts.size() * 2);
    int k = 0;
    for (const auto& p : pts) { // 下凸包
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0.0) --k;
        hull[k++] = p;
    }
    const int lower = k + 1;
    for (int i = static_cast<int>(pts.size()) - 2; i >= 0; --i) { // 上凸包
        const P2& p = pts[i];
        while (k >= lower && cross(hull[k - 2], hull[k - 1], p) <= 0.0) --k;
        hull[k++] = p;
    }
    hull.resize(static_cast<size_t>(k) - 1);
    return hull;
}

/// 围度环的截面几何：拟合环所在平面（Newell 法向量 = 环截面法向，即肢体/躯干
/// 的轴向），把环顶点正交投影到该平面后求 2D 凸包（软尺拉紧），再映回 3D。
///
/// 折线路径测量：custom/* 自定义项（如肩宽经 C7 的曲线测量）按折线累加；
/// 其余 measure/* 距离类取首末两点直线距离。
bool isPathMeasure(const std::string& key) {
    return key.rfind("custom/", 0) == 0;
}

/// 截面链 → 当前网格上的截面点 3D 坐标环。动态截面：以锚点当前 y 为截面
/// 高度，对每条记录边重新求交（morph 后环仍严格水平，不依赖 base 网格高度）。
std::vector<Vec3> sectionPoints3D(const SectionChain& sc, const std::vector<Vec3>& verts) {
    const int n = static_cast<int>(verts.size());
    // 截面高度：固定 t 插值点的平均 y。跟随 morph（平均随体型平滑变化），
    // 但不像单锚点那样被某个 morph 单独拉动（避免求解时截面高度剧烈漂移）。
    double Y = 0.0;
    int cnt = 0;
    for (const auto& sp : sc.points) {
        if (sp.a < 0 || sp.a >= n || sp.b < 0 || sp.b >= n)
            continue;
        const Vec3& va = verts[static_cast<size_t>(sp.a)];
        const Vec3& vb = verts[static_cast<size_t>(sp.b)];
        Y += va.y * (1.0 - sp.t) + vb.y * sp.t;
        ++cnt;
    }
    if (cnt == 0)
        return {};
    Y /= static_cast<double>(cnt);
    if (sc.anchor >= 0 && sc.anchor < n)
        Y = Y * 0.5 + verts[static_cast<size_t>(sc.anchor)].y * 0.5; // 锚点与均值各半
    std::vector<Vec3> pts;
    pts.reserve(sc.points.size());
    for (const auto& sp : sc.points) {
        if (sp.a < 0 || sp.a >= n || sp.b < 0 || sp.b >= n)
            continue; // 顶点越界（网格缩减后）剔除该点
        const Vec3& va = verts[static_cast<size_t>(sp.a)];
        const Vec3& vb = verts[static_cast<size_t>(sp.b)];
        const double dy = vb.y - va.y;
        if (std::fabs(dy) < 1e-12)
            continue; // 边近水平，与截面平行
        const double t = (Y - va.y) / dy;
        if (t < 0.0 || t > 1.0)
            continue; // morph 极端扭曲时边不再穿过当前截面
        pts.push_back(va * (1.0 - t) + vb * t);
    }
    return pts;
}

/// 直线链端点 3D 坐标：顶点端点直接取，截面端点按当前顶点插值。
Vec3 straightEndpoint(int vertex, const SectionPoint& sp, const std::vector<Vec3>& verts) {
    const int n = static_cast<int>(verts.size());
    if (vertex >= 0 && vertex < n)
        return verts[static_cast<size_t>(vertex)];
    if (sp.a >= 0 && sp.a < n && sp.b >= 0 && sp.b < n)
        return verts[static_cast<size_t>(sp.a)] * (1.0 - sp.t) +
               verts[static_cast<size_t>(sp.b)] * sp.t;
    return Vec3::zero();
}

/// 截面链周长（cm）：截面点环 → Newell 截面凸包周长（软尺水平拉紧）。
double sectionCircumference(const SectionChain& sc, const std::vector<Vec3>& verts) {
    const std::vector<Vec3> pts = sectionPoints3D(sc, verts);
    if (pts.size() < 3)
        return 0.0;
    const std::vector<Vec3> hull = circularSectionGeometry(pts);
    if (hull.size() < 3)
        return 0.0;
    double len = 0.0;
    for (size_t i = 0; i < hull.size(); ++i)
        len += hull[i].distanceTo(hull[(i + 1) % hull.size()]);
    return len * 10.0;
}

} // namespace

std::vector<Vec3> circularSectionGeometry(const std::vector<Vec3>& pts) {
    // Newell 法向量（闭合环环绕一圈的面积加权法向，与绕序无关的平面朝向）。
    Vec3 n{0.0, 0.0, 0.0};
    for (size_t i = 0; i < pts.size(); ++i)
        n = n + pts[i].cross(pts[(i + 1) % pts.size()]);
    n = n.normalized();
    if (n.length() < 1e-12) // 退化平面（点共线/重合）回退水平面
        n = Vec3{0.0, 1.0, 0.0};

    // 由法向量构造正交基 (u, v)，张成垂直于 n 的截面平面。
    Vec3 u = (std::fabs(n.y) < 0.9) ? n.cross(Vec3{0.0, 1.0, 0.0}).normalized()
                                    : n.cross(Vec3{1.0, 0.0, 0.0}).normalized();
    Vec3 v = n.cross(u); // 右手系，自动单位正交

    // 质心（截面参考点）。
    Vec3 c{0.0, 0.0, 0.0};
    for (const auto& p : pts) c += p;
    c = c / static_cast<double>(pts.size());

    // 正交投影到 (u, v) 平面。
    std::vector<P2> ps;
    ps.reserve(pts.size());
    for (const auto& p : pts) {
        const Vec3 d = p - c;
        ps.push_back({d.dot(u), d.dot(v)});
    }
    const std::vector<P2> hull = convexHull2D(std::move(ps));

    // 凸包顶点映回 3D 截面环。
    std::vector<Vec3> out;
    out.reserve(hull.size());
    for (const auto& h : hull)
        out.push_back(c + u * h.x + v * h.y);
    return out;
}

bool isCircularMeasure(const std::string& key) {
    return key.find("-circ-") != std::string::npos;
}

std::vector<Vec3> measureGeometry(const std::string& key,
                                  const std::vector<int>& chain,
                                  const std::vector<Vec3>& coords) {
    const std::vector<Vec3> pts = collectChainCoords(chain, coords);
    if (pts.size() < 2) return {};

    // 非围度：路径测量（custom/* 折线，如肩宽经 C7）返回全部链点；其余取首末两点直线
    if (!isCircularMeasure(key))
        return isPathMeasure(key) ? pts : std::vector<Vec3>{pts.front(), pts.back()};

    // 围度：拟合环所在截面平面（Newell 法向量），在其上求凸包（软尺拉紧）。
    // 管状部位（颈/臂/腕/腿）截面垂直于肢体轴而非水平面，避免水平投影的斜截面偏大。
    return circularSectionGeometry(pts);
}

double measureValue(const std::string& key,
                    const std::vector<int>& chain,
                    const std::vector<Vec3>& coords) {
    const std::vector<Vec3> geom = measureGeometry(key, chain, coords);
    if (geom.size() < 2) return 0.0;

    double len = 0.0;
    if (isCircularMeasure(key)) {
        for (size_t i = 0; i < geom.size(); ++i)
            len += geom[i].distanceTo(geom[(i + 1) % geom.size()]);
    } else {
        // 路径类：逐段累加（2 点退化为直线距离，多点=折线长度）
        for (size_t i = 0; i + 1 < geom.size(); ++i)
            len += geom[i].distanceTo(geom[i + 1]);
    }
    // MH 坐标单位 = 0.1 米，×10 得 cm。
    return len * 10.0;
}

void MeasureSystem::remapChains(const std::vector<int>& vertexRemap,
                                const std::vector<Vec3>& fullCoords) {
    if (vertexRemap.empty()) return;
    const size_t fullN = fullCoords.size();
    for (auto& [key, chain] : m_chains) {
        std::vector<int> mapped;
        mapped.reserve(chain.size());
        for (const int oldIdx : chain) {
            if (oldIdx < 0 || oldIdx >= static_cast<int>(vertexRemap.size()))
                continue; // 越界索引剔除
            const int newIdx = vertexRemap[static_cast<size_t>(oldIdx)];
            if (newIdx >= 0) {
                mapped.push_back(newIdx);
                continue;
            }
            // 被排除顶点（如肩宽链中间点落在 helper-hair 的 C7 上）：
            // 用完整坐标找最近的保留顶点，重新命中到身体。
            if (oldIdx >= static_cast<int>(fullN)) continue;
            const Vec3& target = fullCoords[static_cast<size_t>(oldIdx)];
            int bestNew = -1;
            double bestDist = 1e30;
            for (size_t j = 0; j < vertexRemap.size(); ++j) {
                if (vertexRemap[j] < 0) continue; // 仅限保留顶点
                if (j >= fullN) continue;
                const double d = fullCoords[j].distanceTo(target);
                if (d < bestDist) { bestDist = d; bestNew = vertexRemap[j]; }
            }
            if (bestNew >= 0)
                mapped.push_back(bestNew);
        }
        chain = std::move(mapped);
    }
    // 截面链：a/b 顶点 remap（被排除顶点 → 剔除该截面点；保留顶点 → 映射）。
    for (auto& [key, sc] : m_sectionChains) {
        std::vector<SectionPoint> mapped;
        mapped.reserve(sc.points.size());
        for (const auto& sp : sc.points) {
            if (sp.a < 0 || sp.a >= static_cast<int>(vertexRemap.size()) ||
                sp.b < 0 || sp.b >= static_cast<int>(vertexRemap.size()))
                continue;
            const int na = vertexRemap[static_cast<size_t>(sp.a)];
            const int nb = vertexRemap[static_cast<size_t>(sp.b)];
            if (na < 0 || nb < 0)
                continue; // 端点被排除（如 helper 网格）→ 剔除该点
            SectionPoint np = sp;
            np.a = na;
            np.b = nb;
            mapped.push_back(np);
        }
        sc.points = std::move(mapped);
    }
    // 直线链：顶点/截面点端点 remap（被排除 → 保留原样，运行时越界则跳过）。
    const auto remapPt = [&](int& vertex, SectionPoint& sp) {
        if (vertex >= 0 && vertex < static_cast<int>(vertexRemap.size()))
            vertex = vertexRemap[static_cast<size_t>(vertex)];
        if (sp.a >= 0 && sp.a < static_cast<int>(vertexRemap.size()))
            sp.a = vertexRemap[static_cast<size_t>(sp.a)];
        if (sp.b >= 0 && sp.b < static_cast<int>(vertexRemap.size()))
            sp.b = vertexRemap[static_cast<size_t>(sp.b)];
    };
    for (auto& [key, sc] : m_straightChains) {
        remapPt(sc.fromVertex, sc.fromSection);
        remapPt(sc.toVertex, sc.toSection);
    }
}

void MeasureSystem::loadChains(const std::string& jsonPath) {
    const JsonValue root = parseJsonFile(jsonPath);
    if (root.type() != JsonValue::Type::Object)
        throw std::runtime_error("MeasureSystem: chains file root is not an object");
    // 文件结构 {source, note, chains: {group: {key: [idx...]}}}
    const JsonValue* chainsVal = root.find("chains");
    const JsonValue* groups = chainsVal ? chainsVal : &root;
    if (groups->type() != JsonValue::Type::Object)
        throw std::runtime_error("MeasureSystem: no 'chains' object");
    m_chains.clear();
    m_pointChains.clear();
    m_sectionChains.clear();
    m_straightChains.clear();
    // 解析截面点 {a,b,t}
    const auto readSecPt = [](const JsonValue& e) -> SectionPoint {
        SectionPoint sp;
        const JsonValue* a = e.find("a");
        const JsonValue* b = e.find("b");
        const JsonValue* t = e.find("t");
        if (a) sp.a = static_cast<int>(a->asInt64());
        if (b) sp.b = static_cast<int>(b->asInt64());
        if (t) sp.t = t->asNumber();
        return sp;
    };
    for (const auto& [group, groupVal] : groups->asObject()) {
        if (groupVal.type() != JsonValue::Type::Object) continue;
        for (const auto& [key, chainVal] : groupVal.asObject()) {
            if (chainVal.type() == JsonValue::Type::Object) {
                // 直线距离链 {type:"straight", from_vertex/from_section, to_vertex/to_section}
                const JsonValue* typeVal = chainVal.find("type");
                if (typeVal && typeVal->asString() == "straight") {
                    StraightChain sc;
                    const JsonValue* fv = chainVal.find("from_vertex");
                    const JsonValue* fs = chainVal.find("from_section");
                    const JsonValue* tv = chainVal.find("to_vertex");
                    const JsonValue* ts = chainVal.find("to_section");
                    if (fv) sc.fromVertex = static_cast<int>(fv->asInt64());
                    if (fs) sc.fromSection = readSecPt(*fs);
                    if (tv) sc.toVertex = static_cast<int>(tv->asInt64());
                    if (ts) sc.toSection = readSecPt(*ts);
                    m_straightChains[key] = std::move(sc);
                    continue;
                }
                // 水平截面链 {section_y, points:[{a,b,t}...]} => 边插值，随 morph 跟随
                const JsonValue* ptsVal = chainVal.find("points");
                if (!ptsVal || ptsVal->type() != JsonValue::Type::Array)
                    continue;
                SectionChain sc;
                const JsonValue* yv = chainVal.find("section_y");
                sc.y = yv ? yv->asNumber() : 0.0;
                const JsonValue* av = chainVal.find("anchor");
                sc.anchor = av ? static_cast<int>(av->asInt64()) : -1;
                for (const auto& e : ptsVal->asArray()) {
                    const JsonValue* a = e.find("a");
                    const JsonValue* b = e.find("b");
                    const JsonValue* t = e.find("t");
                    if (!a || !b || !t)
                        continue;
                    SectionPoint sp;
                    sp.a = static_cast<int>(a->asInt64());
                    sp.b = static_cast<int>(b->asInt64());
                    sp.t = t->asNumber();
                    sc.points.push_back(sp);
                }
                if (sc.points.size() >= 3)
                    m_sectionChains[key] = std::move(sc);
                continue;
            }
            if (chainVal.type() != JsonValue::Type::Array) continue;
            const auto& arr = chainVal.asArray();
            if (arr.empty()) continue;
            // 元素是 [x,y,z] 数组 => 精确坐标链；元素是数字 => 顶点索引链。
            if (arr[0].type() == JsonValue::Type::Array) {
                std::vector<Vec3> pts;
                pts.reserve(arr.size());
                for (const auto& e : arr) {
                    const auto& c = e.asArray();
                    if (c.size() < 3)
                        continue;
                    pts.emplace_back(c[0].asNumber(), c[1].asNumber(), c[2].asNumber());
                }
                m_pointChains[key] = std::move(pts);
            } else {
                std::vector<int> chain;
                chain.reserve(arr.size());
                for (const auto& v : arr)
                    chain.push_back(static_cast<int>(v.asInt64()));
                m_chains[key] = std::move(chain);
            }
        }
    }
}

std::vector<std::string> MeasureSystem::measureKeys() const {
    std::vector<std::string> keys;
    keys.reserve(m_chains.size() + m_pointChains.size() + m_sectionChains.size() + m_straightChains.size());
    for (const auto& [k, v] : m_chains) keys.push_back(k);
    for (const auto& [k, v] : m_pointChains) keys.push_back(k);
    for (const auto& [k, v] : m_sectionChains) keys.push_back(k);
    for (const auto& [k, v] : m_straightChains) keys.push_back(k);
    return keys;
}

bool MeasureSystem::hasChain(const std::string& key) const {
    return m_chains.count(key) != 0 || m_pointChains.count(key) != 0 ||
           m_sectionChains.count(key) != 0 || m_straightChains.count(key) != 0;
}

double MeasureSystem::computeOne(const std::string& key, const std::vector<Vec3>& coords) const {
    // 直线距离链：两端点直线欧氏距离（cm）。
    auto stit = m_straightChains.find(key);
    if (stit != m_straightChains.end()) {
        const Vec3 pa = straightEndpoint(stit->second.fromVertex, stit->second.fromSection, coords);
        const Vec3 pb = straightEndpoint(stit->second.toVertex, stit->second.toSection, coords);
        return pa.distanceTo(pb) * 10.0;
    }
    // 水平截面链：边插值 → 截面凸包周长（cm）。
    auto sit = m_sectionChains.find(key);
    if (sit != m_sectionChains.end())
        return sectionCircumference(sit->second, coords);
    // 精确坐标链：直接按坐标折线算长度（cm）。
    auto pit = m_pointChains.find(key);
    if (pit != m_pointChains.end()) {
        const auto& pts = pit->second;
        if (pts.size() < 2) return 0.0;
        double len = 0.0;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            len += pts[i].distanceTo(pts[i + 1]);
        return len * 10.0;
    }
    auto it = m_chains.find(key);
    if (it == m_chains.end()) return 0.0;
    return measureValue(key, it->second, coords);
}

double MeasureSystem::computeOneMesh(const std::string& key, const Mesh3D& mesh) const {
    // 直线链 / 水平截面链 / 精确坐标链直接按坐标算（不测地）。
    if (m_straightChains.count(key) || m_sectionChains.count(key) || m_pointChains.count(key))
        return computeOne(key, mesh.verts);
    auto it = m_chains.find(key);
    if (it == m_chains.end()) return 0.0;
    const std::vector<int>& chain = it->second;
    // 距离类且恰为两点：贴肤测地线距离（cm）。custom/* 短线（胸点到胸点、
    // 侧颈点到胸点）保持直线。否则退回原坐标接口。
    const bool isCustom = key.rfind("custom/", 0) == 0;
    if (!isCircularMeasure(key) && chain.size() == 2 && !isCustom) {
        // 复用拓扑缓存：网格切换时重建一次，morph 仅改顶点坐标、只重算边权。
        if (!m_geodes)
            m_geodes = std::make_unique<GeodesicSolver>();
        if (m_geoMesh != &mesh) {
            m_geodes->setMesh(mesh);
            m_geoMesh = &mesh;
        }
        const double geo = m_geodes->distance(chain[0], chain[1]);
        if (geo < 0.0)
            return measureValue(key, chain, mesh.verts); // 不可达退回直线
        return geo * 10.0;
    }
    return measureValue(key, chain, mesh.verts);
}

std::map<std::string, double> MeasureSystem::computeAll(const std::vector<Vec3>& coords) const {
    std::map<std::string, double> out;
    for (const auto& [k, v] : m_chains) {
        (void)v;
        out[k] = computeOne(k, coords);
    }
    for (const auto& [k, v] : m_pointChains) {
        (void)v;
        out[k] = computeOne(k, coords);
    }
    for (const auto& [k, v] : m_sectionChains) {
        (void)v;
        out[k] = computeOne(k, coords);
    }
    for (const auto& [k, v] : m_straightChains) {
        (void)v;
        out[k] = computeOne(k, coords);
    }
    return out;
}

std::vector<int> geodesicPath(const Mesh3D& mesh, int src, int dst) {
    const int n = mesh.vertexCount();
    if (src < 0 || src >= n || dst < 0 || dst >= n)
        return {};
    if (src == dst)
        return {src};

    // 建邻接表（无向，边权 = 两端点欧氏距离，0.1m 单位）
    std::vector<std::vector<std::pair<int, double>>> adj(static_cast<size_t>(n));
    const auto& verts = mesh.verts;
    const auto addEdge = [&](int a, int b) {
        const double d = verts[static_cast<size_t>(a)].distanceTo(
                             verts[static_cast<size_t>(b)]);
        adj[static_cast<size_t>(a)].emplace_back(b, d);
        adj[static_cast<size_t>(b)].emplace_back(a, d);
    };
    for (const auto& f : mesh.faces) {
        if (f[0] < 0 || f[0] >= n || f[1] < 0 || f[1] >= n || f[2] < 0 || f[2] >= n)
            continue;
        addEdge(f[0], f[1]); addEdge(f[1], f[2]); addEdge(f[2], f[0]);
    }

    // Dijkstra：小顶二叉堆（自实现，避免 <queue> 重模板依赖）
    const double inf = 1e30;
    std::vector<double> dist(static_cast<size_t>(n), inf);
    std::vector<int> prev(static_cast<size_t>(n), -1);
    dist[static_cast<size_t>(src)] = 0.0;

    // 二叉堆：存 (dist, vertex)，按 dist 升序
    std::vector<std::pair<double, int>> heap;
    heap.reserve(static_cast<size_t>(n));
    heap.emplace_back(0.0, src);
    const auto heapSiftUp = [&](size_t i) {
        while (i > 0) {
            const size_t p = (i - 1) / 2;
            if (heap[p].first <= heap[i].first) break;
            std::swap(heap[p], heap[i]);
            i = p;
        }
    };
    const auto heapSiftDown = [&](size_t i) {
        for (;;) {
            size_t l = i * 2 + 1, r = i * 2 + 2, m = i;
            if (l < heap.size() && heap[l].first < heap[m].first) m = l;
            if (r < heap.size() && heap[r].first < heap[m].first) m = r;
            if (m == i) break;
            std::swap(heap[m], heap[i]);
            i = m;
        }
    };

    while (!heap.empty()) {
        const auto [d, u] = heap[0];
        std::swap(heap[0], heap.back());
        heap.pop_back();
        heapSiftDown(0);
        if (d > dist[static_cast<size_t>(u)]) continue;
        if (u == dst) break;
        for (const auto& [v, w] : adj[static_cast<size_t>(u)]) {
            const double nd = d + w;
            if (nd < dist[static_cast<size_t>(v)]) {
                dist[static_cast<size_t>(v)] = nd;
                prev[static_cast<size_t>(v)] = u;
                heap.emplace_back(nd, v);
                heapSiftUp(heap.size() - 1);
            }
        }
    }
    if (dist[static_cast<size_t>(dst)] >= inf)
        return {};

    std::vector<int> path;
    for (int v = dst; v != -1; v = prev[static_cast<size_t>(v)])
        path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

double geodesicDistance(const Mesh3D& mesh, int src, int dst) {
    const std::vector<int> path = geodesicPath(mesh, src, dst);
    if (path.size() < 2)
        return path.size() == 1 ? 0.0 : -1.0;
    double len = 0.0;
    for (size_t i = 0; i + 1 < path.size(); ++i)
        len += mesh.verts[static_cast<size_t>(path[i])].distanceTo(
                   mesh.verts[static_cast<size_t>(path[i + 1])]);
    return len;
}

void GeodesicSolver::setMesh(const Mesh3D& mesh) {
    const int n = mesh.vertexCount();
    // 拓扑缓存：顶点数不变（morph 只改坐标，不重建 face 结构）时仅更新顶点引用，
    // 避免每帧拖动重建邻接边结构（卡顿主因）。顶点数变化（换模型/衣物过滤开关）
    // 才重建拓扑。
    if (m_n == n && !m_edges.empty()) {
        m_verts = &mesh.verts;
        return;
    }
    m_n = n;
    m_verts = &mesh.verts;
    // 提取无向边（去重）。
    std::vector<std::pair<int, int>> edges;
    const auto add = [&](int a, int b) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        edges.emplace_back(a, b);
    };
    for (const auto& f : mesh.faces) {
        if (f[0] < 0 || f[1] < 0 || f[2] < 0 ||
            f[0] >= m_n || f[1] >= m_n || f[2] >= m_n)
            continue;
        add(f[0], f[1]); add(f[1], f[2]); add(f[2], f[0]);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    m_edges = std::move(edges);
}

std::vector<int> GeodesicSolver::path(int src, int dst) const {
    if (src < 0 || src >= m_n || dst < 0 || dst >= m_n || !m_verts)
        return {};
    if (src == dst)
        return {src};
    const auto& verts = *m_verts;
    // 每次由边结构 + 当前顶点坐标重建带权邻接（边权随 morph 变，必须重算）。
    std::vector<std::vector<std::pair<int, double>>> adj(static_cast<size_t>(m_n));
    for (const auto& [a, b] : m_edges) {
        const double d = verts[static_cast<size_t>(a)].distanceTo(verts[static_cast<size_t>(b)]);
        adj[static_cast<size_t>(a)].emplace_back(b, d);
        adj[static_cast<size_t>(b)].emplace_back(a, d);
    }
    // Dijkstra（小顶二叉堆）
    const double inf = 1e30;
    std::vector<double> dist(static_cast<size_t>(m_n), inf);
    std::vector<int> prev(static_cast<size_t>(m_n), -1);
    dist[static_cast<size_t>(src)] = 0.0;
    std::vector<std::pair<double, int>> heap;
    heap.reserve(static_cast<size_t>(m_n));
    heap.emplace_back(0.0, src);
    const auto up = [&](size_t i){ while(i>0){size_t p=(i-1)/2; if(heap[p].first<=heap[i].first)break; std::swap(heap[p],heap[i]); i=p;} };
    const auto down = [&](size_t i){ for(;;){size_t l=i*2+1,r=i*2+2,m=i; if(l<heap.size()&&heap[l].first<heap[m].first)m=l; if(r<heap.size()&&heap[r].first<heap[m].first)m=r; if(m==i)break; std::swap(heap[m],heap[i]); i=m;} };
    while (!heap.empty()) {
        const auto [d, u] = heap[0];
        std::swap(heap[0], heap.back()); heap.pop_back(); down(0);
        if (d > dist[static_cast<size_t>(u)]) continue;
        if (u == dst) break;
        for (const auto& [v, w] : adj[static_cast<size_t>(u)]) {
            const double nd = d + w;
            if (nd < dist[static_cast<size_t>(v)]) {
                dist[static_cast<size_t>(v)] = nd; prev[static_cast<size_t>(v)] = u;
                heap.emplace_back(nd, v); up(heap.size() - 1);
            }
        }
    }
    if (dist[static_cast<size_t>(dst)] >= inf) return {};
    std::vector<int> path;
    for (int v = dst; v != -1; v = prev[static_cast<size_t>(v)]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

double GeodesicSolver::distance(int src, int dst) const {
    const auto p = path(src, dst);
    if (p.size() < 2) return p.size() == 1 ? 0.0 : -1.0;
    double len = 0.0;
    const auto& v = *m_verts;
    for (size_t i = 0; i + 1 < p.size(); ++i)
        len += v[static_cast<size_t>(p[i])].distanceTo(v[static_cast<size_t>(p[i+1])]);
    return len;
}

} // namespace cad::avatar
