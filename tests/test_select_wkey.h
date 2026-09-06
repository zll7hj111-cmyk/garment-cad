#pragma once

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QStandardItemModel>
#include <QMenu>

#include <cmath>
#include <algorithm>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "canvas/HudItem.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "ElaText.h"
#include "ElaPushButton.h"
#include "ui/LinePropertyDialog.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentRefCard.h"
#include "ui/PointRefEdit.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"   // ReverseSegmentCommand (换向不解耦验证)
#include "parametric/ParamDocument.h"
#include "parametric/FormulaVariable.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "TestHelpers.h"
#include "geometry/CurveMath.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::layerIdAt;

/// End-to-end regression test for the select tool's W key (多选 ↔ 单选 toggle):
/// a REAL QKeyEvent travels CanvasView::keyPressEvent → ToolManager::dispatch
/// → ToolSelect::keyPress, and the resulting mode change is verified through
/// the observable click behaviour (Single replaces, Multi adds).
class TestSelectWKey : public QObject
{
    Q_OBJECT

private slots:
    void wTogglesMultiSelectionThroughFullEventChain();
    void ctrlDragCopiesAfterConfirm();
    void ctrlDragUnselectedBlockDirectly();
    void ctrlClickJitterDoesNotDuplicate();
    void ctrlDragUndoRedo();
    void multiSelectMarqueeSelectsBothLines();
    void endpointClickAfterConfirmKeepsSelectionOperable();
    void endpointDragConnectsToTargetEndToEnd();
    void clickSelectsWithoutDragging();
    void singleModeOverlapPrefersSelectedSegmentPoint();
    void singleModeOverlapWinsEvenWhenOtherPointCloser();
    void multiModeOverlapEntersSourceConfirm();
    void unselectedEndpointPressFallsBackToSelect();
    void bodyDragMovesLine();
    void quickDetachKeyD();
    void angleOnlyEndpointDragReconnects();
    // ── 有对齐点方向 + 变量时接入新线段不覆盖变量 (2026-xx 用户报告:
    //    有对齐点方向时接入新线段 = 仅连接, 原对齐点方向与变量不变,
    //    回车 = 确定连接而非确定角度) ──
    void angleRefWithFormulaReattachKeepsFormula();
    // ── 连接角度会话 (二期: 角度输入并入上下文属性条, AngleHud 退场) ──
    void angleSessionStripInputDrivesConnection();
    // ── 重叠线段消歧 (2026-10) ──
    void overlapHoverShowsClusterHint();
    void overlapClickCyclesWithWKey();
    void overlapPickCandidateByIndex();
    // ── 曲线命中/选择 (2026-10 用户报告: 选择工具对曲线的选中判定比较迷) ──
    void curveBodyClickSelects();
    void curveBodyDragMoves();
    // ── 双击打开线条属性面板: 交叉点处活跃层优先 (2026-11 用户报告:
    //    辅助层线段双击时灵时不灵) ──
    void doubleClickCrossingPrefersActiveLayer();
    // ── 连接卡片两维独立 (2026-xx 用户拍板: 双 拆开/重连 按钮) ──
    void connectionCardNewSemantics();
    // ── 角度基准两点化 — 点2 输入框 (2026-08-31 修复「点2 完全无效」) ──
    void angleRefPoint2TwoPointBasis();
    // ── 对齐点可输入 + 自动态两点回填 (2026-09 设计修正) ──
    void alignPointEditableAndAutoStateTwoPointBackfill();
    // ── 终点指向超出延伸 (2026-09 规则表 ⑤): 指定长度 > 目标距离时终点
    //    越过目标点、方向保持指向角 ──
    void endTargetOvershootExtendsAlongAim();
    // ── 终点连接行 (2026-xx 每端完整连接: 起点 Attachment + 终点 endTarget) ──
    void connectionCardEndConnection();
    // ── 选择工具多选移动图层 ──
    void multiSelectMoveToLayer();
    void multiSelectRightClickMoveToLayerMenu();
    // ── 辅助点挂载 D 键快拆 ──
    void quickDetachAuxPointMountWithDKey();
};

