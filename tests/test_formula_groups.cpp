/// 公式页分组回归测试 (2026-08):
/// ① VirtualCardList 结构性 setRows 后, 存活行 (同 key 幸存) 必须重新绑定
///    —— 否则分组头的成员数量/折叠箭头/卡片序号停留在旧值 (用户报告
///    "组的公式数量不随着加入更新"、"打开小箭头经常显示错误");
/// ② FormulaTabModel: 拖入分组/撤销/折叠的行结构;
/// ③ FormulaCard::setGrouped 缩进 (组内公式右移, 与未分组区分);
/// ④ VirtualCardList 缓存池: 消失行 widget park 而非销毁, 同 key 返回时
///    原实例复用 (展开/回滚不重建);
/// ⑤ 卡片序号 (setIndex, 各页统一) + 交替色 (setAlternate, 缓存复用下
///    奇偶随新行号重设).
///
/// 注意: VirtualCardList/FormulaGroupHeader/FormulaCard 是全局命名空间类
/// (与 VariableCard 一致), 仅 FormulaTabModel 在 cad::ui 命名空间内.

#include <QtTest>
#include <QApplication>
#include <QHash>
#include <QVBoxLayout>
#include <QUuid>
#include <QUndoStack>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <cmath>

#include "ElaText.h"
#include "ui/VirtualCardList.h"
#include "ui/FormulaTabModel.h"
#include "ui/FormulaGroupHeader.h"
#include "ui/FormulaCard.h"
#include "ui/VariableCard.h"
#include "ui/LinkedCard.h"
#include "ui/MeasureCard.h"
#include "ui/AngleMeasureCard.h"
#include "parametric/ParamDocument.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Variable.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"

using namespace cad::param;

class TestFormulaGroups : public QObject
{
    Q_OBJECT
private slots:
    void rebindSurvivorsOnStructureChange();  ///< VirtualCardList 核心回归
    void moveIntoGroupUpdatesRowsAndCount();  ///< 拖入分组: 行结构 + 数量
    void moveIntoGroupUndoRestores();         ///< 命令 undo 还原
    void collapseToggleHidesMemberRows();     ///< 折叠: 成员行消失/恢复
    void groupedCardIndents();                ///< 组内卡片右移缩进
    void cardOrdinalsAndAlternate();          ///< 序号 + 类型色竖线 (缓存复用配套)
};

