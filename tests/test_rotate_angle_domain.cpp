// 2026-12 审计 P0-4 守卫：旋转手势「角度域」收口。
//
// 审计现象：同一个物理姿态在旋转手势里显示两个数字——连接段 HUD 显示 −90.0°、
// 自由段 HUD 显示 270.0°（同一姿态，两个域）。根因是读数与写数各自在调用点
// 选域。收口后：
//   · 写：手势原始角 → 连接存储字段只有 cad::param::writeFollowerAngleForMode
//         一个入口（Angle 存 [0,360)、ArcLength 用 360 域换算、ChordLength 必须
//         用 (−180,180] 折角，因为弦长 2r·sin(θ/2) 无法区分 θ 与 360−θ）；
//   · 读：HUD 徽标只有 cad::tools::formatRotationBadge 一个入口，域由物理量
//         决定：旋转量 Delta 不折叠、连接段跟随折角 Fold 走 (−180,180]、
//         自由段世界方向 World 走 0..360（审计 UI-P0-1 复核后把「姿态角」
//         拆成 Fold/World 两个量——自由段卡片刻意显示 0..360 世界角，
//         连接段卡片显示折角，徽标必须与各自卡片同数）。
// 本文件把上述两条契约钉死，任一侧回退即红。
#include <QtTest>

#include <cmath>

#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/Attachment.h"
#include "parametric/FollowerAngle.h"
#include "tools/RotateDragMath.h"

class TestRotateAngleDomain : public QObject
{
    Q_OBJECT

private slots:
    void angleModeStoresNormalized360();
    void arcLengthModeUsesStorageDomain();
    void chordLengthModeUsesFoldDomain();
    void badgeFoldUsesDisplayDomain();
    void badgeWorldUsesWorldDomain();
    void badgeDeltaKeepsRotationAmount();
    void badgeFoldAgreesWithStoredAngle();
    void badgeWorldAgreesWithCardReadout();
};

// Angle 模式：手势 −90° 写入存储域 270°，并清空公式（手势覆盖公式驱动值）。
void TestRotateAngleDomain::angleModeStoresNormalized360()
{
    cad::param::Attachment att;
    att.rotationMode = cad::param::RotationMode::ArcLength;
    att.followerAngle = 12.0;
    att.followerAngleFormula = QStringLiteral("45");

    const double stored = cad::param::writeFollowerAngleForMode(
        att, cad::param::RotationMode::Angle, -90.0, 60.0);

    QCOMPARE(stored, 270.0);                        // 存储域 [0,360)
    QCOMPARE(att.followerAngle, 270.0);
    QVERIFY(att.followerAngleFormula.isEmpty());    // 手势覆盖公式
    QVERIFY(att.rotationMode == cad::param::RotationMode::Angle);

    // 多圈值同样是合法存储值，不得折叠（tests/test_rotate_strip.cpp:159 锁定 1260°）
    const double multi = cad::param::writeFollowerAngleForMode(
        att, cad::param::RotationMode::Angle, 1260.0, 60.0);
    QCOMPARE(multi, 180.0);
}

// ArcLength 模式：弧长用 360 域换算，θ 与 360−θ 的弧长互为相反数（可区分）。
void TestRotateAngleDomain::arcLengthModeUsesStorageDomain()
{
    cad::param::Attachment att;
    const double r = 60.0;
    cad::param::writeFollowerAngleForMode(
        att, cad::param::RotationMode::ArcLength, 270.0, r);

    const double expected = cad::geo::degToArcMm(270.0, r);
    QVERIFY(std::abs(att.arcLength - expected) < 1e-9);
    QVERIFY(att.arcLength > 0.0);                   // 270° 不能塌成 −90° 的弧长
    QVERIFY(att.arcLengthFormula.isEmpty());
    QVERIFY(att.rotationMode == cad::param::RotationMode::ArcLength);
}

// ChordLength 模式：弦长必须用折角域带符号——270° 与 90° 的弦长互为相反数，
// 若用 360 域则两者同值（degToChordMm 只取 |sin|）。
void TestRotateAngleDomain::chordLengthModeUsesFoldDomain()
{
    const double r = 60.0;
    cad::param::Attachment c270;
    cad::param::Attachment c90;
    cad::param::writeFollowerAngleForMode(
        c270, cad::param::RotationMode::ChordLength, 270.0, r);
    cad::param::writeFollowerAngleForMode(
        c90, cad::param::RotationMode::ChordLength, 90.0, r);

    QVERIFY(std::abs(c270.chordLength - cad::geo::degToChordMm(-90.0, r)) < 1e-9);
    QVERIFY(std::abs(c90.chordLength - cad::geo::degToChordMm(90.0, r)) < 1e-9);
    QVERIFY(c270.chordLength < 0.0);
    QVERIFY(c90.chordLength > 0.0);
    QVERIFY(std::abs(c270.chordLength + c90.chordLength) < 1e-9);
}

