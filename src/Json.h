// Json.h - Minimal recursive-descent JSON parser (no external deps)
#pragma once
#include "pch.h"

struct JsonVal {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;

    bool                                        b   = false;
    double                                      num = 0.0;
    std::string                                 str;
    std::vector<JsonVal>                        arr;
    std::vector<std::pair<std::string,JsonVal>> obj;

    bool IsNull()   const { return type == Type::Null;   }
    bool IsString() const { return type == Type::String; }
    bool IsArray()  const { return type == Type::Array;  }
    bool IsObject() const { return type == Type::Object; }
    bool IsNumber() const { return type == Type::Number; }

    // Key lookup (object)
    const JsonVal& operator[](const char* key) const {
        static JsonVal null;
        for (const auto& kv : obj)
            if (kv.first == key) return kv.second;
        return null;
    }

    // Index lookup (array) - explicit size_t to avoid ambiguity with int literals
    const JsonVal& operator[](size_t idx) const {
        static JsonVal null;
        return idx < arr.size() ? arr[idx] : null;
    }

    // Convenience: int index (explicitly cast to size_t)
    const JsonVal& At(int idx) const {
        return (*this)[static_cast<size_t>(idx)];
    }

    size_t Size() const { return arr.size(); }

    std::wstring WStr() const {
        if (!IsString()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (n <= 0) return {};
        std::wstring w(n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, w.data(), n);
        return w;
    }
    int Int() const { return static_cast<int>(num); }
};

class JsonParser {
    const char* p_;
    const char* end_;

    void SkipWS() {
        while (p_ < end_ && (*p_==' '||*p_=='\t'||*p_=='\n'||*p_=='\r')) ++p_;
    }

    std::string ParseString() {
        ++p_; // skip opening "
        std::string s;
        while (p_ < end_ && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                if (p_ >= end_) break;
                switch (*p_) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        if (p_ + 4 < end_) {
                            char hex[5] = { p_[1],p_[2],p_[3],p_[4], 0 };
                            uint32_t cp = strtoul(hex, nullptr, 16);
                            p_ += 4;
                            if      (cp < 0x80)  { s += (char)cp; }
                            else if (cp < 0x800) { s += (char)(0xC0|(cp>>6)); s += (char)(0x80|(cp&0x3F)); }
                            else                 { s += (char)(0xE0|(cp>>12)); s += (char)(0x80|((cp>>6)&0x3F)); s += (char)(0x80|(cp&0x3F)); }
                        }
                        break;
                    }
                    default: s += *p_; break;
                }
            } else {
                s += *p_;
            }
            ++p_;
        }
        if (p_ < end_) ++p_; // skip closing "
        return s;
    }

    JsonVal ParseValue() {
        SkipWS();
        if (p_ >= end_) return {};

        JsonVal v;
        if (*p_ == '"') {
            v.type = JsonVal::Type::String;
            v.str  = ParseString();
        }
        else if (*p_ == '{') {
            v.type = JsonVal::Type::Object;
            ++p_;
            while (p_ < end_) {
                SkipWS();
                if (*p_ == '}') { ++p_; break; }
                if (*p_ == ',') { ++p_; continue; }
                SkipWS();
                if (*p_ != '"') break;
                std::string key = ParseString();
                SkipWS();
                if (p_ < end_ && *p_ == ':') ++p_;
                v.obj.push_back({ std::move(key), ParseValue() });
                SkipWS();
            }
        }
        else if (*p_ == '[') {
            v.type = JsonVal::Type::Array;
            ++p_;
            while (p_ < end_) {
                SkipWS();
                if (*p_ == ']') { ++p_; break; }
                if (*p_ == ',') { ++p_; continue; }
                v.arr.push_back(ParseValue());
                SkipWS();
            }
        }
        else if (*p_ == 't') { v.type=JsonVal::Type::Bool; v.b=true;  p_+=4; }
        else if (*p_ == 'f') { v.type=JsonVal::Type::Bool; v.b=false; p_+=5; }
        else if (*p_ == 'n') { v.type=JsonVal::Type::Null;             p_+=4; }
        else {
            v.type = JsonVal::Type::Number;
            char* ep = nullptr;
            v.num = strtod(p_, &ep);
            if (ep) p_ = ep;
        }
        return v;
    }

public:
    static JsonVal Parse(const std::string& json) {
        JsonParser parser;
        parser.p_   = json.c_str();
        parser.end_ = json.c_str() + json.size();
        return parser.ParseValue();
    }
};