// ── ① VirtualCardList: 结构变化后存活行必须重绑定 ──
// 用真实 FormulaGroupHeader 做分组头行, binder 镜像 VariablePanel 的
// 头部分支 (setCount/setCollapsed), 断言成员数量文本实时刷新.
// 场景: 头 + 2 成员 → 拖 1 个成员出组 (行序变化, 头幸存) → 折叠 → 展开.
void TestFormulaGroups::rebindSurvivorsOnStructureChange()
{
    VirtualCardList host;
    QVector<QUuid> keys;                       // 当前结构 (binder/factory 用它定位 key)
    QHash<QUuid, int> bindCount;               // 每个 key 的绑定次数
    const QUuid hdr = QUuid::createUuid();
    const QUuid m1  = QUuid::createUuid();
    const QUuid m2  = QUuid::createUuid();
    int groupCount = 0;
    bool groupCollapsed = false;
    host.setProviders(
        [&host, &keys, hdr](int row) -> QWidget* {
            if (keys[row] == hdr)
                return new FormulaGroupHeader(
                    hdr, QStringLiteral("G1"), false, 0, &host);
            return new QWidget(&host);
        },
        [&keys, &bindCount, &groupCount, &groupCollapsed](int row, QWidget* w) {
            ++bindCount[keys[row]];
            if (auto* h = qobject_cast<FormulaGroupHeader*>(w)) {
                h->setCount(groupCount);       // 同 VariablePanel binder
                h->setCollapsed(groupCollapsed);
            }
        });

    // 结构 1: [头G1(2), m1, m2] 展开态, 数量 2.
    groupCount = 2;
    keys = {hdr, m1, m2};
    host.setRows(keys);
    host.ensureMaterialized(hdr);
    host.ensureMaterialized(m1);
    host.ensureMaterialized(m2);
    QCOMPARE(bindCount.value(hdr), 1);
    QWidget* m2Widget = host.widgetFor(m2);
    QVERIFY(m2Widget);

    // 结构 2: 拖 m1 出组 → 未分组段在前: [m1, 头G1(1), m2].
    // keys 顺序改变 = 结构性变化; 头 widget 同 key 幸存, 必须重绑定,
    // 数量文本从 "(2)" 刷新为 "(1)" (旧实现停留 "(2)").
    groupCount = 1;
    QWidget* survivor = host.widgetFor(hdr);
    QVERIFY(survivor);
    keys = {m1, hdr, m2};
    host.setRows(keys);
    QCOMPARE(host.widgetFor(hdr), survivor);   // 幸存而非重建
    QCOMPARE(bindCount.value(hdr), 2);         // 重绑定已发生 (核心回归)
    auto* countLabel = survivor->findChild<ElaText*>(QStringLiteral("groupCount"));
    QVERIFY(countLabel);
    QCOMPARE(countLabel->text(), QStringLiteral("(1)"));

    // 结构 3: 折叠 → [m1, 头G1]: m2 行消失, 头再次重绑定 (箭头刷新).
    // m2 的 widget 现在 park 进缓存池 (不再销毁), widgetFor 返回空.
    groupCollapsed = true;
    keys = {m1, hdr};
    host.setRows(keys);
    QCOMPARE(bindCount.value(hdr), 3);
    QVERIFY(host.widgetFor(hdr) == survivor);
    QVERIFY(host.widgetFor(m2) == nullptr);    // 消失行已移出物化集 (park)

    // 结构 4: 展开 → [m1, 头G1, m2]: m2 从缓存池原实例取回 (不重建),
    // 头重绑定. m2 绑定计数 = S1 创建(1) + S2 重绑定(2) + S4 复用重绑定(3).
    groupCollapsed = false;
    keys = {m1, hdr, m2};
    host.setRows(keys);
    host.ensureMaterialized(m2);               // 无 scroll area, 手动物化
    QCOMPARE(bindCount.value(hdr), 4);
    QCOMPARE(bindCount.value(m2), 3);          // 复用/重建都是创建即绑定
    QVERIFY(host.widgetFor(m2) == m2Widget);   // 缓存复用: 同实例, 未重建
}

// ── ② FormulaTabModel: 拖入分组 → 行结构 + 数量 ──
void TestFormulaGroups::moveIntoGroupUpdatesRowsAndCount()
{
    ParamDocument doc;
    FormulaVariable fa;
    fa.name = QStringLiteral("A");
    FormulaVariable fb;
    fb.name = QStringLiteral("B");
    doc.addFormula(fa);
    doc.addFormula(fb);
    FormulaGroup g;
    g.name = QStringLiteral("G1");
    doc.addFormulaGroup(g);

    QUndoStack stack;
    cad::ui::FormulaTabModel model(&doc);
    model.setUndoStack(&stack);
    model.rebuild();

    // 初始: [A, B, 头G1]
    QCOMPARE(model.rows().size(), 3);
    QVERIFY(!model.rows()[0].isHeader && model.rows()[0].id == fa.id);
    QVERIFY(model.rows()[2].isHeader && model.rows()[2].id == g.id);
    QCOMPARE(model.formulaCountIn(g.id), 0);

    int changed = 0;
    connect(&doc, &ParamDocument::formulasChanged, &doc, [&changed] { ++changed; });

    // 拖 A 进 G1 (追加到组尾).
    model.moveFormula(fa.id, g.id, model.formulaCountIn(g.id));
    QCOMPARE(changed, 1);                      // 面板靠此信号刷新
    model.rebuild();

    // 行: [B, 头G1, A(组内)]; 数量 1.
    QCOMPARE(model.rows().size(), 3);
    QVERIFY(!model.rows()[0].isHeader && model.rows()[0].id == fb.id);
    QVERIFY(model.rows()[1].isHeader && model.rows()[1].id == g.id);
    QVERIFY(!model.rows()[2].isHeader && model.rows()[2].id == fa.id
            && model.rows()[2].groupId == g.id && model.rows()[2].localIndex == 0);
    QCOMPARE(model.formulaCountIn(g.id), 1);
    QVERIFY(doc.findFormula(fa.id)->groupId == g.id);
}

