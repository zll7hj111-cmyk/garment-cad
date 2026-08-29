#pragma once

#include <QPainter>
#include <QPointF>
#include <QPen>
#include <QColor>

#include <cmath>

/// 线段方向指示 (2026-12): 起点 → 终点 的小箭头 (chevron), 画在段中点旁。
///
/// 动机: ReverseSegmentCommand (线段换向) 是"物理换身"——几何零跳变、点标签
/// 按身份绑定不移动, 画布上原本没有任何可见变化 ("点了换向什么都没发生").
/// 方向箭头派生自 start→end, 换向后缓存重算 → 箭头自动翻转, 让换向可见。
///
/// 角度约定: scene 坐标 (Y 向下), 与 BlockItem/CurveItem 标签一致 —— 直线用
/// atan2(p2 − p1), 曲线用 CurveSpanEntry::labelLocalDir 经 BlockItem 预转的
/// labelAngle (已做 Y-flip 补偿)。
inline void drawDirectionChevron(QPainter* painter, const QPointF& mid,
                                 double angleRad, const QColor& color)
{
    const double dirX = std::cos(angleRad);
    const double dirY = std::sin(angleRad);
    const double px = -dirY;   // 垂向单位向量 (scene Y 向下)
    const double py =  dirX;

    constexpr double kArm = 4.0;    ///< 臂长 (scene 单位, cosmetic 画笔)
    constexpr double kOff = 5.0;    ///< 离线距离 (贴着线但不压线)
    constexpr double kSpread = 25.0 * M_PI / 180.0;

    // 顶点 = 中点 + 垂向偏移 + 一个臂长的前进量; 两臂从顶点回开 ±kSpread。
    const QPointF tip(mid.x() + px * kOff + dirX * kArm,
                      mid.y() + py * kOff + dirY * kArm);
    const double bx = -dirX, by = -dirY;   // 回开基准方向
    const double c = std::cos(kSpread), s = std::sin(kSpread);
    // 回开方向旋转 ±kSpread
    const double lx = bx * c - by * s, ly = bx * s + by * c;
    const double rx = bx * c + by * s, ry = -bx * s + by * c;

    QPen pen(color, 1.0);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->drawLine(tip, QPointF(tip.x() + lx * kArm, tip.y() + ly * kArm));
    painter->drawLine(tip, QPointF(tip.x() + rx * kArm, tip.y() + ry * kArm));
}
