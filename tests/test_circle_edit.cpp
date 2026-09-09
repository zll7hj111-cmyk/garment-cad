/// @file test_circle_edit.cpp
/// M2 acceptance for editing a parametric circle from the properties panel
/// (docs/design/CIRCLE_TOOL_DESIGN.md §6/§7, decisions D2/D13/D17/D18/D19/D16).
///
/// M1 (test_circle_fit.cpp) locked the engine: a Bezier segment with
/// fitKind == Circle resolves as a true circle. M2 adds the panel + rendering:
///   ① R/D/C 三向联动: 直径/周长输入换算成半径 (公式按原式除以 2 / 2π);
///   ② 基准角 a0 写起点 Polar 角度, 圆心不动, 包角保持不变 (D18);
///   ③ 包角写终点角度 = a0 + 包角, **绝不归一化** (整圆 360 存成 a0+360);
///   ④ 圆度 = Segment::tension, 夹紧 [−1, +0.25] (D17);
///   ⑤ LineGeometrySection 对圆段早退 (不写终点距离/张力);
///   ⑥ LinePropertySession 快照/提交/撤销覆盖起点距离与两端角度;
///   ⑦ D16: 圆度 0 的圆段绘制路径是**解析**圆弧 (采样径向误差 ~1e-9·r),
///      圆度 ≠ 0 时按真实 Bézier 逐跨画 (不能被画成圆);
///   ⑧ 非圆曲线保持折线绘制路径 (paintPath 为空)。
///
/// Run: test_circle_edit

#include <QtTest>
#include <QApplication>
#include <QUndoStack>
#include <QPainterPath>
#include <QPushButton>
#include <cmath>

#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/FormulaVariable.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "canvas/CurveItem.h"
#include "tools/ToolManager.h"
#include "tools/HitTester.h"
#include "tools/SnapEngine.h"
#include "geometry/CurveMath.h"
#include "ui/LinePropertyDialog.h"
#include "ui/PlacedPointDialog.h"
#include "document/DocumentSerializer.h"
#include "document/CommandTexts.h"
#include "tools/CircleFactory.h"
#include "ui/CircleGeometrySection.h"
#include "ui/LineGeometrySection.h"
#include "ui/LinePropertySession.h"
#include "ElaLineEdit.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

struct CircleIds {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
};

CircleIds makeCircle(ParamDocument& doc, double radiusMm,
                     double startAngleDeg = 0.0,
                     const Vec2& center = Vec2(0.0, 0.0))
{
    CircleIds ids;
    cad::tools::CircleFactory factory(&doc, doc.undoStack());
    ids.blockId = factory.createCircle(center, radiusMm, startAngleDeg);
    if (Block* b = doc.findBlock(ids.blockId); b && !b->segments.empty()) {
        ids.segId = b->segments.front().id;
        ids.startId = b->segments.front().startPointId;
        ids.endId = b->segments.front().endPointId;
        if (const ParamPoint* sp = b->findPoint(ids.startId))
            ids.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return ids;
}

/// 触发 QLineEdit::editingFinished —— 面板的换算槽就挂在这个信号上,
/// 直接 setText() 不会走换算 (真实用户输入才会)。
void fireEditingFinished(ElaLineEdit* edit)
{
    QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
}

double radiusOf(const Block& b, const CircleIds& ids)
{
    const ParamPoint* sp = b.findPoint(ids.startId);
    return sp ? sp->distance : 0.0;
}

double sweepDegOf(const Block& b, const CircleIds& ids)
{
    const ParamPoint* sp = b.findPoint(ids.startId);
    const ParamPoint* ep = b.findPoint(ids.endId);
    return (sp && ep) ? (ep->angle - sp->angle) : 0.0;
}

/// 采样一条 QPainterPath, 返回相对给定圆心的最大径向误差。
double maxRadialError(const QPainterPath& path, double radiusMm,
                      const QPointF& center = QPointF(0, 0), int samples = 720)
{
    double worst = 0.0;
    for (int i = 0; i < samples; ++i) {
        const QPointF p = path.pointAtPercent(double(i) / double(samples));
        const double d = std::hypot(p.x() - center.x(), p.y() - center.y());
        worst = std::max(worst, std::abs(d - radiusMm));
    }
    return worst;
}

CurveItem* firstCurveItem(CanvasScene& scene, const QUuid& blockId)
{
    auto* item = scene.findBlockItem(blockId);
    if (!item) return nullptr;
    // CurveItem 是 QGraphicsObject, 但它的 QObject 父对象**不是** BlockItem
    // (QGraphicsObject(QGraphicsItem*) 只设 QGraphicsItem 父项), 所以不能用
    // findChildren<CurveItem*>() —— 必须走 childItems()。
    for (auto* child : item->childItems()) {
        if (auto* go = child->toGraphicsObject())
            if (auto* ci = qobject_cast<CurveItem*>(go)) return ci;
    }
    return nullptr;
}

} // namespace

