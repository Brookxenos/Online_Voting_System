#include "json.h"

#include <sstream>
#include <stdexcept>
#include <cctype>

namespace json {

// ---------------------------------------------------------------------------
// Serializer
// ---------------------------------------------------------------------------

static void dumpString(std::ostream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    os << buf;
                } else {
                    os << c;
                }
        }
    }
    os << '"';
}

static void dumpValue(std::ostream& os, const Value& v) {
    switch (v.type()) {
        case Value::Type::Null:   os << "null"; break;
        case Value::Type::Bool:   os << (v.asBool() ? "true" : "false"); break;
        case Value::Type::Number: {
            double n = v.asNumber();
            if (n == (double)(long long)n) os << (long long)n;
            else os << n;
            break;
        }
        case Value::Type::String: dumpString(os, v.asString()); break;
        case Value::Type::Array: {
            os << '[';
            bool first = true;
            for (const auto& e : v.asArray()) {
                if (!first) os << ',';
                first = false;
                dumpValue(os, e);
            }
            os << ']';
            break;
        }
        case Value::Type::Object: {
            os << '{';
            bool first = true;
            for (const auto& kv : v.asObject()) {
                if (!first) os << ',';
                first = false;
                dumpString(os, kv.first);
                os << ':';
                dumpValue(os, kv.second);
            }
            os << '}';
            break;
        }
    }
}

std::string Value::dump() const {
    std::ostringstream os;
    dumpValue(os, *this);
    return os.str();
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

struct Parser {
    const std::string& src;
    size_t pos = 0;

    explicit Parser(const std::string& s) : src(s) {}

    void skipWs() {
        while (pos < src.size() && std::isspace((unsigned char)src[pos])) ++pos;
    }

    char peek() { skipWs(); return pos < src.size() ? src[pos] : '\0'; }
    char next() { skipWs(); return pos < src.size() ? src[pos++] : '\0'; }

    Value parseValue() {
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit((unsigned char)c)) return parseNumber();
        throw std::runtime_error(std::string("Unexpected char '") + c + "' at pos " +
                                 std::to_string(pos));
    }

    Value parseObject() {
        if (next() != '{') throw std::runtime_error("expected '{'");
        Object o;
        skipWs();
        if (peek() == '}') { next(); return Value(o); }
        while (true) {
            std::string key = parseStringRaw();
            skipWs();
            if (next() != ':') throw std::runtime_error("expected ':'");
            o[key] = parseValue();
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == '}') break;
            throw std::runtime_error("expected ',' or '}'");
        }
        return Value(o);
    }

    Value parseArray() {
        if (next() != '[') throw std::runtime_error("expected '['");
        Array a;
        skipWs();
        if (peek() == ']') { next(); return Value(a); }
        while (true) {
            a.push_back(parseValue());
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == ']') break;
            throw std::runtime_error("expected ',' or ']'");
        }
        return Value(a);
    }

    std::string parseStringRaw() {
        if (next() != '"') throw std::runtime_error("expected string");
        std::string out;
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos >= src.size()) throw std::runtime_error("bad escape");
                char e = src[pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (pos + 4 > src.size()) throw std::runtime_error("bad \\u");
                        // Decode the 4 hex digits into a UTF-8 byte sequence.
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else throw std::runtime_error("bad hex in \\u");
                        }
                        if (code < 0x80) out.push_back((char)code);
                        else if (code < 0x800) {
                            out.push_back((char)(0xC0 | (code >> 6)));
                            out.push_back((char)(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back((char)(0xE0 | (code >> 12)));
                            out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back((char)(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: throw std::runtime_error(std::string("bad escape '\\") + e + "'");
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("unterminated string");
    }

    Value parseString() { return Value(parseStringRaw()); }

    Value parseBool() {
        if (src.compare(pos, 4, "true") == 0) { pos += 4; return Value(true); }
        if (src.compare(pos, 5, "false") == 0) { pos += 5; return Value(false); }
        throw std::runtime_error("bad literal");
    }

    Value parseNull() {
        if (src.compare(pos, 4, "null") == 0) { pos += 4; return Value(); }
        throw std::runtime_error("bad literal");
    }

    Value parseNumber() {
        size_t start = pos;
        if (src[pos] == '-') ++pos;
        while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            while (pos < src.size() && std::isdigit((unsigned char)src[pos])) ++pos;
        }
        std::string num = src.substr(start, pos - start);
        try { return Value(std::stod(num)); }
        catch (...) { throw std::runtime_error("bad number"); }
    }
};

} // anonymous namespace

Value parse(const std::string& text) {
    Parser p(text);
    Value v = p.parseValue();
    p.skipWs();
    if (p.pos != p.src.size())
        throw std::runtime_error("trailing characters in JSON");
    return v;
}

} // namespace json
