#pragma once

#include "AvatarVec3.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace cad::avatar {


/// 三角网格：顶点 + 三角形面 + 顶点法线。
/// OBJ 中的多边形面（>3 顶点）在加载时扇形三角化。
/// 法线按面平坦法线累加后归一化（与 MakeHuman calcNormals 语义一致）。
struct Mesh3D {
    std::vector<Vec3> verts;            ///< 顶点坐标（米）
    std::vector<std::array<int, 3>> faces; ///< 三角形面（索引，0-based）
    std::vector<Vec3> normals;          ///< 顶点法线（与 verts 一一对应）
    std::vector<std::string> faceGroups; ///< 每个面对应的 OBJ 组名（与 faces 一一对应；无组信息时为空）
    std::vector<Vec2> uvs;              ///< 纹理坐标（OBJ vt 行，与 verts 无索引对应关系）
    std::vector<std::array<int, 3>> faceUvs; ///< 每个三角形角对应的 uv 索引（与 faces 一一对应；-1 = 该角无 UV）

    bool empty() const { return verts.empty(); }
    int vertexCount() const { return static_cast<int>(verts.size()); }
    int faceCount() const { return static_cast<int>(faces.size()); }
    /// 是否有可用 UV（uvs 非空且每个面角都有 UV 索引）。
    bool hasUvs() const {
        return !uvs.empty() && faceUvs.size() == faces.size();
    }
    /// 顶点重映射表（可选）：按组排除面后，记录 vertexRemap[oldIdx]=newIdx（被排除=-1）。
    /// 空表 = 恒等映射（未缩减）。供 morph target 加载时把原始索引映射到缩减网格。
    std::vector<int> vertexRemap;
    /// 是否做了顶点缩减（有重映射表）。
    bool isRemapped() const { return !vertexRemap.empty(); }

    void clear();
    void calcNormals();
};


/// 从 Wavefront OBJ 文件加载网格（仅读取 v / f 行，忽略其余）。
/// 失败抛 std::runtime_error。
Mesh3D loadObjFile(const std::string& path);

/// 从 Wavefront OBJ 文件加载网格，并排除指定组的面与顶点。
/// excludeGroups 为要丢弃的组名集合。排除后顶点按保留集合重编号，
/// 并填充 vertexRemap 供 morph target 同步过滤重映射。
/// 失败抛 std::runtime_error。
Mesh3D loadObjFile(const std::string& path,
                   const std::unordered_set<std::string>& excludeGroups);

} // namespace cad::avatar