class TestCircleEdit : public QObject
{
    Q_OBJECT

private slots:
    void diameterAndCircumferenceConvertToRadius();
    void radiusFormulaIsDerivedFromDandC();
    void baseAngleRotatesSeamAndKeepsSweep();
    void sweepHalfCircleArcAndChord();
    void sweepFullCircleIsNeverNormalized();
    void roundnessIsClamped();
    void lineGeometrySectionSkipsCircleSegments();
    void sessionUndoRedoRestoresCircleEdits();
    void circlePaintPathIsAnalytic();
    void deformedCircleIsNotPaintedAsCircle();
    void plainCurveKeepsFlatPaintPath();
    void detachButtonDetachesAndSignals();
    void circleArcLengthIsAnalytic();
    void publishCircumferenceVariableIsExactAndReferenceable();
    void doubleClickOnCircleArcOpensPropertyDialog();
    void circleAnchorsStayOnPaintedArc();
    void circleGuideFollowsSeamAndSweep();
    void curveAnchorAcceptsFollowerLine();
};

// ③ 排查: 用户报告「只有粉色点才能打开面板」。本用例走真实输入链路
// (CanvasView + ToolManager + 合成鼠标事件) 验证弧身双击是否打开圆属性面板。
void TestCircleEdit::doubleClickOnCircleArcOpensPropertyDialog()
{
    ParamDocument doc;
    // CanvasScene 必须在建圆之前构造: 它靠 ParamDocument::blockAdded 信号补建
    // BlockItem, 构造之前已存在的块不会补建 (与 test_select_wkey_cards 同构)。
    CanvasScene scene(&doc);
    scene.setSceneRect(-200.0, -200.0, 400.0, 400.0);
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);


    auto doubleClickWorld = [&](double wx, double wy) {
        const QPoint hit = view.mapFromScene(QPointF(wx, -wy));
        const QPoint global = view.viewport()->mapToGlobal(hit);
        auto send = [&](QEvent::Type type) {
            QMouseEvent ev(type, hit, global, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
            QApplication::sendEvent(view.viewport(), &ev);
        };
        send(QEvent::MouseButtonPress);
        send(QEvent::MouseButtonRelease);
        send(QEvent::MouseButtonDblClick);
        send(QEvent::MouseButtonRelease);
    };
    auto visibleLineDialogs = [&]() {
        QList<cad::ui::LinePropertyDialog*> out;
        for (auto* d : view.findChildren<cad::ui::LinePropertyDialog*>())
            if (d->isVisible()) out.append(d);
        return out;
    };
    auto visiblePointDialogs = [&]() {
        QList<cad::ui::PlacedPointDialog*> out;
        for (auto* d : view.findChildren<cad::ui::PlacedPointDialog*>())
            if (d->isVisible()) out.append(d);
        return out;
    };
    // close() 只是隐藏; 对话框没有 WA_DeleteOnClose, 必须显式 deleteLater 才不会
    // 被后续 findChildren 反复数到。
    auto closeAll = [&]() {
        for (auto* d : view.findChildren<cad::ui::LinePropertyDialog*>()) {
            d->close();
            d->deleteLater();
        }
        for (auto* d : view.findChildren<cad::ui::PlacedPointDialog*>()) {
            d->close();
            d->deleteLater();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };

    struct Probe { double wx; double wy; int expect; const char* what; };
    // expect: 1 = 必须打开 LinePropertyDialog, 0 = 必须不打开, -1 = 只记录当前行为
    auto runProbes = [&](const Probe* probes, int n) {
        for (int i = 0; i < n; ++i) {
            const Probe& p = probes[i];
            doubleClickWorld(p.wx, p.wy);
            const auto lineDialogs = visibleLineDialogs();
            const auto pointDialogs = visiblePointDialogs();
            qInfo("双击 %s: lineDialogs=%d pointDialogs=%d", p.what,
                  int(lineDialogs.size()), int(pointDialogs.size()));
            if (p.expect >= 0) QCOMPARE(int(lineDialogs.size()), p.expect);
            if (p.expect == 1) {
                QCOMPARE(lineDialogs.first()->targetBlockId(), ids.blockId);
                QCOMPARE(lineDialogs.first()->targetSegmentId(), ids.segId);
            }
            QVERIFY2(pointDialogs.isEmpty(), p.what);
            closeAll();
        }
    };
    auto atDeg = [](double deg) {
        const double th = cad::geo::degToRad(deg);
        return std::pair<double, double>(50.0 * std::cos(th), 50.0 * std::sin(th));
    };

    // 第一轮: 工具刚建出来的**整圆** (包角 360)。
    {
        const auto a60 = atDeg(60.0), a200 = atDeg(200.0), a90 = atDeg(90.0);
        const Probe probes[] = {
            {a60.first, a60.second, 1, "整圆 弧身 60°"},
            {a200.first, a200.second, 1, "整圆 弧身 200°"},
            {a90.first, a90.second, 1, "整圆 粉色锚点 90°"},
            // m01094 ⑤: 圆心是圆的附属对象但必须能双击开面板 (它不在弧上,
            // 到弧距离 = 半径 ≫ 拾取容差, 靠「段命中」分支永远打不开)。
            {0.0, 0.0, 1, "整圆 圆心 (0,0)"},
            {50.0, 0.0, 1, "整圆 接缝点 0°"},
        };
        runProbes(probes, int(std::size(probes)));
    }

    // 用户路径: 属性面板里把包角改成 180 (applyToModel + 全量 resolve)。
    {
        cad::ui::CircleGeometrySection section(&doc, nullptr);
        section.setTarget(ids.blockId, ids.segId);
        section.populateFromModel(*b, *seg);
        section.editSweep()->setText(QStringLiteral("180"));
        section.applyToModel(b, seg);
    }
    doc.resolveAll();

    // 第二轮: 半圆 (弧身只占上半, 下半不应命中)。
    {
        const auto a45 = atDeg(45.0), a135 = atDeg(135.0), a270 = atDeg(270.0);
        const Probe probes[] = {
            {a45.first, a45.second, 1, "半圆 弧身 45°"},
            {a135.first, a135.second, 1, "半圆 弧身 135°"},
            {a270.first, a270.second, 0, "半圆 未绘制方向 270°"},
        };
        runProbes(probes, int(std::size(probes)));
    }
}

// ③ 排查: 用户报告「粉色点在线段之外」。包角/半径编辑后, 3 个粉色曲线锚点
// 必须仍落在**实际绘制**的解析弧线上 (CurveItem::paintPath), 否则就是错位/
// 残影。分别覆盖: 整圆 → 数值包角 → 公式包角 → 公式后再改数值。
void TestCircleEdit::circleAnchorsStayOnPaintedArc()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    auto anchorIds = [&]() {
        QVector<QUuid> out;
        for (const auto& pt : b->points)
            if (pt.constraint == PointConstraint::CurveAnchor) out.append(pt.id);
        return out;
    };
    QCOMPARE(anchorIds().size(), 3);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    auto checkOnArc = [&](const char* what) {
        doc.resolveAll();
        CurveItem* item = firstCurveItem(scene, ids.blockId);
        QVERIFY2(item, what);
        const QPainterPath& pp = item->paintPath();
        QVERIFY2(!pp.isEmpty(), what);
        const cad::geo::Vec2 origin = b->transform.origin;
        for (const QUuid& pid : anchorIds()) {
            const ParamPoint* pt = b->findPoint(pid);
            QVERIFY2(pt && pt->resolved, what);
            const cad::geo::Vec2 w = b->transform.toWorld(pt->resolvedPos);
            const QPointF local =
                cad::geo::Coord::toScene(w.x - origin.x, w.y - origin.y);
            double best = 1e9;
            for (int i = 0; i <= 720; ++i) {
                const QPointF q = pp.pointAtPercent(double(i) / 720.0);
                best = std::min(best, std::hypot(q.x() - local.x(), q.y() - local.y()));
            }
            qInfo("  [%s] anchor local=(%.3f,%.3f) distToPaintedArc=%.4f",
                  what, local.x(), local.y(), best);
            QVERIFY2(best < 0.25, what);
        }
    };

    checkOnArc("full-circle");

    section.editSweep()->setText(QStringLiteral("180"));
    section.applyToModel(b, seg);
    checkOnArc("sweep=180-numeric");

    section.editSweep()->setText(QStringLiteral("(90)+(90)"));
    section.applyToModel(b, seg);
    checkOnArc("sweep-formula");

    section.editSweep()->setText(QStringLiteral("120"));
    section.applyToModel(b, seg);
    checkOnArc("sweep=120-after-formula");

    section.editRadius()->setText(QStringLiteral("3"));
    section.applyToModel(b, seg);
    checkOnArc("radius=3cm-after-formula");
}

