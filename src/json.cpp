#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ms {
namespace {

void append_quoted(std::string& out, std::string_view s) {
    out += '"';
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void append_number(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "null";
        return;
    }
    // Integers print without a fractional part.
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        auto res = std::to_chars(buf, buf + sizeof(buf), static_cast<long long>(v));
        if (res.ec == std::errc()) {
            out.append(buf, res.ptr);
            return;
        }
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec == std::errc()) {
        out.append(buf, res.ptr);
        return;
    }
    int n = std::snprintf(buf, sizeof(buf), "%.10g", v);
    if (n > 0) out.append(buf, static_cast<size_t>(n));
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    Parser(std::string_view text) : s_(text) {}

    bool parse_document(JsonValue& out) {
        skip_ws();
        if (!parse_value(out, 0)) return false;
        skip_ws();
        if (pos_ != s_.size()) return fail("trailing characters after JSON value");
        return true;
    }

    const std::string& error() const { return err_; }

private:
    bool fail(const char* msg) {
        if (err_.empty()) {
            err_ = msg;
            err_pos_ = pos_;
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool parse_value(JsonValue& out, int depth) {
        if (depth > 128) return fail("JSON nesting too deep");
        skip_ws();
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        char c = s_[pos_];
        switch (c) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string tmp;
                if (!parse_string(tmp)) return false;
                out = JsonValue::string(std::move(tmp));
                return true;
            }
            case 't':
                if (s_.substr(pos_, 4) == "true") {
                    pos_ += 4;
                    out = JsonValue::boolean(true);
                    return true;
                }
                return fail("invalid literal, expected 'true'");
            case 'f':
                if (s_.substr(pos_, 5) == "false") {
                    pos_ += 5;
                    out = JsonValue::boolean(false);
                    return true;
                }
                return fail("invalid literal, expected 'false'");
            case 'n':
                if (s_.substr(pos_, 4) == "null") {
                    pos_ += 4;
                    out = JsonValue::null_value();
                    return true;
                }
                return fail("invalid literal, expected 'null'");
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
                return fail("unexpected character while parsing value");
        }
    }

    bool parse_number(JsonValue& out) {
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size()) return fail("truncated number");
        if (s_[pos_] == '0') {
            ++pos_;
        } else if (s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        } else {
            return fail("invalid number");
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            size_t digits = 0;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
                ++pos_;
                ++digits;
            }
            if (digits == 0) return fail("invalid fraction in number");
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            size_t digits = 0;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
                ++pos_;
                ++digits;
            }
            if (digits == 0) return fail("invalid exponent in number");
        }
        double value = 0.0;
        auto res = std::from_chars(s_.data() + start, s_.data() + pos_, value);
        if (res.ec != std::errc()) return fail("number out of range");
        out = JsonValue::number(value);
        return true;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parse_hex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int d = hex_digit(s_[pos_ + static_cast<size_t>(i)]);
            if (d < 0) return fail("invalid hex digit in \\u escape");
            v = (v << 4) | static_cast<uint32_t>(d);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool parse_string(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected string quote");
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= s_.size()) return fail("truncated escape sequence");
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parse_hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                                pos_ += 2;
                                uint32_t low = 0;
                                if (!parse_hex4(low)) return false;
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                } else {
                                    append_utf8(out, cp);
                                    cp = low;
                                }
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: return fail("invalid escape character");
                }
                continue;
            }
            if (c < 0x20) return fail("unescaped control character in string");
            out += static_cast<char>(c);
            ++pos_;
        }
        return fail("unterminated string");
    }

    bool parse_array(JsonValue& out, int depth) {
        ++pos_;  // '['
        out = JsonValue::array();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue item;
            if (!parse_value(item, depth + 1)) return false;
            out.push(std::move(item));
            skip_ws();
            if (pos_ >= s_.size()) return fail("unterminated array");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_object(JsonValue& out, int depth) {
        ++pos_;  // '{'
        out = JsonValue::object();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':' in object");
            ++pos_;
            JsonValue value;
            if (!parse_value(value, depth + 1)) return false;
            out.set(std::move(key), std::move(value));
            skip_ws();
            if (pos_ >= s_.size()) return fail("unterminated object");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    std::string_view s_;
    size_t pos_ = 0;
    size_t err_pos_ = 0;
    std::string err_;
};

}  // namespace

JsonValue JsonValue::boolean(bool v) {
    JsonValue j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}

JsonValue JsonValue::number(double v) {
    JsonValue j;
    j.type_ = Type::Num;
    j.num_ = v;
    return j;
}

JsonValue JsonValue::string(std::string v) {
    JsonValue j;
    j.type_ = Type::Str;
    j.str_ = std::move(v);
    return j;
}

JsonValue JsonValue::array() {
    JsonValue j;
    j.type_ = Type::Arr;
    return j;
}

JsonValue JsonValue::object() {
    JsonValue j;
    j.type_ = Type::Obj;
    return j;
}

bool JsonValue::as_bool(bool def) const { return type_ == Type::Bool ? bool_ : def; }
double JsonValue::as_num(double def) const { return type_ == Type::Num ? num_ : def; }

void JsonValue::push(JsonValue v) {
    if (type_ != Type::Arr) {
        type_ = Type::Arr;
    }
    arr_.push_back(std::move(v));
}

void JsonValue::set(std::string key, JsonValue v) {
    if (type_ != Type::Obj) {
        type_ = Type::Obj;
    }
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}

const JsonValue* JsonValue::find(std::string_view key) const {
    if (type_ != Type::Obj) return nullptr;
    for (auto& kv : obj_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

std::string JsonValue::dump(bool pretty) const {
    std::string out;
    out.reserve(256);
    dump_to(out, pretty, 0);
    return out;
}

void JsonValue::dump_to(std::string& out, bool pretty, int indent) const {
    auto pad = [&](int n) {
        if (pretty) out.append(static_cast<size_t>(n) * 2, ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Num: append_number(out, num_); break;
        case Type::Str: append_quoted(out, str_); break;
        case Type::Arr: {
            if (arr_.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                if (pretty) {
                    out += '\n';
                    pad(indent + 1);
                }
                arr_[i].dump_to(out, pretty, indent + 1);
            }
            if (pretty) {
                out += '\n';
                pad(indent);
            }
            out += ']';
            break;
        }
        case Type::Obj: {
            if (obj_.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                if (pretty) {
                    out += '\n';
                    pad(indent + 1);
                }
                append_quoted(out, obj_[i].first);
                out += ':';
                if (pretty) out += ' ';
                obj_[i].second.dump_to(out, pretty, indent + 1);
            }
            if (pretty) {
                out += '\n';
                pad(indent);
            }
            out += '}';
            break;
        }
    }
}

std::optional<JsonValue> JsonValue::parse(std::string_view text, std::string* error) {
    Parser p(text);
    JsonValue out;
    if (!p.parse_document(out)) {
        if (error) *error = p.error();
        return std::nullopt;
    }
    return out;
}

JsonValue make_error_json(std::string code, std::string message) {
    JsonValue j = JsonValue::object();
    j.set("error", JsonValue::string(std::move(message)));
    j.set("code", JsonValue::string(std::move(code)));
    return j;
}

}  // namespace ms
