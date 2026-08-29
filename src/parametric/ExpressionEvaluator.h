#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <deque>
#include <vector>

namespace cad::param {

class ExpressionCache;

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
/// stack-machine bytecode and cached (identical text always yields identical
/// bytecode, so a cache entry never needs invalidation). Subsequent
/// evaluate() calls execute the cached bytecode directly, skipping
/// normalization, tokenization and parse-tree construction.
///
/// WHICH cache is used is now explicit (2026-12 P1-5): the compile cache is a
/// first-class object (ExpressionCache) owned by the document / pass, not a
/// process-wide static. Context-free callers still get one via
/// defaultCache(), which is THREAD-LOCAL — no shared mutable global state, so
/// a future worker-thread solver cannot race on the cache.
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

    /// Compile an expression (or fetch its cached bytecode) from the
    /// CONTEXT-FREE fallback cache (defaultCache()). Prefer an explicit
    /// ExpressionCache (document- or pass-owned) when one is available —
    /// see cacheFor(EvalContext*).
    static const Compiled& compiled(const QString& expression);

    /// Context-free fallback cache: thread-local, so no cross-thread sharing.
    /// Used only by callers that have no document/pass context (UI validation,
    /// one-shot command evaluations, tests).
    static ExpressionCache& defaultCache();

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
    /// ExpressionCache drives compilation on behalf of its owner (it is the
    /// only non-member allowed to build bytecode).
    friend class ExpressionCache;

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

/// Compile cache: expression text -> bytecode. Replaces the old process-wide
/// static (2026-12 P1-5) with an INSTANCE the owner controls — a document owns
/// one and threads it through every resolve pass, so its bytecode is
/// partitioned per document and released on document close/reload.
///
/// Reference stability (this is the whole point): entries live in a deque and
/// are NEVER erased or moved, so a `const Compiled&` handed out earlier stays
/// valid for the lifetime of the cache. The old static cache reset its entire
/// store on hitting 8192 entries, silently dangling every reference it had
/// already handed out (the header could only warn "callers must re-fetch").
/// The growth guard here instead opens a NEW generation: the old one is
/// retired (no longer searched or inserted) but stays allocated, so live
/// references keep pointing at valid memory.
class ExpressionCache
{
public:
    ExpressionCache() { m_generations.emplace_back(); }

    /// Compile @p expression, or return the cached bytecode. The returned
    /// reference is stable for the lifetime of this cache.
    [[nodiscard]] const ExpressionEvaluator::Compiled& compiled(const QString& expression);

    /// Drop every cached entry, reclaiming the memory. Only safe at a
    /// lifecycle boundary (document clear/close) — references handed out
    /// earlier are invalidated, so never call it mid-pass.
    void clear() { m_generations.clear(); m_generations.emplace_back(); }

    /// Entries in the LIVE generation (retired ones are no longer reachable).
    [[nodiscard]] int size() const
    { return static_cast<int>(m_generations.back().store.size()); }

    /// Number of generations (1 = never hit the growth guard). Diagnostics.
    [[nodiscard]] int generationCount() const
    { return static_cast<int>(m_generations.size()); }

private:
    /// Max entries in the live generation before a new one is opened. Sizing:
    /// a real document holds at most a few hundred distinct formulas; the guard
    /// only exists so live-typing validation (one distinct text per keystroke)
    /// cannot grow a single generation without bound.
    static constexpr int kMaxLiveEntries = 8192;

    struct Generation {
        QHash<QString, int> index;
        std::deque<ExpressionEvaluator::Compiled> store;
    };

    /// [0 .. n-2] retired (kept alive only so old references stay valid),
    /// back() = live generation. std::deque never moves existing elements on
    /// push_back, so Generation references survive appends.
    std::deque<Generation> m_generations;
};

/// Per-pass evaluation memo. Create ONE instance per resolve pass (during
/// which the variable map is guaranteed unchanged) and thread it through
/// ConditionEngine::evaluate: identical expression texts then execute only
/// once per pass no matter how many points/attachments reference them.
struct EvalContext {
    /// Compile cache for this pass. Null = use ExpressionEvaluator's
    /// thread-local fallback (ExpressionEvaluator::defaultCache()). Resolve
    /// passes run with the owning document's cache, so bytecode is partitioned
    /// per document and released with it.
    ExpressionCache* cache = nullptr;
    QHash<QString, ExpressionEvaluator::Result> memo;
    /// Case-folding index: lower(name) -> exact key in the pass's variable
    /// map. Lazily built on first use; a lookup miss falls back to the
    /// original linear scan, so keys added mid-pass stay resolvable.
    QHash<QString, QString> normLookup;
    bool normBuilt = false;
};

/// The cache a formula evaluation should use: the pass's explicit cache when
/// the caller threaded one through EvalContext, otherwise the thread-local
/// fallback. Single decision point — no call site picks a cache ad hoc.
[[nodiscard]] inline ExpressionCache& cacheFor(EvalContext* ctx)
{
    return (ctx && ctx->cache) ? *ctx->cache : ExpressionEvaluator::defaultCache();
}

} // namespace cad::param