// ① 一期补充 (2026-12): 圆心→接缝半径基准虚线 + 世界角标注。数据必须跟着
// 接缝 (基准角 a₀ + 块旋转) 与半径走; 窄弧的包围盒必须并进圆心 (否则基准线
// 画到盒外 = 残影); 变形圆 (圆度 ≠ 0) 不是圆, 不画基准线。
void TestCircleEdit::circleGuideFollowsSeamAndSweep()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    auto curveItem = [&]() -> CurveItem* {
        doc.resolveAll();
        return firstCurveItem(scene, ids.blockId);
    };

    // 整圆, a₀ = 0°: 圆心在块原点, 基准半径指向 +X, 半径 = 50。
    CurveItem* item = curveItem();
    QVERIFY(item);
    CurveItem::Data::CircleGuide g = item->circleGuide();
    QVERIFY2(g.valid, "解析圆必须带半径基准数据");
    QVERIFY2(std::hypot(g.center.x(), g.center.y()) < 1e-9, "圆心 = 块原点");
    QCOMPARE(g.radius, 50.0);
    QVERIFY2(std::abs(g.worldAngleDeg) < 1e-9, "a₀ = 0°");
    QVERIFY2(std::hypot(g.seam.x() - 50.0, g.seam.y()) < 1e-6,
             "接缝 (虚线外端点) = 场景 (50, 0)");

    // a₀ = 90°: 基准半径转到 +Y (世界) → 场景 y 向下即 (0,-50)。
    section.editBaseAngle()->setText(QStringLiteral("90"));
    section.applyToModel(b, seg);
    item = curveItem();
    QVERIFY(item);
    g = item->circleGuide();
    QVERIFY(g.valid);
    const double shownDeg = cad::geo::toDisplayDeg(
        g.worldAngleDeg, cad::geo::AngleDisplayRole::WorldDirection);
    QVERIFY2(std::abs(shownDeg - 90.0) < 1e-6, qPrintable(QString::number(shownDeg)));
    QVERIFY2(std::hypot(g.seam.x(), g.seam.y() + 50.0) < 1e-6,
             qPrintable(QStringLiteral("接缝场景坐标 (%1,%2)")
                            .arg(g.seam.x(), 0, 'f', 3)
                            .arg(g.seam.y(), 0, 'f', 3)));

    // 块整体旋转 90° (用户报告 m01094 ③「圆的角度是恒定的世界角度, 这个是不
    // 对的」/ ④「虚线跟随角度移动」): 基准半径与标注都必须跟着世界一起转,
    // 且虚线方向必须与标注角一致 —— 否则「虚线不指外圆点」。
    b->transform.rotation = cad::geo::degToRad(90.0);
    item = curveItem();
    QVERIFY(item);
    g = item->circleGuide();
    QVERIFY(g.valid);
    QVERIFY2(std::abs(g.worldAngleDeg - 180.0) < 1e-6,
             qPrintable(QStringLiteral("块旋转 90° 后世界角 = %1°")
                            .arg(g.worldAngleDeg)));
    const QPointF dir = g.seam - g.center;
    const double dirDeg = cad::geo::radToDeg(std::atan2(-dir.y(), dir.x()));
    QVERIFY2(std::abs(cad::geo::normalizeDeg360(dirDeg)
                      - cad::geo::normalizeDeg360(g.worldAngleDeg)) < 1e-6,
             qPrintable(QStringLiteral("虚线方向 %1° vs 标注 %2°")
                            .arg(dirDeg).arg(g.worldAngleDeg)));
    // 接缝仍必须落在外圆点上 (半径 = 50)。
    QVERIFY2(std::abs(std::hypot(dir.x(), dir.y()) - 50.0) < 1e-6,
             "虚线长度 = 半径");
    b->transform.rotation = 0.0;

    // 窄弧 (包角 10°): 折线包围盒只是一小段, 必须并进圆心。
    section.editSweep()->setText(QStringLiteral("10"));
    section.applyToModel(b, seg);
    item = curveItem();
    QVERIFY(item);
    QVERIFY2(item->boundingRect().contains(QPointF(0.0, 0.0)),
             "圆心必须在包围盒内, 否则半径基准虚线画到盒外");

    // 变形圆 (圆度 −1 = 内接四边形): 不再是圆, 不画基准线。
    seg->tension = -1.0;
    item = curveItem();
    QVERIFY(item);
    QVERIFY2(!item->circleGuide().valid, "变形圆不画半径基准");
}

