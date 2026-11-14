#ifndef JSON_H
#define JSON_H

//
// Minimal JSON support — enough for the voting system's request/response
// payloads. Not a full RFC 8259 implementation; handles flat objects and
// arrays of strings + flat objects + primitives.
//

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>

namespace json {

class Value;
using Null   = std::monostate;
using Bool   = bool;
using Number = double;
using String = std::string;
using Array  = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(const std::string& s) : type_(Type::String), str_(s) {}
    Value(const Array& a) : type_(Type::Array), arr_(a) {}
    Value(const Object& o) : type_(Type::Object), obj_(o) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool   asBool() const   { return bool_; }
    double asNumber() const { return num_; }
    int    asInt() const    { return (int)num_; }
    const std::string& asString() const { return str_; }
    const Array&  asArray() const  { return arr_; }
    const Object& asObject() const { return obj_; }

    // Object helpers
    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_.find(key) != obj_.end();
    }
    const Value& at(const std::string& key) const {
        static Value nullVal;
        if (type_ != Type::Object) return nullVal;
        auto it = obj_.find(key);
        return it == obj_.end() ? nullVal : it->second;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const Value& v = at(key);
        return v.isString() ? v.asString() : def;
    }
    int getInt(const std::string& key, int def = 0) const {
        const Value& v = at(key);
        return v.isNumber() ? v.asInt() : def;
    }

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool   bool_ = false;
    double num_  = 0;
    String str_;
    Array  arr_;
    Object obj_;
};

// Parse a JSON string. Throws std::runtime_error on syntax error.
Value parse(const std::string& text);

} // namespace json

#endif // JSON_H
