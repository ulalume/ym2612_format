/**
 * Minimal JSON parser/serializer for ym2612_format.
 * Replaces nlohmann/json to avoid pulling iostream/locale into WASM builds.
 * Supports the subset needed by gin.cpp and ginpkg.cpp:
 *   - Parse from string/bytes
 *   - Navigate: at(key), operator[], contains(), size(), is_*()
 *   - Get typed values: get_string(), get_int(), get_uint8(), get_bool(),
 *     get_int32_vec()
 *   - Build: object(), array(), push_back(), operator[]= assignment
 *   - Serialize: dump(indent)
 *
 * Internal header — not part of the public API.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ym2612_format::json_minimal {

enum class Type { Null, Bool, Int, Float, String, Array, Object };

class Value {
public:
  // Constructors
  Value() : type_(Type::Null) {}
  Value(std::nullptr_t) : type_(Type::Null) {}
  Value(bool v) : type_(Type::Bool), bool_(v) {}
  Value(int v) : type_(Type::Int), int_(v) {}
  Value(int64_t v) : type_(Type::Int), int_(v) {}
  Value(uint8_t v) : type_(Type::Int), int_(v) {}
  Value(double v) : type_(Type::Float), float_(v) {}
  Value(const char *v) : type_(Type::String), str_(v) {}
  Value(const std::string &v) : type_(Type::String), str_(v) {}
  Value(std::string &&v) : type_(Type::String), str_(std::move(v)) {}

  // Named constructors
  static Value object() {
    Value v;
    v.type_ = Type::Object;
    return v;
  }
  static Value array() {
    Value v;
    v.type_ = Type::Array;
    return v;
  }

  // Type checks
  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Int || type_ == Type::Float; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  // Value access
  bool get_bool() const {
    if (type_ != Type::Bool) throw std::runtime_error("not a bool");
    return bool_;
  }
  int64_t get_int() const {
    if (type_ == Type::Int) return int_;
    if (type_ == Type::Float) return static_cast<int64_t>(float_);
    throw std::runtime_error("not a number");
  }
  uint8_t get_uint8() const { return static_cast<uint8_t>(get_int()); }
  double get_float() const {
    if (type_ == Type::Float) return float_;
    if (type_ == Type::Int) return static_cast<double>(int_);
    throw std::runtime_error("not a number");
  }
  const std::string &get_string() const {
    if (type_ != Type::String) throw std::runtime_error("not a string");
    return str_;
  }

  // Convenience: get vector of int32
  std::vector<int32_t> get_int32_vec() const {
    if (type_ != Type::Array) throw std::runtime_error("not an array");
    std::vector<int32_t> result;
    result.reserve(arr_.size());
    for (const auto &v : arr_)
      result.push_back(static_cast<int32_t>(v.get_int()));
    return result;
  }

  // Object access
  const Value &at(const std::string &key) const {
    if (type_ != Type::Object) throw std::runtime_error("not an object");
    auto it = obj_.find(key);
    if (it == obj_.end())
      throw std::runtime_error("key not found: " + key);
    return it->second;
  }
  bool contains(const std::string &key) const {
    return type_ == Type::Object && obj_.find(key) != obj_.end();
  }
  Value &operator[](const std::string &key) {
    if (type_ != Type::Object) throw std::runtime_error("not an object");
    return obj_[key];
  }

  // Array access
  const Value &operator[](size_t idx) const {
    if (type_ != Type::Array) throw std::runtime_error("not an array");
    return arr_.at(idx);
  }
  size_t size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
  }
  bool empty() const { return size() == 0; }
  void push_back(Value v) {
    if (type_ != Type::Array) throw std::runtime_error("not an array");
    arr_.push_back(std::move(v));
  }

  // Iterators for arrays
  using array_iter = std::vector<Value>::const_iterator;
  array_iter begin() const { return arr_.begin(); }
  array_iter end() const { return arr_.end(); }

  // Object iteration
  const std::map<std::string, Value> &items() const { return obj_; }

  // ---- Serialization ----
  std::string dump(int indent = -1) const {
    std::string out;
    dump_impl(out, indent, 0);
    if (indent >= 0) out += '\n';
    return out;
  }

  // ---- Parsing ----
  static Value parse(const char *data, size_t size) {
    Parser p(data, size);
    Value v = p.parse_value();
    p.skip_ws();
    if (p.pos_ < p.size_)
      throw std::runtime_error("trailing data after JSON");
    return v;
  }
  static Value parse(const uint8_t *data, size_t size) {
    return parse(reinterpret_cast<const char *>(data), size);
  }
  static Value parse(const std::string &s) {
    return parse(s.data(), s.size());
  }

private:
  Type type_ = Type::Null;
  bool bool_ = false;
  int64_t int_ = 0;
  double float_ = 0.0;
  std::string str_;
  std::vector<Value> arr_;
  std::map<std::string, Value> obj_;

  // ---- Serialization helpers ----

  static void write_indent(std::string &out, int indent, int depth) {
    if (indent < 0) return;
    out += '\n';
    for (int i = 0; i < indent * depth; ++i) out += ' ';
  }

  static void write_string(std::string &out, const std::string &s) {
    out += '"';
    for (char c : s) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x",
                          static_cast<unsigned char>(c));
            out += buf;
          } else {
            out += c;
          }
      }
    }
    out += '"';
  }

  void dump_impl(std::string &out, int indent, int depth) const {
    switch (type_) {
      case Type::Null: out += "null"; break;
      case Type::Bool: out += bool_ ? "true" : "false"; break;
      case Type::Int: {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(int_));
        out += buf;
        break;
      }
      case Type::Float: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", float_);
        out += buf;
        break;
      }
      case Type::String:
        write_string(out, str_);
        break;
      case Type::Array: {
        out += '[';
        for (size_t i = 0; i < arr_.size(); ++i) {
          if (i > 0) out += ',';
          write_indent(out, indent, depth + 1);
          arr_[i].dump_impl(out, indent, depth + 1);
        }
        if (!arr_.empty()) write_indent(out, indent, depth);
        out += ']';
        break;
      }
      case Type::Object: {
        out += '{';
        bool first = true;
        for (const auto &[k, v] : obj_) {
          if (!first) out += ',';
          first = false;
          write_indent(out, indent, depth + 1);
          write_string(out, k);
          out += ':';
          if (indent >= 0) out += ' ';
          v.dump_impl(out, indent, depth + 1);
        }
        if (!obj_.empty()) write_indent(out, indent, depth);
        out += '}';
        break;
      }
    }
  }

  // ---- Parser ----

  struct Parser {
    const char *data_;
    size_t size_;
    size_t pos_ = 0;

    Parser(const char *data, size_t size) : data_(data), size_(size) {}

    char peek() const {
      if (pos_ >= size_) throw std::runtime_error("unexpected end of JSON");
      return data_[pos_];
    }
    char next() {
      char c = peek();
      ++pos_;
      return c;
    }
    void skip_ws() {
      while (pos_ < size_ && (data_[pos_] == ' ' || data_[pos_] == '\t' ||
                               data_[pos_] == '\n' || data_[pos_] == '\r'))
        ++pos_;
    }
    void expect(char c) {
      skip_ws();
      char got = next();
      if (got != c) {
        std::string msg = "expected '";
        msg += c;
        msg += "', got '";
        msg += got;
        msg += "'";
        throw std::runtime_error(msg);
      }
    }

    Value parse_value() {
      skip_ws();
      char c = peek();
      switch (c) {
        case '"': return parse_string_value();
        case '{': return parse_object();
        case '[': return parse_array();
        case 't': case 'f': return parse_bool();
        case 'n': return parse_null();
        default:
          if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
          throw std::runtime_error(
              std::string("unexpected character: '") + c + "'");
      }
    }

    Value parse_string_value() {
      return Value(parse_string());
    }

    std::string parse_string() {
      expect('"');
      std::string s;
      while (true) {
        if (pos_ >= size_) throw std::runtime_error("unterminated string");
        char c = data_[pos_++];
        if (c == '"') return s;
        if (c == '\\') {
          if (pos_ >= size_) throw std::runtime_error("unterminated escape");
          char e = data_[pos_++];
          switch (e) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'u': {
              // Parse 4 hex digits, store as UTF-8
              if (pos_ + 4 > size_)
                throw std::runtime_error("incomplete \\u escape");
              char hex[5] = {data_[pos_], data_[pos_+1],
                             data_[pos_+2], data_[pos_+3], 0};
              pos_ += 4;
              unsigned cp = static_cast<unsigned>(std::strtoul(hex, nullptr, 16));
              if (cp < 0x80) {
                s += static_cast<char>(cp);
              } else if (cp < 0x800) {
                s += static_cast<char>(0xC0 | (cp >> 6));
                s += static_cast<char>(0x80 | (cp & 0x3F));
              } else {
                s += static_cast<char>(0xE0 | (cp >> 12));
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (cp & 0x3F));
              }
              break;
            }
            default:
              throw std::runtime_error(
                  std::string("invalid escape: \\") + e);
          }
        } else {
          s += c;
        }
      }
    }

    Value parse_number() {
      size_t start = pos_;
      bool is_float = false;
      if (data_[pos_] == '-') ++pos_;
      while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
      if (pos_ < size_ && data_[pos_] == '.') {
        is_float = true;
        ++pos_;
        while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
      }
      if (pos_ < size_ && (data_[pos_] == 'e' || data_[pos_] == 'E')) {
        is_float = true;
        ++pos_;
        if (pos_ < size_ && (data_[pos_] == '+' || data_[pos_] == '-')) ++pos_;
        while (pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '9') ++pos_;
      }
      std::string num_str(data_ + start, pos_ - start);
      if (is_float)
        return Value(std::strtod(num_str.c_str(), nullptr));
      return Value(static_cast<int64_t>(std::strtoll(num_str.c_str(), nullptr, 10)));
    }

    Value parse_bool() {
      if (pos_ + 4 <= size_ && std::strncmp(data_ + pos_, "true", 4) == 0) {
        pos_ += 4;
        return Value(true);
      }
      if (pos_ + 5 <= size_ && std::strncmp(data_ + pos_, "false", 5) == 0) {
        pos_ += 5;
        return Value(false);
      }
      throw std::runtime_error("invalid boolean");
    }

    Value parse_null() {
      if (pos_ + 4 <= size_ && std::strncmp(data_ + pos_, "null", 4) == 0) {
        pos_ += 4;
        return Value(nullptr);
      }
      throw std::runtime_error("invalid null");
    }

    Value parse_object() {
      expect('{');
      Value obj = Value::object();
      skip_ws();
      if (peek() == '}') { ++pos_; return obj; }
      while (true) {
        skip_ws();
        std::string key = parse_string();
        expect(':');
        obj[key] = parse_value();
        skip_ws();
        char c = next();
        if (c == '}') return obj;
        if (c != ',')
          throw std::runtime_error("expected ',' or '}' in object");
      }
    }

    Value parse_array() {
      expect('[');
      Value arr = Value::array();
      skip_ws();
      if (peek() == ']') { ++pos_; return arr; }
      while (true) {
        arr.push_back(parse_value());
        skip_ws();
        char c = next();
        if (c == ']') return arr;
        if (c != ',')
          throw std::runtime_error("expected ',' or ']' in array");
      }
    }
  };
};

} // namespace ym2612_format::json_minimal