void TestCircleEdit::diameterAndCircumferenceConvertToRadius()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);
    QCOMPARE(section.editRadius()->text(), QStringLiteral("5"));   // 50 mm = 5 cm

    // ── 直径 12 cm → 半径 6 cm ──
    section.editDiameter()->setText(QStringLiteral("12"));
    fireEditingFinished(section.editDiameter());
    QCOMPARE(section.editRadius()->text(), QStringLiteral("6"));
    section.applyToModel(b, seg);
    doc.resolveAll();
    QVERIFY(std::abs(radiusOf(*b, ids) - 60.0) < 1e-9);
    // 圆心不动, 仍是圆。
    const ParamPoint* center = b->findPoint(ids.centerId);
    QVERIFY(center);
    QVERIFY(center->resolvedPos.distanceTo(Vec2(0.0, 0.0)) < 1e-9);

    // ── 周长 10 cm → r = 100 mm / 2π ──
    // 换算结果经半径输入框 (2 位小数 cm) 回写, 所以只到 0.01 cm ≈ 0.1 mm 精度。
    section.editCircumference()->setText(QStringLiteral("10"));
    fireEditingFinished(section.editCircumference());
    section.applyToModel(b, seg);
    doc.resolveAll();
    QVERIFY(std::abs(radiusOf(*b, ids) - 100.0 / (2.0 * cad::geo::kPi)) < 0.02);
}

void TestCircleEdit::radiusFormulaIsDerivedFromDandC()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    // 直径公式 → 半径公式 = (F)/2
    section.editDiameter()->setText(QStringLiteral("(2*3)"));
    fireEditingFinished(section.editDiameter());
    QCOMPARE(section.editRadius()->text(), QStringLiteral("((2*3))/2"));
    section.applyToModel(b, seg);
    QCOMPARE(b->findPoint(ids.startId)->distanceFormula, QStringLiteral("((2*3))/2"));

    // 周长公式 → 半径公式 = (F)/(2π)
    section.editCircumference()->setText(QStringLiteral("(3*4)"));
    fireEditingFinished(section.editCircumference());
    section.applyToModel(b, seg);
    const QString rf = b->findPoint(ids.startId)->distanceFormula;
    QVERIFY2(rf.startsWith(QStringLiteral("((3*4))/(")),
             qPrintable(rf));
    QVERIFY2(rf.contains(QString::number(2.0 * cad::geo::kPi, 'g', 17)),
             qPrintable(rf));
}

