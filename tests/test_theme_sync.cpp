/// @file test_theme_sync.cpp
/// Theme ↔ CanvasStyle 令牌同步守卫（2026-12 审计 P0-3 / 工单 G3）。
/// 两张令牌表分属 ui 与 canvas 两层，无法共享同一个类型，历史上只靠注释
/// 「Kept in sync ... by hand」维持。本测试把这份手工契约变成可执行断言：
/// 同语义令牌的 QColor 必须逐通道相等（含 alpha），任何一侧改动而另一侧
/// 未跟随都会在这里失败，而不是在界面上变成两个颜色。
///
/// 有意分歧（不是漂移，本测试显式锁定，改动时必须同步更新本文件）：
///  1. 暗色画布点的填充色是纯白 #FFFFFF，而暗色 ThemeTokens::text1 是柔和白
///     #ECE9E2 —— 点要比文字更亮才在夜纸上立得住（CanvasStyle.cpp darkTheme）。
///  2. 暗色 roleDefaults() 反向引用浅色系（Outline 纯白、Internal 取浅色
///     borderStrong、Auxiliary 取暗色 text2），因为画布线在夜纸上要取亮线家族。
///  3. 画布专属色（曲线锚点粉、放置点琥珀、框选蓝、测量橙、HUD 暗胶囊……）
///     在 ThemeTokens 没有对应项，不在此断言范围内。

#include <QtTest>
#include <QColor>
#include <QStringList>

#include "ui/Theme.h"
#include "canvas/CanvasStyle.h"
#include "parametric/Segment.h"

// 注意：CanvasStyle / EntityState 在全局命名空间（src/canvas/CanvasStyle.h，历史遗留），
// 不在 cad::canvas 下；这里直接用全局名。
using cad::ui::ThemeTokens;

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexArgb); }

/// 收集全部不一致后再一次性失败：一次运行就能看到所有漂移点，
/// 不必改一处跑一次。
class Collector
{
public:
    void add(const char* what, const QColor& canvas, const QColor& theme)
    {
        if (canvas != theme) {
            m_mismatches << QStringLiteral("%1: CanvasStyle %2 != ThemeTokens %3")
                                .arg(QString::fromLatin1(what), hex(canvas), hex(theme));
        }
    }

    void equal(const char* what, const QColor& actual, const QColor& expected)
    {
        if (actual != expected) {
            m_mismatches << QStringLiteral("%1: %2 != %3")
                                .arg(QString::fromLatin1(what), hex(actual), hex(expected));
        }
    }

    [[nodiscard]] QString report() const
    {
        if (m_mismatches.isEmpty()) {
            return QString();
        }
        return QStringLiteral("Theme/CanvasStyle 令牌漂移 %1 处:\n  ")
             + m_mismatches.join(QStringLiteral("\n  "));
    }

private:
    QStringList m_mismatches;
};

} // namespace

class TestThemeSync : public QObject
{
    Q_OBJECT

private slots:
    void lightCanvasStyleMirrorsTheme();
    void darkCanvasStyleMirrorsTheme();
    void defaultAppearanceSingleSource();
};

void TestThemeSync::lightCanvasStyleMirrorsTheme()
{
    const ThemeTokens t = ThemeTokens::light();
    const CanvasStyle s = CanvasStyle::lightTheme();
    Collector c;

    // ── 面 / 底 / 发丝线 ──
    c.add("canvasBackground", s.canvasBackground, t.canvasBg);
    c.add("surfaceColor()", s.surfaceColor(), t.surface);
    c.add("borderSoft()", s.borderSoft(), t.border);
    c.add("crosshairColor", s.crosshairColor, t.borderStrong);
    c.add("accentWash()", s.accentWash(), t.accentTint);

    // ── 文字与强调色 ──
    c.add("hudText", s.hudText, t.text1);
    c.add("pointColor(Normal,false)", s.pointColor(EntityState::Normal, false), t.text1);
    c.add("labelColor(Normal,false)", s.labelColor(EntityState::Normal, false), t.text2);
    c.add("textSecondary()", s.textSecondary(), t.text2);
    c.add("selectColorForBadge()", s.selectColorForBadge(), t.accent);
    c.add("pointColor(Hover,false)", s.pointColor(EntityState::Hover, false), t.accent);
    c.add("previewLineColor", s.previewLineColor, t.accent);

    // ── 语义色（连接 / 警告 / 吸附成功） ──
    c.add("attachmentNodeColor", s.attachmentNodeColor, t.teal);
    c.add("lockedAttachmentColor", s.lockedAttachmentColor, t.warning);
    c.add("snapIndicatorColor", s.snapIndicatorColor, t.success);
    c.add("snapPointColor", s.snapPointColor, t.success);
    c.add("auxMarkerColor", s.auxMarkerColor, t.success);
    c.add("pointColor(Normal,true)", s.pointColor(EntityState::Normal, true), t.success);
    c.add("labelColor(Normal,true)", s.labelColor(EntityState::Normal, true), t.success);

    // ── 角色默认线色（浅色系直接取自文字色阶） ──
    c.add("roleDefaults(Outline)", s.roleDefaults(cad::param::SegmentRole::Outline).color, t.text1);
    c.add("roleDefaults(Internal)", s.roleDefaults(cad::param::SegmentRole::Internal).color, t.text2);
    c.add("roleDefaults(Auxiliary)", s.roleDefaults(cad::param::SegmentRole::Auxiliary).color, t.text3);

    // ── 跨主题引用：暗胶囊字取暗色 text1；纯白徽标字取 onAccent ──
    c.add("hudDarkPillFg", s.hudDarkPillFg, ThemeTokens::dark().text1);
    c.add("gizmoBadgeFg", s.gizmoBadgeFg, t.onAccent);

    QVERIFY2(c.report().isEmpty(), qPrintable(c.report()));
}

