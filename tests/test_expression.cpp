#include <QtTest>
#include <QHash>
#include <QString>

#include <cmath>

#include "parametric/ExpressionEvaluator.h"

using cad::param::ExpressionEvaluator;

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

QTEST_GUILESS_MAIN(TestExpression)
#include "test_expression.moc"
