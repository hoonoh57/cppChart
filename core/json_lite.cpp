#include "json_lite.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace json_lite
{
    namespace
    {
        const Value::Array kEmptyArray;
        const Value::Object kEmptyObject;
        const std::string kEmptyString;

        int HexValue(char ch) noexcept
        {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        }

        void AppendUtf8(std::string& out, unsigned int codePoint)
        {
            if (codePoint <= 0x7F) {
                out.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else if (codePoint <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else {
                out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        class Parser final
        {
        public:
            explicit Parser(const std::string& text)
                : text_(text)
            {
            }

            ParseResult Run()
            {
                ParseResult result;
                SkipWhitespace();

                if (!ParseValue(result.value)) {
                    result.error = error_;
                    result.errorOffset = errorOffset_;
                    return result;
                }

                SkipWhitespace();
                if (offset_ != text_.size()) {
                    SetError("unexpected trailing content");
                    result.error = error_;
                    result.errorOffset = errorOffset_;
                    return result;
                }

                result.ok = true;
                return result;
            }

        private:
            bool ParseValue(Value& out)
            {
                if (offset_ >= text_.size()) {
                    return SetError("unexpected end of input");
                }

                const char ch = text_[offset_];
                if (ch == 'n') return ParseLiteral("null", Value(), out);
                if (ch == 't') return ParseLiteral("true", Value(true), out);
                if (ch == 'f') return ParseLiteral("false", Value(false), out);
                if (ch == '"') {
                    std::string value;
                    if (!ParseString(value)) return false;
                    out = Value(std::move(value));
                    return true;
                }
                if (ch == '[') return ParseArray(out);
                if (ch == '{') return ParseObject(out);
                if (ch == '-' || (ch >= '0' && ch <= '9')) return ParseNumber(out);

                return SetError("unexpected token");
            }

            bool ParseLiteral(const char* literal, Value value, Value& out)
            {
                while (*literal != '\0') {
                    if (offset_ >= text_.size() || text_[offset_] != *literal) {
                        return SetError("invalid literal");
                    }
                    ++offset_;
                    ++literal;
                }

                out = std::move(value);
                return true;
            }

            bool ParseString(std::string& out)
            {
                if (offset_ >= text_.size() || text_[offset_] != '"') {
                    return SetError("string expected");
                }

                ++offset_;
                out.clear();

                while (offset_ < text_.size()) {
                    const unsigned char ch =
                        static_cast<unsigned char>(text_[offset_++]);

                    if (ch == '"') return true;
                    if (ch < 0x20) return SetError("control character in string");

                    if (ch != '\\') {
                        out.push_back(static_cast<char>(ch));
                        continue;
                    }

                    if (offset_ >= text_.size()) {
                        return SetError("incomplete escape sequence");
                    }

                    const char escaped = text_[offset_++];
                    switch (escaped) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned int first = 0;
                        if (!ParseHex4(first)) return false;

                        unsigned int codePoint = first;
                        if (first >= 0xD800 && first <= 0xDBFF) {
                            if (
                                offset_ + 2 > text_.size() ||
                                text_[offset_] != '\\' ||
                                text_[offset_ + 1] != 'u')
                            {
                                return SetError("missing low surrogate");
                            }

                            offset_ += 2;
                            unsigned int second = 0;
                            if (!ParseHex4(second)) return false;
                            if (second < 0xDC00 || second > 0xDFFF) {
                                return SetError("invalid low surrogate");
                            }

                            codePoint =
                                0x10000 +
                                ((first - 0xD800) << 10) +
                                (second - 0xDC00);
                        }
                        else if (first >= 0xDC00 && first <= 0xDFFF) {
                            return SetError("unexpected low surrogate");
                        }

                        AppendUtf8(out, codePoint);
                        break;
                    }
                    default:
                        return SetError("invalid escape sequence");
                    }
                }

                return SetError("unterminated string");
            }

            bool ParseHex4(unsigned int& value)
            {
                if (offset_ + 4 > text_.size()) {
                    return SetError("incomplete unicode escape");
                }

                value = 0;
                for (int index = 0; index < 4; ++index) {
                    const int digit = HexValue(text_[offset_++]);
                    if (digit < 0) return SetError("invalid unicode escape");
                    value = (value << 4) | static_cast<unsigned int>(digit);
                }

                return true;
            }

            bool ParseArray(Value& out)
            {
                ++offset_;
                SkipWhitespace();

                Value::Array values;
                if (offset_ < text_.size() && text_[offset_] == ']') {
                    ++offset_;
                    out = Value(std::move(values));
                    return true;
                }

                for (;;) {
                    Value value;
                    if (!ParseValue(value)) return false;
                    values.push_back(std::move(value));
                    SkipWhitespace();

                    if (offset_ >= text_.size()) {
                        return SetError("unterminated array");
                    }

                    const char ch = text_[offset_++];
                    if (ch == ']') break;
                    if (ch != ',') return SetError("array comma expected");
                    SkipWhitespace();
                }

                out = Value(std::move(values));
                return true;
            }

            bool ParseObject(Value& out)
            {
                ++offset_;
                SkipWhitespace();

                Value::Object values;
                if (offset_ < text_.size() && text_[offset_] == '}') {
                    ++offset_;
                    out = Value(std::move(values));
                    return true;
                }

                for (;;) {
                    std::string key;
                    if (!ParseString(key)) return false;
                    SkipWhitespace();

                    if (offset_ >= text_.size() || text_[offset_] != ':') {
                        return SetError("object colon expected");
                    }
                    ++offset_;
                    SkipWhitespace();

                    Value value;
                    if (!ParseValue(value)) return false;
                    values[std::move(key)] = std::move(value);
                    SkipWhitespace();

                    if (offset_ >= text_.size()) {
                        return SetError("unterminated object");
                    }

                    const char ch = text_[offset_++];
                    if (ch == '}') break;
                    if (ch != ',') return SetError("object comma expected");
                    SkipWhitespace();
                }

                out = Value(std::move(values));
                return true;
            }

            bool ParseNumber(Value& out)
            {
                const std::size_t start = offset_;

                if (text_[offset_] == '-') ++offset_;
                if (offset_ >= text_.size()) return SetError("invalid number");

                if (text_[offset_] == '0') {
                    ++offset_;
                }
                else {
                    if (text_[offset_] < '1' || text_[offset_] > '9') {
                        return SetError("invalid number");
                    }
                    while (
                        offset_ < text_.size() &&
                        text_[offset_] >= '0' &&
                        text_[offset_] <= '9')
                    {
                        ++offset_;
                    }
                }

                if (offset_ < text_.size() && text_[offset_] == '.') {
                    ++offset_;
                    const std::size_t fractionStart = offset_;
                    while (
                        offset_ < text_.size() &&
                        text_[offset_] >= '0' &&
                        text_[offset_] <= '9')
                    {
                        ++offset_;
                    }
                    if (fractionStart == offset_) {
                        return SetError("fraction digit expected");
                    }
                }

                if (
                    offset_ < text_.size() &&
                    (text_[offset_] == 'e' || text_[offset_] == 'E'))
                {
                    ++offset_;
                    if (
                        offset_ < text_.size() &&
                        (text_[offset_] == '+' || text_[offset_] == '-'))
                    {
                        ++offset_;
                    }

                    const std::size_t exponentStart = offset_;
                    while (
                        offset_ < text_.size() &&
                        text_[offset_] >= '0' &&
                        text_[offset_] <= '9')
                    {
                        ++offset_;
                    }
                    if (exponentStart == offset_) {
                        return SetError("exponent digit expected");
                    }
                }

                const std::string token = text_.substr(start, offset_ - start);
                errno = 0;
                char* end = nullptr;
                const double value = std::strtod(token.c_str(), &end);

                if (
                    errno == ERANGE ||
                    end == nullptr ||
                    *end != '\0' ||
                    !std::isfinite(value))
                {
                    return SetError("number out of range");
                }

                out = Value(value);
                return true;
            }

            void SkipWhitespace() noexcept
            {
                while (offset_ < text_.size()) {
                    const char ch = text_[offset_];
                    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                        break;
                    }
                    ++offset_;
                }
            }

            bool SetError(const char* message)
            {
                if (error_.empty()) {
                    error_ = message;
                    errorOffset_ = offset_;
                }
                return false;
            }

            const std::string& text_;
            std::size_t offset_ = 0;
            std::string error_;
            std::size_t errorOffset_ = 0;
        };
    }

    Value::Value(bool value)
        : type_(Type::Boolean), boolean_(value)
    {
    }

    Value::Value(double value)
        : type_(Type::Number), number_(value)
    {
    }

    Value::Value(std::string value)
        : type_(Type::String), string_(std::move(value))
    {
    }

    Value::Value(Array value)
        : type_(Type::Array), array_(std::move(value))
    {
    }

    Value::Value(Object value)
        : type_(Type::Object), object_(std::move(value))
    {
    }

    Value::Type Value::GetType() const noexcept { return type_; }
    bool Value::IsNull() const noexcept { return type_ == Type::Null; }
    bool Value::IsBoolean() const noexcept { return type_ == Type::Boolean; }
    bool Value::IsNumber() const noexcept { return type_ == Type::Number; }
    bool Value::IsString() const noexcept { return type_ == Type::String; }
    bool Value::IsArray() const noexcept { return type_ == Type::Array; }
    bool Value::IsObject() const noexcept { return type_ == Type::Object; }

    bool Value::AsBoolean(bool fallback) const noexcept
    {
        return IsBoolean() ? boolean_ : fallback;
    }

    double Value::AsNumber(double fallback) const noexcept
    {
        return IsNumber() ? number_ : fallback;
    }

    int Value::AsInt(int fallback) const noexcept
    {
        if (!IsNumber()) return fallback;
        if (
            number_ < static_cast<double>((std::numeric_limits<int>::min)()) ||
            number_ > static_cast<double>((std::numeric_limits<int>::max)()))
        {
            return fallback;
        }
        return static_cast<int>(number_);
    }

    const std::string& Value::AsString() const noexcept
    {
        return IsString() ? string_ : kEmptyString;
    }

    const Value::Array& Value::AsArray() const noexcept
    {
        return IsArray() ? array_ : kEmptyArray;
    }

    const Value::Object& Value::AsObject() const noexcept
    {
        return IsObject() ? object_ : kEmptyObject;
    }

    const Value* Value::Find(const std::string& key) const noexcept
    {
        if (!IsObject()) return nullptr;
        const auto found = object_.find(key);
        return found == object_.end() ? nullptr : &found->second;
    }

    std::string Value::StringOr(const std::string& fallback) const
    {
        return IsString() ? string_ : fallback;
    }

    ParseResult Parse(const std::string& text)
    {
        return Parser(text).Run();
    }

    std::string EscapeString(const std::string& value)
    {
        std::ostringstream out;
        out << '"';

        for (unsigned char ch : value) {
            switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u"
                        << std::hex
                        << std::setw(4)
                        << std::setfill('0')
                        << static_cast<int>(ch)
                        << std::dec;
                }
                else {
                    out << static_cast<char>(ch);
                }
                break;
            }
        }

        out << '"';
        return out.str();
    }
}
