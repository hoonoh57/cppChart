#include "parameter_store.h"

#include "json_lite.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace trading
{
    namespace
    {
        struct PendingAssignment final
        {
            const ParameterDescriptor* descriptor = nullptr;
            int integerValue = 0;
            float floatValue = 0.0f;
            bool booleanValue = false;
            std::array<float, 4> colorValue{};
        };

        bool IsFiniteInRange(
            double value,
            double minimum,
            double maximum) noexcept
        {
            return
                std::isfinite(value) &&
                value >= minimum &&
                value <= maximum;
        }
    }

    bool ParameterStore::RegisterInteger(
        const std::string& group,
        const std::string& key,
        const std::string& displayName,
        int& value,
        int minimum,
        int maximum,
        const std::string& description)
    {
        if (minimum > maximum) return false;

        ParameterDescriptor descriptor;
        descriptor.group = group;
        descriptor.key = key;
        descriptor.displayName = displayName;
        descriptor.type = ParameterType::Integer;
        descriptor.storage = &value;
        descriptor.minimum = minimum;
        descriptor.maximum = maximum;
        descriptor.description = description;
        return Register(std::move(descriptor));
    }

    bool ParameterStore::RegisterFloat(
        const std::string& group,
        const std::string& key,
        const std::string& displayName,
        float& value,
        float minimum,
        float maximum,
        const std::string& description)
    {
        if (
            !std::isfinite(minimum) ||
            !std::isfinite(maximum) ||
            minimum > maximum)
        {
            return false;
        }

        ParameterDescriptor descriptor;
        descriptor.group = group;
        descriptor.key = key;
        descriptor.displayName = displayName;
        descriptor.type = ParameterType::Float;
        descriptor.storage = &value;
        descriptor.minimum = minimum;
        descriptor.maximum = maximum;
        descriptor.description = description;
        return Register(std::move(descriptor));
    }

    bool ParameterStore::RegisterBoolean(
        const std::string& group,
        const std::string& key,
        const std::string& displayName,
        bool& value,
        const std::string& description)
    {
        ParameterDescriptor descriptor;
        descriptor.group = group;
        descriptor.key = key;
        descriptor.displayName = displayName;
        descriptor.type = ParameterType::Boolean;
        descriptor.storage = &value;
        descriptor.description = description;
        return Register(std::move(descriptor));
    }

    bool ParameterStore::RegisterColor4(
        const std::string& group,
        const std::string& key,
        const std::string& displayName,
        float (&value)[4],
        const std::string& description)
    {
        ParameterDescriptor descriptor;
        descriptor.group = group;
        descriptor.key = key;
        descriptor.displayName = displayName;
        descriptor.type = ParameterType::Color4;
        descriptor.storage = value;
        descriptor.minimum = 0.0;
        descriptor.maximum = 1.0;
        descriptor.description = description;
        return Register(std::move(descriptor));
    }

    const std::vector<ParameterDescriptor>&
    ParameterStore::Descriptors() const noexcept
    {
        return descriptors_;
    }

    std::string ParameterStore::SaveJson() const
    {
        std::ostringstream out;
        out << std::setprecision(9);
        out << "{\n  \"schema\": 1,\n  \"parameters\": {";

        bool first = true;
        for (const ParameterDescriptor& descriptor : descriptors_) {
            out << (first ? "\n" : ",\n");
            first = false;

            out << "    "
                << json_lite::EscapeString(descriptor.key)
                << ": ";

            switch (descriptor.type) {
            case ParameterType::Integer:
                out << *static_cast<const int*>(descriptor.storage);
                break;

            case ParameterType::Float:
                out << *static_cast<const float*>(descriptor.storage);
                break;

            case ParameterType::Boolean:
                out << (
                    *static_cast<const bool*>(descriptor.storage)
                        ? "true"
                        : "false");
                break;

            case ParameterType::Color4: {
                const float* color =
                    static_cast<const float*>(descriptor.storage);
                out << '['
                    << color[0] << ", "
                    << color[1] << ", "
                    << color[2] << ", "
                    << color[3] << ']';
                break;
            }
            }
        }

        if (!first) out << '\n';
        out << "  }\n}\n";
        return out.str();
    }

    bool ParameterStore::LoadJson(
        const std::string& json,
        std::string& error,
        bool rejectUnknownKeys)
    {
        const json_lite::ParseResult parsed =
            json_lite::Parse(json);

        if (!parsed.ok) {
            std::ostringstream message;
            message
                << "JSON parse error at byte "
                << parsed.errorOffset
                << ": "
                << parsed.error;
            error = message.str();
            return false;
        }

        if (!parsed.value.IsObject()) {
            error = "parameter document must be a JSON object";
            return false;
        }

        const json_lite::Value* schema =
            parsed.value.Find("schema");

        if (
            schema == nullptr ||
            !schema->IsNumber() ||
            schema->AsInt(-1) != 1)
        {
            error = "unsupported parameter schema";
            return false;
        }

        const json_lite::Value* parameters =
            parsed.value.Find("parameters");

        if (parameters == nullptr || !parameters->IsObject()) {
            error = "parameters object is required";
            return false;
        }

        std::vector<PendingAssignment> pending;
        pending.reserve(parameters->AsObject().size());

        for (const auto& entry : parameters->AsObject()) {
            const std::string& key = entry.first;
            const json_lite::Value& value = entry.second;
            const ParameterDescriptor* descriptor = Find(key);

            if (descriptor == nullptr) {
                if (rejectUnknownKeys) {
                    error = "unknown parameter key: " + key;
                    return false;
                }
                continue;
            }

            PendingAssignment assignment;
            assignment.descriptor = descriptor;

            switch (descriptor->type) {
            case ParameterType::Integer: {
                if (!value.IsNumber()) {
                    error = "integer parameter has non-number value: " + key;
                    return false;
                }

                const double numeric = value.AsNumber();
                if (
                    !IsFiniteInRange(
                        numeric,
                        descriptor->minimum,
                        descriptor->maximum) ||
                    std::floor(numeric) != numeric ||
                    numeric < static_cast<double>((std::numeric_limits<int>::min)()) ||
                    numeric > static_cast<double>((std::numeric_limits<int>::max)()))
                {
                    error = "integer parameter out of range: " + key;
                    return false;
                }

                assignment.integerValue = static_cast<int>(numeric);
                break;
            }

            case ParameterType::Float: {
                if (!value.IsNumber()) {
                    error = "float parameter has non-number value: " + key;
                    return false;
                }

                const double numeric = value.AsNumber();
                if (!IsFiniteInRange(
                        numeric,
                        descriptor->minimum,
                        descriptor->maximum))
                {
                    error = "float parameter out of range: " + key;
                    return false;
                }

                assignment.floatValue = static_cast<float>(numeric);
                break;
            }

            case ParameterType::Boolean:
                if (!value.IsBoolean()) {
                    error = "boolean parameter has non-boolean value: " + key;
                    return false;
                }
                assignment.booleanValue = value.AsBoolean();
                break;

            case ParameterType::Color4: {
                if (!value.IsArray() || value.AsArray().size() != 4) {
                    error = "color parameter must have four elements: " + key;
                    return false;
                }

                for (std::size_t index = 0; index < 4; ++index) {
                    const json_lite::Value& component =
                        value.AsArray()[index];

                    if (
                        !component.IsNumber() ||
                        !IsFiniteInRange(
                            component.AsNumber(),
                            0.0,
                            1.0))
                    {
                        error = "color component out of range: " + key;
                        return false;
                    }

                    assignment.colorValue[index] =
                        static_cast<float>(component.AsNumber());
                }
                break;
            }
            }

            pending.push_back(assignment);
        }

        for (const PendingAssignment& assignment : pending) {
            const ParameterDescriptor& descriptor =
                *assignment.descriptor;

            switch (descriptor.type) {
            case ParameterType::Integer:
                *static_cast<int*>(descriptor.storage) =
                    assignment.integerValue;
                break;

            case ParameterType::Float:
                *static_cast<float*>(descriptor.storage) =
                    assignment.floatValue;
                break;

            case ParameterType::Boolean:
                *static_cast<bool*>(descriptor.storage) =
                    assignment.booleanValue;
                break;

            case ParameterType::Color4: {
                float* color = static_cast<float*>(descriptor.storage);
                for (std::size_t index = 0; index < 4; ++index) {
                    color[index] = assignment.colorValue[index];
                }
                break;
            }
            }
        }

        error.clear();
        return true;
    }

    bool ParameterStore::SaveFile(
        const std::string& path,
        std::string& error) const
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "cannot open parameter file for writing: " + path;
            return false;
        }

        const std::string json = SaveJson();
        stream.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!stream) {
            error = "cannot write parameter file: " + path;
            return false;
        }

        error.clear();
        return true;
    }

    bool ParameterStore::LoadFile(
        const std::string& path,
        std::string& error,
        bool rejectUnknownKeys)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            error = "cannot open parameter file for reading: " + path;
            return false;
        }

        std::ostringstream content;
        content << stream.rdbuf();
        if (!stream.good() && !stream.eof()) {
            error = "cannot read parameter file: " + path;
            return false;
        }

        return LoadJson(content.str(), error, rejectUnknownKeys);
    }

    bool ParameterStore::Register(
        ParameterDescriptor descriptor)
    {
        if (
            descriptor.group.empty() ||
            descriptor.key.empty() ||
            descriptor.displayName.empty() ||
            descriptor.storage == nullptr ||
            Find(descriptor.key) != nullptr)
        {
            return false;
        }

        descriptors_.push_back(std::move(descriptor));
        return true;
    }

    const ParameterDescriptor* ParameterStore::Find(
        const std::string& key) const noexcept
    {
        for (const ParameterDescriptor& descriptor : descriptors_) {
            if (descriptor.key == key) return &descriptor;
        }
        return nullptr;
    }
}
