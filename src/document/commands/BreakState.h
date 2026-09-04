#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/ParamPoint.h"

namespace cad::cmd {

enum class BreakMode {
    Formula,   // Line with formula: algebraic split (p*orig and (1-p)*orig)
    Freeze,    // Line with pure number: numeric split
    RefChain,  // Aux point defined relative to another reference point
};

/// BreakState 承载 redo 流水线各阶段间的中间状态（几何 → 位置 → 分配 →
/// 前段 → 后段 → 收尾）。函数间不传递裸指针：每阶段自行 re-acquire，
/// 避免 addPoint/erase 引起的指针失效。
struct BreakState {
    // --- 几何（阶段 1 输出） ---
    double segLenMm = 0.0;
    double localAngleDeg = 0.0;
    double worldAngleRad = 0.0;
    bool isCurve = false;
    cad::geo::Vec2 curveTanAtBreak;          // 断点处原始曲线切线（方向）
    std::vector<QUuid> frontPassIds;         // 归前段的 pass 点
    std::vector<cad::param::ParamPoint> backPassPoints;  // 归后段的 pass 点
    double backEndLocalAngle = 0.0;          // 后段终点在后段局部系的角度
    double curveFrontDist = 0.0;             // |start → 断点|（曲线真实距离）
    double curveBackDist = 0.0;              // |断点 → end|
    double curveBreakPolarAngleDeg = 0.0;    // start → 断点 的真实极角
    QHash<QUuid, cad::geo::Vec2> frozenTanIn;
    QHash<QUuid, cad::geo::Vec2> frozenTanOut;
    bool hasSubSpans = false;
    QHash<QUuid, cad::geo::Vec2> subTanInOverride;
    QHash<QUuid, cad::geo::Vec2> subTanOutOverride;
    double breakArc = 0.0;
    bool auxArcValid = false;
    QHash<QUuid, double> auxArc;
    double refDeltaRad = 0.0;

    // --- 位置求值（阶段 2 输出） ---
    BreakMode mode = BreakMode::Formula;
    QString origFormula;                     // 打断前的原长度公式（可能空）
    QString frontFormula;
    QString backFormula;
    double frontDistMm = 0.0;
    double backDistMm = 0.0;
    double breakAlong = 0.0;                 // 距起点的 mm 数
    QUuid polarRefId;                        // RefChain：Polar 锚点（空=冻结）
    double refOffsetMm = 0.0;
    QString refOffsetFormula;

    // --- 辅助点分配（阶段 3 输出） ---
    std::vector<QUuid> frontAuxIds;
    std::vector<cad::param::ParamPoint> backAuxPoints;

    // --- 后段构建（阶段 5 输出） ---
    cad::geo::Vec2 breakWorld;               // 断点世界坐标（后段原点）
    QUuid bpStartId, bpEndId, backSegId;
    double rotToLocal = 0.0;                 // 切线向量 原块系 → 后段系
    bool endPtKept = false;                  // 原端点仍被别的段引用（不删除）

    // --- 端点延长线 (EXTEND_LINE_DESIGN.md D8) ---
    double  origExtendEndMm = 0.0;
    QString origExtendEndFormula;
};

} // namespace cad::cmd
