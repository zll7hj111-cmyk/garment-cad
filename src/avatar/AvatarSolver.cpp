#include "AvatarSolver.h"

#include "JsonReader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

namespace cad::avatar {
namespace {

constexpr double kNormalMin = -1.0;
constexpr double kNormalMax = 1.0;
constexpr double kUnitMin = 0.0;
constexpr double kUnitMax = 1.0;
constexpr double kSingleTol = 0.05;   ///< 单测量收敛容差（cm）
constexpr double kMultiTol = 0.10;    ///< 多测量收敛容差（cm，联动后放宽）
constexpr int kSingleIters = 14;      ///< 单测量二分迭代次数
constexpr int kMultiRounds = 20;      ///< 多测量轮次上限（截面链双向耦合，需更多轮收敛）
constexpr double kDefaultMacro = 0.5; ///< 宏变量默认值（MH MacroModifier._defaultValue）
constexpr double kDefaultEthnic = 1.0 / 3.0;
constexpr double kDefaultGender = 0.0; ///< 默认体型：完全女性（male token=0, female=1）
constexpr double kDefaultAsian = 1.0;  ///< 默认体型：完全亚洲（Asian=1，其余人种 0）

/// 文件名 token -> 类别（lib/targets.py _cat_data 移植）。
/// 采样文件名如 'universal-male-baby-averagemuscle-averageweight'，
/// 每段在 token 表中命中即该文件依赖此 token。
const std::unordered_map<std::string, std::string>& tokenCategories() {
    static const std::unordered_map<std::string, std::string> m = {
        {"male", "gender"}, {"female", "gender"},
        {"baby", "age"}, {"child", "age"}, {"young", "age"}, {"old", "age"},
        {"caucasian", "race"}, {"asian", "race"}, {"african", "race"},
        {"maxmuscle", "muscle"}, {"averagemuscle", "muscle"}, {"minmuscle", "muscle"},
        {"minweight", "weight"}, {"averageweight", "weight"}, {"maxweight", "weight"},
        {"minheight", "height"}, {"averageheight", "height"}, {"maxheight", "height"},
        {"mincup", "breastsize"}, {"averagecup", "breastsize"}, {"maxcup", "breastsize"},
        {"minfirmness", "breastfirmness"}, {"averagefirmness", "breastfirmness"},
        {"maxfirmness", "breastfirmness"},
        {"uncommonproportions", "bodyproportions"},
        {"regularproportions", "bodyproportions"},
        {"idealproportions", "bodyproportions"},
    };
    return m;
}

SliderKind parseKind(const std::string& s) {
    if (s == "single") return SliderKind::Single;
    if (s == "macro") return SliderKind::Macro;
    if (s == "ethnic") return SliderKind::Ethnic;
    return SliderKind::Normal;
}

/// 3 锚点三角插值（weight/muscle/height/breast/proportions，MH _setWeightVals 系列）。
void setTriVals(double v, double& maxVal, double& minVal, double& avgVal) {
    maxVal = std::max(0.0, v * 2.0 - 1.0);
    minVal = std::max(0.0, 1.0 - v * 2.0);
    avgVal = 1.0 - maxVal - minVal;
}

/// 默认体型宏变量：完全女性（Gender=0）+ 完全亚洲（Asian=1），其余宏取中性 0.5。
void fillDefaultMacroVars(std::map<std::string, double>& vars) {
    const std::string macroVars[] = {"Gender", "Age", "Muscle", "Weight", "Height",
                                     "BodyProportions", "BreastSize", "BreastFirmness"};
    for (const auto& v : macroVars)
        vars[v] = kDefaultMacro;
    vars["Gender"] = kDefaultGender;
    vars["African"] = 0.0;
    vars["Asian"] = kDefaultAsian;
    vars["Caucasian"] = 0.0;
}

} // namespace

AvatarSolver::AvatarSolver(AvatarModel* model, MeasureSystem* measures)
    : m_model(model), m_measures(measures) {
}

void AvatarSolver::loadSliders(const std::string& jsonPath) {
    const JsonValue root = parseJsonFile(jsonPath);
    if (root.type() != JsonValue::Type::Object)
        throw std::runtime_error("AvatarSolver: sliders.json root must be an object");

    const JsonValue* slidersIt = nullptr;
    for (const auto& [k, v] : root.asObject())
        if (k == "sliders")
            slidersIt = &v;
    if (!slidersIt || slidersIt->type() != JsonValue::Type::Array)
        throw std::runtime_error("AvatarSolver: missing 'sliders' array");

    m_sliders.clear();
    for (const auto& item : slidersIt->asArray()) {
        if (item.type() != JsonValue::Type::Object)
            throw std::runtime_error("AvatarSolver: slider entry must be an object");
        SliderDef def;
        for (const auto& [k, v] : item.asObject()) {
            if (k == "id") { if (v.type() == JsonValue::Type::String) def.id = v.asString(); }
            else if (k == "label") { if (v.type() == JsonValue::Type::String) def.label = v.asString(); }
            else if (k == "group") { if (v.type() == JsonValue::Type::String) def.group = v.asString(); }
            else if (k == "subgroup") { if (v.type() == JsonValue::Type::String) def.subgroup = v.asString(); }
            else if (k == "kind") { if (v.type() == JsonValue::Type::String) def.kind = parseKind(v.asString()); }
            else if (k == "target") { if (v.type() == JsonValue::Type::String) def.target = v.asString(); }
            else if (k == "minExt") { if (v.type() == JsonValue::Type::String) def.minExt = v.asString(); }
            else if (k == "maxExt") { if (v.type() == JsonValue::Type::String) def.maxExt = v.asString(); }
            else if (k == "macrovar") { if (v.type() == JsonValue::Type::String) def.macrovar = v.asString(); }
            else if (k == "measureKey") { if (v.type() == JsonValue::Type::String) def.measureKey = v.asString(); }
            else if (k == "hidden") { if (v.type() == JsonValue::Type::Bool) def.hidden = v.asBool(); }
            else if (k == "default") { if (v.type() == JsonValue::Type::Number) def.defaultValue = v.asNumber(); }
        }
        if (def.id.empty())
            throw std::runtime_error("AvatarSolver: slider entry missing string 'id'");
        m_sliders.push_back(std::move(def));
    }

    scanMacroSamplers();

    // 初始状态：普通滑杆 0，宏变量默认体型（完全女性 + 完全亚洲），tokenVal 就绪后应用
    m_values.clear();
    m_macroVars.clear();
    fillDefaultMacroVars(m_macroVars);
    refreshTokenVals();
    applyCurrent();
}

const SliderDef* AvatarSolver::findSlider(const std::string& id) const {
    for (const auto& s : m_sliders)
        if (s.id == id)
            return &s;
    return nullptr;
}

double AvatarSolver::sliderValue(const std::string& id) const {
    const SliderDef* def = findSlider(id);
    if (!def) return 0.0;
    if (def->kind == SliderKind::Macro || def->kind == SliderKind::Ethnic) {
        auto it = m_macroVars.find(def->macrovar);
        return it == m_macroVars.end() ? 0.0 : it->second;
    }
    auto it = m_values.find(id);
    return it == m_values.end() ? 0.0 : it->second;
}

double AvatarSolver::macroVar(const std::string& name) const {
    auto it = m_macroVars.find(name);
    return it == m_macroVars.end() ? 0.0 : it->second;
}

void AvatarSolver::setSliderValue(const std::string& id, double v) {
    setValue(id, v);
}

void AvatarSolver::setValue(const std::string& id, double v) {
    const SliderDef* def = findSlider(id);
    if (!def) return;
    switch (def->kind) {
    case SliderKind::Normal:
        v = std::clamp(v, kNormalMin, kNormalMax);
        m_values[id] = v;
        break;
    case SliderKind::Single:
        v = std::clamp(v, kUnitMin, kUnitMax);
        m_values[id] = v;
        break;
    case SliderKind::Macro:
        v = std::clamp(v, kUnitMin, kUnitMax);
        m_macroVars[def->macrovar] = v;
        refreshTokenVals();
        break;
    case SliderKind::Ethnic:
        v = std::clamp(v, kUnitMin, kUnitMax);
        m_macroVars[def->macrovar] = v;
        normalizeEthnic(def->macrovar);
        refreshTokenVals();
        break;
    }
    applyCurrent();
}

void AvatarSolver::resetAll() {
    for (auto& [name, w] : m_values)
        w = 0.0;
    fillDefaultMacroVars(m_macroVars);
    refreshTokenVals();
    applyCurrent();
}

void AvatarSolver::resetSlidersOnly() {
    for (auto& [name, w] : m_values)
        w = 0.0;
    applyCurrent();
}

/// tokenVal 插值（apps/human.py _set*Vals 移植）。
/// 键 = 采样文件名 token（'male'/'female'/'baby'/'african'…），与 tokenCategories 一致。
void AvatarSolver::refreshTokenVals() {
    const auto g = m_macroVars.at("Gender");
    m_tokenVals["male"] = g;
    m_tokenVals["female"] = 1.0 - g;

    const double age = m_macroVars.at("Age");
    if (age < 0.5) {
        m_tokenVals["old"] = 0.0;
        m_tokenVals["baby"] = std::max(0.0, 1.0 - age * 5.333);
        m_tokenVals["young"] = std::max(0.0, (age - 0.1875) * 3.2);
        m_tokenVals["child"] = std::max(0.0, std::min(1.0, 5.333 * age)) - m_tokenVals["young"];
    } else {
        m_tokenVals["child"] = 0.0;
        m_tokenVals["baby"] = 0.0;
        m_tokenVals["old"] = std::max(0.0, age * 2.0 - 1.0);
        m_tokenVals["young"] = 1.0 - m_tokenVals["old"];
    }

    double maxVal = 0.0, minVal = 0.0, avgVal = 0.0;
    const struct { const char* var; const char *max, *min, *avg; } tri[] = {
        {"Weight", "maxweight", "minweight", "averageweight"},
        {"Muscle", "maxmuscle", "minmuscle", "averagemuscle"},
        {"Height", "maxheight", "minheight", "averageheight"},
        {"BreastSize", "maxcup", "mincup", "averagecup"},
        {"BreastFirmness", "maxfirmness", "minfirmness", "averagefirmness"},
        {"BodyProportions", "idealproportions", "uncommonproportions", "regularproportions"},
    };
    for (const auto& t : tri) {
        setTriVals(m_macroVars.at(t.var), maxVal, minVal, avgVal);
        m_tokenVals[t.max] = maxVal;
        m_tokenVals[t.min] = minVal;
        m_tokenVals[t.avg] = avgVal;
    }

    for (const char* e : {"african", "asian", "caucasian"}) {
        std::string key = std::string(e);
        std::string var = key;
        var[0] = char(std::toupper(static_cast<unsigned char>(var[0])));
        m_tokenVals[key] = m_macroVars.at(var);
    }
}

/// 人种归一化（apps/human.py _setEthnicVals 移植：其余人种按比例缩放到和=1）。
void AvatarSolver::normalizeEthnic(const std::string& exclude) {
    const std::string others[] = {"African", "Asian", "Caucasian"};
    const double remaining = 1.0 - m_macroVars[exclude];
    double otherTotal = 0.0;
    for (const auto& e : others)
        if (e != exclude)
            otherTotal += m_macroVars[e];
    if (otherTotal == 0.0) {
        if (m_macroVars[exclude] == 0.0) {
            for (const auto& e : others)
                m_macroVars[e] = kDefaultEthnic;
        } else if (std::fabs(m_macroVars[exclude] - 1.0) <= 0.001) {
            for (const auto& e : others)
                if (e != exclude)
                    m_macroVars[e] = 0.0;
        } else {
            for (const auto& e : others)
                if (e != exclude)
                    m_macroVars[e] = 0.01;
            normalizeEthnic(exclude);
            return;
        }
    } else {
        for (const auto& e : others)
            if (e != exclude)
                m_macroVars[e] = remaining * (m_macroVars[e] / otherTotal);
    }
}

void AvatarSolver::scanMacroSamplers() {
    m_macroSamplers.clear();
    const auto& cats = tokenCategories();
    const std::filesystem::path root(m_model->targetsDir());
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".bin" && ext != ".target") continue;
        auto noExt = entry.path();
        noExt.replace_extension(); // 移除 .bin/.target
        const std::string rel = std::filesystem::relative(noExt, root, ec).generic_string();
        if (ec) continue;
        const std::string stem = noExt.stem().string();
        // 文件名按 '-' 分词，命中 token 表即依赖；无 token 的普通 morph 文件跳过
        std::vector<std::string> hits;
        size_t pos = 0;
        while (pos <= stem.size()) {
            const size_t dash = stem.find('-', pos);
            const std::string seg = stem.substr(pos, dash == std::string::npos ? std::string::npos : dash - pos);
            if (seg == "target") { pos = (dash == std::string::npos ? stem.size() : dash + 1); continue; }
            if (cats.count(seg))
                hits.push_back(seg);
            if (dash == std::string::npos) break;
            pos = dash + 1;
        }
        if (hits.empty()) continue; // 非宏采样（普通 morph），不进采样表
        MacroSampler ms;
        ms.path = rel;
        ms.tokens = std::move(hits);
        m_macroSamplers.push_back(std::move(ms));
    }
    std::sort(m_macroSamplers.begin(), m_macroSamplers.end(),
              [](const MacroSampler& a, const MacroSampler& b) { return a.path < b.path; });
}

