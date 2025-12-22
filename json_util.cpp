// json_util.cpp
// Minimal JSON helpers implementation.

#include "json_util.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace json_util {
// Implementation of JsonValue

JsonValue::JsonValue() : type_(Null), boolVal_(false), numVal_(0.0) {}
JsonValue::JsonValue(std::nullptr_t) : type_(Null), boolVal_(false), numVal_(0.0) {}
JsonValue::JsonValue(bool b) : type_(Bool), boolVal_(b), numVal_(0.0) {}
JsonValue::JsonValue(double n) : type_(Number), boolVal_(false), numVal_(n) {}
JsonValue::JsonValue(const std::string& s) : type_(String), boolVal_(false), numVal_(0.0), strVal_(s) {}

JsonValue JsonValue::object() {
    JsonValue v;
    v.type_ = Object;
    v.objVal_ = std::make_shared<std::map<std::string, JsonValue>>();
    return v;
}

JsonValue JsonValue::array() {
    JsonValue v;
    v.type_ = Array;
    v.arrVal_ = std::make_shared<std::vector<JsonValue>>();
    return v;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type_ != Object) {
        type_ = Object;
        objVal_ = std::make_shared<std::map<std::string, JsonValue>>();
    }
    return (*objVal_)[key];
}

void JsonValue::set(const std::string& key, const JsonValue& val) {
    if (type_ != Object) {
        type_ = Object;
        objVal_ = std::make_shared<std::map<std::string, JsonValue>>();
    }
    (*objVal_)[key] = val;
}

void JsonValue::push_back(const JsonValue& v) {
    if (type_ != Array) {
        type_ = Array;
        arrVal_ = std::make_shared<std::vector<JsonValue>>();
    }
    arrVal_->push_back(v);
}

std::string JsonValue::escapeString(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

void JsonValue::appendToString(std::string& out) const {
    switch (type_) {
        case Null:
            out += "null";
            break;
        case Bool:
            out += (boolVal_ ? "true" : "false");
            break;
        case Number: {
            std::ostringstream oss;
            // Use maximum precision necessary
            oss << std::setprecision(12) << numVal_;
            out += oss.str();
            break;
        }
        case String:
            out += '"';
            out += escapeString(strVal_);
            out += '"';
            break;
        case Object: {
            out += '{';
            bool first = true;
            for (const auto& kv : *objVal_) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += escapeString(kv.first);
                out += "\":";
                kv.second.appendToString(out);
            }
            out += '}';
            break;
        }
        case Array: {
            out += '[';
            bool first = true;
            for (const auto& item : *arrVal_) {
                if (!first) out += ',';
                first = false;
                item.appendToString(out);
            }
            out += ']';
            break;
        }
    }
}

std::string JsonValue::toString() const {
    std::string out;
    appendToString(out);
    return out;
}

// Backwards-compatible helper
std::string escapeString(const std::string& s) {
    return JsonValue::escapeString(s);
}

} // namespace json_util
