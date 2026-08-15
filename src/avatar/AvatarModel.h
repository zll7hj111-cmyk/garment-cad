#pragma once

#include "Mesh3D.h"
#include "TargetData.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cad::avatar {

/// 一次目标应用：目标 key（相对 targets 根目录的文件名）+ 权重。
/// 权重语义与 MakeHuman morphFactor 一致：coord += delta * weight，
/// 可超出 [0,1]（含负值）。应用顺序 = 容器顺序。
struct TargetWeight {
    std::string key;
    double weight = 0.0;
};

/// 虚拟模特模型：base 网格 + morph 目标叠加。
/// 变形管线（对齐 MakeHuman apps/human.py applyAllTargets 的种子网格部分）：
/// 坐标 = base 坐标 + Σ (目标位移 × 权重)。
///
/// 两种应用路径：
///  - applyTargets：全量重置后按序叠加（golden 对照 / 一次性设置用）；
///  - applyDeltaWeights：增量应用——调用方给出「完整目标集」，
///    内部与当前已应用状态 diff，只叠加变化的权重（拖动滑杆的性能路径）。
class AvatarModel {
public:
    /// @param baseMesh base 网格（种子坐标，0.1m 单位）
    /// @param targetsDir morph 目标数据根目录（含 measure/ 等子目录）
    explicit AvatarModel(Mesh3D baseMesh, std::string targetsDir);

    /// 变形后的网格（每次应用后刷新）。
    const Mesh3D& mesh() const { return m_mesh; }

    /// morph 目标数据根目录（构造时设置）。
    const std::string& targetsDir() const { return m_cache.rootDir(); }

    /// 全量应用：重置为 base 后按序叠加并重算法线。
    void applyTargets(const std::vector<TargetWeight>& targets);

    /// 增量应用：与已应用状态 diff，仅叠加权重变化（性能路径）。
    /// targets 必须是「完整目标集」（包含所有已应用项），否则残留旧权重。
    void applyDeltaWeights(const std::vector<TargetWeight>& targets);

    /// 清空全部已应用权重并恢复 base 网格（完全中性体）。
    void resetAllTargets();

    /// 法线更新开关：拖动体型时置 false 跳过每帧全量算法线（最贵开销），
    /// 松开后置 true 并补算；渲染用上一帧法线（光照略滞后，几乎无感）。
    void setUpdateNormals(bool on);

    /// 手动补算法线（法线更新被关闭后、需要精确法线时调用）。
    void syncNormals();

    /// 当前已应用的目标（key -> 权重），与 mesh() 一致。
    const std::map<std::string, double>& appliedTargets() const { return m_applied; }

    /// 导出当前网格为 Wavefront OBJ（v + 三角面 f，%.6f 精度，UTF-8）。
    /// 失败（路径不可写）返回 false。
    bool saveObj(const std::string& path) const;

private:
    void calcNormals();

    Mesh3D m_base;                     ///< base 网格（种子坐标）
    Mesh3D m_mesh;                     ///< 当前网格
    TargetCache m_cache;               ///< morph 目标懒加载缓存
    std::map<std::string, double> m_applied; ///< 已应用权重（key -> weight）
    bool m_updateNormals = true;       ///< 法线更新开关（拖动时 false）
    bool m_normalsDirty = false;       ///< 法线已过期待补算
};

} // namespace cad::avatar