// ── ②b 命令 undo: 撤销拖入 → 回未分组 ──
void TestFormulaGroups::moveIntoGroupUndoRestores()
{
    ParamDocument doc;
    FormulaVariable fa;
    fa.name = QStringLiteral("A");
    doc.addFormula(fa);
    FormulaGroup g;
    doc.addFormulaGroup(g);

    QUndoStack stack;
    cad::ui::FormulaTabModel model(&doc);
    model.setUndoStack(&stack);

    model.moveFormula(fa.id, g.id, 0);
    model.rebuild();
    QCOMPARE(model.formulaCountIn(g.id), 1);
    QVERIFY(model.rows()[0].isHeader && model.rows()[0].id == g.id);

    stack.undo();
    model.rebuild();
    QCOMPARE(model.formulaCountIn(g.id), 0);
    QVERIFY(doc.findFormula(fa.id)->groupId.isNull());
    QVERIFY(!model.rows()[0].isHeader && model.rows()[0].id == fa.id);

    stack.redo();
    model.rebuild();
    QCOMPARE(model.formulaCountIn(g.id), 1);
}

// ── ③ 折叠: 成员行隐藏 / 展开恢复; 空组切换只留头 ──
void TestFormulaGroups::collapseToggleHidesMemberRows()
{
    ParamDocument doc;
    FormulaVariable fa;
    fa.name = QStringLiteral("A");
    doc.addFormula(fa);
    FormulaGroup g;
    doc.addFormulaGroup(g);
    doc.moveFormula(fa.id, g.id, 0);

    cad::ui::FormulaTabModel model(&doc);
    model.rebuild();
    QCOMPARE(model.rows().size(), 2);          // [头G1, A]

    model.toggleCollapsed(g.id);               // 折叠
    model.rebuild();
    QVERIFY(doc.findFormulaGroup(g.id)->collapsed);
    QCOMPARE(model.rows().size(), 1);
    QVERIFY(model.rows()[0].isHeader && model.rows()[0].id == g.id);

    model.toggleCollapsed(g.id);               // 展开
    model.rebuild();
    QVERIFY(!doc.findFormulaGroup(g.id)->collapsed);
    QCOMPARE(model.rows().size(), 2);
    QVERIFY(!model.rows()[1].isHeader && model.rows()[1].id == fa.id);
}

// ── ④ FormulaCard::setGrouped: 内容 + 竖线右移 kGroupIndent ──
void TestFormulaGroups::groupedCardIndents()
{
    FormulaVariable f;
    f.name = QStringLiteral("X");
    FormulaCard card(f, false);

    const QMargins plain = card.layout()->contentsMargins();
    QCOMPARE(plain.left(), 12);                // 未分组: 原左距

    card.setGrouped(true);
    const QMargins grouped = card.layout()->contentsMargins();
    QCOMPARE(grouped.left(), 12 + FormulaCard::kGroupIndent);

    card.setGrouped(false);
    QCOMPARE(card.layout()->contentsMargins().left(), 12);
}

