#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cad::avatar {

/// 极简只读 JSON 解析器（零依赖）。
/// 仅用于引擎读取自有数据文件（measurement_chains.json 等），
/// 不追求完整 JSON 规范——不支持注释、转义超集、大数精度等。
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : m_type(Type::Null) {}

    static JsonValue makeNull() { return JsonValue(); }
    static JsonValue makeBool(bool b) {
        JsonValue v; v.m_type = Type::Bool; v.m_bool = b; return v;
    }
    static JsonValue makeNumber(double n) {
        JsonValue v; v.m_type = Type::Number; v.m_number = n; return v;
    }
    static JsonValue makeString(std::string s) {
        JsonValue v; v.m_type = Type::String; v.m_string = std::move(s); return v;
    }
    static JsonValue makeArray() { JsonValue v; v.m_type = Type::Array; v.m_array.emplace(); return v; }
    static JsonValue makeObject() { JsonValue v; v.m_type = Type::Object; v.m_object.emplace(); return v; }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }

    bool asBool() const {
        if (m_type != Type::Bool) throw std::runtime_error("JsonValue: not a bool");
        return m_bool;
    }
    double asNumber() const {
        if (m_type != Type::Number) throw std::runtime_error("JsonValue: not a number");
        return m_number;
    }
    int64_t asInt64() const {
        if (m_type != Type::Number) throw std::runtime_error("JsonValue: not a number");
        return static_cast<int64_t>(m_number);
    }
    const std::string& asString() const {
        if (m_type != Type::String) throw std::runtime_error("JsonValue: not a string");
        return m_string;
    }
    const Array& asArray() const {
        if (m_type != Type::Array) throw std::runtime_error("JsonValue: not an array");
        return *m_array;
    }
    const Object& asObject() const {
        if (m_type != Type::Object) throw std::runtime_error("JsonValue: not an object");
        return *m_object;
    }

    Array& array() {
        if (m_type != Type::Array) throw std::runtime_error("JsonValue: not an array");
        return *m_array;
    }
    Object& object() {
        if (m_type != Type::Object) throw std::runtime_error("JsonValue: not an object");
        return *m_object;
    }

    /// 按 key 查找对象成员（返回 nullptr 表示不存在）。
    const JsonValue* find(const std::string& key) const {
        if (m_type != Type::Object) return nullptr;
        for (const auto& [k, v] : *m_object)
            if (k == key) return &v;
        return nullptr;
    }

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::optional<Array> m_array;
    std::optional<Object> m_object;
};

/// 解析 JSON 文本。失败抛 std::runtime_error。
JsonValue parseJson(const std::string& text);

/// 从文件解析 JSON。失败抛 std::runtime_error。
JsonValue parseJsonFile(const std::string& path);

} // namespace cad::avatar
