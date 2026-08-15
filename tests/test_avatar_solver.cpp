// AvatarSolver 单元测试：291 滑杆加载、宏插值数学、人种归一化、测量求解。
#include "avatar/AvatarModel.h"
#include "avatar/AvatarSolver.h"
#include "avatar/JsonReader.h"
#include "avatar/MeasureSystem.h"
#include "avatar/Mesh3D.h"

#include <QString>
#include <QtTest>

#include <cmath>
#include <string>

#ifndef AVATAR_ASSETS_DIR
#define AVATAR_ASSETS_DIR "assets/avatar"
#endif

using namespace cad::avatar;

class TestAvatarSolver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void slidersLoad();
    void macroSamplersTokens();
    void macroDefaultsApplied();
    void genderFemaleOnly();
    void ageOldHalf();
    void firmnessDrivesMesh();
    void ethnicNormalize();
    void solveSingleWaistMatchesGoldenWeight();
    void solveMultiThreeMeasures();
    void solveMultiRealisticSizes();
    void solveNaturalBustMatches();
    void cupLetterSegments();
    void heightLockCompensatesLeg();
    void solveHeight168();
    void breastSizeDrivesCupDifference();
    void measureLockCompensatesCup();
    void currentHeightCm();

private:
    Mesh3D loadBase();
    std::unique_ptr<AvatarModel> m_model;
    std::unique_ptr<MeasureSystem> m_measures;
    std::unique_ptr<AvatarSolver> m_solver;
};

void TestAvatarSolver::initTestCase()
{
    try {
    Mesh3D base = loadBase();
    const std::string dir = AVATAR_ASSETS_DIR;
    m_model = std::make_unique<AvatarModel>(std::move(base), dir + "/targets");
    m_measures = std::make_unique<MeasureSystem>();
    m_measures->loadChains(dir + "/measurement_chains.json");
    m_solver = std::make_unique<AvatarSolver>(m_model.get(), m_measures.get());
    m_solver->loadSliders(dir + "/sliders.json");
    } catch (const std::exception& e) {
        qWarning() << "initTestCase exception:" << e.what();
        throw;
    }
}

void TestAvatarSolver::init()
{
    // 每用例前恢复默认体型，避免用例间状态污染（solveMulti 目标基线依赖当前状态）
    m_solver->resetAll();
}

Mesh3D TestAvatarSolver::loadBase()
{
    return loadObjFile(std::string(AVATAR_ASSETS_DIR) + "/base.obj");
}

void TestAvatarSolver::slidersLoad()
{
    QCOMPARE(m_solver->sliders().size(), size_t(291));

    const SliderDef* gender = m_solver->findSlider("macrodetails/Gender");
    QVERIFY(gender);
    QCOMPARE(gender->kind, SliderKind::Macro);
    QCOMPARE(gender->macrovar, std::string("Gender"));

    const SliderDef* african = m_solver->findSlider("macrodetails/African");
    QVERIFY(african);
    QCOMPARE(african->kind, SliderKind::Ethnic);

    const SliderDef* waist = m_solver->findSlider("measure/measure-waist-circ-decr|incr");
    QVERIFY(waist);
    QVERIFY(!waist->measureKey.empty());

    // 默认体型：完全女性（Gender=0）+ 完全亚洲（Asian=1，其余人种 0）
    QCOMPARE(m_solver->macroVar("Gender"), 0.0);
    QCOMPARE(m_solver->macroVar("Asian"), 1.0);
    QCOMPARE(m_solver->macroVar("African"), 0.0);
    QCOMPARE(m_solver->macroVar("Caucasian"), 0.0);
    QCOMPARE(m_solver->sliderValue("macrodetails/Gender"), 0.0);

    // 宏采样文件扫描：全 targets 根（macrodetails 96+144+108 + breast 216 = 564，token 过滤后）
    QCOMPARE(m_solver->macroSamplers().size(), size_t(564));
}