// 连接段姿态读数 = 跟随折角：一律 (−180,180]（与角度卡「跟随角」同域）。
void TestRotateAngleDomain::badgeFoldUsesDisplayDomain()
{
    const QChar deg(0x00B0);
    const auto fold = [](double d) {
        return cad::tools::formatRotationBadge(d, cad::tools::RotateBadgeQuantity::Fold);
    };

    QCOMPARE(fold(270.0), QStringLiteral("-90") + deg);
    QCOMPARE(fold(-90.0), QStringLiteral("-90") + deg);
    QCOMPARE(fold(180.0), QStringLiteral("180") + deg);   // 折角域含 +180
    QCOMPARE(fold(1260.0), QStringLiteral("180") + deg);  // 多圈存储折角后一致

    // 回归锁：折角不得出现 360 域读数
    QVERIFY(fold(270.0) != cad::geo::Units::formatDegTrimmed(270.0));
}

// 自由段姿态读数 = 世界方向：一律 0..360（审计 UI-P0-1，与角度卡「世界角度」同数）。
void TestRotateAngleDomain::badgeWorldUsesWorldDomain()
{
    const QChar deg(0x00B0);
    const auto world = [](double d) {
        return cad::tools::formatRotationBadge(d, cad::tools::RotateBadgeQuantity::World);
    };

    QCOMPARE(world(-90.0), QStringLiteral("270") + deg);  // 旧读数 −90.0° 已收口
    QCOMPARE(world(270.0), QStringLiteral("270") + deg);
    QCOMPARE(world(180.0), QStringLiteral("180") + deg);
    QCOMPARE(world(450.0), QStringLiteral("90") + deg);   // 多圈归一
    QCOMPARE(world(0.0), QStringLiteral("0") + deg);

    // 回归锁：世界域不得再折叠成负角
    QVERIFY(world(-90.0) != QStringLiteral("-90") + deg);
    // 同一输入两个物理量必须给出不同读数（防再次合并成一个域）
    QVERIFY(world(-90.0) != cad::tools::formatRotationBadge(
        -90.0, cad::tools::RotateBadgeQuantity::Fold));
}

// 旋转量读数（多选/框选）：累积角是「转了多少」，不是姿态角，不折叠；带符号。
void TestRotateAngleDomain::badgeDeltaKeepsRotationAmount()
{
    const QChar deg(0x00B0);
    const auto delta = [](double d) {
        return cad::tools::formatRotationBadge(d, cad::tools::RotateBadgeQuantity::Delta);
    };

    QVERIFY(delta(0.005).isEmpty());                      // 微小抖动不显示
    QCOMPARE(delta(-45.0), QStringLiteral("-45") + deg);  // 逆时针保留符号
    QCOMPARE(delta(270.0), QStringLiteral("270") + deg);  // 旋转量不折叠成 −90°
}

// 写读一致（连接段）：手势角写入存储后，HUD 折角徽标与角度卡折角读数同数
// （P0-4 的「同一姿态两个数字」由本断言封口）。
void TestRotateAngleDomain::badgeFoldAgreesWithStoredAngle()
{
    const double raws[] = {-90.0, 90.0, 270.0, 450.0, 1260.0};
    for (const double raw : raws) {
        cad::param::Attachment att;
        const double stored = cad::param::writeFollowerAngleForMode(
            att, cad::param::RotationMode::Angle, raw, 60.0);
        const QString badge = cad::tools::formatRotationBadge(
            stored, cad::tools::RotateBadgeQuantity::Fold);
        const QString card = cad::geo::Units::formatDegTrimmed(
            cad::param::followerAngleToDisplay(att.followerAngle));
        QCOMPARE(badge, card);
    }
}

// 读读一致（自由段）：世界方向徽标与角度卡「= 世界角度 N°」读数同数
// （审计 UI-P0-1 的「属性对话框 270° / 画布条带 −90°」由本断言封口）。
void TestRotateAngleDomain::badgeWorldAgreesWithCardReadout()
{
    const double raws[] = {-90.0, 0.0, 90.0, 270.0, 450.0, 720.0};
    for (const double raw : raws) {
        const QString badge = cad::tools::formatRotationBadge(
            raw, cad::tools::RotateBadgeQuantity::World);
        // SegmentAngleCard 的世界角读数：normalizeDeg360 + formatDegValue + "°"。
        const QString card = cad::geo::Units::formatDegTrimmed(
            cad::geo::normalizeDeg360(raw));
        QCOMPARE(badge, card);
    }
}

QTEST_GUILESS_MAIN(TestRotateAngleDomain)

#include "test_rotate_angle_domain.moc"