/// 展开滑杆值 -> morph 目标集（普通/单 morph 滑杆；宏走 getMacroWeights）。
void AvatarSolver::expandValues(const std::map<std::string, double>& values,
                                std::map<std::string, double>& merged) const {
    for (const auto& [id, v] : values) {
        const SliderDef* def = findSlider(id);
        if (!def) continue;
        if (def->kind == SliderKind::Normal) {
            // 0 权重也产出 key（权重 0 恒以 maxExt 侧出现），保证 applyDeltaWeights
            // 能算出负 diff 把旧权重归零；applyTargets 内 weight==0 会跳过，无副作用
            if (v >= 0.0)
                merged[def->target + "-" + def->maxExt] += v;
            else
                merged[def->target + "-" + def->minExt] += -v;
        } else if (def->kind == SliderKind::Single) {
            merged[def->target] += v;
        }
    }
    // 宏采样：权重 = Π tokenVal（MH getTargetWeights：value=1.0，滑杆值经 setter 已化为 tokenVal）
    for (const auto& ms : m_macroSamplers) {
        double w = 1.0;
        for (const auto& tok : ms.tokens) {
            const auto& tv = m_tokenVals;
            auto it = tv.find(tok);
            w *= (it != tv.end()) ? it->second : 0.0;
        }
        merged[ms.path] += w; // 0 也产出，理由同上（宏归零时需负 diff 移除）
    }
}