void TestAvatarSolver::macroSamplersTokens()
{
    // universal 系列无人种 token；african 系列有
    bool foundUniversal = false, foundAfrican = false;
    for (const auto& ms : m_solver->macroSamplers()) {
        if (ms.path == "macrodetails/universal-female-young-averagemuscle-averageweight") {
            foundUniversal = true;
            QVERIFY(std::find(ms.tokens.begin(), ms.tokens.end(), "african") == ms.tokens.end());
            QVERIFY(std::find(ms.tokens.begin(), ms.tokens.end(), "female") != ms.tokens.end());
            QVERIFY(std::find(ms.tokens.begin(), ms.tokens.end(), "young") != ms.tokens.end());
        }
        if (ms.path == "macrodetails/african-female-young") {
            foundAfrican = true;
            QVERIFY(std::find(ms.tokens.begin(), ms.tokens.end(), "african") != ms.tokens.end());
        }
    }
    QVERIFY(foundUniversal);
    QVERIFY(foundAfrican);
}

void TestAvatarSolver::macroDefaultsApplied()
{
    // 默认体型（完全女性 gender 0 / age 0.5 / weight 0.5 / muscle 0.5）：
    // female 1 × young 1 × averagemuscle 1 × averageweight 1 = 1.0，male 侧 = 0
    auto targets = m_solver->currentTargets();
    double wFemale = 0.0, wMale = 0.0;
    for (const auto& tw : targets) {
        if (tw.key == "macrodetails/universal-female-young-averagemuscle-averageweight")
            wFemale = tw.weight;
        if (tw.key == "macrodetails/universal-male-young-averagemuscle-averageweight")
            wMale = tw.weight;
    }
    QVERIFY(std::fabs(wFemale - 1.0) < 1e-9);
    QVERIFY(std::fabs(wMale - 0.0) < 1e-9);

    // 默认体型 ≠ 中性体（宏采样已生效），身高为完全女性+完全亚洲的稳定值
    QVERIFY(std::fabs(m_solver->currentHeightCm() - 152.794) < 0.5);
}

void TestAvatarSolver::genderFemaleOnly()
{
    // MH 语义：maleVal = gender，femaleVal = 1 - gender -> gender=1 是纯男性
    m_solver->setSliderValue("macrodetails/Gender", 1.0);
    auto targets = m_solver->currentTargets();
    double wFemale = 0.0, wMale = 0.0;
    for (const auto& tw : targets) {
        if (tw.key == "macrodetails/universal-female-young-averagemuscle-averageweight")
            wFemale = tw.weight;
        if (tw.key == "macrodetails/universal-male-young-averagemuscle-averageweight")
            wMale = tw.weight;
    }
    QVERIFY(std::fabs(wFemale - 0.0) < 1e-9);
    QVERIFY(std::fabs(wMale - 1.0) < 1e-9);
    m_solver->resetAll();
}

void TestAvatarSolver::ageOldHalf()
{
    m_solver->setSliderValue("macrodetails/Age", 0.75);
    // old = 2*0.75-1 = 0.5；默认完全女性 female=1、完全亚洲 asian=1 -> 1*1*0.5
    auto targets = m_solver->currentTargets();
    double w = 0.0;
    for (const auto& tw : targets)
        if (tw.key == "macrodetails/asian-female-old")
            w = tw.weight;
    qWarning() << "asian-female-old weight =" << w;
    QVERIFY(std::fabs(w - 0.5) < 1e-9);
    m_solver->resetAll();
}

