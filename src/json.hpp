#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>

namespace astrae::json {
class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Data = std::variant<std::nullptr_t, bool, int64_t, uint64_t, double, std::string, Array, Object>;
    Value() : v_(nullptr) {}
    Value(std::nullptr_t) : v_(nullptr) {}
    Value(bool v) : v_(v) {}
    Value(int v) : v_(static_cast<int64_t>(v)) {}
    Value(unsigned v) : v_(static_cast<uint64_t>(v)) {}
    Value(int64_t v) : v_(v) {}
    Value(uint64_t v) : v_(v) {}
    Value(double v) : v_(v) {}
    Value(const char *v) : v_(std::string(v ? v : "")) {}
    Value(std::string v) : v_(std::move(v)) {}
    Value(Array v) : v_(std::move(v)) {}
    Value(Object v) : v_(std::move(v)) {}
    static Value object() { return Object{}; }
    static Value array() { return Array{}; }
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v_); }
    bool is_bool() const { return std::holds_alternative<bool>(v_); }
    bool is_number() const { return std::holds_alternative<int64_t>(v_) || std::holds_alternative<uint64_t>(v_) || std::holds_alternative<double>(v_); }
    bool is_string() const { return std::holds_alternative<std::string>(v_); }
    bool is_array() const { return std::holds_alternative<Array>(v_); }
    bool is_object() const { return std::holds_alternative<Object>(v_); }
    bool as_bool(bool d=false) const;
    int64_t as_i64(int64_t d=0) const;
    uint64_t as_u64(uint64_t d=0) const;
    double as_double(double d=0) const;
    std::string as_string(const std::string &d={}) const;
    std::string scalar_string() const;
    Array &as_array();
    const Array &as_array() const;
    Object &as_object();
    const Object &as_object() const;
    Value &operator[](const std::string &key);
    const Value &operator[](const std::string &key) const;
    Value &operator[](size_t index);
    const Value &operator[](size_t index) const;
    bool contains(const std::string &key) const;
    size_t size() const;
    void push_back(Value x);
    void erase(const std::string &key);
    const Data &data() const { return v_; }
    Data &data() { return v_; }
private:
    Data v_;
};
Value parse(const std::string &text);
std::string dump(const Value &value, bool pretty=false, int indent=2);
}
