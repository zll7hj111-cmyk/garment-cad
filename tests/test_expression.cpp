#include <QtTest>
#include <QHash>
#include <QString>

#include <cmath>

#include "parametric/ExpressionEvaluator.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"

using cad::param::ExpressionEvaluator;
using cad::param::ExpressionCache;

class TestExpression : public QObject
{
    Q_OBJECT

private slots:
    void basicArithmetic();
    void operatorPrecedence();
    void parentheses();
    void unaryOperators();
    void variables();
    void cjkVariables();
    void fullWidthNormalization();
    void divisionByZero();
    void unknownVariable();
    void emptyExpression();
    void referencedNames();
    void standaloneIdentifier();
    void brackets();
    void trigFunctions();
    void trigWithVariable();
    void namingConvention();
    void degreeSymbol();
    void garmentFormula();
    void parseDepthGuard();
    void powerOperator();
    void sqrtAbsFunctions();
    void inverseTrigFunctions();
    void twoArgFunctions();
    void roundingFunctions();
    void functionNamingFallback();
    void functionDomainErrors();
    void backShoulderCorrection();

    // P1-5: the compile cache is an instance, not a process-wide static.
    void cacheIsInstanceScoped();
    void cacheReferencesSurviveGrowthGuard();
    void documentOwnsCompileCache();
};

void TestExpression::basicArithmetic()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("2+3", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);

    r = ExpressionEvaluator::evaluate("10-4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 6.0);

    r = ExpressionEvaluator::evaluate("3*7", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 21.0);

    r = ExpressionEvaluator::evaluate("20/4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);
}

void TestExpression::operatorPrecedence()
{
    QHash<QString, double> vars;
    // Multiplication before addition
    auto r = ExpressionEvaluator::evaluate("2+3*4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 14.0);

    r = ExpressionEvaluator::evaluate("10-2*3", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 4.0);
}

void TestExpression::parentheses()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("(2+3)*4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 20.0);

    r = ExpressionEvaluator::evaluate("((1+2)*(3+4))", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 21.0);
}

void TestExpression::unaryOperators()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("-5", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -5.0);

    r = ExpressionEvaluator::evaluate("+3", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);

    r = ExpressionEvaluator::evaluate("-(2+3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -5.0);
}

void TestExpression::variables()
{
    QHash<QString, double> vars;
    vars["hip"] = 92.0;
    vars["waist"] = 70.0;

    auto r = ExpressionEvaluator::evaluate("hip/4+2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 25.0);  // 92/4+2 = 25

    r = ExpressionEvaluator::evaluate("hip+waist", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 162.0);
}

void TestExpression::cjkVariables()
{
    QHash<QString, double> vars;
    vars[QString::fromUtf8("臀围")] = 92.0;

    auto r = ExpressionEvaluator::evaluate(QString::fromUtf8("臀围/4"), vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 23.0);
}

void TestExpression::fullWidthNormalization()
{
    QHash<QString, double> vars;
    // Full-width: （2+3）×4 → (2+3)*4 = 20
    QString expr = QString(QChar(0xFF08)) + "2+3" + QString(QChar(0xFF09)) + QString(QChar(0x00D7)) + "4";
    auto r = ExpressionEvaluator::evaluate(expr, vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 20.0);
}

void TestExpression::divisionByZero()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("5/0", vars);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestExpression::unknownVariable()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("unknown_var+1", vars);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("unknown_var"));
}

void TestExpression::emptyExpression()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("", vars);
    QVERIFY(!r.ok);

    r = ExpressionEvaluator::evaluate("   ", vars);
    QVERIFY(!r.ok);
}

void TestExpression::referencedNames()
{
    QStringList names = ExpressionEvaluator::referencedNames("hip/4+waist*2");
    QVERIFY(names.contains("hip"));
    QVERIFY(names.contains("waist"));
    QCOMPARE(names.size(), 2);
}

void TestExpression::standaloneIdentifier()
{
    QCOMPARE(ExpressionEvaluator::standaloneIdentifier("hip"), QString("hip"));
    QCOMPARE(ExpressionEvaluator::standaloneIdentifier("hip/4"), QString());
    QCOMPARE(ExpressionEvaluator::standaloneIdentifier("  hip  "), QString("hip"));
    QCOMPARE(ExpressionEvaluator::standaloneIdentifier(""), QString());
}

