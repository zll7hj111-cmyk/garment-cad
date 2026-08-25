#include "ExpressionEvaluator.h"

#include <cmath>
#include <deque>

namespace cad::param {

namespace {
/// Trig arguments are interpreted as degrees (garment-making convention).
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Maximum bytecode stack depth (far beyond any realistic expression).
constexpr int kMaxStack = 256;

/// RAII recursion guard for the recursive-descent parser: pathological
/// inputs like "((((((...1" or "-----...-1" recurse once per nesting level
/// and would overflow the C++ call stack during compile (execute()'s
/// kMaxStack only protects the bytecode run, which happens AFTER parsing).
struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
};

/// Function signature: arity + the bytecode op it compiles to.
struct FuncSig {
    int arity = 0;
    ExpressionEvaluator::Op op = ExpressionEvaluator::Op::PushNum;
};

/// Built-in function registry. Names are lowercase ASCII ONLY (case-sensitive
/// by design — see the parser's "lowercase reserved for functions" rule).
const QHash<QString, FuncSig>& functionTable()
{
    static const QHash<QString, FuncSig> table = [] {
        QHash<QString, FuncSig> t;
        t.insert(QStringLiteral("cos"),   {1, ExpressionEvaluator::Op::Cos});
        t.insert(QStringLiteral("sin"),   {1, ExpressionEvaluator::Op::Sin});
        t.insert(QStringLiteral("tan"),   {1, ExpressionEvaluator::Op::Tan});
        t.insert(QStringLiteral("sqrt"),  {1, ExpressionEvaluator::Op::Sqrt});
        t.insert(QStringLiteral("abs"),   {1, ExpressionEvaluator::Op::Abs});
        t.insert(QStringLiteral("atan"),  {1, ExpressionEvaluator::Op::Atan});
        t.insert(QStringLiteral("asin"),  {1, ExpressionEvaluator::Op::Asin});
        t.insert(QStringLiteral("acos"),  {1, ExpressionEvaluator::Op::Acos});
        t.insert(QStringLiteral("floor"), {1, ExpressionEvaluator::Op::Floor});
        t.insert(QStringLiteral("ceil"),  {1, ExpressionEvaluator::Op::Ceil});
        t.insert(QStringLiteral("round"), {1, ExpressionEvaluator::Op::Round});
        t.insert(QStringLiteral("pow"),   {2, ExpressionEvaluator::Op::Pow});
        t.insert(QStringLiteral("min"),   {2, ExpressionEvaluator::Op::Min});
        t.insert(QStringLiteral("max"),   {2, ExpressionEvaluator::Op::Max});
        t.insert(QStringLiteral("atan2"), {2, ExpressionEvaluator::Op::Atan2});
        return t;
    }();
    return table;
}
} // namespace

// ============================================================
// Compile cache (process-wide, stable references via deque)
// ============================================================

const ExpressionEvaluator::Compiled&
ExpressionEvaluator::compiled(const QString& expression)
{
    // deque storage keeps element references stable across insertions, so
    // callers may hold a `const Compiled&` without lifetime concerns.
    static QHash<QString, int> index;
    static std::deque<Compiled> store;

    auto it = index.constFind(expression);
    if (it != index.constEnd())
        return store[static_cast<std::size_t>(it.value())];

    // Pathological-growth guard (unique expression texts are few in practice).
    // NOTE: resetting invalidates every previously returned reference — the
    // documented contract (see header) is that callers must re-fetch per call.
    if (index.size() >= 8192) {
        index.clear();
        store.clear();
    }

    Compiled c;
    ExpressionEvaluator ev(normalized(expression));
    ev.compile(c);
    store.push_back(std::move(c));
    index.insert(expression, static_cast<int>(store.size()) - 1);
    return store.back();
}

// ============================================================
// Public API
// ============================================================

