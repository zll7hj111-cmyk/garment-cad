// AvatarPanel UI 集成测试：真实信号链路（滑杆拖动/重置/导出）+ 宏滑杆联动。
#include "avatar/AvatarPanel.h"
#include "avatar/AvatarSolver.h"
#include "avatar/Mesh3D.h"

#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QtTest>

#include <cmath>
#include <string>

#ifndef AVATAR_ASSETS_DIR
#define AVATAR_ASSETS_DIR "assets/avatar"
#endif

using namespace cad::avatar;

class TestAvatarPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void panelConstructsAndLoads();
    void sliderDrivesMesh();
    void macroSliderDrivesMesh();
    void resetRestoresDefault();
    void exportObjRoundtrip();

private:
    int sliderIndex(const std::string& id) const;
    QSlider* rowSlider(int idx) const;
    double waistCm() const;

    std::unique_ptr<cad::ui::AvatarPanel> m_panel;
};

void TestAvatarPanel::initTestCase()
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    m_panel = std::make_unique<cad::ui::AvatarPanel>();
    QVERIFY(m_panel->loadDefault());
}

int TestAvatarPanel::sliderIndex(const std::string& id) const
{
    const auto& sliders = m_panel->solver()->sliders();
    for (size_t i = 0; i < sliders.size(); ++i)
        if (sliders[i].id == id)
            return static_cast<int>(i);
    return -1;
}

QSlider* TestAvatarPanel::rowSlider(int idx) const
{
    return m_panel->findChild<QSlider*>(QStringLiteral("slider_%1").arg(idx));
}

double TestAvatarPanel::waistCm() const
{
    return m_panel->solver()->measureNow("measure/measure-waist-circ-decr|incr");
}

void TestAvatarPanel::panelConstructsAndLoads()
{
    QCOMPARE(m_panel->solver()->sliders().size(), size_t(291)); // 数据层含 hidden
    QCOMPARE(m_panel->findChildren<QSlider*>().size(), 107);    // 82 可见滑杆 + 「调整」页 25 个复制滑杆（全身2+上半身16+下半身7）
    // 默认体型：完全女性 Gender=0
    const int genderIdx = sliderIndex("macrodetails/Gender");
    QVERIFY(genderIdx >= 0);
    QCOMPARE(rowSlider(genderIdx)->value(), 0);
    // 身高状态行
    auto* heightLabel = m_panel->findChild<QLabel*>(QStringLiteral("heightLabel"));
    QVERIFY(heightLabel);
    QVERIFY(heightLabel->text().contains(QStringLiteral("152.8")));
}

void TestAvatarPanel::sliderDrivesMesh()
{
    const int waistIdx = sliderIndex("measure/measure-waist-circ-decr|incr");
    QVERIFY(waistIdx >= 0);
    const double before = waistCm();
    rowSlider(waistIdx)->setValue(60); // 权重 0.6
    const double after = waistCm();
    QVERIFY(after > before + 1.0); // 腰围显著增大
    QVERIFY(std::fabs(m_panel->solver()->sliderValue("measure/measure-waist-circ-decr|incr") - 0.6) < 1e-9);
    m_panel->resetToDefault();
}

void TestAvatarPanel::macroSliderDrivesMesh()
{
    const int genderIdx = sliderIndex("macrodetails/Gender");
    const auto beforeMesh = m_panel->solver()->mesh().verts;
    // 默认已是纯女性（Gender=0），拨到 100（纯男性）验证宏滑块驱动网格
    rowSlider(genderIdx)->setValue(100);
    QCOMPARE(m_panel->solver()->macroVar("Gender"), 1.0);
    QVERIFY(m_panel->solver()->mesh().verts != beforeMesh);
    auto targets = m_panel->solver()->currentTargets();
    bool maleApplied = false;
    for (const auto& tw : targets)
        if (tw.key.find("macrodetails/universal-male-young-averagemuscle-averageweight") == 0)
            maleApplied = std::fabs(tw.weight - 1.0) < 1e-9;
    QVERIFY(maleApplied);
    m_panel->resetToDefault();
}

void TestAvatarPanel::resetRestoresDefault()
{
    const int genderIdx = sliderIndex("macrodetails/Gender");
    const int waistIdx = sliderIndex("measure/measure-waist-circ-decr|incr");
    const double defaultWaist = waistCm(); // 当前默认体型（完全女性 + 完全亚洲）腰围
    rowSlider(waistIdx)->setValue(60);
    rowSlider(genderIdx)->setValue(100); // 男性
    m_panel->resetToDefault();
    QCOMPARE(rowSlider(genderIdx)->value(), 0);
    QCOMPARE(rowSlider(waistIdx)->value(), 0);
    QVERIFY(std::fabs(waistCm() - defaultWaist) < 0.5); // 默认体型腰围复原
}

void TestAvatarPanel::exportObjRoundtrip()
{
    const QString path = QDir::temp().filePath(QStringLiteral("avatar_panel_test.obj"));
    const size_t vertCount = m_panel->solver()->mesh().verts.size();
    QVERIFY(m_panel->exportObjTo(path));
    Mesh3D reloaded = loadObjFile(path.toStdString());
    QCOMPARE(reloaded.verts.size(), vertCount);
    QFile::remove(path);
}

QTEST_MAIN(TestAvatarPanel)
#include "test_avatar_panel.moc"