void TestExpression::brackets()
{
    QHash<QString, double> vars;
    // Square brackets act as parentheses.
    auto r = ExpressionEvaluator::evaluate("[2+3]*4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 20.0);

    // Curly braces too.
    r = ExpressionEvaluator::evaluate("{2+3}*4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 20.0);

    // Mixed nesting.
    r = ExpressionEvaluator::evaluate("[(1+2)*3]", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 9.0);
}

void TestExpression::trigFunctions()
{
    QHash<QString, double> vars;
    // cos(0) = 1
    auto r = ExpressionEvaluator::evaluate("cos(0)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 1.0) < 1e-9);

    // cos(60) = 0.5  (argument in degrees)
    r = ExpressionEvaluator::evaluate("cos(60)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // sin(30) = 0.5
    r = ExpressionEvaluator::evaluate("sin(30)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // tan(45) = 1
    r = ExpressionEvaluator::evaluate("tan(45)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 1.0) < 1e-9);

    // Direct-number form without parentheses: cos60
    r = ExpressionEvaluator::evaluate("cos60", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // Function of a variable: cos(a) with a=60
    vars["a"] = 60.0;
    r = ExpressionEvaluator::evaluate("cos(a)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // Function of a sub-expression: cos(30+30)
    r = ExpressionEvaluator::evaluate("cos(30+30)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);
}

void TestExpression::trigWithVariable()
{
    QHash<QString, double> vars;
    vars[QString::fromUtf8("前肩角度")] = 22.0;
    vars[QString::fromUtf8("前胸宽")] = 40.0;
    vars[QString::fromUtf8("前领宽")] = 7.0;

    // cos directly followed by a CJK variable name: cos前肩角度
    auto r = ExpressionEvaluator::evaluate(QString::fromUtf8("cos前肩角度"), vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - std::cos(22.0 * 3.14159265358979323846 / 180.0)) < 1e-9);

    // The user's full formula: (前胸宽-前领宽)/cos前肩角度°+1.8
    const QString expr = QString::fromUtf8("(前胸宽-前领宽)/cos前肩角度\u00B0+1.8");
    r = ExpressionEvaluator::evaluate(expr, vars);
    QVERIFY(r.ok);
    const double expected = (40.0 - 7.0)
        / std::cos(22.0 * 3.14159265358979323846 / 180.0) + 1.8;
    QVERIFY(qAbs(r.value - expected) < 1e-9);

    // cos followed by an uppercase ASCII variable: cosA == cos(A)
    vars["A"] = 60.0;
    r = ExpressionEvaluator::evaluate("cosA", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // A longer lowercase identifier is still a variable, not a function: cosine
    vars["cosine"] = 7.0;
    r = ExpressionEvaluator::evaluate("cosine", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 7.0);

    // cos_a (lowercase + underscore) is a variable, not a function.
    vars["cos_a"] = 9.0;
    r = ExpressionEvaluator::evaluate("cos_a", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 9.0);
}

void TestExpression::namingConvention()
{
    QHash<QString, double> vars;
    // Functions are lowercase-only: COS60 is a variable reference, NOT cos(60).
    auto r = ExpressionEvaluator::evaluate("COS60", vars);
    QVERIFY(!r.ok);  // unknown variable COS60

    // An uppercase variable named COS never collides with the cos function.
    vars["COS"] = 42.0;
    r = ExpressionEvaluator::evaluate("COS", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 42.0);

    // The lowercase function still works alongside the COS variable.
    r = ExpressionEvaluator::evaluate("cos60", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // Variable lookup is case-insensitive: "b" resolves uppercase variable "B".
    vars["B"] = 88.0;
    r = ExpressionEvaluator::evaluate("b/4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 22.0);

    // Exact match takes precedence over case-insensitive fallback.
    r = ExpressionEvaluator::evaluate("B/4", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 22.0);
}

void TestExpression::degreeSymbol()
{
    QHash<QString, double> vars;
    // Degree symbol is stripped; cos22° == cos(22)
    auto r = ExpressionEvaluator::evaluate(QString::fromUtf8("cos22\u00B0"), vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - std::cos(22.0 * 3.14159265358979323846 / 180.0)) < 1e-9);

    // Standalone number with degree symbol.
    r = ExpressionEvaluator::evaluate(QString::fromUtf8("45\u00B0"), vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 45.0);
}

void TestExpression::garmentFormula()
{
    // The exact formula from the requirement: [(B/8 + 6.2) - (B/24 + 3.4) + 1.8] / cos22°
    QHash<QString, double> vars;
    vars["B"] = 88.0;  // 胸围 (bust) in cm
    QString expr = QString::fromUtf8("[(B/8 + 6.2) - (B/24 + 3.4) + 1.8] / cos22\u00B0");
    auto r = ExpressionEvaluator::evaluate(expr, vars);
    QVERIFY(r.ok);
    const double expected = ((88.0/8 + 6.2) - (88.0/24 + 3.4) + 1.8)
                            / std::cos(22.0 * 3.14159265358979323846 / 180.0);
    QVERIFY(qAbs(r.value - expected) < 1e-9);
}

// 病理性嵌套输入必须安全拒绝：解析深度防护（否则编译期 C++ 递归栈溢出崩溃）。
void TestExpression::parseDepthGuard()
{
    QHash<QString, double> vars;

    // 5000 层括号嵌套：无防护时 parseExpression↔parseFactor 递归 ~15000 帧
    // C++ 调用栈必然溢出；防护下应在 kMaxParseDepth 处干净失败。
    QString deep = QString(5000, QLatin1Char('(')) + QStringLiteral("1")
                 + QString(5000, QLatin1Char(')'));
    auto r = ExpressionEvaluator::evaluate(deep, vars);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());

    // 5000 个一元负号同理（parseFactor 每层递归一次）。
    QString neg = QString(5000, QLatin1Char('-')) + QStringLiteral("1");
    r = ExpressionEvaluator::evaluate(neg, vars);
    QVERIFY(!r.ok);

    // 合理嵌套深度不受影响。
    QString ok = QString(20, QLatin1Char('(')) + QStringLiteral("2+3")
               + QString(20, QLatin1Char(')'));
    r = ExpressionEvaluator::evaluate(ok, vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);
}

void TestExpression::powerOperator()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("2^3", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 8.0);

    // Higher precedence than *: 2*3^2 = 2*9
    r = ExpressionEvaluator::evaluate("2*3^2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 18.0);

    // Parens override: (2+3)^2 = 25
    r = ExpressionEvaluator::evaluate("(2+3)^2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 25.0);

    // Right-associative: 2^3^2 = 2^9 = 512
    r = ExpressionEvaluator::evaluate("2^3^2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 512.0);

    // Binds tighter than unary minus: -2^2 = -(2^2) = -4
    r = ExpressionEvaluator::evaluate("-2^2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -4.0);

    // Negative exponent works: 2^-1 = 0.5
    r = ExpressionEvaluator::evaluate("2^-1", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.5) < 1e-9);

    // Fractional exponent: 3^0.5 = sqrt(3)
    r = ExpressionEvaluator::evaluate("3^0.5", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - std::sqrt(3.0)) < 1e-9);

    // pow function form + chaining with variables.
    vars["B"] = 84.0;
    r = ExpressionEvaluator::evaluate("(B/12+13.7)^2", vars);
    QVERIFY(r.ok);
    const double expected = std::pow(84.0 / 12 + 13.7, 2.0);
    QVERIFY(qAbs(r.value - expected) < 1e-9);
}

void TestExpression::sqrtAbsFunctions()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("sqrt(16)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 4.0);

    // Bare argument form: sqrt2 == sqrt(2)
    r = ExpressionEvaluator::evaluate("sqrt2", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - std::sqrt(2.0)) < 1e-9);

    // Sub-expression argument.
    r = ExpressionEvaluator::evaluate("sqrt(2+14)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 4.0);

    // The canonical pattern-geometry usage: 勾股定理.
    vars["前"] = 3.0;
    vars["垂"] = 4.0;
    r = ExpressionEvaluator::evaluate("sqrt(前^2+垂^2)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);

    r = ExpressionEvaluator::evaluate("abs(-5)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);

    r = ExpressionEvaluator::evaluate("abs3", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);

    r = ExpressionEvaluator::evaluate("abs(2-7)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 5.0);

    r = ExpressionEvaluator::evaluate("abs(-前*垂)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 12.0);

    // sqrt of a negative number is a domain error.
    r = ExpressionEvaluator::evaluate("sqrt(-1)", vars);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void TestExpression::inverseTrigFunctions()
{
    QHash<QString, double> vars;
    // Results in DEGREES (garment convention).
    auto r = ExpressionEvaluator::evaluate("atan2(1,1)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 45.0) < 1e-9);

    r = ExpressionEvaluator::evaluate("atan2(0,7)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.0) < 1e-9);

    r = ExpressionEvaluator::evaluate("atan2(1,0)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 90.0) < 1e-9);

    r = ExpressionEvaluator::evaluate("atan2(-1,-1)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - (-135.0)) < 1e-9);

    // 3-4-5 triangle: atan2(y,x) angle, then round-trip through cos/sin.
    r = ExpressionEvaluator::evaluate("cos(atan2(3,4))", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.8) < 1e-9);
    r = ExpressionEvaluator::evaluate("sin(atan2(3,4))", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 0.6) < 1e-9);

    // atan / asin / acos also produce degrees.
    r = ExpressionEvaluator::evaluate("atan(1)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 45.0) < 1e-9);
    r = ExpressionEvaluator::evaluate("atan1", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 45.0) < 1e-9);
    r = ExpressionEvaluator::evaluate("asin(0.5)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 30.0) < 1e-9);
    r = ExpressionEvaluator::evaluate("acos(0.5)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 60.0) < 1e-9);

    // CJK variables inside atan2.
    vars[QString::fromUtf8("肩褶宽")] = 1.825;
    vars[QString::fromUtf8("后肩长")] = 14.336;
    r = ExpressionEvaluator::evaluate(QString::fromUtf8("atan2(肩褶宽, 后肩长-肩褶宽)"), vars);
    QVERIFY(r.ok);
    const double expected = std::atan2(1.825, 14.336 - 1.825) * 180.0 / M_PI;
    QVERIFY(qAbs(r.value - expected) < 1e-9);
}

void TestExpression::twoArgFunctions()
{
    QHash<QString, double> vars;
    auto r = ExpressionEvaluator::evaluate("pow(2,10)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 1024.0);

    r = ExpressionEvaluator::evaluate("pow(2,0.5)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - std::sqrt(2.0)) < 1e-9);

    r = ExpressionEvaluator::evaluate("min(3,7)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);
    r = ExpressionEvaluator::evaluate("min(7,3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);
    r = ExpressionEvaluator::evaluate("max(3,7)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 7.0);
    r = ExpressionEvaluator::evaluate("max(7,3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 7.0);

    // Nested / arithmetic in args.
    r = ExpressionEvaluator::evaluate("min(3,7)+max(3,7)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 10.0);
    r = ExpressionEvaluator::evaluate("max(sqrt(16), abs(-9))", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 9.0);

    // CJK variable arguments.
    vars[QString::fromUtf8("肩褶宽")] = 1.825;
    vars["B32"] = 2.0;
    r = ExpressionEvaluator::evaluate(QString::fromUtf8("min(肩褶宽, B32)"), vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 1.825) < 1e-9);

    // Uppercase parse of the classic garment formula still works.
    vars["B"] = 84.0;
    vars["W"] = 65.0;
    r = ExpressionEvaluator::evaluate("[(B/2+6)-(W/2+3)]*0.35", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 4.375) < 1e-9);
}

void TestExpression::roundingFunctions()
{
    QHash<QString, double> vars;
    // std::round: half away from zero.
    auto r = ExpressionEvaluator::evaluate("round(2.5)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);
    r = ExpressionEvaluator::evaluate("round(-2.5)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -3.0);
    r = ExpressionEvaluator::evaluate("floor(2.9)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 2.0);
    r = ExpressionEvaluator::evaluate("ceil(2.1)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);
    r = ExpressionEvaluator::evaluate("floor(-2.1)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -3.0);
    r = ExpressionEvaluator::evaluate("ceil(-2.9)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, -2.0);

    // In a garment context: round a computed cm value to 0.5 mm.
    vars["B"] = 84.0;
    r = ExpressionEvaluator::evaluate("round((B/2+6)*10)/10", vars);
    QVERIFY(r.ok);
    const double expected = std::round((84.0 / 2 + 6.0) * 10.0) / 10.0;
    QVERIFY(qAbs(r.value - expected) < 1e-9);
}

void TestExpression::functionNamingFallback()
{
    QHash<QString, double> vars;
    // Lowercase identifiers that merely START with a function name stay
    // variables (old semantics): cosine / cos_a / absx / minx / min2.
    vars["cosine"] = 7.0;
    vars["cos_a"] = 9.0;
    vars["absx"] = 11.0;
    vars["minx"] = 13.0;
    vars["min2"] = 15.0;
    vars["atan2x"] = 17.0;

    auto r = ExpressionEvaluator::evaluate("cosine", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 7.0);
    r = ExpressionEvaluator::evaluate("cos_a", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 9.0);
    r = ExpressionEvaluator::evaluate("absx", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 11.0);
    r = ExpressionEvaluator::evaluate("minx", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 13.0);
    r = ExpressionEvaluator::evaluate("min2", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 15.0);
    r = ExpressionEvaluator::evaluate("atan2x", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 17.0);

    // The real functions still win when called.
    r = ExpressionEvaluator::evaluate("abs(-3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 3.0);
    r = ExpressionEvaluator::evaluate("min(2,4)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 2.0);
    r = ExpressionEvaluator::evaluate("atan2(1,1)", vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 45.0) < 1e-9);

    // Uppercase function-like names remain variables (naming convention).
    vars["SQRTA"] = 21.0;
    r = ExpressionEvaluator::evaluate("SQRTA", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, 21.0);
}

void TestExpression::functionDomainErrors()
{
    QHash<QString, double> vars;
    // asin/acos outside [-1,1].
    auto r = ExpressionEvaluator::evaluate("asin(2)", vars);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    r = ExpressionEvaluator::evaluate("acos(-1.5)", vars);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());

    // sqrt(-4).
    r = ExpressionEvaluator::evaluate("sqrt(-4)", vars);
    QVERIFY(!r.ok);

    // Missing comma / parens for two-arg functions.
    r = ExpressionEvaluator::evaluate("min(3 7)", vars);
    QVERIFY(!r.ok);
    r = ExpressionEvaluator::evaluate("atan2(3)", vars);
    QVERIFY(!r.ok);
    r = ExpressionEvaluator::evaluate("atan2 3,4", vars);
    QVERIFY(!r.ok);  // bare two-arg form is rejected (variable "atan2" unknown)

    // pow domain: (-1)^0.5 is NaN -> non-finite guard.
    r = ExpressionEvaluator::evaluate("(-1)^0.5", vars);
    QVERIFY(!r.ok);

    // Division by zero still rejected.
    r = ExpressionEvaluator::evaluate("sqrt(4)/0", vars);
    QVERIFY(!r.ok);
}

// 用户 2.gcad「后肩线修正」组的公式原样回归（B=84, 全部度制/厘米）：
// 独立几何核验（向量旋转法）与公式链在 1e-12 内一致。
void TestExpression::backShoulderCorrection()
{
    QHash<QString, double> vars;
    vars["肩褶E"] = 9.95;
    vars["后领宽"] = 7.1;
    vars["后肩角度"] = 18.0;
    vars["肩胛尖点"] = 8.0;
    vars["后领高"] = 7.1 / 3.0;
    vars["肩褶宽"] = 1.825;
    vars["后肩长"] = 14.336;

    const QString nearMouth = QString::fromUtf8("(肩褶E-后领宽)/cos(后肩角度°)+1.5");
    const QString proj = QString::fromUtf8("(肩褶E-后领宽)*cos(后肩角度°)+(肩胛尖点+后领高)*sin(后肩角度°)");
    const QString perp = QString::fromUtf8("(肩胛尖点+后领高)*cos(后肩角度°)-(肩褶E-后领宽)*sin(后肩角度°)");
    const QString apexF = QString::fromUtf8("atan2(肩褶宽*垂距, (近口距-投影距)*(近口距+肩褶宽-投影距)+垂距^2)");
    const QString xF = QString::fromUtf8("投影距+(后肩长-投影距)*cos(省道角)-垂距*sin(省道角)");
    const QString yF = QString::fromUtf8("(后肩长-投影距)*sin(省道角)+垂距*cos(省道角)-垂距");
    const QString lenF = QString::fromUtf8("sqrt(修正X^2+修正Y^2)");
    const QString angF = QString::fromUtf8("atan2(修正Y, 修正X)");

    auto r = ExpressionEvaluator::evaluate(nearMouth, vars);
    QVERIFY(r.ok);
    vars["近口距"] = r.value;
    QVERIFY(qAbs(r.value - 4.49666733907906) < 1e-9);

    r = ExpressionEvaluator::evaluate(proj, vars);
    QVERIFY(r.ok);
    vars["投影距"] = r.value;
    QVERIFY(qAbs(r.value - 5.91398724646148) < 1e-9);

    r = ExpressionEvaluator::evaluate(perp, vars);
    QVERIFY(r.ok);
    vars["垂距"] = r.value;
    QVERIFY(qAbs(r.value - 8.97858745162449) < 1e-9);

    r = ExpressionEvaluator::evaluate(apexF, vars);
    QVERIFY(r.ok);
    vars["省道角"] = r.value;
    QVERIFY(qAbs(r.value - 11.570212) < 1e-6);

    r = ExpressionEvaluator::evaluate(xF, vars);
    QVERIFY(r.ok);
    vars["修正X"] = r.value;   // 2.gcad 里的公式名是小写"修正x"，引用方写"修正X"（大小写不敏感）
    vars["修正x"] = r.value;
    QVERIFY(qAbs(r.value - 12.3640389280444) < 1e-9);

    r = ExpressionEvaluator::evaluate(yF, vars);
    QVERIFY(r.ok);
    vars["修正Y"] = r.value;
    QVERIFY(qAbs(r.value - 1.50674348030817) < 1e-9);

    // 引用"修正X"但只有"修正x"在变量表里：大小写不敏感回退必须命中。
    vars.remove("修正X");
    vars.remove("修正x");
    vars["修正x"] = 12.3640389280444;
    vars["修正Y"] = 1.50674348030817;
    r = ExpressionEvaluator::evaluate(lenF, vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 12.4555102075) < 1e-9);
    r = ExpressionEvaluator::evaluate(angF, vars);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 6.94808923) < 1e-6);

    // B=88 冒烟：所有量保持有意义（u0>0、省道角在 (0,30)°、长度减少但为正）。
    QHash<QString, double> v2;
    v2["肩褶E"] = 10.2;
    v2["后领宽"] = 7.266666666666666;
    v2["后肩角度"] = 18.0;
    v2["肩胛尖点"] = 8.0;
    v2["后领高"] = 7.266666666666666 / 3.0;
    v2["肩褶宽"] = 1.95;
    v2["后肩长"] = 14.82;
    r = ExpressionEvaluator::evaluate(nearMouth, v2);
    QVERIFY(r.ok);
    v2["近口距"] = r.value;
    r = ExpressionEvaluator::evaluate(proj, v2);
    QVERIFY(r.ok);
    v2["投影距"] = r.value;
    r = ExpressionEvaluator::evaluate(perp, v2);
    QVERIFY(r.ok);
    v2["垂距"] = r.value;
    r = ExpressionEvaluator::evaluate(apexF, v2);
    QVERIFY(r.ok);
    v2["省道角"] = r.value;
    r = ExpressionEvaluator::evaluate(xF, v2);
    QVERIFY(r.ok);
    v2["修正X"] = r.value;
    v2["修正x"] = r.value;
    r = ExpressionEvaluator::evaluate(yF, v2);
    QVERIFY(r.ok);
    v2["修正Y"] = r.value;
    r = ExpressionEvaluator::evaluate(lenF, v2);
    QVERIFY(r.ok);
    QVERIFY(qAbs(r.value - 12.8039) < 0.005);
    QVERIFY(v2["省道角"] > 0.0 && v2["省道角"] < 30.0);
}

// P1-5: the compile cache is an INSTANCE (document/pass-owned), not the old
// process-wide static. Two caches must be fully independent.
void TestExpression::cacheIsInstanceScoped()
{
    ExpressionCache a;
    ExpressionCache b;
    const QString expr = QStringLiteral("2+3*hip");

    const auto& ra1 = a.compiled(expr);
    const auto& ra2 = a.compiled(expr);
    QVERIFY(&ra1 == &ra2);   // one entry per cache: identical reference
    QVERIFY(ra1.ok);

    const auto& rb = b.compiled(expr);
    QVERIFY(&rb != &ra1);    // separate storage, separate bytecode objects
    QVERIFY(rb.ok);

    QHash<QString, double> vars;
    vars[QStringLiteral("hip")] = 4.0;
    QCOMPARE(ExpressionEvaluator::execute(ra1, vars).value, 14.0);
    QCOMPARE(ExpressionEvaluator::execute(rb, vars).value, 14.0);

    // Clearing one cache must not disturb the other's entries (the old static
    // cache had no owner at all, so nothing could ever release it).
    a.clear();
    QCOMPARE(a.size(), 0);
    QVERIFY(&b.compiled(expr) == &rb);
}

// The old static cache reset its whole store at 8192 entries, dangling every
// reference it had already handed out (the header could only warn callers to
// re-fetch). The instance cache retires a generation instead: references stay
// valid for the lifetime of the cache.
void TestExpression::cacheReferencesSurviveGrowthGuard()
{
    ExpressionCache cache;
    const auto& first = cache.compiled(QStringLiteral("hip/4+2"));
    QVERIFY(first.ok);

    // Push past the live-generation cap with unique texts (live-typing
    // validation produces exactly this pattern).
    for (int i = 0; i < 8300; ++i)
        cache.compiled(QStringLiteral("x%1*2").arg(i));

    QVERIFY(cache.generationCount() > 1);   // a generation was retired

    // `first` must still be readable AND produce correct results.
    QVERIFY(first.ok);
    QHash<QString, double> vars;
    vars[QStringLiteral("hip")] = 8.0;
    QCOMPARE(ExpressionEvaluator::execute(first, vars).value, 4.0);  // 8/4 + 2
}

// End-to-end: resolve passes compile through the document's own cache, and
// clear() releases it with the rest of the model.
void TestExpression::documentOwnsCompileCache()
{
    cad::param::ParamDocument doc;
    QCOMPARE(doc.expressionCache().size(), 0);

    cad::param::Block block;
    cad::param::ParamPoint origin;
    origin.constraint = cad::param::PointConstraint::Free;
    origin.freePos = {0.0, 0.0};
    const QUuid idO = block.addPoint(origin);

    cad::param::ParamPoint end;
    end.constraint = cad::param::PointConstraint::Polar;
    end.refPointId = idO;
    end.distance = 0.0;                                  // driven by formula
    end.distanceFormula = QStringLiteral("hip/4+2");
    end.angle = 0.0;
    const QUuid idE = block.addPoint(end);

    cad::param::Segment seg;
    seg.startPointId = idO;
    seg.endPointId = idE;
    block.addSegment(seg);
    doc.addBlock(std::move(block));

    doc.resolveAll();

    // The pass compiled into the DOCUMENT's cache (before P1-5 every
    // compilation went to the process-wide static, so this was 0).
    QVERIFY(doc.expressionCache().size() > 0);
    QVERIFY(doc.expressionCache().compiled(QStringLiteral("hip/4+2")).ok);

    doc.clear();
    QCOMPARE(doc.expressionCache().size(), 0);           // released with the model
}

QTEST_GUILESS_MAIN(TestExpression)
#include "test_expression.moc"
