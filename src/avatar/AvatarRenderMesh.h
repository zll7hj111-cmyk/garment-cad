#pragma once

#include "AvatarVec3.h"
#include "Mesh3D.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cad::avatar {

/// 渲染白名单：base.obj 只显示身体与面部细节，剔除衣服辅助几何
/// （helper-tights/skirt）、头发、生殖器与骨骼关节标记（joint-*）。
/// 渲染（AvatarRenderMesh::build）与拾取（AvatarView3D::pickSurfacePoint）
/// 必须共用此过滤，否则拾取会命中被隐藏的几何导致标注点浮空。
/// 无组信息时（g 为空，手工构造网格）视为全量渲染。
bool isRenderableGroup(const std::string& g);

/// 渲染顶点：位置 + 法线 + 纹理坐标（interleaved float，GL 上传友好）。
struct RenderVertex {
    float px = 0.f, py = 0.f, pz = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
    float u = 0.f, v = 0.f;
};

/// CPU 侧渲染数据打包：Mesh3D → 顶点数组 + 索引数组 + 包围盒。
/// 不包含任何 GL 调用（可单测）；3D 视口用 upload() 语义上传。
class AvatarRenderMesh {
public:
    void build(const Mesh3D& mesh);

    bool empty() const { return m_vertices.empty(); }
    int vertexCount() const { return static_cast<int>(m_vertices.size()); }
    int indexCount() const { return static_cast<int>(m_indices.size()); }

    const std::vector<RenderVertex>& vertices() const { return m_vertices; }
    const std::vector<uint32_t>& indices() const { return m_indices; }

    /// 包围盒中心（相机对准点）。
    Vec3 center() const { return m_center; }
    /// 包围盒包围球半径（相机距离 = radius / sin(fov/2)）。
    double radius() const { return m_radius; }
    /// 包围盒最小 Y（脚底高度，地面基准）。
    double minY() const { return m_minY; }

private:
    std::vector<RenderVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    Vec3 m_center{0, 0, 0};
    double m_radius = 0.0;
    double m_minY = 0.0;
};

} // namespace cad::avatar
