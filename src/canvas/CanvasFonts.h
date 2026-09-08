#pragma once

#include <QFont>
#include <QString>

/// 画布内标签字体的唯一来源（2026-12 审计 P1-8 收口）。
/// 构造 QFont 代价高（字体引擎解析），逐帧复用的固定字号一律取静态实例；
/// 像素字号设备无关，取值与 Theme 字号阶梯 10/11/12/13/15/18 对齐。
/// 注意：等宽族必须用 setFamilies 传族列表——"Consolas, monospace" 这类
/// 逗号串会被当成单个族名而静默回退到默认字体。
namespace canvas_fonts {

/// 通用 10px 标签（点名 / 编号）。
inline const QFont& nameFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(10); return fnt; }();
    return f;
}

/// 通用 11px 标签（长度 / 说明）。
inline const QFont& labelFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(11); return fnt; }();
    return f;
}

/// 长度读数：等宽数字，拖拽读数不抖动。
inline const QFont& lengthFont()
{
    static QFont f = [] {
        QFont fnt;
        fnt.setFamilies({QStringLiteral("Consolas"),
                         QStringLiteral("Courier New"),
                         QStringLiteral("monospace")});
        fnt.setPixelSize(10);
        return fnt;
    }();
    return f;
}

/// HUD / 徽标：等宽读数（可指定像素字号与粗体）。
inline QFont monoFont(int pixelSize, bool bold = false)
{
    QFont f;
    f.setFamilies({QStringLiteral("Consolas"),
                   QStringLiteral("Courier New"),
                   QStringLiteral("monospace")});
    f.setPixelSize(pixelSize);
    f.setBold(bold);
    return f;
}

/// 界面字体（点字号版）：仅用于以「点」定尺寸的徽标（9pt ≈ 12px@96dpi，与像素
/// 字号语义不同，勿混用）；族名与 uiFont 同源。
inline QFont uiFontPt(int pointSize, bool bold = false)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSize(pointSize);
    f.setBold(bold);
    return f;
}

/// HUD / 徽标：界面字体（可指定像素字号与粗体）。
inline QFont uiFont(int pixelSize, bool bold = false)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(pixelSize);
    f.setBold(bold);
    return f;
}

} // namespace canvas_fonts
