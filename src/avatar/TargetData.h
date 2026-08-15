#pragma once

#include "AvatarVec3.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cad::avatar {

/// morph target 数据：稀疏顶点位移表。
/// 文件格式（MakeHuman .target）：注释行以 # 开头，
/// 数据行 "<顶点索引> <dx> <dy> <dz>"（索引 0-based，位移单位米）。
/// 应用语义（algos3d.py）：coord += delta * weight，weight 可超出 [0,1]（含负值）。
struct TargetData {
    std::vector<int> indices;    ///< 受影响顶点索引
    std::vector<Vec3> deltas;    ///< 对应位移

    bool empty() const { return indices.empty(); }
};

/// 从 .target 文件加载稀疏位移。失败抛 std::runtime_error。
TargetData loadTargetFile(const std::string& path);

/// 目标缓存：key 为相对 data/targets 目录的文件名（如 "measure/measure-waist-circ-incr"）。
/// 按需懒加载，同一目标只解析一次。
class TargetCache {
public:
    /// 设置 targets 根目录（默认 assets/avatar/targets）。
    void setRootDir(std::string root);

    /// 设置顶点重映射表（来自 base 网格 vertexRemap，可选）。非空时加载
    /// target 会丢弃 remap=-1 的顶点、把保留顶点索引重映射到缩减网格；
    /// 设为空表=恒等。会清空已缓存（缓存未记录 remap 版本）。
    void setVertexRemap(std::vector<int> remap);

    /// targets 根目录。
    const std::string& rootDir() const { return m_root; }

    /// 获取目标数据（加载并缓存）。key 形如 "measure/measure-waist-circ-incr"。
    const TargetData& get(const std::string& key);

    /// 仅获取已缓存的目标，不触发磁盘读取（未缓存返回 nullptr）。
    const TargetData* findCached(const std::string& key) const;

    size_t cachedCount() const { return m_cache.size(); }

private:
    std::string m_root;
    std::vector<int> m_vertexRemap;               ///< 顶点重映射表（空 = 恒等）
    std::unordered_map<std::string, TargetData> m_cache;
};

} // namespace cad::avatar
