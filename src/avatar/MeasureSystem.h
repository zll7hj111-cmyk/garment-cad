#pragma once

#include "AvatarVec3.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cad::avatar {

struct Mesh3D; // 前置声明：测地线函数只按引用使用，避免头文件重依赖
class GeodesicSolver; // 前置声明：MeasureSystem 以 unique_ptr 持有，避免类定义顺序依赖

/// 水平截面链的点：网格边 (a,b) 上 t 处的插值点（0.1m 单位）。
/// 运行时 morph 后按当前顶点插值 → 截面点跟随变形，且天然保持水平截面语义。
struct SectionPoint {
    int a = 0;
    int b = 0;
    double t = 0.0;
};

/// 水平截面链：在固定截面高度 y 处对网格求交得到的环（软尺水平绕一圈）。
struct SectionChain {
    double y = 0.0;                     ///< 截面高度（base 网格 0.1m 单位，仅记录用）
    int anchor = -1;                    ///< 截面高度锚点顶点（morph 后以其当前 y 重新求交）
    std::vector<SectionPoint> points;   ///< 环绕序截面点（边插值）
};

/// 直线距离链（如脊柱至腰、腰至臀距）：沿中线两点直线，不走贴肤测地线。
/// 端点可为网格顶点（vertex>=0）或截面插值点（顶点<0 时用 section 边插值）。
struct StraightChain {
    int fromVertex = -1;
    SectionPoint fromSection;
    int toVertex = -1;
    SectionPoint toSection;
};

/// 身体测量系统：按 MakeHuman 的测量链（硬编码顶点索引序列）计算体围。
/// 每条测量 = 一组顶点索引的折线总长；坐标单位为 MH 约定 0.1 米，对外单位厘米。
/// 数据源：assets/avatar/measurement_chains.json（源自 0_modeling_a_measurement.py）。
class MeasureSystem {
public:
    /// 加载测量链定义。失败抛 std::runtime_error。
    void loadChains(const std::string& jsonPath);

    /// 用顶点重映射表（来自 base 网格 vertexRemap）重映射链顶点索引。
    /// - 保留顶点（remap>=0）映射到缩减网格索引；
    /// - 被排除顶点（remap=-1）用 fullCoords 找最近的保留顶点替换（如肩宽链
    ///   中间点落到了被排的头发 C7 上，应重新命中到身体最近的颈后顶点）。
    /// fullCoords 为完整原始网格顶点坐标（未缩减，用于最近邻匹配）。
    /// remap 空表 = 不重映射。
    void remapChains(const std::vector<int>& vertexRemap,
                     const std::vector<Vec3>& fullCoords);

    /// 20 条测量键（与 MH 一致）。空链（如 neck-height）在计算中跳过。
    std::vector<std::string> measureKeys() const;

    /// 计算全部测量值（cm）。链缺失的键返回 0。
    /// coords 为当前网格顶点坐标（0.1 米单位）。
    std::map<std::string, double> computeAll(const std::vector<Vec3>& coords) const;

    /// 计算单条测量值（cm）；无此链返回 0。
    double computeOne(const std::string& key, const std::vector<Vec3>& coords) const;

    /// 计算单条测量值（cm），带网格拓扑：距离类且恰为两点时走贴肤测地线
    /// （绕开身体凸起，值 >= 直线），其余（围度/多点路径）等同 computeOne。
    double computeOneMesh(const std::string& key, const Mesh3D& mesh) const;

    /// 该测量是否有顶点链定义。
    bool hasChain(const std::string& key) const;

    /// 全部测量链（key -> 顶点索引折线），供视口绘制测量线。
    const std::map<std::string, std::vector<int>>& chains() const { return m_chains; }

    /// 精确坐标测量链（key -> 世界坐标点折线，0.1m 单位）。JSON 中链元素为
    /// [x,y,z] 数组时解析到此；用于手工标定的关键点测量（前胸宽、侧颈到胸等），
    /// 精确等于标注坐标、不吸附顶点。供视口绘制与测量值计算。
    const std::map<std::string, std::vector<Vec3>>& pointChains() const { return m_pointChains; }

