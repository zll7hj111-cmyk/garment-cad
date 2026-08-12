/// @file test_p612_colinearity.cpp
/// 真实文档回归：P612（借用点交点）的射线共线性验证。
///
/// P612 是借用点模式交点（interAimPointId 指向 P560，射线起点 P489）。
/// 借用点语义下，起点 → 借用点 → 交点 必须始终共线（交点 =
/// 起点 + (起点→借用点方向) × s）。用户报告肩褶高从 8 加大到 15/20 时
/// 三者"感觉不在一条直线上"——本测试量化偏离距离。
///
/// 依赖 build/out/Debug/1.gcad 存在（不存在则跳过）。

#include <QtTest>
#include <QFileInfo>

#include <cmath>

#include "document/DocumentFile.h"
#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"

class TestP612Colinearity : public QObject
{
    Q_OBJECT
private slots:
    void colinearityAtDartHeights();
};

void TestP612Colinearity::colinearityAtDartHeights()
{
    const QString path = QStringLiteral("e:/garment-cad/build/out/Debug/1.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("1.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));

    // 定位 P489(射线起点) / P560(借用点) / P612(交点)。
    QUuid idOrigin, idAim, idInter;
    for (const auto& b : doc.blocks()) {
        for (const auto& p : b.points) {
            if (p.serial.endsWith(QLatin1String("P489"))) idOrigin = p.id;
            else if (p.serial.endsWith(QLatin1String("P560"))) idAim = p.id;
            else if (p.serial.endsWith(QLatin1String("P612"))) idInter = p.id;
        }
    }
    // 文档存在但内容不是 P612 文档（被其他文档覆盖）→ 跳过而非回归失败。
    if (idOrigin.isNull() || idAim.isNull() || idInter.isNull())
        QSKIP("1.gcad 不是 P612 文档（缺少 P489/P560/P612），跳过");

    // 确认 P612 是借用点模式（方向由借用点驱动，非数值角）。
    {
        const cad::param::Block* host = nullptr;
        for (const auto& b : doc.blocks())
            if (b.findPoint(idInter)) { host = &b; break; }
        QVERIFY(host);
        const auto* p = host->findPoint(idInter);
        QVERIFY(p);
        QCOMPARE(p->constraint, cad::param::PointConstraint::Intersection);
        QVERIFY(!p->interAimPointId.isNull());
        QCOMPARE(p->interAimPointId, idAim);
    }

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (b.findPoint(id))
                return b.worldPos(id);
        return {};
    };

    for (double h : {8.0, 15.0, 20.0}) {
        doc.setParameter(QString::fromUtf8("肩褶高"), h);
        doc.resolveAll();
        const cad::geo::Vec2 o = worldPos(idOrigin);
        const cad::geo::Vec2 a = worldPos(idAim);
        const cad::geo::Vec2 i = worldPos(idInter);
        const cad::geo::Vec2 ao = a - o;
        const double len = ao.length();
        const double dist = (len > 1e-9)
            ? std::abs((i - o).cross(ao)) / len : -1.0;
        qInfo() << "肩褶高" << h << "起点(" << o.x << "," << o.y << ")"
                << "借用点(" << a.x << "," << a.y << ")"
                << "交点(" << i.x << "," << i.y << ")"
                << "偏离直线" << dist << "mm";
        // 借用点语义：交点必须在起点→借用点直线上（0.5mm 容差）。
        QVERIFY2(dist >= 0.0 && dist < 0.5,
                 qPrintable(QStringLiteral("肩褶高=%1 时交点偏离直线 %2 mm")
                            .arg(h).arg(dist)));
    }
}

QTEST_MAIN(TestP612Colinearity)
#include "test_p612_colinearity.moc"
