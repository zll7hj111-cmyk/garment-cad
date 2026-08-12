#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <tracy/Tracy.hpp>

// Smoke test for the two new third-party integrations:
//   spdlog -- log to a rotating/basic file sink, then read it back
//   Tracy  -- ZoneScoped macro compiles and the probe runs (no-op unless a
//             profiler is connected; TRACY_ON_DEMAND=ON).
class TestLogProbe : public QObject
{
    Q_OBJECT

private slots:
    void spdlogWritesFile();
    void tracyZoneCompiles();
};

static int tracyProbe(int n)
{
    ZoneScoped;
    int acc = 0;
    for (int i = 0; i < n; ++i)
        acc += i;
    return acc;
}

void TestLogProbe::spdlogWritesFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath("probe.log");
    try
    {
        auto logger = spdlog::basic_logger_mt("probe", logPath.toStdString());
        logger->info("hello spdlog {}", 42);
        logger->flush();
        spdlog::drop("probe");
    }
    catch (const spdlog::spdlog_ex &)
    {
        QFAIL("spdlog init failed");
    }
    QFile f(logPath);
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("hello spdlog 42"));
}

void TestLogProbe::tracyZoneCompiles()
{
    QCOMPARE(tracyProbe(10), 45);
}

QTEST_MAIN(TestLogProbe)
#include "test_log_probe.moc"
