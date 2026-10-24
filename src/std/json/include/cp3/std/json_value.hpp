// =============================================================================
// cp3/std/json_value.hpp - the `std/json` dynamic value model.
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the
// generated-code ABI (D14): all functions are `inline`, header-only. Included
// by cp3/std/json.hpp, which owns the codegen contract comment.
//
// One node per JSON value: the untyped parse result and every temporary a
// composite `stringify` or a record builder walks. `value`/`arrayValue`/
// `objectValue` are the constructors codegen spells for composite literals.
// =============================================================================
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime::json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    // Completed after Value. std::vector supports an incomplete element type;
    // std::pair<std::string, Value> does not, and Clang rejects instantiating
    // it while Value is still incomplete.
    struct Field;

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;                              // Array
    std::vector<Field> fields;                             // Object
};

struct Value::Field {
    std::string key;
    Value value;

    Field(std::string fieldKey, Value fieldValue)
        : key(std::move(fieldKey)), value(std::move(fieldValue)) {}
};

inline Value nullValue() { return Value{}; }

inline Value value(double v) {
    Value out;
    out.kind = Value::Kind::Number;
    out.number = v;
    return out;
}

inline Value value(bool v) {
    Value out;
    out.kind = Value::Kind::Bool;
    out.boolean = v;
    return out;
}

inline Value value(const std::string& v) {
    Value out;
    out.kind = Value::Kind::String;
    out.text = v;
    return out;
}

inline Value value(std::nullptr_t) { return nullValue(); }

inline Value arrayValue(std::vector<Value> items) {
    Value out;
    out.kind = Value::Kind::Array;
    out.items = std::move(items);
    return out;
}

using Fields = std::vector<Value::Field>;

inline Value objectValue(Fields fields) {
    Value out;
    out.kind = Value::Kind::Object;
    out.fields = std::move(fields);
    return out;
}

// A non-literal composite element (`{ tags: a }` with `a: string[]`): convert
// an array value element-wise; a scalar keeps its dedicated overload above.
template <class E>
inline Value value(const Array<E>& elements) {
    std::vector<Value> items;
    if (elements) {
        items.reserve(elements->size());
        for (const E& element : *elements) {
            items.push_back(value(element));
        }
    }
    return arrayValue(std::move(items));
}

// Invalid JSON text and shape mismatches both raise this ordinary
// `std::exception` subclass (STDLIB docs §04: an ordinary catchable error).
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message)
        : std::runtime_error(message) {}
};

}  // namespace cp3::runtime::json