// ── ⑤ 各卡片序号 (setIndex) + 交替色 (setAlternate) ──
// 缓存复用下卡片会在不同行位置被重绑: 序号与奇偶色都必须随新行号重设,
// 且 label 文本只在变化时更新 (VirtualCardList 复用配套).
void TestFormulaGroups::cardOrdinalsAndAlternate()
{
    //── 序号: 各页卡片经 setIndex 刷新标签文本 (1 起) ──
    Variable v;
    v.name = QStringLiteral("V");
    VariableCard vc(v, false);
    auto* vIndex = vc.findChild<ElaText*>(QStringLiteral("varIndex"));
    QVERIFY(vIndex);
    vc.setIndex(3);
    QCOMPARE(vIndex->text(), QStringLiteral("3"));
    vc.setIndex(0);                        // 非正数 → 空 (不显示序号)
    QVERIFY(vIndex->text().isEmpty());
    vc.setIndex(1);
    QCOMPARE(vIndex->text(), QStringLiteral("1"));

    LinkedVariable lv;
    lv.name = QStringLiteral("L");
    LinkedCard lc(lv, QStringLiteral("M_xx"), false);
    auto* lIndex = lc.findChild<ElaText*>(QStringLiteral("linkedIndex"));
    QVERIFY(lIndex);
    lc.setIndex(2);
    QCOMPARE(lIndex->text(), QStringLiteral("2"));

    FormulaVariable f;
    f.name = QStringLiteral("F");
    FormulaCard fc(f, false);
    fc.setIndex(4);
    QCOMPARE(fc.findChild<ElaText*>(QStringLiteral("cardIndex"))->text(),
             QStringLiteral("4"));

    MeasureVariable mv;
    mv.name = QStringLiteral("M");
    MeasureCard mc(mv, QStringLiteral("P1·P2"));
    mc.setIndex(7);
    QCOMPARE(mc.findChild<ElaText*>(QStringLiteral("measureIndex"))->text(),
             QStringLiteral("测 7"));

    AngleMeasureVariable am;
    am.name = QStringLiteral("A");
    AngleMeasureCard ac(am, QStringLiteral("S1·S2"));
    ac.setIndex(5);
    QCOMPARE(ac.findChild<ElaText*>(QStringLiteral("angleIndex"))->text(),
             QStringLiteral("角 5"));

    //── 类型色竖线 (ui-redesign-2026-08 §2.5 方案 A): 左侧竖线 = 卡片类型 ──
    // 变量=piece1 碳灰 / 公式=piece2 深青 / 测量(含角度)=piece3 陶土。
    // setAlternate 仅保留 bind 契约, 不再改变竖线颜色 (幂等, 不抖动)。
    auto barColor = [](QWidget& card) {
        const QPixmap pm = card.grab();     // 触发 paintEvent 渲染
        return pm.toImage().pixelColor(1, card.height() / 2);
    };
    auto near = [](const QColor& a, const QColor& b) {
        return std::abs(a.red() - b.red()) <= 8
            && std::abs(a.green() - b.green()) <= 8
            && std::abs(a.blue() - b.blue()) <= 8;
    };
    const QColor piece1(0x1E, 0x29, 0x3B);  // 变量 = 碳灰 (亮色 token)
    const QColor piece2(0x0F, 0x76, 0x6E);  // 公式 = 深青
    const QColor piece3(0xC8, 0x5A, 0x3E);  // 测量 = 陶土

    VariableCard va(v, false);
    va.resize(300, 72);
    QVERIFY(near(barColor(va), piece1));     // 变量: 碳灰 (奇偶同色)
    va.setAlternate(true);
    QVERIFY(near(barColor(va), piece1));     // 行奇偶不再驱动竖线色
    va.setAlternate(true);                   // 幂等: 重复设置不抖动
    QVERIFY(near(barColor(va), piece1));
    va.setAlternate(false);
    QVERIFY(near(barColor(va), piece1));

    FormulaCard fa(f, false);
    fa.resize(300, 72);
    fa.setAlternate(true);
    QVERIFY(near(barColor(fa), piece2));     // 公式: 深青
    MeasureCard ma(mv, QStringLiteral("P1·P2"));
    ma.resize(300, 72);
    ma.setAlternate(true);
    QVERIFY(near(barColor(ma), piece3));     // 测量: 陶土
    AngleMeasureCard aa(am, QStringLiteral("S1·S2"));
    aa.resize(300, 72);
    aa.setAlternate(true);
    QVERIFY(near(barColor(aa), piece3));     // 角度测量: 陶土
}

QTEST_MAIN(TestFormulaGroups)
#include "test_formula_groups.moc"