void TestCircleEdit::baseAngleRotatesSeamAndKeepsSweep()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    section.editBaseAngle()->setText(QStringLiteral("90"));
    section.applyToModel(b, seg);
    doc.resolveAll();

    const ParamPoint* sp = b->findPoint(ids.startId);
    const ParamPoint* ep = b->findPoint(ids.endId);
    QVERIFY(sp && ep);
    QVERIFY(std::abs(sp->angle - 90.0) < 1e-9);
    // 包角不变: 终点角度跟着平移 (整圆 a0+360)。
    QVERIFY(std::abs(ep->angle - 450.0) < 1e-9);
    // 圆心仍在原点, 起点仍在半径上。
    QVERIFY(b->findPoint(ids.centerId)->resolvedPos.distanceTo(Vec2(0.0, 0.0)) < 1e-9);
    QVERIFY(std::abs(sp->resolvedPos.length() - 50.0) < 1e-6);
    // 回读: 基准角 90°, 包角 360°。
    section.refreshDerived();
    QCOMPARE(section.editBaseAngle()->text(), QStringLiteral("90"));
    QCOMPARE(section.editSweep()->text(), QStringLiteral("360"));

    // 公式基准角: 终点公式必须带上包角, 否则整圆退化成零包角。
    section.editBaseAngle()->setText(QStringLiteral("(30+60)"));
    section.applyToModel(b, seg);
    QCOMPARE(sp->angleFormula, QStringLiteral("(30+60)"));
    QVERIFY2(ep->angleFormula.endsWith(QStringLiteral("+ 360")),
             qPrintable(ep->angleFormula));
}

void TestCircleEdit::sweepHalfCircleArcAndChord()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    section.editSweep()->setText(QStringLiteral("180"));
    section.applyToModel(b, seg);
    doc.resolveAll();

    QVERIFY(std::abs(sweepDegOf(*b, ids) - 180.0) < 1e-9);
    // 弧长 πr, 弦长 2r。
    const double arc = b->segmentBaseLength(ids.segId);
    QVERIFY2(std::abs(arc - cad::geo::kPi * 50.0) < 0.01, qPrintable(QString::number(arc)));
    const double chord = cad::geo::degToChordMm(180.0, 50.0);
    QVERIFY(std::abs(chord - 100.0) < 1e-9);
    // 象限锚按包角百分比分布 (半圆 → 45/90/135°), 全部落在圆周上。
    for (const QUuid& pid : seg->passPointIds) {
        const ParamPoint* pp = b->findPoint(pid);
        QVERIFY(pp && pp->resolved);
        QVERIFY2(std::abs(pp->resolvedPos.length() - 50.0) < 1e-3,
                 qPrintable(QString::number(pp->resolvedPos.length())));
    }
    // 回读为 180。
    section.refreshDerived();
    QCOMPARE(section.editSweep()->text(), QStringLiteral("180"));
}

void TestCircleEdit::sweepFullCircleIsNeverNormalized()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    // 先把基准角挪开, 再显式输入 360 —— 终点角度必须存 a0+360, 不能折成 a0。
    section.editBaseAngle()->setText(QStringLiteral("30"));
    section.applyToModel(b, seg);
    doc.resolveAll();
    section.editSweep()->setText(QStringLiteral("360"));
    section.applyToModel(b, seg);
    doc.resolveAll();

    const ParamPoint* sp = b->findPoint(ids.startId);
    const ParamPoint* ep = b->findPoint(ids.endId);
    QVERIFY(sp && ep);
    QVERIFY(std::abs(sp->angle - 30.0) < 1e-9);
    QVERIFY2(std::abs(ep->angle - 390.0) < 1e-9,
             qPrintable(QString::number(ep->angle)));
    // 位置重合 (整圆), 但 ID 不同 —— 起点/终点判定不能同时成立。
    QVERIFY(sp->resolvedPos.distanceTo(ep->resolvedPos) < 1e-6);
    QVERIFY(ids.startId != ids.endId);
    // 圆仍完整: 三个象限锚都在圆周上 (归一化会让它们全塌到圆心)。
    for (const QUuid& pid : seg->passPointIds) {
        const ParamPoint* pp = b->findPoint(pid);
        QVERIFY(pp && pp->resolved);
        QVERIFY2(std::abs(pp->resolvedPos.length() - 50.0) < 1e-3,
                 qPrintable(QString::number(pp->resolvedPos.length())));
    }
    // 输入 0 也当整圆处理 (避免「0° 包角」这种无几何意义的输入)。
    section.editSweep()->setText(QStringLiteral("0"));
    section.applyToModel(b, seg);
    doc.resolveAll();
    QVERIFY(std::abs(sweepDegOf(*b, ids) - 360.0) < 1e-9);
}

