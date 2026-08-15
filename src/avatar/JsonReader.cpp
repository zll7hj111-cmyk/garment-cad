#include "JsonReader.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace cad::avatar {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    JsonValue parse() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (m_pos != m_text.size())
            fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("JsonReader: " + msg + " at offset " + std::to_string(m_pos));
    }

    void skipWs() {
        while (m_pos < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos])))
            ++m_pos;
    }

    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }
    char take() {
        if (m_pos >= m_text.size()) fail("unexpected end of input");
        return m_text[m_pos++];
    }
    void expect(char c) {
        if (take() != c) fail(std::string("expected '") + c + "'");
    }

    JsonValue parseValue() {
        switch (peek()) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return JsonValue::makeString(parseString());
        case 't':
            literal("true");
            return JsonValue::makeBool(true);
        case 'f':
            literal("false");
            return JsonValue::makeBool(false);
        case 'n':
            literal("null");
            return JsonValue::makeNull();
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())))
                return JsonValue::makeNumber(parseNumber());
            fail("unexpected character");
        }
    }

    void literal(const char* word) {
        for (const char* p = word; *p; ++p)
            if (take() != *p) fail(std::string("invalid literal '") + word + "'");
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue obj = JsonValue::makeObject();
        skipWs();
        if (peek() == '}') {
            take();
            return obj;
        }
        for (;;) {
            skipWs();
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            skipWs();
            expect(':');
            skipWs();
            JsonValue val = parseValue();
            obj.object().emplace_back(std::move(key), std::move(val));
            skipWs();
            if (peek() == ',') {
                take();
                continue;
            }
            if (peek() == '}') {
                take();
                return obj;
            }
            fail("expected ',' or '}'");
        }
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue arr = JsonValue::makeArray();
        skipWs();
        if (peek() == ']') {
            take();
            return arr;
        }
        for (;;) {
            skipWs();
            arr.array().push_back(parseValue());
            skipWs();
            if (peek() == ',') {
                take();
                continue;
            }
            if (peek() == ']') {
                take();
                return arr;
            }
            fail("expected ',' or ']'");
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        for (;;) {
            char c = take();
            if (c == '"') return out;
            if (c == '\\') {
                char e = take();
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = take();
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else fail("invalid \\u escape");
                    }
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: fail("invalid escape");
                }
            } else {
                out += c;
            }
        }
    }

    double parseNumber() {
        size_t start = m_pos;
        if (peek() == '-') take();
        while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        if (peek() == '.') {
            take();
            while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        }
        if (peek() == 'e' || peek() == 'E') {
            take();
            if (peek() == '+' || peek() == '-') take();
            while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        }
        std::string token = m_text.substr(start, m_pos - start);
        char* end = nullptr;
        double v = std::strtod(token.c_str(), &end);
        if (end == token.c_str()) fail("invalid number");
        return v;
    }

    const std::string& m_text;
    size_t m_pos = 0;
};

} // namespace

JsonValue parseJson(const std::string& text) {
    return Parser(text).parse();
}

JsonValue parseJsonFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("JsonReader: cannot open file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseJson(ss.str());
}

} // namespace cad::avatar