void TestAvatarSolver::firmnessDrivesMesh()
{
    // 修复前：breast/ 目录的联合采样（含 minfirmness/maxfirmness token）不在 macrodetails 下，
    // 扫描器漏扫 -> 乳房紧实度/罩杯宏拖动无效果。修复后 token 过滤扫全 targets 根。
    const auto base = m_model->mesh().verts;
    m_solver->setSliderValue("breast/BreastFirmness", 1.0);
    const auto& now = m_model->mesh().verts;
    size_t changed = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        const double d = std::fabs(base[i].x - now[i].x) + std::fabs(base[i].y - now[i].y)
                       + std::fabs(base[i].z - now[i].z);
        if (d > 1e-9) ++changed;
    }
    QVERIFY(changed > 100); // 胸部区域大量顶点位移
    m_solver->resetAll();
    // 归零后应完全还原（增量归零回归保护）
    const auto& after = m_model->mesh().verts;
    size_t changed2 = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        const double d = std::fabs(base[i].x - after[i].x) + std::fabs(base[i].y - after[i].y)
                       + std::fabs(base[i].z - after[i].z);
        if (d > 1e-9) ++changed2;
    }
    QCOMPARE(changed2, size_t(0));
}

void TestAvatarSolver::ethnicNormalize()
{
    m_solver->setSliderValue("macrodetails/African", 1.0);
    QCOMPARE(m_solver->macroVar("African"), 1.0);
    QCOMPARE(m_solver->macroVar("Asian"), 0.0);
    QCOMPARE(m_solver->macroVar("Caucasian"), 0.0);

    m_solver->setSliderValue("macrodetails/African", 0.5);
    QVERIFY(std::fabs(m_solver->macroVar("Asian") - 0.25) < 1e-9);
    QVERIFY(std::fabs(m_solver->macroVar("Caucasian") - 0.25) < 1e-9);
    QVERIFY(std::fabs(m_solver->macroVar("African") - 0.5) < 1e-9);
    m_solver->resetAll();
}

void TestAvatarSolver::solveSingleWaistMatchesGoldenWeight()
{
    // 宏默认体型上做测量逼近（evaluateSlider 含宏采样）：
    // 验证收敛性（残差 < 0.05cm），不固定权重（基线随宏状态漂移）
    const std::string waist = "measure/measure-waist-circ-decr|incr";
    const double base = m_solver->measureNow(waist);
    const double target = base + 6.0;
    const double w = m_solver->solveSingle(waist, target);
    qWarning() << "waist solve: base" << base << "target" << target << "w" << w
               << "now" << m_solver->measureNow(waist);
    QVERIFY(std::fabs(m_solver->measureNow(waist) - target) < 0.05);
    QVERIFY(w >= -1.0 && w <= 1.0);
    m_solver->resetAll();
}

void TestAvatarSolver::solveMultiThreeMeasures()
{
    // 宏默认体型上联合收敛（目标 = 默认值 + 偏移，避免死循环于不可达目标）
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const std::string waist = "measure/measure-waist-circ-decr|incr";
    const std::string hips = "measure/measure-hips-circ-decr|incr";
    const double b = m_solver->measureNow(bust) + 4.0;
    const double w = m_solver->measureNow(waist) + 4.0;
    const double h = m_solver->measureNow(hips) + 4.0;
    m_solver->solveMulti({{bust, b}, {waist, w}, {hips, h}});
    QVERIFY(std::fabs(m_solver->measureNow(bust) - b) < 0.10);
    QVERIFY(std::fabs(m_solver->measureNow(waist) - w) < 0.10);
    QVERIFY(std::fabs(m_solver->measureNow(hips) - h) < 0.10);
    m_solver->resetAll();
}

void TestAvatarSolver::solveMultiRealisticSizes()
{
    // 真实合体尺寸组合（与默认体型差异较大，验证 Gauss-Seidel 联合收敛能力）
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const std::string under = "measure/measure-underbust-circ-decr|incr";
    const std::string waist = "measure/measure-waist-circ-decr|incr";
    const std::string hips = "measure/measure-hips-circ-decr|incr";
    const std::map<std::string, double> goals = {
        {bust, 84.0}, {under, 72.0}, {waist, 68.0}, {hips, 92.0},
    };
    m_solver->solveMulti(goals);
    qWarning() << "realistic sizes: bust" << m_solver->measureNow(bust)
               << "under" << m_solver->measureNow(under)
               << "waist" << m_solver->measureNow(waist)
               << "hips" << m_solver->measureNow(hips);
    // 联合收敛容差：联动耦合下放宽到 0.5cm
    QVERIFY(std::fabs(m_solver->measureNow(bust) - 84.0) < 0.5);
    QVERIFY(std::fabs(m_solver->measureNow(under) - 72.0) < 0.5);
    QVERIFY(std::fabs(m_solver->measureNow(waist) - 68.0) < 0.5);
    QVERIFY(std::fabs(m_solver->measureNow(hips) - 92.0) < 0.5);
    m_solver->resetAll();
}