/// Strongly-curved Bezier block (off-chord anchor 45mm) on the active layer.
/// The control polygon (start→anchor→end) deviates a lot from the actual
/// drawn curve — a pick region based on it misses clicks on the curve body.
struct CurveSetup {
    QUuid blockId;
    QUuid segId;
};

inline CurveSetup makeCurveBlock(ParamDocument& doc)
{
    Block block;
    block.transform.origin = Vec2::zero();
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid p1Id = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1Id;
    p2.distance = 120.0;
    p2.angle = 0.0;
    QUuid p2Id = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    seg.tension = 0.0;
    QUuid segId = seg.id;

    ParamPoint pp;
    pp.constraint = PointConstraint::CurveAnchor;
    pp.hostSegmentId = segId;
    pp.interpPercent = 0.5;
    pp.interpOffsetDist = 90.0;   // 90mm off-chord: strong curvature
    pp.autoTangent = true;
    QUuid ppId = pp.id;

    block.addPoint(std::move(pp));
    seg.passPointIds = {ppId};
    QUuid blockId = block.id;
    block.addSegment(std::move(seg));
    doc.addBlock(std::move(block));
    doc.resolveAll();
    return {blockId, segId};
}

/// Create a straight line block on an EXPLICIT layer (addBlock with a null
/// layer would land on the first working layer, never the aux calc layer):
/// Free start at @p origin, Polar end (angleDeg, lenMm). Returns block id.
inline QUuid addLineOnLayer(ParamDocument& doc, const QUuid& layer,
                            const Vec2& origin, double angleDeg, double lenMm)
{
    Block block;
    block.layer = layer;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid s = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = s;
    p2.distance = lenMm;
    p2.angle = angleDeg;
    QUuid e = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = s;
    seg.endPointId = e;
    block.addSegment(std::move(seg));

    QUuid id = block.id;
    doc.addBlock(std::move(block));
    return id;
}

/// Curve point at parametric position t (0..spanCount of the WHOLE curve), world coords.
inline Vec2 curvePointAt(const Block& b, const QUuid& segId, double t){
    const auto* entry = b.curveSpanEntry(segId);
    Q_ASSERT(entry && !entry->spans.empty());
    return b.transform.toWorld(cad::geo::evalCurve(entry->spans, t));
}

/// 数值采样整条曲线, 返回“距控制折线最远”的曲线点 (transform 必须为恒等 —
/// 测试夹具的块都在原点). 那正是旧“控制折线命中区”漏掉的位置: 点在曲线上
/// 但距折线边 > 8px → items() 命中失败.
inline Vec2 curveWorstHitPoint(const Block& b, const QUuid& segId, double* distOut = nullptr)
{
    const auto* entry = b.curveSpanEntry(segId);
    Q_ASSERT(entry && entry->spans.size() >= 2);
    const auto& anchors = entry->anchors;
    auto distToSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        const Vec2 d = s1 - s0;
        const double l2 = d.lengthSquared();
        if (l2 < 1e-12) return p.distanceTo(s0);
        const double t = std::clamp((p - s0).dot(d) / l2, 0.0, 1.0);
        return p.distanceTo(s0 + d * t);
    };
    const double spanCount = static_cast<double>(entry->spans.size());
    Vec2 best = curvePointAt(b, segId, 0.5);
    double bestD = -1.0;
    for (int k = 1; k < 200; ++k) {
        const Vec2 p = curvePointAt(b, segId, spanCount * static_cast<double>(k) / 200.0);
        double d = 1e18;
        for (size_t i = 0; i + 1 < anchors.size(); ++i)
            d = std::min(d, distToSeg(p, anchors[i], anchors[i + 1]));
        if (d > bestD) { bestD = d; best = p; }
    }
    if (distOut) *distOut = bestD;
    return best;
}