ExpressionEvaluator::Result
ExpressionEvaluator::evaluate(const QString& expression,
                              const QHash<QString, double>& variables)
{
    return execute(compiled(expression), variables);
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

    // Brackets: [ ] { } and their full-width / CJK variants all act as parentheses.
    t.replace(QLatin1Char('['), QLatin1Char('('));
    t.replace(QLatin1Char(']'), QLatin1Char(')'));
    t.replace(QLatin1Char('{'), QLatin1Char('('));
    t.replace(QLatin1Char('}'), QLatin1Char(')'));
    t.replace(QChar(0xFF3B), QLatin1Char('('));  // ［
    t.replace(QChar(0xFF3D), QLatin1Char(')'));  // ］
    t.replace(QChar(0xFF5B), QLatin1Char('('));  // ｛
    t.replace(QChar(0xFF5D), QLatin1Char(')'));  // ｝
    t.replace(QChar(0x3010), QLatin1Char('('));  // 【
    t.replace(QChar(0x3011), QLatin1Char(')'));  // 】

    // Degree symbol is a decorative unit suffix (arguments are always degrees).
    t.remove(QChar(0x00B0));  // °
    t.remove(QChar(0x00BA));  // º (masculine ordinal, often typed as degrees)
    return t;
}

QStringList ExpressionEvaluator::referencedNames(const QString& expression)
{
    // NOTE: deliberately NO normalized() pass here — identifier extraction
    // only cares about letters/digits/underscore, and the full-width
    // ×÷（）＋－ characters do not affect that token set (full-width LETTERS
    // are isLetter() either way). Skipping the 15-pass string rewrite keeps
    // this cheap — it is the hot path of per-recompute dependency extraction.
    const QString& t = expression;
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
    const Compiled& c = compiled(expression);
    return (c.ok && c.isStandalone) ? c.standaloneName : QString();
}

// ============================================================
// Bytecode execution (stack machine)
// ============================================================

