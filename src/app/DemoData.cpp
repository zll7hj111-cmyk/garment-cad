#include "DemoData.h"

namespace cad::demo {

QList<cad::param::Variable> defaultVariables()
{
    QList<cad::param::Variable> vars;

    cad::param::Variable v1;
    v1.name = QStringLiteral("胸围");
    v1.refName = QStringLiteral("b");
    v1.value = 840.0;  // 84 cm in mm
    v1.comment = QStringLiteral("胸围尺寸");
    vars.append(v1);

    cad::param::Variable v2;
    v2.name = QStringLiteral("腰围");
    v2.refName = QStringLiteral("w");
    v2.value = 700.0;  // 70 cm
    v2.comment = QStringLiteral("腰围尺寸");
    vars.append(v2);

    cad::param::Variable v3;
    v3.name = QStringLiteral("臀围");
    v3.refName = QStringLiteral("h");
    v3.value = 920.0;  // 92 cm
    v3.comment = QStringLiteral("臀围尺寸");
    vars.append(v3);

    cad::param::Variable v4;
    v4.name = QStringLiteral("肩宽");
    v4.refName = QStringLiteral("sw");
    v4.value = 380.0;  // 38 cm
    v4.comment = QStringLiteral("肩宽尺寸");
    vars.append(v4);

    return vars;
}

QList<cad::param::FormulaVariable> defaultFormulas()
{
    QList<cad::param::FormulaVariable> formulas;

    cad::param::FormulaVariable f1;
    f1.name = QStringLiteral("胸宽");
    f1.expression = QStringLiteral("胸围/2+6");
    f1.comment = QStringLiteral("胸围的一半加松量");
    formulas.append(f1);

    cad::param::FormulaVariable f2;
    f2.name = QStringLiteral("背宽");
    f2.expression = QStringLiteral("b/2+4");
    f2.comment = QStringLiteral("使用引用名 b 计算");
    formulas.append(f2);

    return formulas;
}

} // namespace cad::demo