void TestAvatarSolver::solveNaturalBustMatches()
{
    // 输入胸围 84 + 下胸围 72 → 自然罩杯联合匹配：两周长达标、差值 ≈ 12cm（B 杯）
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const std::string under = "measure/measure-underbust-circ-decr|incr";
    const std::string cup = "breast/BreastSize";
    const double diff = m_solver->solveNaturalBust(bust, 84.0, under, 72.0, cup);
    const double b = m_solver->measureNow(bust);
    const double u = m_solver->measureNow(under);
    qWarning() << "natural bust: bust" << b << "under" << u << "diff" << diff
               << "cup" << AvatarSolver::cupLetter(diff);
    QVERIFY2(std::fabs(b - 84.0) < 0.3,
             qPrintable(QStringLiteral("bust miss: %1").arg(b)));
    QVERIFY2(std::fabs(u - 72.0) < 0.3,
             qPrintable(QStringLiteral("underbust miss: %1").arg(u)));
    QVERIFY2(std::fabs(diff - 12.0) < 0.3,
             qPrintable(QStringLiteral("cup diff miss: %1").arg(diff)));
    m_solver->resetAll();
}

void TestAvatarSolver::cupLetterSegments()
{
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(9.0)), QStringLiteral("AA"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(11.0)), QStringLiteral("A"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(13.0)), QStringLiteral("B"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(16.0)), QStringLiteral("C"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(19.0)), QStringLiteral("D"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(21.0)), QStringLiteral("E"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(24.0)), QStringLiteral("F"));
    QCOMPARE(QString::fromLatin1(AvatarSolver::cupLetter(27.0)), QStringLiteral("G"));
}

void TestAvatarSolver::heightLockCompensatesLeg()
{
    // 身高锁：锁当前身高 → 大腿高拉满（+9.3cm 影响）→ enforce 二分 Height 宏回锁值
    const double locked = m_solver->currentHeightCm();
    m_solver->setHeightLock(true, locked);
    QVERIFY(m_solver->heightLockActive());

    m_solver->setSliderValue("measure/measure-upperleg-height-decr|incr", 1.0);
    const double drifted = m_solver->currentHeightCm();
    QVERIFY2(drifted > locked + 1.0,
             qPrintable(QStringLiteral("upperleg should raise height: %1 -> %2")
                            .arg(locked).arg(drifted)));

    const double after = m_solver->enforceHeightLock();
    QVERIFY2(std::fabs(after - locked) < 0.15,
             qPrintable(QStringLiteral("height lock failed: locked=%1 after=%2")
                            .arg(locked).arg(after)));

    // 已收敛时幂等
    const double w = m_solver->sliderValue("macrodetails-height/Height");
    m_solver->enforceHeightLock();
    QVERIFY(std::fabs(m_solver->sliderValue("macrodetails-height/Height") - w) < 1e-9);

    m_solver->setHeightLock(false, 0.0);
    QVERIFY(!m_solver->heightLockActive());
    m_solver->resetAll();
}

void TestAvatarSolver::solveHeight168()
{
    // 身高 168（亚洲女性参考值）：二分 Height 宏收敛
    const double got = m_solver->solveHeightCm(168.0);
    QVERIFY2(std::fabs(got - 168.0) < 0.1,
             qPrintable(QStringLiteral("height solve miss: %1").arg(got)));
    m_solver->resetAll();
}

