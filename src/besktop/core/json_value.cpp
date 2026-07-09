#include "besktop/core/json_value.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <sstream>

namespace besktop {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text)
        : text_(text)
    {
    }

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (!AtEnd()) {
            Fail("unexpected trailing content");
        }
        return value;
    }

private:
    JsonValue ParseValue()
    {
        SkipWhitespace();
        if (AtEnd()) {
            Fail("unexpected end of input");
        }

        const char ch = Peek();
        if (ch == '{') {
            return ParseObject();
        }
        if (ch == '[') {
            return ParseArray();
        }
        if (ch == '"') {
            JsonValue value;
            value.type = JsonType::String;
            value.stringValue = ParseString();
            return value;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return ParseNumber();
        }
        if (ConsumeLiteral("true")) {
            JsonValue value;
            value.type = JsonType::Boolean;
            value.boolValue = true;
            return value;
        }
        if (ConsumeLiteral("false")) {
            JsonValue value;
            value.type = JsonType::Boolean;
            value.boolValue = false;
            return value;
        }
        if (ConsumeLiteral("null")) {
            return JsonValue{};
        }

        Fail("unexpected token");
    }

    JsonValue ParseObject()
    {
        Expect('{');

        JsonValue value;
        value.type = JsonType::Object;

        SkipWhitespace();
        if (TryConsume('}')) {
            return value;
        }

        while (true) {
            SkipWhitespace();
            if (Peek() != '"') {
                Fail("expected object key");
            }
            std::string key = ParseString();

            SkipWhitespace();
            Expect(':');

            value.objectValue.emplace(std::move(key), ParseValue());

            SkipWhitespace();
            if (TryConsume('}')) {
                break;
            }
            Expect(',');
        }

        return value;
    }

    JsonValue ParseArray()
    {
        Expect('[');

        JsonValue value;
        value.type = JsonType::Array;

        SkipWhitespace();
        if (TryConsume(']')) {
            return value;
        }

        while (true) {
            value.arrayValue.push_back(ParseValue());

            SkipWhitespace();
            if (TryConsume(']')) {
                break;
            }
            Expect(',');
        }

        return value;
    }

    JsonValue ParseNumber()
    {
        const std::size_t start = position_;

        if (TryConsume('-')) {
            // Sign consumed.
        }

        if (AtEnd()) {
            Fail("expected number");
        }

        if (TryConsume('0')) {
            // Leading zero is allowed only as the whole integer part.
        } else if (std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                ++position_;
            }
        } else {
            Fail("expected number");
        }

        if (!AtEnd() && Peek() == '.') {
            ++position_;
            if (AtEnd() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                Fail("expected digits after decimal point");
            }
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                ++position_;
            }
        }

        if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
            ++position_;
            if (!AtEnd() && (Peek() == '+' || Peek() == '-')) {
                ++position_;
            }
            if (AtEnd() || std::isdigit(static_cast<unsigned char>(Peek())) == 0) {
                Fail("expected exponent digits");
            }
            while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
                ++position_;
            }
        }

        const std::string numberText(text_.substr(start, position_ - start));
        char* end = nullptr;
        const double parsed = std::strtod(numberText.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            Fail("invalid number");
        }

        JsonValue value;
        value.type = JsonType::Number;
        value.numberValue = parsed;
        return value;
    }

    std::string ParseString()
    {
        Expect('"');

        std::string result;
        while (!AtEnd()) {
            const char ch = Next();
            if (ch == '"') {
                return result;
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }

            if (AtEnd()) {
                Fail("unterminated escape sequence");
            }

            const char escaped = Next();
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                AppendUtf8(ParseHexCodePoint(), result);
                break;
            default:
                Fail("unknown escape sequence");
            }
        }

        Fail("unterminated string");
    }

    std::uint32_t ParseHexCodePoint()
    {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (AtEnd()) {
                Fail("unterminated unicode escape");
            }
            const char ch = Next();
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<std::uint32_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<std::uint32_t>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<std::uint32_t>(ch - 'A' + 10);
            } else {
                Fail("invalid unicode escape");
            }
        }
        return value;
    }

    static void AppendUtf8(std::uint32_t codePoint, std::string& output)
    {
        if (codePoint <= 0x7F) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool ConsumeLiteral(std::string_view literal)
    {
        if (text_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    void SkipWhitespace()
    {
        while (!AtEnd() && std::isspace(static_cast<unsigned char>(Peek())) != 0) {
            ++position_;
        }
    }

    void Expect(char expected)
    {
        if (AtEnd() || Next() != expected) {
            std::ostringstream stream;
            stream << "expected '" << expected << "'";
            Fail(stream.str());
        }
    }

    bool TryConsume(char expected)
    {
        if (!AtEnd() && Peek() == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    char Peek() const
    {
        return text_[position_];
    }

    char Next()
    {
        return text_[position_++];
    }

    bool AtEnd() const noexcept
    {
        return position_ >= text_.size();
    }

    [[noreturn]] void Fail(const std::string& message) const
    {
        std::ostringstream stream;
        stream << "JSON parse error at byte " << position_ << ": " << message;
        throw JsonParseError(stream.str());
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace

const JsonValue* JsonValue::Find(std::string_view key) const noexcept
{
    if (!IsObject()) {
        return nullptr;
    }

    const auto iterator = objectValue.find(std::string(key));
    if (iterator == objectValue.end()) {
        return nullptr;
    }
    return &iterator->second;
}

JsonParseError::JsonParseError(const std::string& message)
    : std::runtime_error(message)
{
}

JsonValue ParseJson(std::string_view text)
{
    return Parser(text).Parse();
}

} // namespace besktop
