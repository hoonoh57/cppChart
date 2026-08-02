#pragma once

#include <map>
#include <string>
#include <vector>

namespace json_lite
{
    class Value final
    {
    public:
        enum class Type
        {
            Null,
            Boolean,
            Number,
            String,
            Array,
            Object
        };

        using Array = std::vector<Value>;
        using Object = std::map<std::string, Value>;

        Value() = default;
        explicit Value(bool value);
        explicit Value(double value);
        explicit Value(std::string value);
        explicit Value(Array value);
        explicit Value(Object value);

        Type GetType() const noexcept;
        bool IsNull() const noexcept;
        bool IsBoolean() const noexcept;
        bool IsNumber() const noexcept;
        bool IsString() const noexcept;
        bool IsArray() const noexcept;
        bool IsObject() const noexcept;

        bool AsBoolean(bool fallback = false) const noexcept;
        double AsNumber(double fallback = 0.0) const noexcept;
        int AsInt(int fallback = 0) const noexcept;
        const std::string& AsString() const noexcept;
        const Array& AsArray() const noexcept;
        const Object& AsObject() const noexcept;

        const Value* Find(const std::string& key) const noexcept;
        std::string StringOr(const std::string& fallback = {}) const;

    private:
        Type type_ = Type::Null;
        bool boolean_ = false;
        double number_ = 0.0;
        std::string string_;
        Array array_;
        Object object_;
    };

    struct ParseResult final
    {
        bool ok = false;
        Value value;
        std::string error;
        std::size_t errorOffset = 0;
    };

    ParseResult Parse(const std::string& text);
    std::string EscapeString(const std::string& value);
}
