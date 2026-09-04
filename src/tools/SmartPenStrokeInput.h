#pragma once

#include "LineFactory.h"   // LineBuildOptions
#include "LinePreInput.h"  // LinePreInput (共享定义)
#include "geometry/Vec2.h"

#include <QString>
#include <functional>

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// 智能笔的预输入值计算 (阶段 3 拆分): 只负责把状态栏传入的 LinePreInput
/// 解析成 StrokeInput (数值或公式), 计算约束端点, 装配 LineBuildOptions,
/// 以及"内容已被使用就清空"的一次性语义。
///
/// 设计约束 (过度拆分防护): 本类**不含状态** —— stroke 的几何状态
/// (startPoint/startSnap/leaderPicker/angleSnap) 全部留在 ToolSmartPen;
/// 每次调用传纯参数。依赖只有 doc (公式求值) 与一个 toast 回调。
///
/// ⚠️ 不直接持有 ToolSmartPen 状态: `apply` 用 `angleSnap` std::function 把
/// Shift 吸附的实现注入 (ToolSmartPen::applyAngleSnap), 避免本类反向依赖工具。
class SmartPenStrokeInput
{
public:
    using ToastFn = std::function<void(const QString&)>;

    explicit SmartPenStrokeInput(cad::param::ParamDocument* doc, ToastFn toast)
        : m_paramDoc(doc), m_toast(std::move(toast)) {}

    /// 解析 @p preInput 到内部 Value; 无效项 toast 并忽略 (不阻断 stroke)。
    void capture(const LinePreInput& preInput);

    [[nodiscard]] bool hasConstraint() const { return m_hasLength || m_hasAngle; }
    [[nodiscard]] bool hasLength() const { return m_hasLength; }
    [[nodiscard]] bool hasAngle() const { return m_hasAngle; }
    [[nodiscard]] double lengthMm() const { return m_lengthMm; }
    [[nodiscard]] const QString& lengthFormula() const { return m_lengthFormula; }
    [[nodiscard]] double displayAngleDeg() const { return m_displayAngleDeg; }
    [[nodiscard]] const QString& angleFormula() const { return m_angleFormula; }
    [[nodiscard]] const LinePreInput& raw() const { return m_raw; }

    /// 供 HUD 使用: 应用预输入约束时记录应显示的折角值 (原 m_snapAngleDeg)。
    [[nodiscard]] double currentDisplayAngle() const { return m_currentDisplayAngle; }
    void setCurrentDisplayAngle(double d) { m_currentDisplayAngle = d; }

    /// 应用预输入到光标: 角度已定 → 沿固定方向射线投影; 长度已定 → 沿用
    /// (角度吸附后的) 方向并缩放到长度。两者皆有 → 返回固定端点。
    /// @p angleSnapped 已由调用方按 Shift 吸附 (applyAngleSnap) 计算;
    /// @p worldAngleDeg 提供"显示角→世界角"的换算闭包 (附着/自由起点语义)。
    [[nodiscard]] cad::geo::Vec2 applyToCursor(
        const cad::geo::Vec2& cursor, const cad::geo::Vec2& startPoint,
        const cad::geo::Vec2& angleSnapped,
        const std::function<double()>& worldAngleDeg);

    /// 长度+角度皆确定 → 固定端点 (无需第二击)。
    [[nodiscard]] cad::geo::Vec2 fixedEnd(const cad::geo::Vec2& startPoint) const;

    /// 输入域角 → 世界角: 附着起点 = 跟随折角 (闭合基准 α = 180° − 相对角);
    /// 自由起点 = 绝对世界角。
    [[nodiscard]] double toWorldAngleDeg(double displayDeg, bool snappedStart,
                                         double refDirDeg) const;

    /// 装配 LineBuildOptions (传给 LineFactory)。
    [[nodiscard]] LineBuildOptions buildOptions(const QString& strokeName) const;

    /// 一次性消耗: 只清空本次真正生效的字段。仅当状态栏当前文本仍等于
    /// 快照时才清 —— 画线过程中输入新值保留给下一条线。
    void consume(LinePreInput& preInput);

    void reset();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    ToastFn m_toast;

    LinePreInput m_raw;
    bool   m_hasLength = false;
    double m_lengthMm = 0.0;       ///< Evaluated length (mm).
    QString m_lengthFormula;
    bool   m_hasAngle = false;
    double m_displayAngleDeg = 0.0;
    QString m_angleFormula;
    double m_currentDisplayAngle = 0.0;  ///< HUD 显示角 (applyToCursor 更新).
};

} // namespace cad::tools