ExpressionEvaluator::Result
ExpressionEvaluator::execute(const Compiled& code,
                             const QHash<QString, double>& variables,
                             const QHash<QString, QString>* normLookup)
{
    Result r;
    if (!code.ok) {
        r.error = code.error;
        return r;
    }

    // Stack discipline is guaranteed by the compiler's construction, so only
    // overflow needs a defensive guard.
    double stack[kMaxStack];
    int sp = 0;

    for (const Instr& in : code.code) {
        switch (in.op) {
        case Op::PushNum:
            if (sp >= kMaxStack) { r.error = QStringLiteral("表达式过深"); return r; }
            stack[sp++] = in.num;
            break;

        case Op::PushVar: {
            const QString& name = code.names[in.nameIdx];
            auto it = variables.constFind(name);
            double v = 0.0;
            if (it != variables.constEnd()) {
                v = it.value();
            } else {
                // Case-insensitive fallback: reference names are uppercase by
                // convention, but users may type lowercase (e.g. "b/4"
                // resolves variable "B"). O(1) via the per-pass normLookup
                // when available; exact keys always win over both paths.
                bool found = false;
                if (normLookup) {
                    const auto nit = normLookup->constFind(name.toLower());
                    if (nit != normLookup->constEnd()) {
                        const auto vit = variables.constFind(nit.value());
                        if (vit != variables.constEnd()) {
                            v = vit.value();
                            found = true;
                        }
                    }
                }
                // Index miss (e.g. a key added mid-pass): fall back to the
                // original linear scan so behaviour is unchanged.
                if (!found) {
                    for (auto cit = variables.constBegin(); cit != variables.constEnd(); ++cit) {
                        if (cit.key().compare(name, Qt::CaseInsensitive) == 0) {
                            v = cit.value();
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    r.error = QStringLiteral("未知变量: \"%1\"").arg(name);
                    return r;
                }
            }
            if (sp >= kMaxStack) { r.error = QStringLiteral("表达式过深"); return r; }
            stack[sp++] = v;
            break;
        }

        case Op::Add: { const double b = stack[--sp]; stack[sp - 1] += b; break; }
        case Op::Sub: { const double b = stack[--sp]; stack[sp - 1] -= b; break; }
        case Op::Mul: { const double b = stack[--sp]; stack[sp - 1] *= b; break; }
        case Op::Div: {
            const double b = stack[--sp];
            if (qFuzzyIsNull(b)) {
                r.error = QStringLiteral("除数为零");
                return r;
            }
            stack[sp - 1] /= b;
            break;
        }
        case Op::Neg: stack[sp - 1] = -stack[sp - 1]; break;
        case Op::Cos: stack[sp - 1] = std::cos(stack[sp - 1] * kDegToRad); break;
        case Op::Sin: stack[sp - 1] = std::sin(stack[sp - 1] * kDegToRad); break;
        case Op::Tan: stack[sp - 1] = std::tan(stack[sp - 1] * kDegToRad); break;

        case Op::Sqrt: {
            const double a = stack[sp - 1];
            if (a < 0.0) { r.error = QStringLiteral("负数不能开平方"); return r; }
            stack[sp - 1] = std::sqrt(a);
            break;
        }
        case Op::Abs: stack[sp - 1] = std::abs(stack[sp - 1]); break;
        case Op::Pow: {
            const double b = stack[--sp];
            stack[sp - 1] = std::pow(stack[sp - 1], b);
            break;
        }
        case Op::Atan2: {
            const double x = stack[--sp];   // atan2(y, x): second arg is x.
            const double y = stack[sp - 1];
            stack[sp - 1] = std::atan2(y, x) / kDegToRad;   // degrees (garment convention)
            break;
        }
        case Op::Atan: stack[sp - 1] = std::atan(stack[sp - 1]) / kDegToRad; break;
        case Op::Asin: {
            const double a = stack[sp - 1];
            if (a < -1.0 || a > 1.0) {
                r.error = QStringLiteral("asin 参数超出 [-1, 1]");
                return r;
            }
            stack[sp - 1] = std::asin(a) / kDegToRad;
            break;
        }
        case Op::Acos: {
            const double a = stack[sp - 1];
            if (a < -1.0 || a > 1.0) {
                r.error = QStringLiteral("acos 参数超出 [-1, 1]");
                return r;
            }
            stack[sp - 1] = std::acos(a) / kDegToRad;
            break;
        }
        case Op::Floor: stack[sp - 1] = std::floor(stack[sp - 1]); break;
        case Op::Ceil: stack[sp - 1] = std::ceil(stack[sp - 1]); break;
        case Op::Round: stack[sp - 1] = std::round(stack[sp - 1]); break;
        case Op::Min: {
            const double b = stack[--sp];
            if (b < stack[sp - 1]) stack[sp - 1] = b;
            break;
        }
        case Op::Max: {
            const double b = stack[--sp];
            if (b > stack[sp - 1]) stack[sp - 1] = b;
            break;
        }
        }
    }

    if (!std::isfinite(stack[sp - 1])) {
        r.error = QStringLiteral("结果无效（非有限数值）");
        return r;
    }
    r.ok = true;
    r.value = stack[sp - 1];
    return r;
}

// ============================================================
// Compiler (recursive descent -> bytecode)
// ============================================================

ExpressionEvaluator::ExpressionEvaluator(QString normalizedText)
    : m_text(std::move(normalizedText))
{
}

void ExpressionEvaluator::appendOp(Compiled& out, Op op, double num, int nameIdx)
{
    Instr in;
    in.op = op;
    in.num = num;
    in.nameIdx = nameIdx;
    out.code.push_back(in);
}

int ExpressionEvaluator::internName(const QString& name, Compiled& out)
{
    for (int i = 0; i < out.names.size(); ++i) {
        if (out.names[i] == name)
            return i;
    }
    out.names.append(name);
    return out.names.size() - 1;
}

void ExpressionEvaluator::fail(Compiled& out, const QString& message)
{
    if (m_ok) {
        m_ok = false;
        m_error = message;
    }
    out.ok = false;
    out.error = m_error;
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

void ExpressionEvaluator::compile(Compiled& out)
{
    if (m_text.trimmed().isEmpty()) {
        fail(out, QStringLiteral("表达式为空"));
        return;
    }

    parseExpression(out);
    skipSpaces();
    if (m_ok && m_pos < m_text.size())
        fail(out, QStringLiteral("存在无法解析的内容: \"%1\"")
                    .arg(m_text.mid(m_pos)));

    out.ok = m_ok;
    out.error = m_error;

    // Standalone detection: exactly one PushVar instruction.
    if (out.ok && out.code.size() == 1 && out.code.front().op == Op::PushVar) {
        out.isStandalone = true;
        out.standaloneName = out.names[out.code.front().nameIdx];
    }
}

void ExpressionEvaluator::parseExpression(Compiled& out)
{
    if (m_depth >= kMaxParseDepth) {
        fail(out, QStringLiteral("表达式嵌套过深"));
        return;
    }
    const DepthGuard guard(m_depth);
    parseTerm(out);
    for (;;) {
        skipSpaces();
        const QChar c = peek();
        if (c == QLatin1Char('+')) {
            ++m_pos;
            parseTerm(out);
            appendOp(out, Op::Add);
        } else if (c == QLatin1Char('-')) {
            ++m_pos;
            parseTerm(out);
            appendOp(out, Op::Sub);
        } else {
            return;
        }
        if (!m_ok)
            return;
    }
}

void ExpressionEvaluator::parseTerm(Compiled& out)
{
    parseFactor(out);
    for (;;) {
        skipSpaces();
        const QChar c = peek();
        if (c == QLatin1Char('*')) {
            ++m_pos;
            parseFactor(out);
            appendOp(out, Op::Mul);
        } else if (c == QLatin1Char('/')) {
            ++m_pos;
            parseFactor(out);
            appendOp(out, Op::Div);
        } else {
            return;
        }
        if (!m_ok)
            return;
    }
}

void ExpressionEvaluator::parseFactor(Compiled& out)
{
    if (m_depth >= kMaxParseDepth) {
        fail(out, QStringLiteral("表达式嵌套过深"));
        return;
    }
    const DepthGuard guard(m_depth);

    // Unary +/-
    skipSpaces();
    const QChar c = peek();
    if (c == QLatin1Char('+')) {
        ++m_pos;
        parseFactor(out);
        return;
    }
    if (c == QLatin1Char('-')) {
        ++m_pos;
        parseFactor(out);
        if (!m_ok)
            return;
        appendOp(out, Op::Neg);
        return;
    }

    parsePrimary(out);
    if (!m_ok)
        return;

    // Power operator (right-associative): 2^3^2 == 2^(3^2); binds tighter
    // than unary minus (-2^2 == -(2^2)) because the unary branch above
    // recurses through here.
    skipSpaces();
    if (peek() == QLatin1Char('^')) {
        ++m_pos;
        parseFactor(out);
        if (!m_ok)
            return;
        appendOp(out, Op::Pow);
    }
}

void ExpressionEvaluator::parsePrimary(Compiled& out)
{
    if (m_depth >= kMaxParseDepth) {
        fail(out, QStringLiteral("表达式嵌套过深"));
        return;
    }
    const DepthGuard guard(m_depth);
    skipSpaces();
    const QChar c = peek();

    // Parenthesized sub-expression
    if (c == QLatin1Char('(')) {
        ++m_pos;
        parseExpression(out);
        skipSpaces();
        if (peek() == QLatin1Char(')')) {
            ++m_pos;
        } else {
            fail(out, QStringLiteral("缺少右括号 )"));
        }
        return;
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
        if (!numOk) {
            fail(out, QStringLiteral("无效数字: \"%1\"").arg(m_text.mid(start, m_pos - start)));
            return;
        }
        appendOp(out, Op::PushNum, v);
        return;
    }

    // Identifier: letters (incl. CJK), digits, underscore; can't start with digit.
    if (c.isLetter() || c == QLatin1Char('_')) {
        // Function or variable, decided by a leading lowercase ASCII run.
        // IMPORTANT: function names are matched case-SENSITIVELY in lowercase
        // only, which reserves lowercase for functions so that uppercase
        // reference names (the convention, e.g. B, MA_xxx) can never be
        // mistaken for a function.
        //
        // Argument forms (1-arg): cos(22), cos22, cos22°, cos 22, cos前肩角度
        // (CJK var), cosA (uppercase var). A lowercase run that keeps going
        // with a lowercase letter or '_' (cosine, cos_a) is a VARIABLE.
        if (c.isLower()) {
            const int runStart = m_pos;
            int runEnd = m_pos;
            while (runEnd < m_text.size()) {
                const QChar d = m_text.at(runEnd);
                if (d.unicode() >= QLatin1Char('a').unicode()
                    && d.unicode() <= QLatin1Char('z').unicode())
                    ++runEnd;
                else
                    break;
            }
            const QString run = m_text.mid(runStart, runEnd - runStart);

            // atan2 special case: the '2' is a digit, not part of the
            // lowercase run ("atan2" scans as run "atan" + '2'). Only the
            // exact call form atan2(y,x) is a function; "atan2x"-style
            // identifiers stay variables (unchanged semantics).
            if (run == QLatin1String("atan")
                && runEnd < m_text.size()
                && m_text.at(runEnd) == QLatin1Char('2')) {
                if (runEnd + 1 < m_text.size()
                    && m_text.at(runEnd + 1) == QLatin1Char('(')) {
                    m_pos = runEnd + 1;
                    parseTwoArgCall(out, QStringLiteral("atan2"), Op::Atan2);
                    return;
                }
            } else {
                const auto it = functionTable().constFind(run);
                if (it != functionTable().constEnd()) {
                    const FuncSig sig = it.value();
                    const QChar after = (runEnd < m_text.size())
                        ? m_text.at(runEnd) : QChar();
                    // A following lowercase ASCII letter or underscore continues
                    // a longer identifier (cosine, cos_a) -> variable, not call.
                    const bool continuesIdentifier =
                        after == QLatin1Char('_') ||
                        (after.unicode() >= QLatin1Char('a').unicode()
                         && after.unicode() <= QLatin1Char('z').unicode());
                    if (!continuesIdentifier) {
                        if (sig.arity == 1) {
                            m_pos = runEnd;
                            parseFactor(out);       // bare or parenthesized argument
                            if (!m_ok)
                                return;
                            appendOp(out, sig.op);
                            return;
                        }
                        // Two-arg functions need explicit parentheses: pow(a,b).
                        // Look ahead WITHOUT committing m_pos so the variable
                        // fallback below still reads the full identifier.
                        int parenPos = runEnd;
                        while (parenPos < m_text.size()
                               && m_text.at(parenPos).isSpace())
                            ++parenPos;
                        if (parenPos < m_text.size()
                            && m_text.at(parenPos) == QLatin1Char('(')) {
                            m_pos = parenPos;
                            parseTwoArgCall(out, run, sig.op);
                            return;
                        }
                        // No parenthesis -> keep old semantics: it's a variable
                        // (e.g. "min2", "powx").
                    }
                }
            }
        }

        // Variable: letters (incl. CJK), digits, underscore; can't start with digit.
        const int start = m_pos;
        while (m_pos < m_text.size()) {
            const QChar d = m_text.at(m_pos);
            if (d.isLetterOrNumber() || d == QLatin1Char('_'))
                ++m_pos;
            else
                break;
        }
        const QString name = m_text.mid(start, m_pos - start);
        appendOp(out, Op::PushVar, 0.0, internName(name, out));
        return;
    }

    if (c.isNull())
        fail(out, QStringLiteral("表达式不完整"));
    else
        fail(out, QStringLiteral("无法识别的字符: \"%1\"").arg(c));
}

void ExpressionEvaluator::parseTwoArgCall(Compiled& out, const QString& name, Op op)
{
    if (m_depth >= kMaxParseDepth) {
        fail(out, QStringLiteral("表达式嵌套过深"));
        return;
    }
    const DepthGuard guard(m_depth);
    // m_pos is positioned at the opening '('.
    ++m_pos;
    parseExpression(out);
    if (!m_ok)
        return;
    skipSpaces();
    if (peek() != QLatin1Char(',')) {
        fail(out, QStringLiteral("函数 %1 需要两个参数(逗号分隔)").arg(name));
        return;
    }
    ++m_pos;
    parseExpression(out);
    if (!m_ok)
        return;
    skipSpaces();
    if (peek() != QLatin1Char(')')) {
        fail(out, QStringLiteral("缺少右括号 )"));
        return;
    }
    ++m_pos;
    appendOp(out, op);
}

} // namespace cad::param
