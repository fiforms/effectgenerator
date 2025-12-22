// json_util.h
// Minimal JSON helpers used by the CLI to emit simple JSON output.

#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace json_util {

// Minimal JSON value class supporting objects, arrays, strings, numbers, booleans, and null.
class JsonValue {
public:
	enum Type { Null, Bool, Number, String, Object, Array };

	// Constructors
	JsonValue();
	JsonValue(std::nullptr_t);
	JsonValue(bool b);
	JsonValue(double n);
	JsonValue(const std::string& s);

	// Factory helpers
	static JsonValue object();
	static JsonValue array();

	// Object access
	JsonValue& operator[](const std::string& key);
	void set(const std::string& key, const JsonValue& val);

	// Array access
	void push_back(const JsonValue& v);

	// Convert to compact JSON string
	std::string toString() const;

	// Escape helper (public so wrapper can call it)
	static std::string escapeString(const std::string& s);

private:
	Type type_;
	bool boolVal_;
	double numVal_;
	std::string strVal_;
	std::shared_ptr<std::map<std::string, JsonValue>> objVal_;
	std::shared_ptr<std::vector<JsonValue>> arrVal_;

	void appendToString(std::string& out) const;
};

// Backwards-compatible helper (kept for simple cases)
std::string escapeString(const std::string& s);

} // namespace json_util

#endif // JSON_UTIL_H
