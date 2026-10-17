// Minimal JSON value, parser and writer. Portable C++17, no dependencies.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b) : type_(Bool), num_(b) {}
    Json(double n) : type_(Number), num_(n) {}
    Json(int n) : type_(Number), num_(n) {}
    Json(const char* s) : type_(String), str_(s) {}
    Json(std::string s) : type_(String), str_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Array; return j; }
    static Json object() { Json j; j.type_ = Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isString() const { return type_ == String; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }

    const std::string& str() const { static const std::string empty; return type_ == String ? str_ : empty; }
    double num(double def = 0) const { return type_ == Number || type_ == Bool ? num_ : def; }
    bool boolean(bool def = false) const { return type_ == Bool ? num_ != 0 : def; }

    const std::vector<Json>& items() const { static const std::vector<Json> empty; return type_ == Array ? arr_ : empty; }
    std::vector<Json>& items() { return arr_; }
    const std::map<std::string, Json>& fields() const { static const std::map<std::string, Json> empty; return type_ == Object ? obj_ : empty; }
    size_t size() const { return type_ == Array ? arr_.size() : type_ == Object ? obj_.size() : 0; }

    // Safe lookups: missing keys / out-of-range indices yield a shared null.
    const Json& operator[](const std::string& key) const {
        if (type_ != Object) return nil();
        auto it = obj_.find(key);
        return it == obj_.end() ? nil() : it->second;
    }
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    const Json& operator[](size_t i) const { return type_ == Array && i < arr_.size() ? arr_[i] : nil(); }
    const Json& operator[](int i) const { return i < 0 ? nil() : (*this)[size_t(i)]; }

    // Mutation
    Json& set(const std::string& key, Json v) { type_ = Object; obj_[key] = std::move(v); return *this; }
    Json& push(Json v) { type_ = Array; arr_.push_back(std::move(v)); return *this; }

    static Json parse(const std::string& text, bool* ok = nullptr) {
        Parser p{text.data(), text.data() + text.size()};
        Json v;
        bool good = p.value(v, 0);
        p.ws();
        if (ok) *ok = good && p.s == p.e;
        return good ? v : Json();
    }

    std::string dump() const { std::string out; write(out); return out; }

private:
    Type type_ = Null;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;

    static const Json& nil() { static const Json n; return n; }

    struct Parser {
        const char* s;
        const char* e;
        void ws() { while (s < e && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) ++s; }
        bool lit(const char* w) {
            const char* p = s;
            for (; *w; ++w, ++p) if (p >= e || *p != *w) return false;
            s = p;
            return true;
        }
        static void utf8(std::string& out, uint32_t cp) {
            if (cp < 0x80) out += char(cp);
            else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        }
        bool hex4(uint32_t& v) {
            if (e - s < 4) return false;
            v = 0;
            for (int i = 0; i < 4; ++i, ++s) {
                char c = *s;
                v <<= 4;
                if (c >= '0' && c <= '9') v |= c - '0';
                else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                else return false;
            }
            return true;
        }
        bool string(std::string& out) {
            ++s;  // opening quote
            while (s < e) {
                char c = *s++;
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (s >= e) return false;
                char esc = *s++;
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
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && e - s >= 6 && s[0] == '\\' && s[1] == 'u') {
                        s += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { utf8(out, 0xFFFD); cp = lo; }
                    }
                    utf8(out, cp);
                    break;
                }
                default: return false;
                }
            }
            return false;
        }
        bool value(Json& v, int depth) {
            if (depth > 512) return false;
            ws();
            if (s >= e) return false;
            switch (*s) {
            case '{': {
                ++s;
                v.type_ = Object;
                ws();
                if (s < e && *s == '}') { ++s; return true; }
                for (;;) {
                    ws();
                    if (s >= e || *s != '"') return false;
                    std::string key;
                    if (!string(key)) return false;
                    ws();
                    if (s >= e || *s++ != ':') return false;
                    Json child;
                    if (!value(child, depth + 1)) return false;
                    v.obj_[std::move(key)] = std::move(child);
                    ws();
                    if (s < e && *s == ',') { ++s; continue; }
                    if (s < e && *s == '}') { ++s; return true; }
                    return false;
                }
            }
            case '[': {
                ++s;
                v.type_ = Array;
                ws();
                if (s < e && *s == ']') { ++s; return true; }
                for (;;) {
                    Json child;
                    if (!value(child, depth + 1)) return false;
                    v.arr_.push_back(std::move(child));
                    ws();
                    if (s < e && *s == ',') { ++s; continue; }
                    if (s < e && *s == ']') { ++s; return true; }
                    return false;
                }
            }
            case '"': v.type_ = String; return string(v.str_);
            case 't': v = Json(true); return lit("true");
            case 'f': v = Json(false); return lit("false");
            case 'n': v = Json(); return lit("null");
            default: {
                char* end = nullptr;
                std::string tmp(s, size_t(std::min<ptrdiff_t>(e - s, 64)));
                double d = std::strtod(tmp.c_str(), &end);
                if (end == tmp.c_str()) return false;
                s += end - tmp.c_str();
                v = Json(d);
                return true;
            }
            }
        }
    };

    static void writeString(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                else out += char(c);
            }
        }
        out += '"';
    }

    void write(std::string& out) const {
        switch (type_) {
        case Null: out += "null"; break;
        case Bool: out += num_ ? "true" : "false"; break;
        case Number: {
            char buf[32];
            if (num_ == double(int64_t(num_))) snprintf(buf, sizeof buf, "%lld", (long long)num_);
            else snprintf(buf, sizeof buf, "%.17g", num_);
            out += buf;
            break;
        }
        case String: writeString(out, str_); break;
        case Array:
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) { if (i) out += ','; arr_[i].write(out); }
            out += ']';
            break;
        case Object: {
            out += '{';
            bool first = true;
            for (auto& [k, v] : obj_) {
                if (!first) out += ',';
                first = false;
                writeString(out, k);
                out += ':';
                v.write(out);
            }
            out += '}';
            break;
        }
        }
    }
};
