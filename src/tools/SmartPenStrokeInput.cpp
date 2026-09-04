#include "SmartPenStrokeInput.h"

#include "LineFactory.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ParamDocument.h"

#include <cmath>

namespace cad::tools {

void SmartPenStrokeInput::capture(const LinePreInput& preInput)
{
    m_raw = preInput;
    m_hasLength = false;
    m_lengthMm = 0.0;
    m_lengthFormula.clear();
    m_hasAngle = false;
    m_displayAngleDeg = 0.0;
    m_angleFormula.clear();

    const QString lenText = preInput.lengthCm.trimmed();
    if (!lenText.isEmpty()) {
        const auto parsed = cad::geo::parseNumberOrFormula(lenText);
        if (parsed.isNumber) {
            m_hasLength = true;
            m_lengthMm = cad::geo::Units::cmToMm(parsed.value);
        } else if (m_paramDoc) {
            if (cad::param::ConditionEngine::evaluateLengthMm(
                    parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions(),
                    m_lengthMm)) {
                m_hasLength = true;
                m_lengthFormula = parsed.formula;
            } else if (m_toast) {
                m_toast(QString::fromUtf8("预输入长度无法计算，已忽略"));
            }
        }
    }

    const QString angText = preInput.angleDeg.trimmed();
    if (!angText.isEmpty()) {
        const auto parsed = cad::geo::parseNumberOrFormula(angText);
        if (parsed.isNumber) {
            m_hasAngle = true;
            m_displayAngleDeg = parsed.value;
        } else if (m_paramDoc) {
            auto r = cad::param::ConditionEngine::evaluate(
                parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
            if (r.ok) {
                m_hasAngle = true;
                m_displayAngleDeg = r.value;
                m_angleFormula = parsed.formula;
            } else if (m_toast) {
                m_toast(QString::fromUtf8("预输入角度无法计算，已忽略"));
            }
        }
    }
}

cad::geo::Vec2 SmartPenStrokeInput::applyToCursor(
    const cad::geo::Vec2& cursor, const cad::geo::Vec2& startPoint,
    const cad::geo::Vec2& angleSnapped,
    const std::function<double()>& worldAngleDeg)
{
    if (m_hasAngle && m_hasLength)
        return fixedEnd(startPoint);

    if (m_hasAngle) {
        // 角度已定: 光标沿固定方向射线投影, 第二击只决定长度。
        const double rad = worldAngleDeg() * M_PI / 180.0;
        const cad::geo::Vec2 dir(std::cos(rad), std::sin(rad));
        double t = (cursor - startPoint).dot(dir);
        if (t < 0.0) t = 0.0;
        m_currentDisplayAngle = m_displayAngleDeg;
        return startPoint + dir * t;
    }

    // 长度已定: 沿用 (可 Shift 吸附的) 光标方向, 距离固定。
    const cad::geo::Vec2 delta = angleSnapped - startPoint;
    const double dist = delta.length();
    if (dist > 1e-12)
        return startPoint + delta * (m_lengthMm / dist);
    return angleSnapped;
}

cad::geo::Vec2 SmartPenStrokeInput::fixedEnd(const cad::geo::Vec2& startPoint) const
{
    // 长度+角度皆确定: 唯一端点 (m_displayAngleDeg 已是输入域角)。
    const double rad = m_displayAngleDeg * M_PI / 180.0;
    return startPoint
        + cad::geo::Vec2(std::cos(rad), std::sin(rad)) * m_lengthMm;
}

double SmartPenStrokeInput::toWorldAngleDeg(double displayDeg, bool snappedStart,
                                            double refDirDeg) const
{
    // 附着起点: 显示角 = 跟随折角 (闭合基准 α = 180° − 相对角);
    // 自由起点: 显示角 = 绝对世界角。
    if (snappedStart)
        return refDirDeg + (180.0 - displayDeg);
    return displayDeg;
}

LineBuildOptions SmartPenStrokeInput::buildOptions(const QString& strokeName) const
{
    LineBuildOptions opts;
    opts.name = strokeName.trimmed();
    if (m_hasLength) {
        opts.hasLength = true;
        opts.lengthMm = m_lengthMm;
        opts.lengthFormula = m_lengthFormula;
    }
    if (m_hasAngle) {
        opts.hasAngle = true;
        opts.displayAngleDeg = m_displayAngleDeg;
        opts.angleFormula = m_angleFormula;
    }
    return opts;
}

void SmartPenStrokeInput::consume(LinePreInput& preInput)
{
    auto consume = [](QString& current, const QString& used, bool usedThisTime) {
        if (usedThisTime && !used.isEmpty() && current == used)
            current.clear();
    };
    consume(preInput.name, m_raw.name, true);
    consume(preInput.lengthCm, m_raw.lengthCm, m_hasLength);
    consume(preInput.angleDeg, m_raw.angleDeg, m_hasAngle);
    reset();
}

void SmartPenStrokeInput::reset()
{
    m_raw = LinePreInput{};
    m_hasLength = false;
    m_lengthMm = 0.0;
    m_lengthFormula.clear();
    m_hasAngle = false;
    m_displayAngleDeg = 0.0;
    m_angleFormula.clear();
}

} // namespace cad::tools