void TestCircleEdit::roundnessIsClamped()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    section.editRoundness()->setText(QStringLiteral("5"));
    section.applyToModel(b, seg);
    QVERIFY(std::abs(seg->tension - 0.25) < 1e-12);

    section.editRoundness()->setText(QStringLiteral("-2"));
    section.applyToModel(b, seg);
    QVERIFY(std::abs(seg->tension + 1.0) < 1e-12);

    section.editRoundness()->setText(QStringLiteral("-0.5"));
    section.applyToModel(b, seg);
    QVERIFY(std::abs(seg->tension + 0.5) < 1e-12);
}

void TestCircleEdit::lineGeometrySectionSkipsCircleSegments()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::LineGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    // 线段区的「长度」写的是终点距离; 对圆写它会污染半径 (被 syncCircleFitRadius
    // 覆盖或直接改坏终点), 所以必须早退。
    auto* editLength = section.findChild<ElaLineEdit*>(QStringLiteral("editLength"));
    QVERIFY(editLength);
    editLength->setText(QStringLiteral("99"));
    seg->tension = 0.1;
    section.applyToModel(b, seg);

    const ParamPoint* ep = b->findPoint(ids.endId);
    QVERIFY(ep);
    QVERIFY(std::abs(ep->distance - 50.0) < 1e-9);   // 终点距离未被 99 覆盖
    QVERIFY(std::abs(seg->tension - 0.1) < 1e-12);   // 张力未被线段区改写
}

void TestCircleEdit::sessionUndoRedoRestoresCircleEdits()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    cad::ui::LinePropertySession session;
    session.takeSnapshot(&doc, ids.blockId, ids.segId);

    section.editRadius()->setText(QStringLiteral("7"));
    section.editBaseAngle()->setText(QStringLiteral("30"));
    section.editSweep()->setText(QStringLiteral("90"));
    section.editRoundness()->setText(QStringLiteral("-0.5"));
    section.applyToModel(b, seg);
    QVERIFY(session.commit(&doc, ids.blockId, ids.segId, false));
    doc.resolveAll();

    QVERIFY(std::abs(radiusOf(*b, ids) - 70.0) < 1e-9);
    QVERIFY(std::abs(sweepDegOf(*b, ids) - 90.0) < 1e-9);
    QVERIFY(std::abs(seg->tension + 0.5) < 1e-12);

    doc.undoStack()->undo();
    b = doc.findBlock(ids.blockId);
    seg = b ? b->findSegment(ids.segId) : nullptr;
    QVERIFY(b && seg);
    QVERIFY2(std::abs(radiusOf(*b, ids) - 50.0) < 1e-9,
             qPrintable(QString::number(radiusOf(*b, ids))));
    QVERIFY(std::abs(sweepDegOf(*b, ids) - 360.0) < 1e-9);
    QVERIFY(std::abs(seg->tension) < 1e-12);

    doc.undoStack()->redo();
    b = doc.findBlock(ids.blockId);
    seg = b ? b->findSegment(ids.segId) : nullptr;
    QVERIFY(b && seg);
    QVERIFY(std::abs(radiusOf(*b, ids) - 70.0) < 1e-9);
    QVERIFY(std::abs(sweepDegOf(*b, ids) - 90.0) < 1e-9);
}

void TestCircleEdit::circlePaintPathIsAnalytic()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);

    CanvasScene scene(&doc);
    scene.setSceneRect(-100, -100, 200, 200);
    scene.addBlockItem(ids.blockId);
    scene.syncBlockPositions();

    CurveItem* item = firstCurveItem(scene, ids.blockId);
    QVERIFY(item);
    const QPainterPath& paint = item->paintPath();
    QVERIFY2(!paint.isEmpty(), "圆段必须带解析绘制路径 (D16)");
    // 解析路径 = 少量三次曲线, 没有折线的几十个 LineTo。
    int lineEls = 0, curveEls = 0;
    for (int i = 0; i < paint.elementCount(); ++i) {
        const auto t = paint.elementAt(i).type;
        if (t == QPainterPath::LineToElement) ++lineEls;
        else if (t == QPainterPath::CurveToElement) ++curveEls;
    }
    QVERIFY2(curveEls >= 4, qPrintable(QString::number(curveEls)));
    QVERIFY2(lineEls == 0, qPrintable(QString::number(lineEls)));
    // 采样误差只剩 Qt 自己的椭圆近似 (≈2.7e-4·r), 远小于折线的 0.1 mm 绝对容差。
    const double err = maxRadialError(paint, 50.0);
    QVERIFY2(err < 0.05, qPrintable(QString::number(err)));
}

void TestCircleEdit::deformedCircleIsNotPaintedAsCircle()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);
    seg->tension = -1.0;              // 内接四边形 (D17)
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.setSceneRect(-100, -100, 200, 200);
    scene.addBlockItem(ids.blockId);
    scene.syncBlockPositions();

    CurveItem* item = firstCurveItem(scene, ids.blockId);
    QVERIFY(item);
    const QPainterPath& paint = item->paintPath();
    QVERIFY(!paint.isEmpty());
    // 圆度 ≠ 0 必须按真实 Bézier 画: 内接四边形的边中点在 r/√2 ≈ 0.707r,
    // 若被画成圆, 径向误差会退回 ~0。
    const double err = maxRadialError(paint, 50.0);
    QVERIFY2(err > 5.0, qPrintable(QString::number(err)));
}