std::vector<TargetWeight> AvatarSolver::currentTargets() const {
    std::map<std::string, double> merged;
    expandValues(m_values, merged);
    std::vector<TargetWeight> out;
    out.reserve(merged.size());
    for (const auto& [key, weight] : merged)
        out.push_back({key, weight});
    return out;
}

void AvatarSolver::applyCurrent() {
    m_model->applyDeltaWeights(currentTargets());
}

double AvatarSolver::evaluateSlider(const std::string& id, double v) const {
    // 模拟设置权重后重测（不动成员状态）：快照合并展开（保留其他滑杆当前值）
    std::map<std::string, double> tmp = m_values;
    tmp[id] = v;
    std::map<std::string, double> merged;
    expandValues(tmp, merged);
    std::vector<TargetWeight> targets;
    targets.reserve(merged.size());
    for (const auto& [k, val] : merged)
        targets.push_back({k, val});
    m_model->applyTargets(targets);
    return measureNow(id);
}

double AvatarSolver::measureNow(const std::string& id) const {
    const SliderDef* def = findSlider(id);
    if (!def) return 0.0;
    if (def->measureKey.empty())
        return 0.0;
    // 带网格拓扑：距离类测量走贴肤测地线（绕开身体凸起，避免直线穿体低估）。
    return m_measures->computeOneMesh(def->measureKey, m_model->mesh());
}

