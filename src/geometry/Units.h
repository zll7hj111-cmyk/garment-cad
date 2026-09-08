#pragma once

#include <QString>
#include <QPointF>

#include "Vec2.h"

namespace cad::geo {

/// Coordinate-system boundary helpers.
///
/// User coordinates  : +Y up   (engineering / DXF convention, stored in entities)
/// Scene coordinates : +Y down (Qt QGraphicsScene native)
///
/// Conversion is a pure Y-negation; X is unchanged.
/// Apply these ONLY at the boundary between the document/tool layer and the
/// Qt graphics layer — never inside either layer alone.
struct Coord {
    static constexpr double toSceneY(double userY) { return -userY; }
    static constexpr double toUserY(double sceneY) { return -sceneY; }

    static QPointF toScene(const Vec2& p) { return {p.x, -p.y}; }
    static QPointF toScene(double ux, double uy) { return {ux, -uy}; }

    static Vec2 toUser(const QPointF& p) { return {p.x(), -p.y()}; }
    static Vec2 toUser(double sx, double sy) { return {sx, -sy}; }
};

/// Unit conversion utilities.
/// Internal unit: millimeter (mm).
/// Display unit: centimeter (cm) by default.
struct Units {
    // --- Conversion factors (relative to mm) ---
    static constexpr double MM_PER_CM   = 10.0;
    static constexpr double MM_PER_INCH = 25.4;

    // --- mm <-> cm ---
    static constexpr double mmToCm(double mm) { return mm / MM_PER_CM; }
    static constexpr double cmToMm(double cm) { return cm * MM_PER_CM; }

    // --- mm <-> inch ---
    static constexpr double mmToInch(double mm) { return mm / MM_PER_INCH; }
    static constexpr double inchToMm(double inch) { return inch * MM_PER_INCH; }

    // --- Formatted display strings ---
    //
    // 显示格式契约（审计 P1-6 / §3.6，2026-12 收口）：同一物理量只允许一种
    // 显示精度，且 mm→cm 与数值格式化一律走这里，调用点禁止裸
    // `QString::number(..., 'f', N)`。
    //   * 长度：2 位小数 + 去尾零（"12.5" / "12"）；`formatCm` 不带单位
    //     （数值框 / 卡片值标签），`formatLength` 带 " cm"（句子、提示、
    //     量测结果）。
    //   * 角度：1 位小数 + 去尾零；`formatDegValue` 不带后缀，
    //     `formatDegTrimmed` 带 "°"。
    //   * 纯数字：`formatNumberTrimmed`（默认 2 位，可指定）。

    /// 长度显示的唯一格式化器：mm → cm，2 位小数去尾零，不带单位后缀
    /// （"12.5" / "12"）。
    static QString formatCm(double mm) {
        return trimTrailingZeros(QString::number(mmToCm(mm), 'f', 2));
    }

    /// 长度 + 显示单位（"12.5 cm"）。与 `formatCm` 同一精度约定——刻意不提供
    /// precision 参数（审计 P1-6：同一物理量只允许一种精度）。
    static QString formatLength(double mm) {
        return formatCm(mm) + QStringLiteral(" cm");
    }

    /// Format a coordinate pair (mm) as cm string. 固定 2 位小数（不去尾零）
    /// 是刻意的：状态栏读数宽度必须稳定，光标移动时不能跳动。
    static QString formatPoint(double xMm, double yMm, int precision = 2) {
        return QStringLiteral("X: %1  Y: %2 cm")
            .arg(mmToCm(xMm), 0, 'f', precision)
            .arg(mmToCm(yMm), 0, 'f', precision);
    }

    // --- Trimmed display strings (card value labels) ---

    /// Trim trailing zeros (and a trailing decimal point) from a formatted
    /// number string, e.g. "12.50" -> "12.5", "12.00" -> "12".
    static QString trimTrailingZeros(QString s) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
        return s;
    }

    /// 纯数字（无单位量）：默认 2 位小数、去尾零（"12.5"）。长度走
    /// `formatCm`、角度走 `formatDegValue`，不要用本函数转单位。
    static QString formatNumberTrimmed(double v, int decimals = 2) {
        return trimTrailingZeros(QString::number(v, 'f', decimals));
    }

    /// Format a degree value with trailing zeros trimmed and a degree suffix
    /// (e.g. "12.5°"). Shared by the angle-measure card value label.
    static QString formatDegTrimmed(double deg) {
        return trimTrailingZeros(QString::number(deg, 'f', 1)) + QChar(0x00B0);
    }

    /// Format a degree value with 1 decimal, trailing ".0" trimmed, WITHOUT a
    /// degree suffix (e.g. "22" / "22.5"). Shared by the rotate HUD and the
    /// segment property dialogs (formerly local formatDeg/formatAngleDeg).
    static QString formatDegValue(double deg) {
        return trimTrailingZeros(QString::number(deg, 'f', 1));
    }
};

/// 共享「数值或公式」输入解析入口 (2026-08-28 收口 A6).
///
/// 原散落于 ToolSmartPen/SegmentEditBar/SegmentAngleCard/SegmentExtendCard/
/// ConnectGesture/LinePropertyDialog 的两段式判读:
///   `bool ok; double v = text.toDouble(&ok);` 成功走数值、失败走公式。
/// 本结构只做"判读 + 保留原文", 不做单位换算 (数值域按调用点语义:
/// 长度=cm、角度=度 —— 由调用方各自 cmToMm 等, 不强行统一)。
struct ParsedNumberOrFormula
{
    bool isNumber = false;   ///< text 整体可解析为数值.
    double value = 0.0;      ///< isNumber 时的数值 (原始输入单位: cm 或 度).
    QString formula;         ///< 非数值时的原样输入串 (用户公式, 含空白 trim).
};

inline ParsedNumberOrFormula parseNumberOrFormula(const QString& text)
{
    ParsedNumberOrFormula out;
    out.formula = text.trimmed();
    out.value = out.formula.toDouble(&out.isNumber);
    return out;
}

/// 角度文本判读入口 (2026-12 审计 UI-P0-7 收口).
///
/// 与 `parseNumberOrFormula` 同构, 但数值判读前剥掉度数符号 "°" (U+00B0):
/// 显示侧 `formatDegTrimmed` 与卡片标签会带后缀, 输入框必须能原样回读,
/// 否则回车会被判成公式并静默回滚 (端点偏移框即为此 bug)。公式原文**不剥**
/// °（公式求值器自行处理）。
inline ParsedNumberOrFormula parseAngleText(const QString& text)
{
    ParsedNumberOrFormula out;
    out.formula = text.trimmed();
    QString numText = out.formula;
    numText.remove(QChar(0x00B0));
    out.value = numText.toDouble(&out.isNumber);
    return out;
}

} // namespace cad::geo
