#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <vector>

namespace cad::param {

/// Tiny recursive-descent evaluator for arithmetic expressions.
/// Supports: + - * / ( ), unary +/-, decimal numbers, and identifiers
/// (including CJK characters) resolved through a name -> value map.
/// Full-width characters (×÷（）＋－) are normalized before parsing.
///
/// Performance: each unique expression text is compiled ONCE into compact
/// stack-machine bytecode and cached process-wide (identical text always
/// yields identical bytecode, so the cache never needs invalidation).
/// Subsequent evaluate() calls execute the cached bytecode directly,
/// skipping normalization, tokenization and parse-tree construction.
class ExpressionEvaluator
{
public:
    struct Result {
        bool ok = false;
        double value = 0.0;
        QString error;
    };

    /// Bytecode opcodes emitted by the compiler.
    enum class Op : quint8 {
        PushNum,  ///< Push an immediate constant.
        PushVar,  ///< Push a variable's value (resolved at execute time).
        Add, Sub, Mul, Div,
        Neg,      ///< Unary minus.
        Cos, Sin, Tan  ///< Trig on a degree argument.
    };

    struct Instr {
        Op op = Op::PushNum;
        double num = 0.0;   ///< PushNum only.
        int nameIdx = -1;   ///< PushVar only: index into Compiled::names.
    };

    /// Compiled form of an expression (bytecode + interned identifier names).
    struct Compiled {
        bool ok = false;
        QString error;               ///< Compile-time error, if any.
        std::vector<Instr> code;
        QStringList names;           ///< Unique identifier tokens referenced.
        bool isStandalone = false;   ///< Whole expression is one bare identifier.
        QString standaloneName;      ///< Valid when isStandalone.
    };

    /// Compile an expression (or fetch its cached bytecode). The returned
    /// reference is stable for the lifetime of the process (deque storage).
    static const Compiled& compiled(const QString& expression);

    /// Execute compiled bytecode against a variable map (fast path).
    /// @p normLookup Optional case-folding index (lower(name) -> exact key),
    /// built once per pass from the SAME variable map; turns the case-
    /// insensitive fallback from an O(N) linear scan into an O(1) lookup.
    /// When null, the fallback keeps its original linear-scan behaviour.
    /// Exact matches always win over both paths.
    static Result execute(const Compiled& code,
                          const QHash<QString, double>& variables,
                          const QHash<QString, QString>* normLookup = nullptr);

    /// Convenience: compiled() + execute().
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
    explicit ExpressionEvaluator(QString normalizedText);

    void compile(Compiled& out);
    void parseExpression(Compiled& out);
    void parseTerm(Compiled& out);
    void parseFactor(Compiled& out);
    int internName(const QString& name, Compiled& out);
    void appendOp(Compiled& out, Op op, double num = 0.0, int nameIdx = -1);

    void skipSpaces();
    [[nodiscard]] QChar peek() const;
    void fail(Compiled& out, const QString& message);

    QString m_text;
    int m_pos = 0;
    bool m_ok = true;
    QString m_error;
};

/// Per-pass evaluation memo. Create ONE instance per resolve pass (during
/// which the variable map is guaranteed unchanged) and thread it through
/// ConditionEngine::evaluate: identical expression texts then execute only
/// once per pass no matter how many points/attachments reference them.
struct EvalContext {
    QHash<QString, ExpressionEvaluator::Result> memo;
    /// Case-folding index: lower(name) -> exact key in the pass's variable
    /// map. Lazily built on first use; a lookup miss falls back to the
    /// original linear scan, so keys added mid-pass stay resolvable.
    QHash<QString, QString> normLookup;
    bool normBuilt = false;
};

} // namespace cad::param
