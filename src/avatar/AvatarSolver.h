#pragma once

#include "AvatarModel.h"
#include "MeasureSystem.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cad::avatar {

/// 滑杆类型（对齐 MakeHuman modeling_modifiers.json 语义）：
///  - Normal：双 morph（minExt/maxExt，如 decr/incr），权重域 [-1,1]，
///    w>0 -> <target>-<maxExt> 权重 w，w<0 -> <target>-<minExt> 权重 -w；
///  - Single：单 morph（无 min/max），权重域 [0,1]，直接作用于 <target>；
///  - Macro：宏变量滑杆（性别/年龄/体重/身高/体型…），值域 [0,1]，
///    经 tokenVal 三角插值驱动 macrodetails 采样文件；
///  - Ethnic：人种滑杆（African/Asian/Caucasian），值域 [0,1]，联动归一化和=1。
enum class SliderKind { Normal, Single, Macro, Ethnic };

/// 滑杆定义（读自 assets/avatar/sliders.json，由 MH 数据生成）。
struct SliderDef {
    std::string id;         ///< MH 全名（'head/head-age-decr|incr' / 'macrodetails/Gender'）
    std::string label;      ///< 显示名（英文，来自 MH）
    std::string group;      ///< 分类（Face/Torso/…）
    std::string subgroup;   ///< 子分类
    SliderKind kind = SliderKind::Normal;
    std::string target;     ///< Normal/Single 的 morph 目标基名（'head/head-age'）
    std::string minExt;     ///< Normal 负向扩展（'decr'）
    std::string maxExt;     ///< Normal 正向扩展（'incr'）
    std::string macrovar;   ///< Macro/Ethnic 的宏变量名（'Gender'/'African'…）
    std::string measureKey; ///< 测量链键（'' = 无直接测量）
    bool hidden = false;    ///< 面板隐藏（sliders.json 的 hidden 字段）
    double defaultValue = 0.0;
};

/// 宏采样文件（macrodetails 系）：路径 + 文件名 token 依赖表。
struct MacroSampler {
    std::string path;               ///< 相对 targets 根目录（'macrodetails/xxx'）
    std::vector<std::string> tokens; ///< 依赖 token（'female'/'baby'/'averagemuscle'…）
};

/// 体型求解器：全部 MH 滑杆（含宏/人种）+ 测量驱动。
/// 权重域：Normal [-1,1]，Single/Macro/Ethnic [0,1]（MH 约定）。
/// 应用路径全部走增量式（AvatarModel::applyDeltaWeights），
/// 拖动单个滑杆只叠加变化的 morph。
class AvatarSolver {
public:
    /// @param model 已构造的模特（含 targets 根目录）
    /// @param measures 已 loadChains 的测量系统
    AvatarSolver(AvatarModel* model, MeasureSystem* measures);

    /// 加载滑杆定义 + 扫描宏采样文件，并应用默认体型（完全女性 + 完全亚洲）。
    /// 失败抛 std::runtime_error。
    void loadSliders(const std::string& jsonPath);

    /// 已加载的滑杆（按定义顺序）。
    const std::vector<SliderDef>& sliders() const { return m_sliders; }

    /// 已加载的宏采样文件。
    const std::vector<MacroSampler>& macroSamplers() const { return m_macroSamplers; }

    /// 查找滑杆定义；不存在返回 nullptr。
    const SliderDef* findSlider(const std::string& id) const;

    /// 读取当前滑杆值（Normal [-1,1]；Single/Macro/Ethnic [0,1]）。
    double sliderValue(const std::string& id) const;

    /// 读取宏变量当前值（'Gender'/'Age'/…；不存在返回 0）。
    double macroVar(const std::string& name) const;

    /// 直接设置滑杆值并应用（UI 拖动/脚本用）。
    void setSliderValue(const std::string& id, double v);

    /// 恢复默认体型：普通滑杆归 0，宏变量回默认（完全女性 + 完全亚洲）。
    void resetAll();

    /// 完全中性体（跳过宏采样）：仅清空普通滑杆，宏变量不动。
    /// 测试/对比 base 网格用；面板重置按钮用 resetAll。
    void resetSlidersOnly();

    /// 单测量逼近：调测量滑杆使测量=targetCm（二分，MH MeasurementValueConverter 移植）。
    /// 返回最终滑杆值。超出可达范围时 clamp 到边界（不抛异常）。
    double solveSingle(const std::string& id, double targetCm);

