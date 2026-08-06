#include "ExpressionEvaluator.h"

#include <cmath>
#include <deque>

namespace cad::param {

namespace {
/// Trig arguments are interpreted as degrees (garment-making convention).
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Maximum bytecode stack depth (far beyond any realistic expression).
constexpr int kMaxStack = 256;
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
        }
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
    skipSpaces();
    const QChar c = peek();

    // Unary +/-
    if (c == QLatin1Char('+')) {
        ++m_pos;
        parseFactor(out);
        return;
    }
    if (c == QLatin1Char('-')) {
        ++m_pos;
        parseFactor(out);
        appendOp(out, Op::Neg);
        return;
    }

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
        // Trig functions (cos/sin/tan) with the argument in degrees.
        // IMPORTANT: function names are matched case-SENSITIVELY in lowercase only.
        // This reserves lowercase cos/sin/tan for functions so that uppercase
        // variable reference names (the convention, e.g. B, V1) can never be
        // mistaken for a function.
        //
        // Argument forms: cos(22), cos22, cos22°, cos 22, cos前肩角度 (CJK var),
        // cosA (uppercase var). The call is recognized unless the name continues
        // as a longer LOWERCASE identifier (a-z or '_'), e.g. "cosine" / "cos_a",
        // which falls through to normal variable lookup.
        if (m_text.size() - m_pos >= 3) {
            const QString head = m_text.mid(m_pos, 3);
            const bool isTrig = (head == QLatin1String("cos") ||
                                 head == QLatin1String("sin") ||
                                 head == QLatin1String("tan"));
            if (isTrig) {
                const QChar after = (m_pos + 3 < m_text.size())
                    ? m_text.at(m_pos + 3) : QChar();
                // A following lowercase ASCII letter or underscore continues a
                // longer identifier (cosine, cos_a) -> not a function call.
                // Anything else (CJK, uppercase, digit, '(', operator, end)
                // starts the argument.
                const bool continuesIdentifier =
                    after == QLatin1Char('_') ||
                    (after >= QLatin1Char('a') && after <= QLatin1Char('z'));
                if (!continuesIdentifier) {
                    m_pos += 3;
                    parseFactor(out);
                    if (!m_ok)
                        return;
                    if (head == QLatin1String("cos"))
                        appendOp(out, Op::Cos);
                    else if (head == QLatin1String("sin"))
                        appendOp(out, Op::Sin);
                    else
                        appendOp(out, Op::Tan);
                    return;
                }
            }
        }

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

} // namespace cad::param
