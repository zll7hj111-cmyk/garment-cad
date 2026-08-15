#include "AvatarRenderMesh.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace cad::avatar {

bool isRenderableGroup(const std::string& g)
{
    if (g.empty()) return true; // 无组信息（手工构造的网格）→ 全量
    if (g == "body") return true;
    if (g == "helper-tongue" || g == "helper-upper-teeth" || g == "helper-lower-teeth")
        return true;
    if (g.rfind("helper-l-eye", 0) == 0 || g.rfind("helper-r-eye", 0) == 0)
        return true;
    if (g.rfind("helper-l-eyelashes", 0) == 0 || g.rfind("helper-r-eyelashes", 0) == 0)
        return true;
    return false;
}

void AvatarRenderMesh::build(const Mesh3D& mesh)
{
    m_vertices.clear();
    m_indices.clear();

    const auto& verts = mesh.verts;
    const auto& faces = mesh.faces;
    // 调用方可能只加载了 OBJ（无法线）；缺法线时兜底按顶点法线计算
    const bool needNormals = mesh.normals.size() != verts.size();
    Mesh3D tmp;
    const auto& normals = needNormals ? (tmp = mesh, tmp.calcNormals(), tmp.normals)
                                      : mesh.normals;
    if (verts.empty() || faces.empty())
        return;

    const bool hasGroups = mesh.faceGroups.size() == faces.size();
    const bool hasUvs = mesh.hasUvs();
    // 需要保留的面（白名单过滤）数量
    std::size_t keptFaces = 0;
    for (std::size_t i = 0; i < faces.size(); ++i)
        if (!hasGroups || isRenderableGroup(mesh.faceGroups[i])) ++keptFaces;

    if (hasUvs) {
        // UV 展开模式：每个三角形角生成一个独立渲染顶点（同一几何顶点
        // 在 UV 接缝处可有多个纹理坐标）。morph 只改 mesh.verts 位置，
        // 展开发生在渲染层拷贝，不影响 morph 的顶点索引语义。
        m_vertices.reserve(keptFaces * 3u);
        m_indices.reserve(keptFaces * 3u);
        for (std::size_t i = 0; i < faces.size(); ++i) {
            if (hasGroups && !isRenderableGroup(mesh.faceGroups[i]))
                continue;
            const auto& f = faces[i];
            const auto& fuv = mesh.faceUvs[i];
            for (int j = 0; j < 3; ++j) {
                const std::size_t vi = static_cast<std::size_t>(f[j]);
                RenderVertex v;
                v.px = static_cast<float>(verts[vi].x);
                v.py = static_cast<float>(verts[vi].y);
                v.pz = static_cast<float>(verts[vi].z);
                const bool hasN = vi < normals.size();
                v.nx = hasN ? static_cast<float>(normals[vi].x) : 0.f;
                v.ny = hasN ? static_cast<float>(normals[vi].y) : 0.f;
                v.nz = hasN ? static_cast<float>(normals[vi].z) : 0.f;
                if (fuv[j] >= 0
                    && static_cast<std::size_t>(fuv[j]) < mesh.uvs.size()) {
                    v.u = static_cast<float>(mesh.uvs[fuv[j]].u);
                    v.v = static_cast<float>(mesh.uvs[fuv[j]].v);
                }
                m_vertices.push_back(v);
                m_indices.push_back(static_cast<uint32_t>(m_vertices.size() - 1u));
            }
        }
    } else {
        // 共享顶点模式（无 UV）：顶点全保留，仅过滤面索引
        m_vertices.reserve(verts.size());
        for (std::size_t i = 0; i < verts.size(); ++i) {
            RenderVertex v;
            v.px = static_cast<float>(verts[i].x);
            v.py = static_cast<float>(verts[i].y);
            v.pz = static_cast<float>(verts[i].z);
            const bool hasN = i < normals.size();
            v.nx = hasN ? static_cast<float>(normals[i].x) : 0.f;
            v.ny = hasN ? static_cast<float>(normals[i].y) : 0.f;
            v.nz = hasN ? static_cast<float>(normals[i].z) : 0.f;
            m_vertices.push_back(v);
        }
        m_indices.reserve(faces.size() * 3u);
        for (std::size_t i = 0; i < faces.size(); ++i) {
            if (hasGroups && !isRenderableGroup(mesh.faceGroups[i]))
                continue;
            const auto& f = faces[i];
            for (int j = 0; j < 3; ++j)
                m_indices.push_back(static_cast<uint32_t>(f[j]));
        }
    }

    // 包围盒 + 包围球半径
    Vec3 lo = verts.front(), hi = verts.front();
    for (const auto& v : verts) {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    m_center = 0.5 * (lo + hi);
    m_minY = lo.y;
    double r = 0.0;
    for (const auto& v : verts)
        r = std::max(r, (v - m_center).length());
    m_radius = r > 0.0 ? r : 1.0;
}

} // namespace cad::avatar