double AvatarSolver::currentHeightCm() const {
    const auto& coords = m_model->mesh().verts;
    if (coords.empty()) return 0.0;
    double minY = coords[0].y, maxY = coords[0].y;
    for (const auto& v : coords) {
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }
    return (maxY - minY) * 10.0; // 0.1m 单位 -> cm
}

double AvatarSolver::solveSingle(const std::string& id, double targetCm) {
    const SliderDef* def = findSlider(id);
    if (!def || def->kind == SliderKind::Macro || def->kind == SliderKind::Ethnic)
        return 0.0;
    const double base = measureNow(id);
    if (std::fabs(base - targetCm) < kSingleTol)
        return sliderValue(id);

    // 二分法（MH MeasurementValueConverter displayToData 移植，纯二分化）：
    // 测量随权重单调，权重域 [-1,1]（normal）/[0,1]（single）。
    const double lo0 = (def->kind == SliderKind::Single) ? kUnitMin : kNormalMin;
    const double hi0 = (def->kind == SliderKind::Single) ? kUnitMax : kNormalMax;
    double lo = lo0, hi = hi0;
    double bestW = 0.0, bestErr = std::fabs(base - targetCm);
    for (int i = 0; i < kSingleIters; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double v = evaluateSlider(id, mid);
        const double err = std::fabs(v - targetCm);
        if (err < bestErr) {
            bestErr = err;
            bestW = mid;
        }
        if (v < targetCm)
            lo = mid; // 需要更大权重
        else
            hi = mid; // 需要更小权重
    }
    setValue(id, bestW);
    return bestW;
}

