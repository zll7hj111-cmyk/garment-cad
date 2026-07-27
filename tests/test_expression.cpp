#include <QtTest>
#include <QHash>
#include <QString>

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

QTEST_GUILESS_MAIN(TestExpression)
#include "test_expression.moc"