void TestThemeSync::darkCanvasStyleMirrorsTheme()
{
    const ThemeTokens t = ThemeTokens::dark();
    const CanvasStyle s = CanvasStyle::darkTheme();
    Collector c;

    // ── 面 / 底 / 发丝线（暗色下 crosshair 改用 border 而非 borderStrong） ──
    c.add("canvasBackground", s.canvasBackground, t.canvasBg);
    c.add("surfaceColor()", s.surfaceColor(), t.surface);
    c.add("borderSoft()", s.borderSoft(), t.border);
    c.add("crosshairColor", s.crosshairColor, t.border);
    c.add("accentWash()", s.accentWash(), t.accentTint);

    // ── 文字与强调色 ──
    c.add("hudText", s.hudText, t.text1);
    c.add("labelColor(Normal,false)", s.labelColor(EntityState::Normal, false), t.text2);
    c.add("textSecondary()", s.textSecondary(), t.text2);
    c.add("selectColorForBadge()", s.selectColorForBadge(), t.accent);
    c.add("pointColor(Hover,false)", s.pointColor(EntityState::Hover, false), t.accent);
    c.add("previewLineColor", s.previewLineColor, t.accent);

    // ── 语义色 ──
    c.add("attachmentNodeColor", s.attachmentNodeColor, t.teal);
    c.add("lockedAttachmentColor", s.lockedAttachmentColor, t.warning);
    c.add("snapIndicatorColor", s.snapIndicatorColor, t.success);
    c.add("snapPointColor", s.snapPointColor, t.success);
    c.add("auxMarkerColor", s.auxMarkerColor, t.success);
    c.add("pointColor(Normal,true)", s.pointColor(EntityState::Normal, true), t.success);
    c.add("labelColor(Normal,true)", s.labelColor(EntityState::Normal, true), t.success);

    // ── 有意分歧 1：暗色点填充是纯白，比暗色 text1 (#ECE9E2) 更亮 ──
    c.equal("pointColor(Normal,false) [deliberate pure white]",
            s.pointColor(EntityState::Normal, false), QColor(255, 255, 255));
    QVERIFY2(s.pointColor(EntityState::Normal, false) != t.text1,
             "暗色点填充已等于 text1；若这是有意收敛，请同步更新本测试的分歧说明");

    // ── 有意分歧 2：暗色 roleDefaults 反向引用浅色系 ──
    c.equal("roleDefaults(Outline) [deliberate pure white]",
            s.roleDefaults(cad::param::SegmentRole::Outline).color, QColor(255, 255, 255));
    c.add("roleDefaults(Internal)", s.roleDefaults(cad::param::SegmentRole::Internal).color,
          ThemeTokens::light().borderStrong);
    c.add("roleDefaults(Auxiliary)", s.roleDefaults(cad::param::SegmentRole::Auxiliary).color,
          t.text2);

    // ── 跨主题引用 ──
    c.add("hudDarkPillFg", s.hudDarkPillFg, t.text1);
    c.add("gizmoBadgeFg", s.gizmoBadgeFg, t.onAccent);

    QVERIFY2(c.report().isEmpty(), qPrintable(c.report()));
}

/// 审计 P1-9：默认线宽 1.2 与默认线色 (30,30,30) 曾在模型（Segment.h）、
/// 画布（CanvasStyle EntityPaintParams / roleDefaults）与反序列化
/// （DocumentSerializer 缺省字段回退）三处各自写死；任一处漂移都会让
/// 「同一条线」在不同路径上呈现不同外观。此断言把三者推导自同一常量
/// （src/parametric/DefaultAppearance.h）变成可执行契约。
void TestThemeSync::defaultAppearanceSingleSource()
{
    const cad::param::Segment fresh;
    QCOMPARE(fresh.weight, cad::param::kDefaultSegmentWeight);
    QCOMPARE(fresh.color.rgb(), cad::param::defaultSegmentColor().rgb());

    const EntityPaintParams paint;
    QCOMPARE(paint.lineWidth, cad::param::kDefaultSegmentWeight);

    const CanvasStyle light = CanvasStyle::lightTheme();
    const CanvasStyle dark = CanvasStyle::darkTheme();
    QCOMPARE(light.roleDefaults(cad::param::SegmentRole::Outline).weight,
             cad::param::kDefaultSegmentWeight);
    QCOMPARE(dark.roleDefaults(cad::param::SegmentRole::Outline).weight,
             cad::param::kDefaultSegmentWeight);
}

QTEST_GUILESS_MAIN(TestThemeSync)

#include "test_theme_sync.moc"