    /// 水平截面测量链（key -> 截面环，边插值点）。JSON 中链值为
    /// {section_y, points:[{a,b,t}...]} 对象时解析到此；用于腰围等需要
    /// 严格水平截面的围度（腰最细处网格环线天然倾斜，顶点链做不出水平环）。
    /// 运行时按当前网格顶点插值，跟随 morph。
    const std::map<std::string, SectionChain>& sectionChains() const { return m_sectionChains; }

    /// 直线距离链（key -> 中线两点直线，端点可为顶点或截面插值点）。
    const std::map<std::string, StraightChain>& straightChains() const { return m_straightChains; }

private:
    std::map<std::string, std::vector<int>> m_chains;   ///< key -> 顶点索引链
    std::map<std::string, std::vector<Vec3>> m_pointChains; ///< key -> 精确坐标链
    std::map<std::string, SectionChain> m_sectionChains; ///< key -> 水平截面链（边插值）
    std::map<std::string, StraightChain> m_straightChains; ///< key -> 直线距离链
    mutable std::unique_ptr<GeodesicSolver> m_geodes; ///< 测地求解器（缓存拓扑）
    mutable const Mesh3D* m_geoMesh = nullptr; ///< 上次绑定测地拓扑的网格（地址判变）
};

/// 测量类型：围度（水平截面凸包周长）或距离（首末点欧氏距离）。
/// 按 MH 测量键约定：key 含 "-circ-" 为围度，否则为距离。
bool isCircularMeasure(const std::string& key);

/// 围度环的截面几何：拟合环所在平面（Newell 法向量 = 环截面法向），把环顶点
/// 正交投影到该平面后求 2D 凸包（软尺拉紧），再映回 3D 凸包环。围度测量与
/// 视口渲染共用（保持测量值与画线一致）。
std::vector<Vec3> circularSectionGeometry(const std::vector<Vec3>& pts);

/// 计算一条测量链的显示几何（世界空间，0.1m 单位，未做渲染偏移）。
/// 围度返回闭合凸包环（已投影到水平截面，绕序排列，末点与首点不重复，渲染时首尾相连）；
/// 距离返回 [首点, 末点] 两点。链无效（<2 个合法顶点）返回空。
std::vector<Vec3> measureGeometry(const std::string& key,
                                  const std::vector<int>& chain,
                                  const std::vector<Vec3>& coords);

/// 计算一条测量值（cm）。围度 = 凸包环周长，距离 = 首末欧氏距离。无有效几何返回 0。
double measureValue(const std::string& key,
                    const std::vector<int>& chain,
                    const std::vector<Vec3>& coords);

/// 计算网格上两个顶点之间的测地线路径（沿三角面最短路径，单位 0.1m）。
/// 在 mesh 的全部面上建图走 Dijkstra；返回从 src 到 dst 的顶点索引序列（含两端）。
/// src/dst 越界或不可达（不在同一连通分量）返回空 vector。
std::vector<int> geodesicPath(const Mesh3D& mesh, int src, int dst);

/// 测地线长度（0.1m 单位）= 沿 geodesicPath 的折线总长。不可达返回 -1。
double geodesicDistance(const Mesh3D& mesh, int src, int dst);

/// 带拓扑缓存的测地线求解器：网格 faces 拓扑（谁连谁）不随 morph 变，缓存边结构，
/// 每次查询仅重算边权（顶点坐标变了）+ Dijkstra，避免每帧重建邻接表（拖动卡顿主因）。
class GeodesicSolver {
public:
    /// 重建拓扑缓存。网格 faces 结构变化（换模型）时调用；morph 只改顶点坐标无需重建。
    void setMesh(const Mesh3D& mesh);

    /// 顶点坐标（供计算边权）。morph 后调用方更新引用，不重建拓扑。
    void setVerts(const std::vector<Vec3>& verts) { m_verts = &verts; }

    /// 测地线路径（顶点索引序列，含两端）。src/dst 越界或不可达返回空。
    std::vector<int> path(int src, int dst) const;

    /// 测地线长度（0.1m 单位）。不可达返回 -1。
    double distance(int src, int dst) const;

private:
    std::vector<std::pair<int, int>> m_edges;               ///< 无向边拓扑缓存（去重）
    const std::vector<Vec3>* m_verts = nullptr;             ///< 顶点坐标
    int m_n = 0;
};

} // namespace cad::avatar
