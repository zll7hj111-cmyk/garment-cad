#include "ExpressionEvaluator.h"

namespace cad::param {

ExpressionEvaluator::Result
ExpressionEvaluator::evaluate(const QString& expression,
                              const QHash<QString, double>& variables)
{
    ExpressionEvaluator ev(expression, variables);

    Result r;
    if (ev.m_text.trimmed().isEmpty()) {
        r.error = QStringLiteral("表达式为空");
        return r;
    }

    const double v = ev.parseExpression();
    ev.skipSpaces();
    if (ev.m_ok && ev.m_pos < ev.m_text.size())
        ev.fail(QStringLiteral("存在无法解析的内容: \"%1\"")
                    .arg(ev.m_text.mid(ev.m_pos)));

    r.ok = ev.m_ok;
    r.value = v;
    r.error = ev.m_error;
    return r;
}

ExpressionEvaluator::ExpressionEvaluator(const QString& text,
                                         const QHash<QString, double>& vars)
    : m_text(normalized(text))
    , m_vars(vars)
{
}

QString ExpressionEvaluator::normalized(const QString& text)
{
    QString t = text;
    // Normalize common full-width input to ASCII equivalents.
    t.replace(QChar(0x00D7), QLatin1Char('*'));  // ×
    t.replace(QChar(0x00F7), QLatin1Char('/'));  // ÷
    t.replace(QChar(0xFF08), QLatin1Char('('));  // （
    t.replace(QChar(0xFF09), QLatin1Char(')'));  // ）
    t.replace(QChar(0xFF0B), QLatin1Char('+'));  // ＋
    t.replace(QChar(0xFF0D), QLatin1Char('-'));  // －
    t.replace(QChar(0xFF0E), QLatin1Char('.'));  // ．
    t.replace(QChar(0x3000), QLatin1Char(' '));  // full-width space
    return t;
}

QStringList ExpressionEvaluator::referencedNames(const QString& expression)
{
    const QString t = normalized(expression);
    QStringList names;
    int i = 0;
    while (i < t.size()) {
        const QChar c = t.at(i);
        if (c.isLetter() || c == QLatin1Char('_')) {
            const int start = i;
            while (i < t.size()
                   && (t.at(i).isLetterOrNumber() || t.at(i) == QLatin1Char('_')))
                ++i;
            names.append(t.mid(start, i - start));
        } else {
            ++i;
        }
    }
    return names;
}

QString ExpressionEvaluator::standaloneIdentifier(const QString& expression)
{
    const QString t = normalized(expression).trimmed();
    if (t.isEmpty())
        return QString();
    if (!(t.at(0).isLetter() || t.at(0) == QLatin1Char('_')))
        return QString();
    for (int i = 1; i < t.size(); ++i) {
        const QChar c = t.at(i);
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_')))
            return QString();
    }
    return t;
}

void ExpressionEvaluator::fail(const QString& message)
{
    if (m_ok) {
        m_ok = false;
        m_error = message;
    }
}

void ExpressionEvaluator::skipSpaces()
{
    while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
        ++m_pos;
}

QChar ExpressionEvaluator::peek() const
{
    return m_pos < m_text.size() ? m_text.at(m_pos) : QChar();
}

double ExpressionEvaluator::parseExpression()
{
    double left = parseTerm();
    for (;;) {
        skipSpaces();
        const QChar c = peek();
        if (c == QLatin1Char('+')) {
            ++m_pos;
            left += parseTerm();
        } else if (c == QLatin1Char('-')) {
            ++m_pos;
            left -= parseTerm();
        } else {
            return left;
        }
        if (!m_ok)
            return 0.0;
    }
}

double ExpressionEvaluator::parseTerm()
{
    double left = parseFactor();
    for (;;) {
        skipSpaces();
        const QChar c = peek();
        if (c == QLatin1Char('*')) {
            ++m_pos;
            left *= parseFactor();
        } else if (c == QLatin1Char('/')) {
            ++m_pos;
            const double rhs = parseFactor();
            if (m_ok && qFuzzyIsNull(rhs)) {
                fail(QStringLiteral("除数为零"));
                return 0.0;
            }
            left /= rhs;
        } else {
            return left;
        }
        if (!m_ok)
            return 0.0;
    }
}

double ExpressionEvaluator::parseFactor()
{
    skipSpaces();
    const QChar c = peek();

    // Unary +/-
    if (c == QLatin1Char('+')) {
        ++m_pos;
        return parseFactor();
    }
    if (c == QLatin1Char('-')) {
        ++m_pos;
        return -parseFactor();
    }

    // Parenthesized sub-expression
    if (c == QLatin1Char('(')) {
        ++m_pos;
        const double v = parseExpression();
        skipSpaces();
        if (peek() == QLatin1Char(')')) {
            ++m_pos;
        } else {
            fail(QStringLiteral("缺少右括号 )"));
        }
        return v;
    }

    // Number literal
    if (c.isDigit() || c == QLatin1Char('.')) {
        const int start = m_pos;
        bool seenDot = false;
        while (m_pos < m_text.size()) {
            const QChar d = m_text.at(m_pos);
            if (d.isDigit()) {
                ++m_pos;
            } else if (d == QLatin1Char('.') && !seenDot) {
                seenDot = true;
                ++m_pos;
            } else {
                break;
            }
        }
        bool numOk = false;
        const double v = m_text.mid(start, m_pos - start).toDouble(&numOk);
        if (!numOk)
            fail(QStringLiteral("无效数字: \"%1\"").arg(m_text.mid(start, m_pos - start)));
        return v;
    }

    // Identifier: letters (incl. CJK), digits, underscore; can't start with digit.
    if (c.isLetter() || c == QLatin1Char('_')) {
        const int start = m_pos;
        while (m_pos < m_text.size()) {
            const QChar d = m_text.at(m_pos);
            if (d.isLetterOrNumber() || d == QLatin1Char('_'))
                ++m_pos;
            else
                break;
        }
        const QString name = m_text.mid(start, m_pos - start);
        const auto it = m_vars.constFind(name);
        if (it == m_vars.constEnd()) {
            fail(QStringLiteral("未知变量: \"%1\"").arg(name));
            return 0.0;
        }
        return it.value();
    }

    if (c.isNull())
        fail(QStringLiteral("表达式不完整"));
    else
        fail(QStringLiteral("无法识别的字符: \"%1\"").arg(c));
    return 0.0;
}

} // namespace cad::param