    /// 多测量联合逼近（Gauss-Seidel：多轮，每轮各目标单测量二分）。
    /// goals: 滑杆 id -> 目标 cm（如 measure/measure-bust-circ-decr|incr）。
    /// 返回最终各滑杆值。
    std::map<std::string, double> solveMulti(const std::map<std::string, double>& goals);

    /// 当前完整目标集（普通滑杆展开 + 宏采样非零权重），供 AvatarModel/导出用。
    std::vector<TargetWeight> currentTargets() const;

    /// 重测指定测量滑杆当前测量值（cm）。
    double measureNow(const std::string& id) const;

    // ---- 测量锁（如「胸围锁 84」）----
    /// 设置测量锁：锁定后 enforceMeasureLock() 会把 measureSliderId 的权重
    /// 自动解到使该滑杆测量值 = targetCm（补偿其他滑杆联动，如罩杯变大 →
    /// 胸腔自动内收，胸围恒定）。仅限可求解的测量滑杆（normal/single）。
    void setMeasureLock(const std::string& measureSliderId, bool locked, double targetCm);
    bool measureLockActive() const { return m_lockActive; }
    const std::string& measureLockSliderId() const { return m_lockSliderId; }
    double measureLockTargetCm() const { return m_lockTargetCm; }
    /// 若锁启用且测量偏离锁值，二分解主滑杆权重使测量回到锁值（软尺语义）。
    /// 返回补偿后的测量值（cm）。
    double enforceMeasureLock();

    /// 按「胸围+下胸围」匹配自然罩杯：先联合解两个周长到目标，再把罩杯宏
    /// 二分到使胸围-下胸围差值 = 目标差值（杯码语义，实测差值随罩杯严格单调）。
    /// 返回最终差值（cm，≈ bustCm-underCm）。
    double solveNaturalBust(const std::string& bustId, double bustCm,
                            const std::string& underId, double underCm,
                            const std::string& cupId);

    /// 杯差值（cm）→ 杯码字母（A/B/C…），行业标准分段。
    static const char* cupLetter(double diffCm);

    /// 二分 Height 宏使总身高 = targetCm（身高随 Height 宏单调：0→116 / 0.5→153 / 1→225）。
    /// 返回最终身高（cm）。
    double solveHeightCm(double targetCm);

    // ---- 身高锁 ----
    /// 锁定总身高：锁定后 enforceHeightLock() 自动反调 Height 宏，使调整
    /// 纵向分段（大腿高/小腿高/躯干段）时总身高恒定。
    void setHeightLock(bool locked, double targetCm);
    bool heightLockActive() const { return m_heightLockActive; }
    double heightLockTargetCm() const { return m_heightLockTargetCm; }
    /// 若身高锁启用且偏离锁值，二分 Height 宏使身高回锁值。返回最终身高（cm）。
    double enforceHeightLock();

    /// 当前变形后网格（与视口显示一致）。
    const Mesh3D& mesh() const { return m_model->mesh(); }

    /// 当前身高（头顶-脚底垂直距离，cm）。
    double currentHeightCm() const;

private:
    void applyCurrent();
    void expandValues(const std::map<std::string, double>& values,
                      std::map<std::string, double>& merged) const;
    double evaluateSlider(const std::string& id, double v) const;
    void setValue(const std::string& id, double v);
    void refreshTokenVals();
    void normalizeEthnic(const std::string& exclude);
    void scanMacroSamplers();

    AvatarModel* m_model;
    MeasureSystem* m_measures;
    std::vector<SliderDef> m_sliders;
    std::vector<MacroSampler> m_macroSamplers;
    std::map<std::string, double> m_values;      ///< 普通/单 morph 滑杆值
    std::map<std::string, double> m_macroVars;   ///< 宏变量（Gender/Age/…/African/…）
    std::map<std::string, double> m_tokenVals;   ///< tokenVal 插值结果（maleVal/…）

    bool m_lockActive = false;        ///< 测量锁启用
    std::string m_lockSliderId;       ///< 锁定的测量滑杆 id（补偿自由度）
    double m_lockTargetCm = 0.0;      ///< 锁定目标值（cm）

    bool m_heightLockActive = false;  ///< 身高锁启用
    double m_heightLockTargetCm = 0.0; ///< 身高锁目标值（cm）
};

} // namespace cad::avatar
