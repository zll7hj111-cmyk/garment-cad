#include "test_break_helpers.h"

class TestBreakCurve : public QObject
{
    Q_OBJECT

private slots:
    void curveBreakKeepsShape();
    void curveBreakNearStartKeepsShape();
    void curveBreakAtAnchorKeepsShape();
};

void TestBreakCurve::curveBreakKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    // Break point: interpolated (arc-length 50%) on the curve.
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);
    QVERIFY(!auxId.isNull());

    const auto* preBlk = doc.findBlock(blockId);
    const auto* preEntry = preBlk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*preBlk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    std::vector<Vec2> broken;
    int curveSegCount = 0;
    for (const auto& b : doc.blocks()) {
        for (const auto& s : b.segments) {
            if (!s.isCurve()) continue;
            ++curveSegCount;
            if (const auto* e = b.curveSpanEntry(s.id)) {
                const auto p = worldPolyline(b, *e);
                broken.insert(broken.end(), p.begin(), p.end());
            }
        }
    }
    QCOMPARE(curveSegCount, 2);  // both halves must stay curves

    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("curve shape deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3, qPrintable(QStringLiteral("front/back drifted %1 mm from original").arg(d1, 0, 'f', 4)));
    QVERIFY2(d2 < 0.3, qPrintable(QStringLiteral("broken curve drifted %1 mm from original").arg(d2, 0, 'f', 4)));
}

/// Collect every curve polyline (world space) in the document.
static std::vector<Vec2> collectCurvePolylines(const ParamDocument& doc)
{
    std::vector<Vec2> out;
    for (const auto& b : doc.blocks()) {
        for (const auto& s : b.segments) {
            if (!s.isCurve()) continue;
            if (const auto* e = b.curveSpanEntry(s.id)) {
                const auto p = worldPolyline(b, *e);
                out.insert(out.end(), p.begin(), p.end());
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 曲线打断形状保持 — 断点在第一个 pass 点之前（前段无 pass 点 → 中点插入
// 路径，de Casteljau 半参数化）也必须按原曲线形状重建。
// ---------------------------------------------------------------------------
void TestBreakCurve::curveBreakNearStartKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.08);
    QVERIFY(!auxId.isNull());

    const auto* preBlk = doc.findBlock(blockId);
    const auto* preEntry = preBlk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*preBlk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto broken = collectCurvePolylines(doc);
    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("near-start deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3 && d2 < 0.3,
             qPrintable(QStringLiteral("near-start break drifted (%1, %2) mm")
                        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4)));
}

// ---------------------------------------------------------------------------
// 曲线打断形状保持 — 断点本身就是链点（CurveAnchor / pass point）：无子跨度
// 细分（hasSubSpans=false），冻结原曲线逐点 Hobby in/out 切线必须精确。
// ---------------------------------------------------------------------------
void TestBreakCurve::curveBreakAtAnchorKeepsShape()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);

    // pp1 is a CurveAnchor pass point at 33% chord — break right at it.
    const auto* blk = doc.findBlock(blockId);
    const auto* pp1 = blk->findPoint(pp1Id);
    QVERIFY(pp1);

    const auto* preEntry = blk->curveSpanEntry(segId);
    QVERIFY(preEntry && !preEntry->spans.empty());
    const auto origPoly = worldPolyline(*blk, *preEntry);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, pp1Id);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto broken = collectCurvePolylines(doc);
    const double d1 = maxDeviation(origPoly, broken);
    const double d2 = maxDeviation(broken, origPoly);
    qInfo().noquote() << QStringLiteral("anchor break deviation orig->broken=%1 broken->orig=%2")
        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4);
    QVERIFY2(d1 < 0.3 && d2 < 0.3,
             qPrintable(QStringLiteral("anchor break drifted (%1, %2) mm")
                        .arg(d1, 0, 'f', 4).arg(d2, 0, 'f', 4)));
}

// ---------------------------------------------------------------------------
// 原端点上的连接：打断后应被重指到后段终点（原线尾部）——连接保留、
// 位置不动。
// ---------------------------------------------------------------------------

QTEST_MAIN(TestBreakCurve)
#include "test_break_curve.moc"
