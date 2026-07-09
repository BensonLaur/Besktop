#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace besktop {

enum class JsonType {
    Null,
    Object,
    Array,
    String,
    Number,
    Boolean,
};

struct JsonValue {
    JsonType type = JsonType::Null;
    std::map<std::string, JsonValue> objectValue;
    std::vector<JsonValue> arrayValue;
    std::string stringValue;
    double numberValue = 0.0;
    bool boolValue = false;

    bool IsObject() const noexcept { return type == JsonType::Object; }
    bool IsArray() const noexcept { return type == JsonType::Array; }
    bool IsString() const noexcept { return type == JsonType::String; }
    bool IsNumber() const noexcept { return type == JsonType::Number; }
    bool IsBoolean() const noexcept { return type == JsonType::Boolean; }
    bool IsNull() const noexcept { return type == JsonType::Null; }

    const JsonValue* Find(std::string_view key) const noexcept;
};

class JsonParseError final : public std::runtime_error {
public:
    explicit JsonParseError(const std::string& message);
};

JsonValue ParseJson(std::string_view text);

} // namespace besktop
