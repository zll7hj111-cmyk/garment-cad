#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <vector>

namespace cad::param {

/// Tiny recursive-descent evaluator for arithmetic expressions.
/// Supports: + - * / ^ ( ), unary +/-, decimal numbers, and identifiers
/// (including CJK characters) resolved through a name -> value map.
/// Full-width characters (×÷（）＋－) are normalized before parsing.
///
/// Functions (lowercase-only, case-SENSITIVE; lowercase is reserved for
/// functions so uppercase reference names like B / MA_xxx never collide):
///   1-arg: cos / sin / tan / sqrt / abs / atan / asin / acos / floor /
///          ceil / round        — argument in DEGREES for trig (garment
///                                convention), result in DEGREES for the
///                                inverse trig (atan / asin / acos).
///   2-arg: atan2(y,x) / pow(a,b) / min(a,b) / max(a,b)
///                                — two args need parentheses + comma:
///                                atan2(y,x); inverse result in DEGREES.
/// Bare-argument forms work for 1-arg functions: cos60, sqrt2, cos前肩角度.
/// Power operator ^ is right-associative: 2^3^2 == 2^(3^2), and binds
/// tighter than unary minus: -2^2 == -(2^2).
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
        Cos, Sin, Tan,           ///< Trig on a degree argument.
        Sqrt, Abs, Pow,          ///< sqrt(a), |a|, a^b.
        Atan2,                   ///< atan2(y,x) -> degrees.
        Atan, Asin, Acos,        ///< Inverse trig (degree argument -> degree result).
        Floor, Ceil, Round,      ///< Rounding helpers.
        Min, Max                 ///< Two-argument min/max.
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
    /// reference stays valid until the compile cache is reset by pathological
    /// growth (~8192 unique expression texts) — callers must re-fetch per
    /// call rather than hold the reference across resets.
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
    void parsePrimary(Compiled& out);
    void parseTwoArgCall(Compiled& out, const QString& name, Op op);
    int internName(const QString& name, Compiled& out);
    void appendOp(Compiled& out, Op op, double num = 0.0, int nameIdx = -1);

    void skipSpaces();
    [[nodiscard]] QChar peek() const;
    void fail(Compiled& out, const QString& message);

    QString m_text;
    int m_pos = 0;
    bool m_ok = true;
    QString m_error;
    /// Parse recursion depth guard (pathological nesting would otherwise
    /// overflow the C++ call stack before execute()'s kMaxStack ever runs).
    int m_depth = 0;
};

/// Nesting-depth limit for the recursive-descent parser.
constexpr int kMaxParseDepth = 128;

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
