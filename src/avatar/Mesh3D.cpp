#include "Mesh3D.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cad::avatar {

void Mesh3D::clear() {
    verts.clear();
    faces.clear();
    normals.clear();
    faceGroups.clear();
    uvs.clear();
    faceUvs.clear();
    vertexRemap.clear();
}

void Mesh3D::calcNormals() {
    normals.assign(verts.size(), Vec3::zero());
    for (const auto& f : faces) {
        const Vec3& a = verts[f[0]];
        const Vec3& b = verts[f[1]];
        const Vec3& c = verts[f[2]];
        const Vec3 n = (b - a).cross(c - a);
        normals[f[0]] += n;
        normals[f[1]] += n;
        normals[f[2]] += n;
    }
    for (auto& n : normals) n = n.normalized();
}

namespace {
/// 解析 OBJ 得到完整网格（不裁剪、不重映射）。所有 v/f 行照单全收。
Mesh3D parseObj(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("loadObjFile: cannot open file: " + path);

    Mesh3D mesh;
    std::string line;
    std::string curGroup;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == 'g') {
            if (line.size() > 1 && (line[1] == ' ' || line[1] == '\t')) {
                curGroup = line.substr(2);
                while (!curGroup.empty() && (curGroup.back() == '\r'
                                             || curGroup.back() == ' '
                                             || curGroup.back() == '\t'))
                    curGroup.pop_back();
            }
            continue;
        }
        if (line[0] == 'v') {
            if (line.size() > 1 && (line[1] == ' ' || line[1] == '\t')) {
                std::istringstream ss(line.substr(1));
                double x, y, z;
                if (!(ss >> x >> y >> z))
                    throw std::runtime_error("loadObjFile: malformed vertex line");
                mesh.verts.emplace_back(x, y, z);
            } else if (line.size() > 2 && line[1] == 't'
                       && (line[2] == ' ' || line[2] == '\t')) {
                std::istringstream ss(line.substr(2));
                double u, v = 0.0;
                if (!(ss >> u)) {
                    u = 0.0;
                } else {
                    ss >> v;
                }
                mesh.uvs.emplace_back(u, v);
            }
            continue;
        }
        if (line[0] == 'f') {
            if (line.size() > 1 && (line[1] == ' ' || line[1] == '\t')) {
                std::vector<int> idx;
                std::vector<int> uvIdx; // 每个角对应的 vt 索引（-1 = 无）
                std::istringstream ss(line.substr(1));
                std::string tok;
                while (ss >> tok) {
                    if (tok.empty()) continue;
                    // 支持 v, v/vt, v/vt/vn 三种形式：顶点索引必取，vt 尽力取
                    int v = 0;
                    const char* begin = tok.c_str();
                    const char* end = begin + tok.size();
                    auto [ptr, ec] = std::from_chars(begin, end, v);
                    if (ec != std::errc() || v <= 0)
                        throw std::runtime_error("loadObjFile: malformed face index");
                    idx.push_back(v - 1);
                    int vt = -1;
                    if (ptr != end && *ptr == '/') {
                        const char* vtEnd = std::strchr(ptr + 1, '/');
                        if (vtEnd == nullptr) vtEnd = end;
                        const char* vtBegin = ptr + 1;
                        if (vtBegin != vtEnd && *vtBegin != '\0') {
                            auto [vtPtr, vtEc] =
                                std::from_chars(vtBegin, vtEnd, vt);
                            if (vtEc == std::errc() && vt > 0) vt -= 1;
                            else vt = -1;
                            (void)vtPtr;
                        }
                    }
                    uvIdx.push_back(vt);
                }
                if (idx.size() < 3)
                    throw std::runtime_error("loadObjFile: face with fewer than 3 vertices");
                for (size_t i = 1; i + 1 < idx.size(); ++i) {
                    mesh.faces.push_back({idx[0], idx[i], idx[i + 1]});
                    mesh.faceGroups.push_back(curGroup);
                    mesh.faceUvs.push_back({uvIdx[0], uvIdx[i], uvIdx[i + 1]});
                }
            }
            continue;
        }
    }
    if (mesh.verts.empty())
        throw std::runtime_error("loadObjFile: no vertices loaded: " + path);
    return mesh;
}

} // namespace

Mesh3D loadObjFile(const std::string& path) {
    return parseObj(path);
}

Mesh3D loadObjFile(const std::string& path,
                   const std::unordered_set<std::string>& excludeGroups) {
    Mesh3D full = parseObj(path);
    if (excludeGroups.empty())
        return full;

    // 1) 确定保留的面（组名不在 excludeGroups 中）。
    const size_t faceCount = full.faces.size();
    std::vector<bool> keepFace(faceCount, false);
    for (size_t i = 0; i < faceCount; ++i) {
        const std::string& g = full.faceGroups[i];
        keepFace[i] = excludeGroups.find(g) == excludeGroups.end();
    }

    // 2) 收集保留面引用的旧顶点索引，按旧索引升序构建重映射表。
    std::vector<bool> keepVert(full.verts.size(), false);
    for (size_t i = 0; i < faceCount; ++i) {
        if (!keepFace[i]) continue;
        keepVert[full.faces[i][0]] = true;
        keepVert[full.faces[i][1]] = true;
        keepVert[full.faces[i][2]] = true;
    }
    std::vector<int> remap(full.verts.size(), -1);
    int next = 0;
    for (size_t i = 0; i < full.verts.size(); ++i) {
        if (keepVert[i]) remap[i] = next++;
    }

    // 3) 生成缩减后的网格：顶点按新索引排列，面索引重映射。
    Mesh3D out;
    out.vertexRemap = std::move(remap);
    out.verts.reserve(static_cast<size_t>(next));
    for (size_t i = 0; i < full.verts.size(); ++i) {
        if (out.vertexRemap[i] >= 0)
            out.verts.push_back(full.verts[i]);
    }
    out.faces.reserve(faceCount);
    out.faceGroups.reserve(faceCount);
    out.faceUvs.reserve(faceCount);
    for (size_t i = 0; i < faceCount; ++i) {
        if (!keepFace[i]) continue;
        out.faces.push_back({
            out.vertexRemap[full.faces[i][0]],
            out.vertexRemap[full.faces[i][1]],
            out.vertexRemap[full.faces[i][2]]});
        out.faceGroups.push_back(full.faceGroups[i]);
        out.faceUvs.push_back(full.faceUvs[i]);
    }
    // UV 坐标原样保留（面角 uv 索引不变，uv 表与顶点无 1:1 关系）。
    out.uvs = std::move(full.uvs);
    // normals 留待调用方 calcNormals 重算（顶点集合已变）。

    if (out.verts.empty())
        throw std::runtime_error("loadObjFile: all faces excluded: " + path);
    return out;
}

} // namespace cad::avatar
