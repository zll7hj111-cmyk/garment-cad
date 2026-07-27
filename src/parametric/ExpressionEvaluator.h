#pragma once

#include <QString>
#include <QStringList>
#include <QHash>

namespace cad::param {

/// Tiny recursive-descent evaluator for arithmetic expressions.
/// Supports: + - * / ( ), unary +/-, decimal numbers, and identifiers
/// (including CJK characters) resolved through a name -> value map.
/// Full-width characters (×÷（）＋－) are normalized before parsing.
class ExpressionEvaluator
{
public:
    struct Result {
        bool ok = false;
        double value = 0.0;
        QString error;
    };

    static Result evaluate(const QString& expression,
                           const QHash<QString, double>& variables);

    /// Normalize full-width input characters to ASCII equivalents.
    [[nodiscard]] static QString normalized(const QString& text);

    /// All identifier tokens referenced in the expression (duplicates kept).
    /// Used to discover which variables a formula actually uses.
    [[nodiscard]] static QStringList referencedNames(const QString& expression);

    /// If the whole expression is a single bare identifier, return it;
    /// otherwise return an empty string. Used to detect "standalone" references.
    [[nodiscard]] static QString standaloneIdentifier(const QString& expression);

private:
    ExpressionEvaluator(const QString& text, const QHash<QString, double>& vars);

    double parseExpression();
    double parseTerm();
    double parseFactor();

    void skipSpaces();
    [[nodiscard]] QChar peek() const;
    void fail(const QString& message);

    QString m_text;
    const QHash<QString, double>& m_vars;
    int m_pos = 0;
    bool m_ok = true;
    QString m_error;
};

} // namespace cad::param