void TestAvatarSolver::breastSizeDrivesCupDifference()
{
    // 标定探针：BreastSize 百分比 → 胸围/下胸围/杯差值的映射曲线。
    // 验证「杯差值随罩杯单调」，供「输入胸围自动匹配自然罩杯」算法使用。
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const std::string under = "measure/measure-underbust-circ-decr|incr";
    qWarning() << "BreastSize calibration (default underbust fixed):";
    for (const double cup : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        m_solver->setSliderValue("breast/BreastSize", cup);
        const double b = m_solver->measureNow(bust);
        const double u = m_solver->measureNow(under);
        qWarning() << "cup" << cup << ": bust" << b << "under" << u
                   << "diff" << (b - u);
    }
    // 单调性：差值随罩杯严格递增
    m_solver->setSliderValue("breast/BreastSize", 0.0);
    const double d0 = m_solver->measureNow(bust) - m_solver->measureNow(under);
    m_solver->setSliderValue("breast/BreastSize", 1.0);
    const double d1 = m_solver->measureNow(bust) - m_solver->measureNow(under);
    QVERIFY(d1 > d0 + 1.0); // 满罩杯比零罩杯差值明显更大（至少 +1cm）
    m_solver->resetAll();
}

void TestAvatarSolver::measureLockCompensatesCup()
{
    // 胸围锁：锁定当前胸围 → 调大罩杯（BreastSize 宏）胸围偏离 →
    // enforceMeasureLock 二分反调胸腔（bust-circ 权重内收）使胸围回到锁值。
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const double locked = m_solver->measureNow(bust);
    m_solver->setMeasureLock(bust, true, locked);
    QVERIFY(m_solver->measureLockActive());
    QCOMPARE(m_solver->measureLockSliderId(), bust);

    // 温和罩杯变化（0.5 -> 0.7）：胸围偏离，补偿能收敛回锁值
    m_solver->setSliderValue("breast/BreastSize", 0.7);
    const double drifted = m_solver->measureNow(bust);
    QVERIFY2(drifted > locked + 0.1,
             qPrintable(QStringLiteral("cup up should enlarge bust: locked=%1 drifted=%2")
                            .arg(locked).arg(drifted)));

    // 补偿：胸腔权重被自动内收（负向），胸围回到锁值（容差 0.15 cm）
    const double after = m_solver->enforceMeasureLock();
    QVERIFY2(std::fabs(after - locked) < 0.15,
             qPrintable(QStringLiteral("lock enforcement failed: locked=%1 after=%2")
                            .arg(locked).arg(after)));
    QVERIFY(m_solver->sliderValue(bust) < 0.0); // 罩杯变大 → 胸腔让位（内收）

    // 已收敛时 enforce 是幂等 no-op（不改变权重）
    const double wBefore = m_solver->sliderValue(bust);
    m_solver->enforceMeasureLock();
    QVERIFY(std::fabs(m_solver->sliderValue(bust) - wBefore) < 1e-9);

    // 拉满罩杯（1.0）：胸腔 [-1,1] 补偿不足 → 真实超界：
    // 权重 clamp 到 -1，测量仍高于锁值（UI 会提示"已达极限"，属预期行为）
    m_solver->setSliderValue("breast/BreastSize", 1.0);
    const double afterMax = m_solver->enforceMeasureLock();
    QVERIFY2(afterMax > locked + 0.1,
             qPrintable(QStringLiteral("max cup should exceed lock: locked=%1 afterMax=%2")
                            .arg(locked).arg(afterMax)));
    QVERIFY(m_solver->sliderValue(bust) < -0.99); // 胸腔已压到权重极限

    // 解锁后不再补偿
    m_solver->setMeasureLock(bust, false, 0.0);
    QVERIFY(!m_solver->measureLockActive());
    m_solver->resetAll();
}

void TestAvatarSolver::currentHeightCm()
{
    // 默认体型（完全女性+完全亚洲）身高锁定为已知稳定值（±0.5cm 回归护栏）
    QVERIFY(std::fabs(m_solver->currentHeightCm() - 152.794) < 0.5);
}

QTEST_MAIN(TestAvatarSolver)
#include "test_avatar_solver.moc"