void TestCircleEdit::plainCurveKeepsFlatPaintPath()
{
    ParamDocument doc;
    Block block;
    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = {0.0, 0.0};
    const QUuid aid = block.addPoint(a);
    ParamPoint c;
    c.constraint = PointConstraint::Free;
    c.freePos = {50.0, 30.0};
    const QUuid cid = block.addPoint(c);
    ParamPoint e;
    e.constraint = PointConstraint::Free;
    e.freePos = {100.0, 0.0};
    const QUuid eid = block.addPoint(e);

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = aid;
    seg.endPointId = eid;
    seg.passPointIds = {cid};
    block.addSegment(seg);
    const QUuid blockId = doc.addBlock(std::move(block));
    doc.resolveAll();

    CanvasScene scene(&doc);
    scene.setSceneRect(-50, -50, 200, 150);
    scene.addBlockItem(blockId);
    scene.syncBlockPositions();

    CurveItem* item = firstCurveItem(scene, blockId);
    QVERIFY(item);
    QVERIFY2(item->paintPath().isEmpty(), "非圆曲线不建解析路径 (沿用折线)");
    // 命中/绘制仍用折线路径。
    QVERIFY(!item->shape().isEmpty());
}

void TestCircleEdit::detachButtonDetachesAndSignals()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 30.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);
    QVERIFY(section.detachButton());

    // makeCircle 已压入 1 条 DrawLineCommand: 解除必须是**独立的一步**。
    const int baseCount = doc.undoStack()->count();
    QSignalSpy detachSpy(&section, &cad::ui::CircleGeometrySection::detachRequested);
    section.detachButton()->click();
    QCOMPARE(detachSpy.count(), 1);
    QCOMPARE(doc.undoStack()->count(), baseCount + 1);

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    seg = b->findSegment(ids.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::None);
    QCOMPARE(b->findPoint(ids.startId)->constraint, PointConstraint::Free);

    doc.undoStack()->undo();
    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    QCOMPARE(b->findSegment(ids.segId)->fitKind, FitKind::Circle);
    QCOMPARE(b->findPoint(ids.startId)->constraint, PointConstraint::Polar);
}

/// D15: 圆拟合段 + 圆度 0 = 真圆, 弧长取解析值 r·θ; 数值积分的是 4 跨三次
/// Bézier 近似的弧长 (整圆比 2πr 大 1.4e-4 相对量), 会让面板「弧长」行与
/// 「周长」行显示成两个数 (r=100mm 时 62.84 与 62.83), 也会污染发布的周长变量。
void TestCircleEdit::circleArcLengthIsAnalytic()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 100.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    const double expectedFull = 2.0 * cad::geo::kPi * 100.0;

    // 整圆: 弧长必须精确 == 2πR。
    const double full = b->segmentBaseLength(ids.segId);
    QVERIFY2(std::abs(full - expectedFull) < 1e-9,
             qPrintable(QString::number(full, 'g', 17)));

    // 面板一致性: 「弧长」行与「周长」行显示同一个数, 且是 62.83 (不是 62.84)。
    QCOMPARE(cad::geo::Units::formatCm(full), cad::geo::Units::formatCm(expectedFull));
    QCOMPARE(cad::geo::Units::formatCm(full), QStringLiteral("62.83"));

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);

    // 半圆: πr。
    section.editSweep()->setText(QStringLiteral("180"));
    section.applyToModel(b, seg);
    doc.resolveAll();
    const double half = b->segmentBaseLength(ids.segId);
    QVERIFY2(std::abs(half - cad::geo::kPi * 100.0) < 1e-9,
             qPrintable(QString::number(half, 'g', 17)));

    // 圆度 ≠ 0 时形状不再是圆 (D16 也回落逐跨 Bézier 绘制), 弧长同样回落数值值:
    // 与解析值不再逐位相等, 但仍有限且为正。
    section.editRoundness()->setText(QStringLiteral("0.2"));
    section.applyToModel(b, seg);
    doc.resolveAll();
    const double deformed = b->segmentBaseLength(ids.segId);
    QVERIFY(std::isfinite(deformed));
    QVERIFY(deformed > 0.0);
    QVERIFY2(std::abs(deformed - cad::geo::kPi * 100.0) > 1e-9,
             "圆度 ≠ 0 必须回落数值弧长");
}

