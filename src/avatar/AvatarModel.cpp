#include "AvatarModel.h"

#include <cmath>
#include <cstdio>

namespace cad::avatar {

AvatarModel::AvatarModel(Mesh3D baseMesh, std::string targetsDir)
    : m_base(std::move(baseMesh)) {
    m_cache.setRootDir(std::move(targetsDir));
    // 若 base 网格做过顶点缩减（按组排除衣物等），把重映射表传给 target
    // 缓存，使 morph 加载时同步过滤/重映射顶点索引，保证索引对齐。
    if (m_base.isRemapped())
        m_cache.setVertexRemap(m_base.vertexRemap);
    m_mesh = m_base;
    calcNormals();
}

void AvatarModel::resetAllTargets() {
    m_mesh = m_base;
    m_applied.clear();
    calcNormals();
}

void AvatarModel::applyTargets(const std::vector<TargetWeight>& targets) {
    resetAllTargets();
    for (const auto& tw : targets) {
        if (tw.weight == 0.0) continue;
        const TargetData& td = m_cache.get(tw.key);
        const size_t n = td.indices.size();
        for (size_t i = 0; i < n; ++i) {
            const int idx = td.indices[i];
            if (idx < 0 || idx >= m_mesh.vertexCount())
                continue; // 越界索引静默跳过（与 MH 行为一致：不会因单个坏行崩）
            m_mesh.verts[idx] += td.deltas[i] * tw.weight;
        }
    }
    m_applied.clear();
    for (const auto& tw : targets)
        if (tw.weight != 0.0)
            m_applied[tw.key] = tw.weight;
    calcNormals();
}

void AvatarModel::applyDeltaWeights(const std::vector<TargetWeight>& targets) {
    for (const auto& tw : targets) {
        if (tw.weight == 0.0 && m_applied.find(tw.key) == m_applied.end())
            continue;
        const double w0 = m_applied.count(tw.key) ? m_applied.at(tw.key) : 0.0;
        const double delta = tw.weight - w0;
        if (std::fabs(delta) < 1e-9) {
            m_applied[tw.key] = tw.weight;
            continue;
        }
        const TargetData& td = m_cache.get(tw.key);
        const size_t n = td.indices.size();
        for (size_t i = 0; i < n; ++i) {
            const int idx = td.indices[i];
            if (idx < 0 || idx >= m_mesh.vertexCount())
                continue;
            m_mesh.verts[idx] += td.deltas[i] * delta;
        }
        if (tw.weight == 0.0)
            m_applied.erase(tw.key);
        else
            m_applied[tw.key] = tw.weight;
    }
    // 拖动性能路径：m_updateNormals=false 时跳过全量重算法线（每帧开销大头），
    // 只标记脏；松开滑杆后 setUpdateNormals(true) / syncNormals() 补算一次。
    if (m_updateNormals)
        calcNormals();
    else
        m_normalsDirty = true;
}

void AvatarModel::setUpdateNormals(bool on) {
    if (m_updateNormals == on)
        return;
    m_updateNormals = on;
    // 重新开启时，若法线已过期则立即补算。
    if (on && m_normalsDirty) {
        calcNormals();
        m_normalsDirty = false;
    }
}

void AvatarModel::syncNormals() {
    if (m_normalsDirty) {
        calcNormals();
        m_normalsDirty = false;
    }
}

void AvatarModel::calcNormals() {
    m_mesh.calcNormals();
}

bool AvatarModel::saveObj(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    ok &= std::fprintf(f, "# WildWind Pattern avatar export\n") > 0;
    for (const auto& v : m_mesh.verts)
        ok &= std::fprintf(f, "v %.6f %.6f %.6f\n", v.x, v.y, v.z) > 0;
    for (const auto& face : m_mesh.faces) {
        if (face.size() < 3) continue;
        ok &= std::fprintf(f, "f %d %d %d\n", face[0] + 1, face[1] + 1, face[2] + 1) > 0;
        for (size_t k = 3; k < face.size(); ++k)
            ok &= std::fprintf(f, "f %d %d %d\n", face[0] + 1, face[k - 1] + 1,
                               face[k] + 1) > 0;
    }
    std::fclose(f);
    return ok;
}

} // namespace cad::avatar