void AvatarSolver::setMeasureLock(const std::string& measureSliderId, bool locked,
                                 double targetCm) {
    if (!locked) {
        m_lockActive = false;
        return;
    }
    const SliderDef* def = findSlider(measureSliderId);
    if (!def || def->measureKey.empty() ||
        def->kind == SliderKind::Macro || def->kind == SliderKind::Ethnic)
        return; // 锁必须是可二分求解的测量滑杆（normal/single）
    m_lockSliderId = measureSliderId;
    m_lockTargetCm = targetCm;
    m_lockActive = true;
}

double AvatarSolver::enforceMeasureLock() {
    if (!m_lockActive || m_lockSliderId.empty())
        return 0.0;
    const double now = measureNow(m_lockSliderId);
    if (std::fabs(now - m_lockTargetCm) <= kSingleTol)
        return now; // 已在锁值，无需补偿
    solveSingle(m_lockSliderId, m_lockTargetCm); // 二分反调胸腔使测量回锁值
    return measureNow(m_lockSliderId);
}

double AvatarSolver::solveHeightCm(double targetCm) {
    // 身高随 Height 宏单调（实测 0→116.2 / 0.5→152.8 / 1→224.9 cm），二分。
    const std::string heightId = "macrodetails-height/Height";
    double lo = 0.0, hi = 1.0;
    double best = sliderValue(heightId);
    double bestErr = std::fabs(currentHeightCm() - targetCm);
    for (int i = 0; i < kSingleIters; ++i) {
        const double mid = 0.5 * (lo + hi);
        setValue(heightId, mid); // Macro：refreshTokenVals + applyCurrent
        const double h = currentHeightCm();
        const double err = std::fabs(h - targetCm);
        if (err < bestErr) {
            bestErr = err;
            best = mid;
        }
        if (h < targetCm)
            lo = mid; // 需要更高
        else
            hi = mid; // 需要更矮
    }
    setValue(heightId, best);
    return currentHeightCm();
}