/// D15: 「发布参数」把圆周长 (= 弧长, 整圆 = 2πR) 发布为只读关联参数,
/// 走既有 LinkedVariable 通路 (零新机制), 公式域 cm, 可被 lengthFormula 引用。
void TestCircleEdit::publishCircumferenceVariableIsExactAndReferenceable()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    cad::ui::CircleGeometrySection section(&doc, nullptr);
    section.setTarget(ids.blockId, ids.segId);
    section.populateFromModel(*b, *seg);
    QVERIFY(section.publishButton());
    QCOMPARE(section.publishButton()->text(), cad::cmd::texts::kPublishLinkedVar);
    QVERIFY(section.publishButton()->isEnabled());

    // makeCircle 已压入 1 条 DrawLineCommand: 发布必须是独立的一步。
    const int baseCount = doc.undoStack()->count();
    section.publishButton()->click();
    QCOMPARE(doc.undoStack()->count(), baseCount + 1);

    const double expectedMm = 2.0 * cad::geo::kPi * 50.0;
    auto* lv = doc.findLinkedBySource(ids.blockId, ids.segId);
    QVERIFY(lv);
    QVERIFY2(std::abs(lv->value - expectedMm) < 1e-9,
             qPrintable(QString::number(lv->value, 'g', 17)));
    const QString refName = lv->refName;
    QVERIFY(refName.startsWith(QStringLiteral("L")));

    // 按钮变「已发布」并禁用, 再点不会重复发布。
    QCOMPARE(section.publishButton()->text(), QString::fromUtf8("已发布"));
    QVERIFY(!section.publishButton()->isEnabled());
    section.publishButton()->click();
    QCOMPARE(doc.undoStack()->count(), baseCount + 1);

    // 公式可引用该周长变量: 引用名进参数表 (cm 域), baseValue 仍是 mm。
    FormulaVariable f;
    f.name = QStringLiteral("CircleHalfC");
    f.expression = QStringLiteral("%1 / 2").arg(refName);
    doc.addFormula(f);
    const FormulaVariable* sf = doc.findFormula(f.id);
    QVERIFY(sf && sf->valid);
    const double expectedCm = cad::geo::Units::mmToCm(expectedMm) * 0.5;
    QVERIFY2(std::abs(cad::geo::Units::mmToCm(sf->baseValue) - expectedCm) < 1e-9,
             qPrintable(QString::number(cad::geo::Units::mmToCm(sf->baseValue), 'g', 17)));
}

// 曲线锚点（粉点）本身是合法的连接目标：进吸附池、接受其他线段端点挂载。
// 排查依据（用户报告「不允许别的线段挂在她身上，根本没有吸附项」）：
// 数据层与吸附层都没有点类型过滤 —— 限制只可能出在 UI 入口。
void TestCircleEdit::curveAnchorAcceptsFollowerLine()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 50.0);
    const Block* cb = doc.findBlock(ids.blockId);
    QVERIFY(cb);
    const Segment& cseg = cb->segments.front();
    QCOMPARE(int(cseg.passPointIds.size()), 3);
    const QUuid anchorId = cseg.passPointIds.at(1);   // 180° 象限锚点
    const QUuid hostSegId = cseg.id;
    const ParamPoint* ap = cb->findPoint(anchorId);
    QVERIFY(ap && ap->resolved);
    QVERIFY(ap->selectable);                          // 圆锚点默认可被吸附
    const Vec2 anchorWorld = cb->transform.toWorld(ap->resolvedPos);
    // 锚点不是任何线段的端点，但 exitSegmentAtPoint 回退到宿主曲线
    // (BlockQuery.cpp:187-190)，连接方向取曲线切向 (BlockQuery.cpp:102-119)。
    QCOMPARE(cb->exitSegmentAtPoint(anchorId), hostSegId);

    cad::tools::SnapEngine se;
    const auto snap = se.findSnap(anchorWorld, &doc, 1.0, 12.0);
    QVERIFY(snap.has_value());
    QCOMPARE(snap->pointId, anchorId);                // 吸附命中粉点
    const auto cands = se.findSnapCandidates(anchorWorld, &doc, 1.0, 12.0);
    bool hasAnchor = false;
    for (const auto& c : cands)
        if (c.pointId == anchorId) hasAnchor = true;
    QVERIFY(hasAnchor);                               // 候选池包含粉点

    Block line;
    ParamPoint ls;
    ls.constraint = PointConstraint::Free;
    ls.freePos = {100.0, 100.0};
    const QUuid lsId = line.addPoint(ls);
    ParamPoint le;
    le.constraint = PointConstraint::Free;
    le.freePos = {160.0, 100.0};
    const QUuid leId = line.addPoint(le);
    Segment lseg;
    lseg.type = SegmentType::Line;
    lseg.startPointId = lsId;
    lseg.endPointId = leId;
    line.addSegment(lseg);
    const QUuid lineId = doc.addBlock(std::move(line));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = lineId;
    att.fromPointId = lsId;
    att.toBlockId = ids.blockId;
    att.toPointId = anchorId;
    att.toSegmentId = hostSegId;
    QCOMPARE(cad::param::checkAttachment(doc.attachments(), att),
             cad::param::AttachmentIssue::Ok);        // 图校验无点类型限制
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    const Block* lb = doc.findBlock(lineId);
    const ParamPoint* lsp = lb ? lb->findPoint(lsId) : nullptr;
    QVERIFY(lsp && lsp->resolved);
    const Vec2 w = lb->transform.toWorld(lsp->resolvedPos);
    QVERIFY2(std::hypot(w.x - anchorWorld.x, w.y - anchorWorld.y) < 1e-9,
             "跟随线端点必须精确落在粉点上");
}

QTEST_MAIN(TestCircleEdit)
#include "test_circle_edit.moc"