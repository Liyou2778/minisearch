// Minimal JSON value / parser / serializer (objects, arrays, strings with
// escapes, numbers, booleans, null). No third-party dependencies.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ms {

class JsonValue {
public:
    enum class Type { Null, Bool, Num, Str, Arr, Obj };

    JsonValue() = default;

    static JsonValue null_value() { return JsonValue(); }
    static JsonValue boolean(bool v);
    static JsonValue number(double v);
    static JsonValue string(std::string v);
    static JsonValue array();
    static JsonValue object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_num() const { return type_ == Type::Num; }
    bool is_str() const { return type_ == Type::Str; }
    bool is_arr() const { return type_ == Type::Arr; }
    bool is_obj() const { return type_ == Type::Obj; }

    bool as_bool(bool def = false) const;
    double as_num(double def = 0.0) const;
    const std::string& str() const { return str_; }
    std::string as_str_copy() const { return is_str() ? str_ : std::string(); }
    std::string& str_mut() { return str_; }
    const std::vector<JsonValue>& arr() const { return arr_; }
    std::vector<JsonValue>& arr_mut() { return arr_; }
    const std::vector<std::pair<std::string, JsonValue>>& obj() const { return obj_; }
    std::vector<std::pair<std::string, JsonValue>>& obj_mut() { return obj_; }

    void push(JsonValue v);
    void set(std::string key, JsonValue v);
    const JsonValue* find(std::string_view key) const;

    std::string dump(bool pretty = false) const;
    void dump_to(std::string& out, bool pretty, int indent) const;

    // Returns std::nullopt on malformed input; fills *error when provided.
    static std::optional<JsonValue> parse(std::string_view text, std::string* error = nullptr);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<JsonValue> arr_;
    std::vector<std::pair<std::string, JsonValue>> obj_;
};

// Common response helpers.
JsonValue make_error_json(std::string code, std::string message);

}  // namespace ms