void AvatarSolver::setHeightLock(bool locked, double targetCm) {
    if (!locked) {
        m_heightLockActive = false;
        return;
    }
    m_heightLockTargetCm = targetCm;
    m_heightLockActive = true;
}

double AvatarSolver::enforceHeightLock() {
    if (!m_heightLockActive)
        return 0.0;
    const double now = currentHeightCm();
    if (std::fabs(now - m_heightLockTargetCm) <= 0.05)
        return now;
    return solveHeightCm(m_heightLockTargetCm);
}

const char* AvatarSolver::cupLetter(double diffCm) {
    // 行业标准差值分段（欧码近似）：上胸围-下胸围。
    if (diffCm < 10.0) return "AA";
    if (diffCm < 12.5) return "A";
    if (diffCm < 15.0) return "B";
    if (diffCm < 17.5) return "C";
    if (diffCm < 20.0) return "D";
    if (diffCm < 22.5) return "E";
    if (diffCm < 25.0) return "F";
    return "G";
}

double AvatarSolver::solveNaturalBust(const std::string& bustId, double bustCm,
                                      const std::string& underId, double underCm,
                                      const std::string& cupId) {
    const double targetDiff = bustCm - underCm;
    if (targetDiff < 0.0)
        throw std::runtime_error("solveNaturalBust: bust must be >= underbust");
    const SliderDef* cup = findSlider(cupId);
    if (!cup || cup->kind != SliderKind::Macro)
        throw std::runtime_error("solveNaturalBust: cup slider must be a macro: " + cupId);

    // 1) 联合解两个周长到目标（Gauss-Seidel 二分）。
    solveMulti({{bustId, bustCm}, {underId, underCm}});
    // 2) 二分罩杯宏使差值 = 目标差值（实测差值随罩杯严格单调，域 [0,1]）。
    double lo = 0.0, hi = 1.0;
    double best = 0.5, bestErr = 1e30;
    for (int i = 0; i < kSingleIters; ++i) {
        const double mid = 0.5 * (lo + hi);
        setValue(cupId, mid); // Macro：refreshTokenVals + applyCurrent
        const double diff = measureNow(bustId) - measureNow(underId);
        const double err = std::fabs(diff - targetDiff);
        if (err < bestErr) {
            bestErr = err;
            best = mid;
        }
        if (diff < targetDiff)
            lo = mid; // 罩杯需要更大
        else
            hi = mid; // 罩杯需要更小
    }
    setValue(cupId, best);
    // 3) 罩杯微扰了两个周长，再联合修正一次。
    solveMulti({{bustId, bustCm}, {underId, underCm}});
    return measureNow(bustId) - measureNow(underId);
}

std::map<std::string, double> AvatarSolver::solveMulti(const std::map<std::string, double>& goals) {
    for (const auto& [id, goal] : goals) {
        const SliderDef* def = findSlider(id);
        if (!def || def->kind == SliderKind::Macro || def->kind == SliderKind::Ethnic)
            throw std::runtime_error("AvatarSolver: solveMulti target must be a measure slider: " + id);
    }
    // Gauss-Seidel：多轮，每轮对每个目标滑杆做单测量二分；联动靠轮次收敛
    for (int round = 0; round < kMultiRounds; ++round) {
        bool converged = true;
        for (const auto& [id, goal] : goals) {
            const double before = measureNow(id);
            if (std::fabs(before - goal) > kMultiTol) {
                solveSingle(id, goal);
                converged = false;
            }
        }
        if (converged) {
            bool all = true;
            for (const auto& [id, goal] : goals)
                if (std::fabs(measureNow(id) - goal) > kMultiTol)
                    all = false;
            if (all)
                break;
        }
    }
    std::map<std::string, double> out;
    for (const auto& [id, goal] : goals)
        out[id] = sliderValue(id);
    return out;
}

} // namespace cad::avatar
