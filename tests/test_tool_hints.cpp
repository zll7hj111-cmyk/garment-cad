/// @file test_tool_hints.cpp
/// TOOL_SYSTEM_AUDIT H3 (2026-08-29) / P3 (2026-12): 状态栏工具提示的唯一
/// 出处 = 各工具 ToolDescriptor::describe() 的 hintText, toolHintText(ToolType)
/// 查 ToolRegistry 取回。历史根因: MainWindow 的 if/else 链没有 AngleMeasure
/// 分支, else 兜底让角度测量一直显示「选择」的操作说明 —— 兜底把"具体工具
/// 的文案"当默认值是结构性错误。本测试锁三条契约:
/// ①每个 ToolType 都有非空专属提示 (新增工具漏写 = 红);
/// ②提示以自己的工具名开头且互不相同 (防复制粘贴串行);
/// ③旋转提示必须描述 D15 确认门 (选中 ≠ 可直接拖, H2 文案拍板)。

#include <QtTest>
#include <QCoreApplication>
#include <QSet>
#include <QString>
#include <iterator>   // std::size (N6: kAllTypes 与注册序对账)

#include "tools/ToolRegistry.h"

using cad::tools::ToolType;
using cad::tools::toolHintText;

namespace {

/// u8 字面量 → const char* (L4): 全局数组初始化不能直接用 reinterpret_cast
/// (非常量表达式), 经普通函数运行时求值。u8 保证 UTF-8 字节, 与工具
/// describe().hintText 的编码一致。
[[nodiscard]] const char* u8c(const char8_t* s) { return reinterpret_cast<const char*>(s); }

/// 8 个 ToolType 全集。
///
/// N6 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 这个文件手写 8 项, 光靠它
/// **拦不住** "新增了第 9 个 ToolType 却忘了补进数组" —— 数组少一项既不
/// 编译失败也不断言失败, 新工具就悄悄逃出了守卫 (旧注释的说法不准确)。
/// 真正的兜底是下面的 kExpectedRegistered + registry.order().size() 断言:
/// 注册序多一个工具 → 数量对不上 → 立刻红。两处一起用才闭环。
const ToolType kAllTypes[] = {
    ToolType::Select,  ToolType::SmartPen,   ToolType::CurveEdit,
    ToolType::Rotate,  ToolType::Break,      ToolType::Intersection,
    ToolType::Measure, ToolType::AngleMeasure,
};

/// 注册序预期规模 —— 新增工具必须同步 +1, 否则 registryOrderCoversEveryTool 红。
constexpr int kExpectedRegistered = 8;

} // namespace

class TestToolHints : public QObject
{
    Q_OBJECT

private slots:
    void everyToolTypeHasHint();
    void registryOrderCoversEveryTool();
    void hintsAreDistinctAndSelfNamed();
    void rotateHintDescribesConfirmGate();
};

void TestToolHints::everyToolTypeHasHint()
{
    for (ToolType t : kAllTypes) {
        const QString hint = toolHintText(t);
        QVERIFY2(!hint.isEmpty(),
                 QString("toolHintText(ToolType %1) must not be empty")
                     .arg(int(t)).toUtf8().constData());
    }
}

void TestToolHints::registryOrderCoversEveryTool()
{
    // N6: kAllTypes 是手写清单, 单靠它拦不住"加了第 9 个工具却忘了补进
    // 清单"。注册序是权威来源 (ToolRegistry 构造时全量注册), 拿它跟清单
    // 对账: 数量不符 = 有人加了工具但没同步本文件 → 红。
    const auto& order = cad::tools::ToolRegistry::instance().order();
    QCOMPARE(static_cast<int>(order.size()), kExpectedRegistered);
    QCOMPARE(static_cast<int>(std::size(kAllTypes)), kExpectedRegistered);

    // 双向覆盖: 清单里的每个类型都必须真的注册了 (防枚举值输错/重复)。
    QSet<int> registered;
    for (ToolType t : order)
        registered.insert(static_cast<int>(t));
    for (ToolType t : kAllTypes) {
        QVERIFY2(registered.contains(static_cast<int>(t)),
                 QString("ToolType %1 is in kAllTypes but not registered")
                     .arg(int(t)).toUtf8().constData());
    }
}

void TestToolHints::hintsAreDistinctAndSelfNamed()
{
    QSet<QString> seen;
    const struct { ToolType type; const char* prefix; } expect[] = {
        { ToolType::Select,       u8c(u8"选择") },
        { ToolType::SmartPen,     u8c(u8"智能笔") },
        { ToolType::CurveEdit,    u8c(u8"曲线") },
        { ToolType::Rotate,       u8c(u8"旋转") },
        { ToolType::Break,        u8c(u8"打断") },
        { ToolType::Intersection, u8c(u8"交点") },
        { ToolType::Measure,      u8c(u8"测量") },
        { ToolType::AngleMeasure, u8c(u8"角度测量") },
    };
    for (const auto& e : expect) {
        const QString hint = toolHintText(e.type);
        QVERIFY2(hint.startsWith(QString::fromUtf8(e.prefix)),
                 QString("hint for ToolType %1 must start with its own name, got: %2")
                     .arg(int(e.type)).arg(hint).toUtf8().constData());
        seen.insert(hint);
    }
    // 角度测量曾复用「选择」提示 —— 互异断言让同类错配无处藏身。
    QCOMPARE(seen.size(), 8);
}

void TestToolHints::rotateHintDescribesConfirmGate()
{
    // H2 配套: 旧文案「点击线段选中 | 拖动旋转」描述的是废弃行为 ——
    // 实际有 D15 确认门, 提示必须教用户"右键或回车确认"。
    const QString hint = toolHintText(ToolType::Rotate);
    QVERIFY2(hint.contains(QString::fromUtf8("右键或回车确认")),
             qPrintable(QStringLiteral("rotate hint must teach the confirm gate: ")
                        + hint));
}

QTEST_MAIN(TestToolHints)
#include "test_tool_hints.moc"
